#!/usr/bin/env python3
"""Translate replay actions to the structured ``macro_mode=2`` BC ABI.

The official replay action is a *primitive* action dictionary: each farmer or
hand can move, work, and carry an item while the market queue can contain up
to ten independent orders.  A mode-2 policy has one strategic intent, one
quantity bin, and one target quadrant.  There is therefore no lossless
translation for every replay row.  This module makes that loss explicit:

* compatible same-intent groups are translated to one macro label;
* movement, PASS, and carrying-only rows become HOLD (the native executor owns
  those mechanics); and
* conflicting strategic intents are marked ambiguous and are skipped by the
  strict dataset builder instead of being given a false hard label.

Only the acting player's observation and private inventory are consulted for
legality.  In particular, opponent private state (if accidentally present in
a fixture) is never read.  The output keeps the existing 47-head/1,058-mask
ABI: heads 0--2 are macro, quantity, and target, heads 3--16 are PASS, and
the conditional market tree is STOP.
"""

from __future__ import annotations

import dataclasses
import importlib.util
import pathlib
from collections import Counter
from typing import Any

import numpy as np


_HERE = pathlib.Path(__file__).resolve().parent
_SUBMISSION = _HERE / "submission"


def _load_macro_runtime():
    """Load the checked-in mode-2 runtime without requiring a package import."""

    path = _SUBMISSION / "native_macro_runtime.py"
    spec = importlib.util.spec_from_file_location(
        "kaggriculture_native_macro_runtime_for_bc", path
    )
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load native macro runtime from {path}")
    module = importlib.util.module_from_spec(spec)
    # native_macro_runtime's fallback loader imports the adjacent top_bot
    # source directly, so this remains usable when this file is run as a
    # script from a checkout rather than as an installed package.
    spec.loader.exec_module(module)
    return module


RUNTIME = _load_macro_runtime()

QUANTITIES = tuple(int(value) for value in RUNTIME.QUANTITIES)
TARGETS = tuple(int(value) for value in RUNTIME.TARGETS)
MACRO_COUNT = int(RUNTIME.MACRO_COUNT)
UNIT_HEADS = int(RUNTIME.UNIT_HEADS)
NUM_HEADS = UNIT_HEADS + 3 * int(RUNTIME.MARKET_SLOTS)
MASK_SIZE = int(RUNTIME.MASK_SIZE)
MASK_BYTES = (MASK_SIZE + 7) // 8

_UNIT_MAINTENANCE = frozenset(
    {"WATER", "FERTILIZE", "FEED", "CARE"}
)
_UNIT_HARVEST = frozenset({"HARVEST", "COLLECT_FERTILIZER"})
_UNIT_MOVEMENT = frozenset({"PASS", "NORTH", "SOUTH", "EAST", "WEST"})
_UNIT_STRUCTURES = {"BUILD_COOP": "COOP", "BUILD_PASTURE": "PASTURE"}
_ANIMALS = tuple(RUNTIME.ANIMALS)
_CROPS = tuple(RUNTIME.CROPS)
_PRODUCTS = tuple(RUNTIME.PRODUCTS)


@dataclasses.dataclass(frozen=True)
class MacroLabel:
    """One candidate mode-2 decision inferred from a primitive replay row.

    ``quantity`` is the source quantity before mode-2 binning.  The native
    decoder executes ``QUANTITIES[quantity_bin]``; ``quantity_exact`` records
    whether that decoder value equals the source quantity.  ``ambiguous`` is
    intentionally separate from confidence so callers can retain diagnostic
    rows while filtering them from hard-label training.
    """

    macro_id: int
    quantity: int
    quantity_bin: int
    target: int
    target_bin: int
    confidence: float
    reason: str
    source_ops: tuple[str, ...] = ()
    quantity_exact: bool = True
    ambiguous: bool = False

    @property
    def decoded_quantity(self) -> int:
        return QUANTITIES[self.quantity_bin]

    @property
    def decoded_target(self) -> int:
        return TARGETS[self.target_bin]

    def as_dict(self) -> dict[str, Any]:
        return {
            "macro_id": self.macro_id,
            "quantity": self.quantity,
            "quantity_bin": self.quantity_bin,
            "decoded_quantity": self.decoded_quantity,
            "target": self.target,
            "target_bin": self.target_bin,
            "decoded_target": self.decoded_target,
            "confidence": self.confidence,
            "reason": self.reason,
            "source_ops": list(self.source_ops),
            "quantity_exact": self.quantity_exact,
            "ambiguous": self.ambiguous,
        }


