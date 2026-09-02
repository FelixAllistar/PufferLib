"""Observation-safe macro-action catalog for offline Kaggriculture planning.

This module deliberately does not change the native primitive action ABI.  It
describes direct market candidates and, when requested, appends deterministic
multi-turn plans from :mod:`macro_executor`.  Callers that need the original
C0 direct-only catalog can pass ``include_strategic=False``; strategic plans
are never silently encoded as PASS.
"""

from __future__ import annotations

from dataclasses import dataclass
import copy
import json
from typing import Any, Iterable


PRODUCTS = (
    "WHEAT", "CARROT", "TOMATO", "STRAWBERRY", "MELON",
    "EGG", "MILK", "WOOL", "FERTILIZER",
)
CROPS = ("WHEAT", "CARROT", "TOMATO", "STRAWBERRY", "MELON")
ANIMALS = ("GOOSE", "COW", "SHEEP")
BUYABLE_PRODUCTS = ("WHEAT", "FERTILIZER")

# These values mirror the native rule definitions used by the deterministic
# executor.  They are only used to remove impossible candidate rows; realized
# cash still comes from the exact native simulator.
SEED_COSTS = {
    "WHEAT": 10, "CARROT": 20, "TOMATO": 50, "STRAWBERRY": 100, "MELON": 80,
}
ANIMAL_COSTS = {"GOOSE": 300, "COW": 400, "SHEEP": 500}
MAX_HANDS = 240

ACTION_KIND_CODES = {
    "HOLD": 0,
    "SELL": 1,
    "BUY_SEED": 2,
    "BUY_ANIMAL": 3,
    "BUY_PRODUCT": 4,
    "BUY_LAND": 5,
    "HIRE": 6,
    # Strategic plans occupy an appended range so the original C0 direct
    # feature encoding remains byte-for-byte stable.
    "PLANT": 7,
    "BUILD_ANIMAL": 8,
    "HARVEST": 9,
    "MAINTAIN": 10,
    "SELL_ALL": 11,
}
ITEM_CODES = {name: index for index, name in enumerate((*PRODUCTS, *ANIMALS))}

# Stable numeric feature columns.  Models should use these columns rather than
# the raw JSON blobs emitted by the dataset writer.
FEATURE_FIELDS = (
    "step", "day", "hour", "remaining_turns", "remaining_days",
    "own_money", "opponent_money", "own_hands", "opponent_hands",
    "own_unlocked_tiles", "opponent_unlocked_tiles", "own_quadrants",
    "opponent_quadrants", "own_plants", "opponent_plants", "own_animals",
    "opponent_animals", "own_weeds", "opponent_weeds", "own_maintenance_due",
    "opponent_maintenance_due", "own_product_units", "own_seed_units",
    "own_shed_capacity_used", "own_shed_capacity_fraction", "market_shop_count",
    *(f"market_{product.lower()}_inventory" for product in PRODUCTS),
    *(f"market_{product.lower()}_price" for product in PRODUCTS),
    *(f"own_shed_{product.lower()}" for product in PRODUCTS),
    *(f"own_seed_{crop.lower()}" for crop in CROPS),
)