def _get(obj: Any, key: str, default: Any = None) -> Any:
    if obj is None:
        return default
    try:
        return obj.get(key, default)
    except AttributeError:
        return getattr(obj, key, default)


def _op(value: Any) -> str:
    if not isinstance(value, (list, tuple)) or not value:
        return "INVALID"
    return str(value[0]).upper()


def _positive_quantity(value: Any, default: int = 1) -> int | None:
    if value is None:
        return default
    if isinstance(value, bool):
        return None
    try:
        amount = int(value)
    except (TypeError, ValueError, OverflowError):
        return None
    return amount if amount > 0 else None


def _order_quantity(order: Any) -> int | None:
    if not isinstance(order, (list, tuple)):
        return None
    return _positive_quantity(order[2] if len(order) > 2 else None)


def _unit_quantity(command: Any) -> int | None:
    """Read an optional PICKUP/PLACE batch without treating ``ALL`` as exact."""

    if not isinstance(command, (list, tuple)):
        return None
    if len(command) < 3:
        return 1
    value = command[2]
    # The submission codec uses a very large sentinel for primitive ALL.  It
    # means "as much as possible", not an observable exact source quantity.
    if isinstance(value, bool):
        return None
    try:
        amount = int(value)
    except (TypeError, ValueError, OverflowError):
        return None
    if amount <= 0:
        return None
    return amount


def _quantity_bin(quantity: int) -> tuple[int, bool]:
    amount = max(1, int(quantity))
    index = 0
    for candidate, value in enumerate(QUANTITIES):
        if value > amount:
            break
        index = candidate
    return index, QUANTITIES[index] == amount


def _player_farm(observation: dict[str, Any]) -> dict[str, Any]:
    """Return only the acting farm (never an opponent farm)."""

    player = int(_get(observation, "player", 0))
    farms = _get(observation, "farms", ())
    if player < 0 or player >= len(farms):
        raise ValueError(f"invalid acting player {player}")
    return farms[player]


def _positions(observation: dict[str, Any]) -> list[tuple[int, int]]:
    farm = _player_farm(observation)
    values = [_get(farm, "farmer", (4, 4)), *_get(farm, "hands", ())]
    positions = []
    for value in values:
        if not isinstance(value, (list, tuple)) or len(value) < 2:
            positions.append(None)
            continue
        try:
            positions.append((int(value[0]), int(value[1])))
        except (TypeError, ValueError, OverflowError):
            positions.append(None)
    return positions


def _quadrant(position: tuple[int, int] | None) -> int:
    if position is None:
        return 0
    x, y = position
    return 1 << ((1 if x >= 5 else 0) + (2 if y >= 5 else 0))


def _target_from_positions(
    observation: dict[str, Any], positions: list[tuple[int, int] | None]
) -> tuple[int, float, str]:
    """Infer a planting target from current public worker positions.

    Replays do not carry an explicit target: a PLANT action is legal only at
    the worker's current tile.  If every observed planting worker is in one
    unlocked quadrant with a currently reclaimable tile, that quadrant is a
    defensible label.  Otherwise AUTO is the only honest label.
    """

    farm = _player_farm(observation)
    unlocked = set(_get(farm, "unlocked_quadrants", ()))
    names = {1: "NW", 2: "NE", 4: "SW", 8: "SE"}
    quadrants = {_quadrant(position) for position in positions if position}
    quadrants.discard(0)
    if len(quadrants) != 1:
        return 0, 0.65, "plant target not uniquely inferable; AUTO"
    quadrant = next(iter(quadrants))
    name = names.get(quadrant)
    if name not in unlocked:
        return 0, 0.65, "plant target quadrant is locked; AUTO"
    tiles = _get(farm, "tiles", ())
    reclaimable = any(
        (tile is None or _get(tile, "kind", "") == "WEED")
        and _quadrant((x, y)) == quadrant
        for y, row in enumerate(tiles)
        for x, tile in enumerate(row)
    )
    if not reclaimable:
        return 0, 0.65, "plant target quadrant has no reclaimable tile; AUTO"
    return quadrant, 0.96, f"plant workers agree on {name}"