def _int(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def _float(value: Any, default: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def pass_action() -> dict[str, Any]:
    """Return a fresh no-op structured action."""

    return {"farmer": ["PASS"], "hands": [], "market": []}


def merge_macro_action(
    macro: dict[str, Any], fallback: dict[str, Any],
) -> dict[str, Any]:
    """Overlay an explicit macro command on ordinary primitive control.

    A macro owns only the fields it specifies.  In particular, the executor's
    farmer-only plans emit ``PASS`` for hired hands; those PASS placeholders
    must not erase fallback maintenance/harvest work.  Market orders are
    replaced deliberately because selecting a direct macro is the strategic
    decision for that turn.  The same merge is used by offline labels and the
    live evaluator so the learned scorer's target matches runtime behavior.
    """

    # ``HOLD`` is represented by the canonical empty/pass payload.  It is a
    # true no-intervention decision, so it must not clear a fallback market
    # queue (or any other field).  The live controller normally skips the
    # merge for HOLD; keeping the invariant here is essential for offline
    # candidate labels, where HOLD is the same-state baseline by definition.
    if (
        macro.get("farmer") == ["PASS"]
        and not macro.get("hands")
        and not macro.get("market")
    ):
        return copy.deepcopy(fallback)
    result = copy.deepcopy(fallback)
    farmer = macro.get("farmer")
    if isinstance(farmer, list) and farmer and farmer != ["PASS"]:
        result["farmer"] = copy.deepcopy(farmer)
    macro_hands = macro.get("hands")
    if isinstance(macro_hands, list):
        hands = result.setdefault("hands", [])
        if not isinstance(hands, list):
            hands = []
            result["hands"] = hands
        for index, command in enumerate(macro_hands):
            if isinstance(command, list) and command and command != ["PASS"]:
                while len(hands) <= index:
                    hands.append(["PASS"])
                hands[index] = copy.deepcopy(command)
    if "market" in macro:
        result["market"] = copy.deepcopy(macro.get("market") or [])
    return result


def _market_action(kind: str, item: str | None = None, quantity: int = 0) -> dict[str, Any]:
    order: list[Any] = [kind]
    if item is not None:
        order.append(item)
    if quantity > 0:
        order.append(int(quantity))
    action = pass_action()
    action["market"] = [order]
    return action


@dataclass(frozen=True)
class MacroAction:
    """One strategic candidate and its directly representable native action."""

    action_id: str
    kind: str
    item: str = ""
    quantity: int = 0
    action: dict[str, Any] | None = None
    # A macro can span several primitive turns.  ``sequence[0]`` is always the
    # first-turn action and is mirrored by ``action`` when provided.  Keeping
    # the sequence in the candidate object lets the offline brancher evaluate
    # a real plant/build/service plan without changing the primitive PPO ABI.
    sequence: tuple[dict[str, Any], ...] = ()
    executable: bool = True
    note: str = ""

    def payload(self) -> dict[str, Any]:
        return copy.deepcopy(self.action if self.action is not None else pass_action())

    def action_sequence(self) -> tuple[dict[str, Any], ...]:
        sequence = self.sequence or (self.payload(),)
        return tuple(copy.deepcopy(item) for item in sequence)

    def columns(self) -> dict[str, Any]:
        return {
            "candidate_id": self.action_id,
            "candidate_kind": self.kind,
            "candidate_kind_code": ACTION_KIND_CODES.get(self.kind, -1),
            "candidate_item": self.item,
            "candidate_item_code": ITEM_CODES.get(self.item, -1),
            "candidate_quantity": self.quantity,
            "candidate_executable": int(self.executable),
            "candidate_note": self.note,
            "candidate_action_json": _json_action(self.payload()),
            "candidate_plan_json": json.dumps(
                list(self.action_sequence()), separators=(",", ":"), sort_keys=True
            ),
            "candidate_plan_steps": len(self.action_sequence()),
        }


def _json_action(action: dict[str, Any]) -> str:
    # Local import keeps this module cheap for callers that only need features.
    import json

    return json.dumps(action, separators=(",", ":"), sort_keys=True)


def _farm(snapshot: dict[str, Any], player: int) -> dict[str, Any]:
    farms = snapshot.get("farms") or []
    if 0 <= player < len(farms) and isinstance(farms[player], dict):
        return farms[player]
    return {}


def _private(snapshot: dict[str, Any], player: int) -> dict[str, Any]:
    privates = snapshot.get("privates") or []
    if 0 <= player < len(privates) and isinstance(privates[player], dict):
        return privates[player]
    return {}


def tile_stats(farm: dict[str, Any]) -> dict[str, int]:
    """Summarize public tiles without reading any private opponent fields."""

    counts = {
        "unlocked_tiles": 0,
        "plants": 0,
        "animals": 0,
        "weeds": 0,
        "maintenance_due": 0,
    }
    for row in farm.get("tiles") or []:
        if not isinstance(row, list):
            continue
        for tile in row:
            if tile == "LOCKED":
                continue
            counts["unlocked_tiles"] += 1
            if not isinstance(tile, dict):
                continue
            kind = str(tile.get("kind", "")).upper()
            if kind == "PLANT":
                counts["plants"] += 1
                if not bool(tile.get("watered_today", False)):
                    counts["maintenance_due"] += 1
                if _int(tile.get("consecutive_unwatered")) > 0:
                    counts["maintenance_due"] += 1
            elif kind in {"COOP", "PASTURE"} and tile.get("animal"):
                counts["animals"] += 1
                if not bool(tile.get("fed_today", False)):
                    counts["maintenance_due"] += 1
                if kind == "PASTURE" and not bool(tile.get("cared_today", False)):
                    counts["maintenance_due"] += 1
                if _int(tile.get("consecutive_unfed")) > 0:
                    counts["maintenance_due"] += 1
            elif kind == "WEED":
                counts["weeds"] += 1
    return counts


def _product_units(private: dict[str, Any]) -> int:
    shed = private.get("shed") or {}
    total = sum(max(0, _int(shed.get(item))) for item in PRODUCTS)
    for inventory in private.get("inventories") or []:
        if isinstance(inventory, dict):
            total += sum(max(0, _int(inventory.get(item))) for item in PRODUCTS)
    return total


def _shed_counts(private: dict[str, Any]) -> dict[str, int]:
    shed = private.get("shed") or {}
    return {item: max(0, _int(shed.get(item))) for item in PRODUCTS}


def _seed_counts(private: dict[str, Any]) -> dict[str, int]:
    seeds = private.get("seeds") or {}
    return {crop: max(0, _int(seeds.get(crop))) for crop in CROPS}


def _fib(n: int) -> int:
    a, b = 1, 1
    for _ in range(max(0, int(n))):
        a, b = b, a + b
    return a


def _direct_market_feasible(
    snapshot: dict[str, Any], player: int, kind: str,
    item: str = "", quantity: int = 1,
) -> bool:
    """Check cheap visible prerequisites before emitting a direct candidate.

    Native ``kg_step`` remains authoritative and can reject an action for a
    race/order interaction.  This predicate only removes candidates that are
    unambiguously impossible from the selected player's public/private state,
    preventing the value model from learning thousands of artificial no-op
    rows.
    """

    quantity = int(quantity)
    if quantity <= 0:
        return False
    farm = _farm(snapshot, player)
    private = _private(snapshot, player)
    money = _float(farm.get("money"))
    shed = _shed_counts(private)
    if kind == "SELL":
        return item in PRODUCTS and shed.get(item, 0) >= quantity
    if kind == "BUY_SEED":
        return item in CROPS and money >= SEED_COSTS[item] * quantity
    if kind == "BUY_ANIMAL":
        if item not in ANIMALS:
            return False
        return (
            money >= ANIMAL_COSTS[item] * quantity
            and sum(shed.values()) + quantity <= 100
        )
    if kind == "BUY_PRODUCT":
        if item not in BUYABLE_PRODUCTS:
            return False
        prices = (snapshot.get("market") or {}).get("prices") or {}
        price = _float(prices.get(item))
        return price > 0 and money >= price * quantity and sum(shed.values()) + quantity <= 100
    if kind == "BUY_LAND":
        quadrants = len(farm.get("unlocked_quadrants") or [])
        # A synthetic snapshot may omit the initial quadrant; treating it as
        # the first expansion keeps fixtures and native roots equivalent.
        extra = max(0, quadrants - 1)
        prices = (1000, 2000, 4000)
        return extra < len(prices) and money >= prices[extra]
    if kind == "HIRE":
        hands = len(farm.get("hands") or [])
        hires_today = _int(farm.get("hires_today"))
        return hands + 1 < MAX_HANDS and money >= _fib(hires_today)
    return kind == "HOLD"


def public_features(
    snapshot: dict[str, Any], player: int, *, episode_steps: int = 720,
    turns_per_day: int = 24, shed_capacity: int = 100,
) -> dict[str, float | int]:
    """Build features available to the selected player.

    The opponent's ``privates`` entry is intentionally never accessed.  The
    opponent farm, positions/counts, money, market, and shops are public in the
    native snapshot.  This function is the observation-safety boundary for the
    learned scorer.
    """

    opponent = 1 - player
    own_farm = _farm(snapshot, player)
    opponent_farm = _farm(snapshot, opponent)
    own_stats = tile_stats(own_farm)
    opponent_stats = tile_stats(opponent_farm)
    own_private = _private(snapshot, player)
    own_shed = _shed_counts(own_private)
    own_seeds = _seed_counts(own_private)
    market = snapshot.get("market") or {}
    inventory = market.get("inventory") or {}
    prices = market.get("prices") or {}
    step = _int(snapshot.get("step"))
    remaining = max(0, int(episode_steps) - step)
    own_quadrants = len(own_farm.get("unlocked_quadrants") or [])
    opponent_quadrants = len(opponent_farm.get("unlocked_quadrants") or [])
    own_hands = len(own_farm.get("hands") or [])
    opponent_hands = len(opponent_farm.get("hands") or [])
    features: dict[str, float | int] = {
        "step": step,
        "day": _int(snapshot.get("day")),
        "hour": _int(snapshot.get("hour")),
        "remaining_turns": remaining,
        "remaining_days": remaining / max(1, int(turns_per_day)),
        "own_money": _float(own_farm.get("money")),
        "opponent_money": _float(opponent_farm.get("money")),
        "own_hands": own_hands,
        "opponent_hands": opponent_hands,
        "own_unlocked_tiles": own_stats["unlocked_tiles"],
        "opponent_unlocked_tiles": opponent_stats["unlocked_tiles"],
        "own_quadrants": own_quadrants,
        "opponent_quadrants": opponent_quadrants,
        "own_plants": own_stats["plants"],
        "opponent_plants": opponent_stats["plants"],
        "own_animals": own_stats["animals"],
        "opponent_animals": opponent_stats["animals"],
        "own_weeds": own_stats["weeds"],
        "opponent_weeds": opponent_stats["weeds"],
        "own_maintenance_due": own_stats["maintenance_due"],
        "opponent_maintenance_due": opponent_stats["maintenance_due"],
        "own_product_units": _product_units(own_private),
        "own_seed_units": sum(own_seeds.values()),
        "own_shed_capacity_used": sum(own_shed.values()),
        "market_shop_count": len((snapshot.get("town") or {}).get("unlocked_shops") or []),
    }
    for product in PRODUCTS:
        features[f"market_{product.lower()}_inventory"] = _int(inventory.get(product))
        features[f"market_{product.lower()}_price"] = _float(prices.get(product))
        features[f"own_shed_{product.lower()}"] = own_shed[product]
    for crop in CROPS:
        features[f"own_seed_{crop.lower()}"] = own_seeds[crop]
    # Keep the argument meaningful for callers constructing hypothetical
    # features with a different capacity, while preserving raw units.
    features["own_shed_capacity_fraction"] = (
        sum(own_shed.values()) / max(1, int(shed_capacity))
    )
    return features


def _unique_quantities(values: Iterable[int]) -> tuple[int, ...]:
    return tuple(dict.fromkeys(int(value) for value in values if int(value) > 0))


def candidate_actions(
    snapshot: dict[str, Any], player: int, *, max_candidates: int = 0,
    include_strategic: bool = True, episode_steps: int = 720,
    shed_capacity: int = 100,
) -> list[MacroAction]:
    """Return direct and (optionally) deterministic multi-turn candidates.

    Direct market actions are always first for backwards-compatible truncation.
    ``include_strategic`` appends plans produced by :mod:`macro_executor`; each
    plan contains legal primitive actions and can be simulated without changing
    the native policy ABI.
    """

    private = _private(snapshot, player)
    shed = _shed_counts(private)
    candidates: list[MacroAction] = [
        MacroAction("HOLD", "HOLD", action=pass_action(), note="no market order"),
    ]
    for item in PRODUCTS:
        units = shed[item]
        for quantity in _unique_quantities((1, min(5, units), units)):
            if units <= 0 or not _direct_market_feasible(
                snapshot, player, "SELL", item, quantity
            ):
                continue
            candidates.append(MacroAction(
                f"SELL:{item}:{quantity}", "SELL", item, quantity,
                _market_action("SELL", item, quantity),
            ))
    for crop in CROPS:
        for quantity in (1, 5, 10):
            if not _direct_market_feasible(snapshot, player, "BUY_SEED", crop, quantity):
                continue
            candidates.append(MacroAction(
                f"BUY_SEED:{crop}:{quantity}", "BUY_SEED", crop, quantity,
                _market_action("BUY_SEED", crop, quantity),
            ))
    for animal in ANIMALS:
        for quantity in (1, 3):
            if not _direct_market_feasible(snapshot, player, "BUY_ANIMAL", animal, quantity):
                continue
            candidates.append(MacroAction(
                f"BUY_ANIMAL:{animal}:{quantity}", "BUY_ANIMAL", animal, quantity,
                _market_action("BUY_ANIMAL", animal, quantity),
            ))
    for item in BUYABLE_PRODUCTS:
        for quantity in (1, 5):
            if not _direct_market_feasible(snapshot, player, "BUY_PRODUCT", item, quantity):
                continue
            candidates.append(MacroAction(
                f"BUY_PRODUCT:{item}:{quantity}", "BUY_PRODUCT", item, quantity,
                _market_action("BUY_PRODUCT", item, quantity),
            ))
    if _direct_market_feasible(snapshot, player, "BUY_LAND"):
        candidates.append(MacroAction("BUY_LAND", "BUY_LAND", action=_market_action("BUY_LAND")))
    if _direct_market_feasible(snapshot, player, "HIRE"):
        candidates.append(MacroAction("HIRE:1", "HIRE", quantity=1, action=_market_action("HIRE")))
    if include_strategic:
        # Import lazily to keep the feature/candidate contract usable without
        # pulling in the route planner for callers that only need direct rows.
        from macro_executor import strategic_actions

        candidates.extend(strategic_actions(
            snapshot, player, episode_steps=episode_steps,
            shed_capacity=shed_capacity,
        ))
    if max_candidates > 0:
        candidates = candidates[:max_candidates]
    return candidates


def state_fingerprint(snapshot: dict[str, Any]) -> str:
    """Canonical state comparison that ignores the clock fields only."""

    import json

    value = copy.deepcopy(snapshot)
    for key in ("step", "day", "hour", "done"):
        value.pop(key, None)
    return json.dumps(value, sort_keys=True, separators=(",", ":"))