def _target_bin(target: int) -> int:
    try:
        return TARGETS.index(int(target))
    except ValueError:
        return 0


def _ambiguous(reason: str, source_ops: tuple[str, ...]) -> MacroLabel:
    return MacroLabel(
        macro_id=int(RUNTIME.MACRO_HOLD), quantity=1, quantity_bin=0,
        target=0, target_bin=0, confidence=0.0, reason=reason,
        source_ops=source_ops, quantity_exact=True, ambiguous=True,
    )


def _make_label(
    macro_id: int,
    quantity: int,
    target: int,
    confidence: float,
    reason: str,
    source_ops: tuple[str, ...],
    ambiguous: bool = False,
) -> MacroLabel:
    quantity_bin, quantity_exact = _quantity_bin(quantity)
    target = int(target) if int(target) in TARGETS else 0
    return MacroLabel(
        macro_id=int(macro_id), quantity=max(1, int(quantity)),
        quantity_bin=quantity_bin, target=target,
        target_bin=_target_bin(target), confidence=float(confidence),
        reason=reason, source_ops=source_ops,
        quantity_exact=quantity_exact, ambiguous=ambiguous,
    )


def _market_intents(action: dict[str, Any]):
    """Collect market intent groups and malformed-order diagnostics."""

    groups: dict[str, list[tuple[str | None, int]]] = {}
    unknown: list[str] = []
    orders = _get(action, "market", ())
    if orders is None:
        orders = ()
    if not isinstance(orders, (list, tuple)):
        return groups, ["INVALID_MARKET"]
    for order in orders:
        op = _op(order)
        if op == "BUY_SEED":
            if not isinstance(order, (list, tuple)) or len(order) < 2:
                unknown.append(op)
                continue
            crop = str(order[1]).upper()
            amount = _order_quantity(order)
            if crop not in _CROPS or amount is None:
                unknown.append(op)
                continue
            groups.setdefault("buy_seed", []).append((crop, amount))
        elif op == "BUY_PRODUCT":
            if not isinstance(order, (list, tuple)) or len(order) < 2:
                unknown.append(op)
                continue
            item = str(order[1]).upper()
            amount = _order_quantity(order)
            if item == "WHEAT":
                key = "buy_wheat"
            elif item == "FERTILIZER":
                key = "buy_fertilizer"
            else:
                # There is intentionally no generic product-purchase macro.
                unknown.append(f"BUY_PRODUCT:{item}")
                continue
            if amount is None:
                unknown.append(op)
                continue
            groups.setdefault(key, []).append((item, amount))
        elif op == "BUY_ANIMAL":
            if not isinstance(order, (list, tuple)) or len(order) < 2:
                unknown.append(op)
                continue
            animal = str(order[1]).upper()
            amount = _order_quantity(order)
            if animal not in _ANIMALS or amount is None:
                unknown.append(op)
                continue
            groups.setdefault("buy_animal", []).append((animal, amount))
        elif op == "SELL":
            if not isinstance(order, (list, tuple)) or len(order) < 2:
                unknown.append(op)
                continue
            product = str(order[1]).upper()
            amount = _order_quantity(order)
            if product not in _PRODUCTS or amount is None:
                unknown.append(op)
                continue
            groups.setdefault("sell", []).append((product, amount))
        elif op == "HIRE":
            groups.setdefault("hire", []).append((None, 1))
        elif op == "BUY_LAND":
            groups.setdefault("expand", []).append((None, 1))
        elif op in ("", "PASS"):
            unknown.append("INVALID_MARKET")
        else:
            unknown.append(op)
    return groups, unknown


def translate_action(
    observation: dict[str, Any], action: dict[str, Any], *,
    strict: bool = True, runtime: Any | None = None,
    legality_mask: np.ndarray | None = None,
) -> MacroLabel | None:
    """Translate one primitive replay action into a mode-2 strategic label.

    In strict mode, any conflicting or otherwise ambiguous row returns
    ``None``.  With ``strict=False`` a diagnostic ``MacroLabel`` is returned
    with ``ambiguous=True``; this is used by the importer to count and report
    filtered rows.
    """

    if not isinstance(action, dict):
        result = _ambiguous("action is not a dictionary", ("INVALID",))
        return None if strict else result

    commands = [_get(action, "farmer", ["PASS"])]
    hands = _get(action, "hands", ())
    if hands is None:
        hands = ()
    if not isinstance(hands, (list, tuple)):
        result = _ambiguous("hands is not a sequence", ("INVALID",))
        return None if strict else result
    commands.extend(hands)
    positions = _positions(observation)

    # Unit evidence is deliberately grouped by strategic family.  Neutral
    # routes/carrying are executor mechanics and are not treated as strategy.
    plants: Counter[str] = Counter()
    plant_positions: list[tuple[int, int] | None] = []
    animals: Counter[str] = Counter()
    animal_structures: Counter[str] = Counter()
    harvest_count = 0
    maintain_count = 0
    neutral_count = 0
    unknown_units: list[str] = []
    source_ops: list[str] = []
    unit_quantities: dict[str, int] = {}
    for index, command in enumerate(commands):
        op = _op(command)
        source_ops.append(op)
        if op == "PLANT":
            if not isinstance(command, (list, tuple)) or len(command) < 2:
                unknown_units.append(op)
                continue
            crop = str(command[1]).upper()
            if crop not in _CROPS:
                unknown_units.append(f"PLANT:{crop}")
                continue
            plants[crop] += 1
            plant_positions.append(positions[index] if index < len(positions) else None)
            continue
        if op in ("PLACE", "PICKUP"):
            if not isinstance(command, (list, tuple)) or len(command) < 2:
                unknown_units.append(op)
                continue
            item = str(command[1]).upper()
            if item in _ANIMALS:
                amount = _unit_quantity(command)
                if amount is None:
                    unknown_units.append(f"{op}:{item}")
                    continue
                animals[item] += 1
                unit_quantities[item] = unit_quantities.get(item, 0) + amount
            else:
                # Carrying crops/products is a mechanical operation handled by
                # the executor.  It carries no inferable production intent.
                neutral_count += 1
            continue
        if op in _UNIT_STRUCTURES:
            animal_structures[_UNIT_STRUCTURES[op]] += 1
            continue
        if op in _UNIT_HARVEST:
            harvest_count += 1
            continue
        if op in _UNIT_MAINTENANCE:
            maintain_count += 1
            continue
        if op in _UNIT_MOVEMENT or op == "DROP":
            neutral_count += 1
            continue
        if op == "INVALID":
            unknown_units.append(op)
            continue
        unknown_units.append(op)

    market, unknown_market = _market_intents(action)
    unknown = tuple(unknown_units + unknown_market)
    if unknown:
        result = _ambiguous(
            "unrecognized primitive operation(s): " + ",".join(unknown),
            tuple(source_ops),
        )
        return None if strict else result

    # Feed purchases are an executor-supported maintenance adjunct.  Other
    # market groups must agree on one strategic family.
    market_main = {key: values for key, values in market.items()
                   if key != "buy_wheat"}
    market_keys = set(market_main)

    # Same-crop seed purchase plus PLANT is one coherent production decision;
    # same-animal purchase plus PLACE/BUILD is likewise coherent.  Everything
    # else involving two strategic families is intentionally filtered.
    if plants:
        if len(plants) != 1:
            result = _ambiguous("different crop PLANT commands in one row", tuple(source_ops))
            return None if strict else result
        crop = next(iter(plants))
        if market_keys - {"buy_seed"}:
            result = _ambiguous("PLANT conflicts with market intent", tuple(source_ops))
            return None if strict else result
        seed_values = market.get("buy_seed", ())
        if seed_values and {item for item, _ in seed_values} != {crop}:
            result = _ambiguous("PLANT crop differs from BUY_SEED crop", tuple(source_ops))
            return None if strict else result
        if animal_structures or harvest_count:
            result = _ambiguous("PLANT conflicts with structure/harvest work", tuple(source_ops))
            return None if strict else result
        amount = max(
            len(plants),
            sum(amount for _, amount in seed_values) if seed_values else 0,
        )
        target, target_confidence, target_reason = _target_from_positions(
            observation, plant_positions
        )
        confidence = min(0.97, target_confidence)
        if market.get("buy_wheat"):
            confidence -= 0.02
        label = _make_label(
            int(RUNTIME.MACRO_PLANT_BASE) + _CROPS.index(crop), amount, target,
            confidence, f"PLANT {crop}; {target_reason}", tuple(source_ops),
        )
    elif animals:
        if len(animals) != 1:
            result = _ambiguous("different animal PLACE/PICKUP commands in one row", tuple(source_ops))
            return None if strict else result
        animal = next(iter(animals))
        if market_keys - {"buy_animal"}:
            result = _ambiguous("animal mechanics conflict with market intent", tuple(source_ops))
            return None if strict else result
        animal_values = market.get("buy_animal", ())
        if animal_values and {item for item, _ in animal_values} != {animal}:
            result = _ambiguous("animal differs from BUY_ANIMAL species", tuple(source_ops))
            return None if strict else result
        # A structure is compatible only when it can host the selected species.
        required_structure = "COOP" if animal == "GOOSE" else "PASTURE"
        if animal_structures and any(
            structure != required_structure for structure in animal_structures
        ):
            result = _ambiguous("animal conflicts with incompatible structure", tuple(source_ops))
            return None if strict else result
        amount = max(
            unit_quantities.get(animal, 1),
            sum(amount for _, amount in animal_values) if animal_values else 0,
        )
        label = _make_label(
            int(RUNTIME.MACRO_ANIMAL_BASE) + _ANIMALS.index(animal), amount,
            0, 0.92,
            f"{animal} animal mechanics" + (" plus purchase" if animal_values else ""),
            tuple(source_ops),
        )
    else:
        # A bare structure does not identify species (PASTURE supports both
        # cow and sheep), so do not silently turn it into an ANIMAL decision.
        if animal_structures:
            result = _ambiguous("structure build does not identify animal species", tuple(source_ops))
            return None if strict else result
        # A standalone wheat purchase is a real mode-2 intent.  When direct
        # FEED/WATER work accompanies it, BUY_WHEAT still reproduces the
        # executor's feed reservation; harvest work is handled below because
        # HARVEST also intentionally preserves feed orders.
        if market.get("buy_wheat") and not market_keys and not harvest_count:
            label = _make_label(
                int(RUNTIME.MACRO_BUY_WHEAT),
                sum(amount for _, amount in market["buy_wheat"]), 0, 0.86,
                "BUY_PRODUCT WHEAT", tuple(source_ops),
            )
        elif harvest_count and (market_keys or maintain_count):
            result = _ambiguous("HARVEST conflicts with another strategic family", tuple(source_ops))
            return None if strict else result
        if harvest_count:
            label = _make_label(
                int(RUNTIME.MACRO_HARVEST), max(1, harvest_count), 0, 0.90,
                "harvest/collect-fertilizer commands", tuple(source_ops),
            )
        elif market_keys:
            if len(market_keys) != 1:
                result = _ambiguous("multiple market strategic intents in one row", tuple(source_ops))
                return None if strict else result
            key = next(iter(market_keys))
            values = market[key]
            items = {item for item, _ in values}
            amount = sum(amount for _, amount in values)
            if key == "buy_seed":
                if len(items) != 1:
                    result = _ambiguous("BUY_SEED spans multiple crops", tuple(source_ops))
                    return None if strict else result
                crop = next(iter(items))
                macro_id = int(RUNTIME.MACRO_BUY_SEED_BASE) + _CROPS.index(crop)
                reason = f"BUY_SEED {crop}"
            elif key == "buy_animal":
                if len(items) != 1:
                    result = _ambiguous("BUY_ANIMAL spans multiple species", tuple(source_ops))
                    return None if strict else result
                animal = next(iter(items))
                macro_id = int(RUNTIME.MACRO_BUY_ANIMAL_BASE) + _ANIMALS.index(animal)
                reason = f"BUY_ANIMAL {animal}"
            elif key == "sell":
                if len(items) == 1:
                    product = next(iter(items))
                    macro_id = int(RUNTIME.MACRO_SELL_BASE) + _PRODUCTS.index(product)
                    reason = f"SELL {product}"
                else:
                    # The native executor distinguishes ordinary diversified
                    # liquidation from the final two-day cash-out path.  The
                    # step is current public state, so this does not use a
                    # future outcome or opponent information.
                    step = int(_get(observation, "step", 0))
                    if 720 - step <= 48:
                        macro_id = int(RUNTIME.MACRO_CASH_OUT)
                        reason = "SELL multiple products in final two days -> CASH_OUT"
                    else:
                        macro_id = int(RUNTIME.MACRO_SELL_ALL)
                        reason = "SELL multiple products -> SELL_ALL"
            elif key == "hire":
                macro_id = int(RUNTIME.MACRO_HIRE)
                reason = "HIRE"
                amount = max(1, len(values))
            elif key == "expand":
                macro_id = int(RUNTIME.MACRO_EXPAND)
                reason = "BUY_LAND"
                amount = 1
            elif key == "buy_fertilizer":
                macro_id = int(RUNTIME.MACRO_BUY_FERTILIZER)
                reason = "BUY_PRODUCT FERTILIZER"
            else:  # buy_wheat is retained as a strategic action when alone.
                macro_id = int(RUNTIME.MACRO_BUY_WHEAT)
                reason = "BUY_PRODUCT WHEAT"
            confidence = 0.90 if key not in {"buy_wheat", "sell"} else 0.86
            label = _make_label(
                macro_id, amount, 0, confidence, reason, tuple(source_ops)
            )
        elif maintain_count:
            label = _make_label(
                int(RUNTIME.MACRO_MAINTAIN), max(1, maintain_count), 0, 0.87,
                "maintenance commands", tuple(source_ops),
            )
        else:
            # PASS/routes and carrying are exactly the mechanics that the
            # native executor should fill in after a strategic HOLD decision.
            confidence = 0.78 if neutral_count == 0 else 0.56
            label = _make_label(
                int(RUNTIME.MACRO_HOLD), 1, 0, confidence,
                "no strategic command; executor owns mechanics", tuple(source_ops),
            )

    # Feed purchase may accompany one compatible strategic family.  It is not
    # evidence for a second intent because the native executor reserves feed.
    if market.get("buy_wheat") and label.macro_id not in (
        int(RUNTIME.MACRO_BUY_WHEAT),
    ):
        label = dataclasses.replace(
            label, reason=label.reason + "; feed purchase retained as adjunct",
        )

    # Primitive queues can be legal only after an earlier same-turn purchase;
    # mode 2 has no queue representation.  Reject any intent that is not legal
    # in the public/runtime state at this decision boundary.  Keep the reason
    # on non-strict diagnostics so audits can separate ambiguity from legality.
    if not label.ambiguous:
        legal_mask = (
            np.asarray(legality_mask, dtype=np.bool_)
            if legality_mask is not None
            else mode2_mask(observation, runtime=runtime)
        )
        values = (label.macro_id, label.quantity_bin, label.target_bin)
        offsets = (
            0, int(RUNTIME.UNIT_COMMANDS), 2 * int(RUNTIME.UNIT_COMMANDS)
        )
        if not all(
            0 <= value < int(RUNTIME.UNIT_COMMANDS)
            and bool(legal_mask[offset + value])
            for offset, value in zip(offsets, values)
        ):
            label = dataclasses.replace(
                label, ambiguous=True, confidence=0.0,
                reason=label.reason + "; rejected by current mode-2 legality mask",
            )

    if strict and label.ambiguous:
        return None
    return label


def mode2_mask(
    observation: dict[str, Any], runtime: Any | None = None
) -> np.ndarray:
    """Return the exact structured mode-2 semantic legality mask."""

    runtime = runtime or RUNTIME.NativeMacroRuntime()
    mask = np.asarray(runtime.action_mask(observation), dtype=np.bool_)
    if mask.shape != (MASK_SIZE,):
        raise ValueError(f"mode-2 runtime mask shape {mask.shape} != {(MASK_SIZE,)}")
    return mask


def build_macro_row(
    observation: dict[str, Any], action: dict[str, Any], *,
    runtime: Any | None = None,
) -> tuple[np.ndarray, np.ndarray, MacroLabel | None, np.ndarray]:
    """Build (expert, packed_mask, diagnostic, unpacked_mask) for one row.

    Ambiguous rows carry an all-negative expert vector, which is the existing
    BC kernel's documented skip marker.  The runtime mask is still emitted so
    auditing can verify legality and no row can accidentally train against a
    primitive mask.
    """

    runtime = runtime or RUNTIME.NativeMacroRuntime()
    mask = mode2_mask(observation, runtime)
    diagnostic = translate_action(
        observation, action, strict=False, runtime=runtime,
        legality_mask=mask,
    )
    expert = np.full(NUM_HEADS, -1.0, dtype="<f4")
    if diagnostic is None or diagnostic.ambiguous:
        return expert, np.packbits(mask, bitorder="little"), diagnostic, mask

    # Mode 2 uses the first three 44-way heads as intent/quantity/target.
    values = (diagnostic.macro_id, diagnostic.quantity_bin, diagnostic.target_bin)
    offsets = (0, int(RUNTIME.UNIT_COMMANDS), 2 * int(RUNTIME.UNIT_COMMANDS))
    legal = all(
        0 <= value < int(RUNTIME.UNIT_COMMANDS)
        and bool(mask[offset + value])
        for offset, value in zip(offsets, values)
    )
    if not legal:
        diagnostic = dataclasses.replace(
            diagnostic,
            ambiguous=True,
            confidence=0.0,
            reason=diagnostic.reason + "; rejected by current mode-2 legality mask",
        )
        return expert, np.packbits(mask, bitorder="little"), diagnostic, mask

    expert[0:3] = values
    # Remaining unit heads are canonical PASS.  Conditional market heads are
    # canonical STOP; later command/quantity nodes are inactive by design.
    expert[3:UNIT_HEADS] = 0.0
    expert[UNIT_HEADS:] = 0.0
    return expert, np.packbits(mask, bitorder="little"), diagnostic, mask


def fill_mode2_observation(
    observation: dict[str, Any], encoded: np.ndarray, *, runtime: Any | None = None
) -> np.ndarray:
    """Fill the strategic tail of a 1,280-byte observation in place."""

    runtime = runtime or RUNTIME.NativeMacroRuntime()
    result = runtime.fill_observation(observation, encoded)
    if result is not encoded:
        encoded[:] = result
    if encoded.shape != (1280,):
        raise ValueError(f"observation shape {encoded.shape} != (1280,)")
    return encoded


def remember_label(runtime: Any, observation: dict[str, Any], label: MacroLabel | None) -> None:
    """Update the mode-2 runtime tail state after importing one decision.

    The C mode-2 path decides every turn, so this only records the previous
    intent/parameters for the next observation tail.  Ambiguous rows become
    HOLD, which avoids leaking a discarded primitive decision into features.
    """

    if label is None or label.ambiguous:
        macro_id, quantity_bin, target_bin = 0, 0, 0
    else:
        macro_id, quantity_bin, target_bin = (
            label.macro_id, label.quantity_bin, label.target_bin
        )
    entry = runtime._entry(observation)  # checked-in runtime state, no private data
    entry.update(
        intent=int(macro_id), quantity=int(QUANTITIES[quantity_bin]),
        target=int(TARGETS[target_bin]), ticks=0,
    )


__all__ = [
    "MASK_BYTES", "MASK_SIZE", "MACRO_COUNT", "MacroLabel", "NUM_HEADS",
    "QUANTITIES", "TARGETS", "build_macro_row", "fill_mode2_observation",
    "mode2_mask", "remember_label", "translate_action",
]
