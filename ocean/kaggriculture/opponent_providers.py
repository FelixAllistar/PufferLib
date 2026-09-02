"""Reactive continuation policies for offline counterfactual rollouts.

The provider API is deliberately tiny: a provider receives the complete state
that a player would observe and returns one structured action for that player.
The offline brancher invokes it after every native transition, so the opponent
is not a prerecorded tape.  ``RuleProvider`` is a deterministic baseline and
``NativePolicyProvider`` wraps the same NumPy policy adapter used by the
submission for learned-league continuations.
"""

from __future__ import annotations

import configparser
import copy
import hashlib
import importlib.util
import os
import pathlib
from dataclasses import dataclass
from typing import Any, Protocol

from macro_actions import CROPS, ANIMALS, PRODUCTS, pass_action
from macro_executor import ANIMAL_STRUCTURES, SEED_COSTS, ANIMAL_COSTS


BASE_PRICES = {
    "WHEAT": 25, "CARROT": 35, "TOMATO": 60, "STRAWBERRY": 120,
    "MELON": 250, "EGG": 50, "MILK": 160, "WOOL": 200, "FERTILIZER": 100,
}


class _TorchNativeMinGRU:
    """Torch implementation of the submission adapter for accelerated
    offline/live inference.

    The NumPy adapter remains the reference path. This class uses the same
    flat checkpoint layout and MinGRU equations, but keeps weights and hidden
    state on the requested device. Callers select it through
    NativePolicyProvider(backend="torch").
    """

    def __init__(self, module, path: str | pathlib.Path, device: str = "cuda"):
        import numpy as np
        import torch

        self._torch = torch
        self.device = torch.device(device)
        flat = np.fromfile(path, dtype=np.float32)
        self.hidden, self.layers_n = module._infer_arch(flat.size)
        hidden = self.hidden
        index = 0
        count = hidden * module.OBS_SIZE
        self.encoder = torch.as_tensor(
            flat[index:index + count].reshape(hidden, module.OBS_SIZE),
            device=self.device,
        )
        index += count
        count = (module.MASK_SIZE + 1) * hidden
        self.decoder = torch.as_tensor(
            flat[index:index + count].reshape(module.MASK_SIZE + 1, hidden),
            device=self.device,
        )
        index += count
        self.layers = []
        for _ in range(self.layers_n):
            count = 3 * hidden * hidden
            self.layers.append(torch.as_tensor(
                flat[index:index + count].reshape(3 * hidden, hidden),
                device=self.device,
            ))
            index += count
        self.state = [
            torch.zeros((1, hidden), dtype=torch.float32, device=self.device)
            for _ in range(self.layers_n)
        ]

    def reset(self):
        for state in self.state:
            state.zero_()

    def clone(self):
        result = object.__new__(type(self))
        result._torch = self._torch
        result.device = self.device
        result.hidden = self.hidden
        result.layers_n = self.layers_n
        result.encoder = self.encoder
        result.decoder = self.decoder
        result.layers = self.layers
        result.state = [state.clone() for state in self.state]
        return result

    def forward(self, obs):
        torch = self._torch
        with torch.no_grad():
            value = torch.as_tensor(obs, dtype=torch.float32, device=self.device)
            if value.ndim == 1:
                value = value.unsqueeze(0)
            x = (value * (1.0 / 255.0)) @ self.encoder.T
            hidden = self.hidden
            for layer_id, weights in enumerate(self.layers):
                combined = x @ weights.T
                h = combined[:, :hidden]
                gate = torch.sigmoid(combined[:, hidden:2 * hidden])
                candidate = torch.where(h >= 0, h + 0.5, torch.sigmoid(h))
                state = self.state[layer_id]
                next_state = state + gate * (candidate - state)
                highway = torch.sigmoid(combined[:, 2 * hidden:])
                x = highway * next_state + (1.0 - highway) * x
                self.state[layer_id] = next_state
            return (x @ self.decoder.T)[0].detach().cpu().numpy()

    def forward_batch(self, observations):
        """Forward independent snapshot-local observations as one GPU batch.

        Counterfactual branches intentionally reset recurrent state at every
        native snapshot because the serialized replay state has no hidden
        tensor.  That makes those inferences independent and safe to batch;
        live episode providers continue to use :meth:`forward` so their GRU
        state is never accidentally shared between branches.
        """
        torch = self._torch
        with torch.no_grad():
            value = torch.as_tensor(observations, dtype=torch.float32, device=self.device)
            if value.ndim == 1:
                value = value.unsqueeze(0)
            x = (value * (1.0 / 255.0)) @ self.encoder.T
            hidden = self.hidden
            batch = value.shape[0]
            for layer_id, weights in enumerate(self.layers):
                combined = x @ weights.T
                h = combined[:, :hidden]
                gate = torch.sigmoid(combined[:, hidden:2 * hidden])
                candidate = torch.where(h >= 0, h + 0.5, torch.sigmoid(h))
                # Snapshot-local calls have no recurrent history.  Do not use
                # the mutable single-episode state tensor here.
                state = torch.zeros((batch, hidden), dtype=torch.float32, device=self.device)
                next_state = state + gate * (candidate - state)
                highway = torch.sigmoid(combined[:, 2 * hidden:])
                x = highway * next_state + (1.0 - highway) * x
            return (x @ self.decoder.T).detach().cpu().numpy()


class ActionProvider(Protocol):
    def action(self, snapshot: dict[str, Any], player: int) -> dict[str, Any]:
        """Return one legal structured action for ``player``."""

    def begin_episode(self, episode_id: str, turn: int) -> None:
        """Select/reset any episode-scoped continuation state."""

    def fork(self) -> "ActionProvider":
        """Return an independent provider for one counterfactual branch."""


def _int(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


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


def _positions(snapshot: dict[str, Any], player: int) -> list[tuple[int, int]]:
    farm = _farm(snapshot, player)
    out: list[tuple[int, int]] = []
    farmer = farm.get("farmer")
    if isinstance(farmer, list) and len(farmer) >= 2:
        out.append((_int(farmer[0]), _int(farmer[1])))
    for value in farm.get("hands") or []:
        if isinstance(value, list) and len(value) >= 2:
            out.append((_int(value[0]), _int(value[1])))
    return out or [(4, 4)]


def _tiles(snapshot: dict[str, Any], player: int):
    for y, row in enumerate(_farm(snapshot, player).get("tiles") or []):
        if not isinstance(row, list):
            continue
        for x, tile in enumerate(row):
            yield x, y, tile


def _shed(private: dict[str, Any]) -> dict[str, int]:
    raw = private.get("shed") or {}
    return {item: max(0, _int(raw.get(item))) for item in (*PRODUCTS, *ANIMALS)}


def _inventory(private: dict[str, Any], unit: int) -> dict[str, int]:
    inventories = private.get("inventories") or []
    raw = inventories[unit] if 0 <= unit < len(inventories) and isinstance(inventories[unit], dict) else {}
    return {item: max(0, _int(raw.get(item))) for item in (*PRODUCTS, *ANIMALS)}


def _primitive(snapshot: dict[str, Any], player: int, unit: int, command: list[Any] | None, market: list[list[Any]]):
    hands = [["PASS"] for _ in (_farm(snapshot, player).get("hands") or [])]
    command = command or ["PASS"]
    if unit == 0:
        farmer = command
    else:
        farmer = ["PASS"]
        if unit <= len(hands):
            hands[unit - 1] = command
    return {"farmer": farmer, "hands": hands, "market": market}


def _toward(start: tuple[int, int], target: tuple[int, int]) -> list[Any]:
    x, y = start
    tx, ty = target
    if x < tx:
        return ["EAST"]
    if x > tx:
        return ["WEST"]
    if y < ty:
        return ["SOUTH"]
    if y > ty:
        return ["NORTH"]
    return ["PASS"]


def _task_candidates(snapshot: dict[str, Any], player: int, unit: int) -> list[tuple[tuple[int, int], list[Any]]]:
    private = _private(snapshot, player)
    inventory = _inventory(private, unit)
    out: list[tuple[tuple[int, int], list[Any]]] = []
    for x, y, tile in _tiles(snapshot, player):
        if not isinstance(tile, dict):
            continue
        kind = str(tile.get("kind", "")).upper()
        if kind == "PLANT":
            if not bool(tile.get("watered_today", False)):
                out.append(((x, y), ["WATER"]))
            elif _int(tile.get("yield_units")) > 0:
                out.append(((x, y), ["HARVEST"]))
        elif kind in {"COOP", "PASTURE"} and tile.get("animal"):
            if not bool(tile.get("fed_today", False)) and inventory.get("WHEAT", 0) > 0:
                out.append(((x, y), ["FEED"]))
            elif kind == "PASTURE" and not bool(tile.get("cared_today", False)):
                out.append(((x, y), ["CARE"]))
            elif bool(tile.get("fertilizer_available", False)):
                out.append(((x, y), ["COLLECT_FERTILIZER"]))
        elif kind in {"COOP", "PASTURE"} and not tile.get("animal"):
            for animal in ANIMALS:
                if inventory.get(animal, 0) > 0 and ANIMAL_STRUCTURES[animal] == kind:
                    out.append(((x, y), ["PLACE", animal]))
                    break
        elif tile is None:
            seeds = private.get("seeds") or {}
            available = [(crop, _int(seeds.get(crop))) for crop in CROPS]
            available = [(crop, count) for crop, count in available if count > 0]
            if available:
                # Price/cost chooses a sensible crop while remaining reactive
                # to the visible market rather than hard-coding strawberries.
                prices = (snapshot.get("market") or {}).get("prices") or {}
                crop = max(available, key=lambda value: float(prices.get(value[0], BASE_PRICES[value[0]])) / SEED_COSTS[value[0]])[0]
                out.append(((x, y), ["PLANT", crop]))
    return out


@dataclass
class RuleProvider:
    """A deterministic, reactive economy/maintenance opponent baseline."""

    episode_steps: int = 720
    liquidation_turns: int = 48
    sell_price_ratio: float = 1.25

    def _market_order(self, snapshot: dict[str, Any], player: int) -> list[list[Any]]:
        farm = _farm(snapshot, player)
        private = _private(snapshot, player)
        shed = _shed(private)
        step = _int(snapshot.get("step"))
        prices = (snapshot.get("market") or {}).get("prices") or {}
        remaining = max(0, self.episode_steps - step)
        if remaining <= self.liquidation_turns:
            return [["SELL", item, amount] for item, amount in shed.items() if item in PRODUCTS and amount > 0]
        valuable = [
            (float(prices.get(item, BASE_PRICES[item])) / BASE_PRICES[item], item, amount)
            for item, amount in shed.items() if item in PRODUCTS and amount > 0
        ]
        valuable = [item for item in valuable if item[0] >= self.sell_price_ratio]
        if valuable:
            _, item, amount = max(valuable)
            return [["SELL", item, amount]]
        money = _int(farm.get("money"))
        unlocked = len(farm.get("unlocked_quadrants") or [])
        land_prices = (1000, 2000, 4000)
        if unlocked < 4 and money >= int(land_prices[max(0, unlocked - 1)] * 1.1):
            return [["BUY_LAND"]]
        empty = sum(1 for _, _, tile in _tiles(snapshot, player) if tile is None)
        seeds = private.get("seeds") or {}
        prices = (snapshot.get("market") or {}).get("prices") or {}
        if empty and money >= min(SEED_COSTS.values()):
            crop = max(CROPS, key=lambda item: float(prices.get(item, BASE_PRICES[item])) / SEED_COSTS[item])
            cost = SEED_COSTS[crop]
            if money >= cost:
                return [["BUY_SEED", crop, 1]]
        shed_room = 100 - sum(shed.values())
        if shed_room > 0:
            for animal in ANIMALS:
                structure = ANIMAL_STRUCTURES[animal]
                held = shed.get(animal, 0) + sum(
                    _inventory(private, unit).get(animal, 0)
                    for unit in range(len(private.get("inventories") or []))
                )
                has_structure = any(
                    isinstance(tile, dict) and str(tile.get("kind", "")).upper() == structure
                    and not tile.get("animal") for _, _, tile in _tiles(snapshot, player)
                )
                if has_structure and held == 0 and money >= ANIMAL_COSTS[animal]:
                    return [["BUY_ANIMAL", animal, 1]]
        return []

    def action(self, snapshot: dict[str, Any], player: int) -> dict[str, Any]:
        positions = _positions(snapshot, player)
        tasks = _task_candidates(snapshot, player, 0)
        private = _private(snapshot, player)
        shed = _shed(private)
        # A feed/placement task is impossible until the farmer has moved an
        # item from the shed into its unit inventory.  Make that transfer an
        # explicit reactive task instead of emitting a guaranteed no-op FEED.
        needs_feed = any(
            isinstance(tile, dict)
            and str(tile.get("kind", "")).upper() in {"COOP", "PASTURE"}
            and tile.get("animal")
            and not bool(tile.get("fed_today", False))
            for _, _, tile in _tiles(snapshot, player)
        )
        needs_animal_pickup = any(
            isinstance(tile, dict)
            and str(tile.get("kind", "")).upper() in {"COOP", "PASTURE"}
            and not tile.get("animal")
            for _, _, tile in _tiles(snapshot, player)
        )
        access = (4, 4)
        if needs_feed and shed.get("WHEAT", 0) > 0 and _inventory(private, 0).get("WHEAT", 0) == 0:
            tasks.insert(0, (access, ["PICKUP", "WHEAT", min(10, shed["WHEAT"])]))
        elif needs_animal_pickup:
            empty_structures = {
                str(tile.get("kind", "")).upper()
                for _, _, tile in _tiles(snapshot, player)
                if isinstance(tile, dict) and not tile.get("animal")
            }
            animal = next(
                (candidate for candidate in ANIMALS
                 if shed.get(candidate, 0) > 0
                 and ANIMAL_STRUCTURES[candidate] in empty_structures),
                None,
            )
            if animal is not None and _inventory(private, 0).get(animal, 0) == 0:
                tasks.insert(0, (access, ["PICKUP", animal, 1]))
        # Assign each unit its nearest currently visible task.  Tasks are
        # removed as they are assigned so hands do not all pile onto one tile.
        remaining = list(tasks)
        chosen: list[tuple[int, list[Any]]] = []
        for unit, position in enumerate(positions):
            if not remaining:
                chosen.append((unit, ["PASS"]))
                continue
            index, (target, command) = min(
                enumerate(remaining),
                key=lambda item: abs(position[0] - item[1][0][0]) + abs(position[1] - item[1][0][1]),
            )
            remaining.pop(index)
            chosen.append((unit, command if position == target else _toward(position, target)))
        market = self._market_order(snapshot, player)
        action = pass_action()
        action["hands"] = [["PASS"] for _ in (_farm(snapshot, player).get("hands") or [])]
        action["market"] = market
        for unit, command in chosen:
            if unit == 0:
                action["farmer"] = command
            elif unit - 1 < len(action["hands"]):
                action["hands"][unit - 1] = command
        return action

    def begin_episode(self, episode_id: str, turn: int) -> None:
        del episode_id, turn

    def fork(self) -> "RuleProvider":
        return self


@dataclass
class PassProvider:
    def action(self, snapshot: dict[str, Any], player: int) -> dict[str, Any]:
        return pass_action()

    def begin_episode(self, episode_id: str, turn: int) -> None:
        del episode_id, turn

    def fork(self) -> "PassProvider":
        return self


def _submission_module():
    """Load the submission's pure NumPy policy adapter without side effects."""

    path = pathlib.Path(__file__).with_name("submission") / "main.py"
    spec = importlib.util.spec_from_file_location("kaggriculture_submission_policy", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load policy adapter: {path}")
    module = importlib.util.module_from_spec(spec)
    # The submission module normally auto-loads ``kaggriculture_v4.bin`` at
    # import time.  That is useful in Kaggle, but an offline provider must load
    # its explicit checkpoints instead (and a stale cwd file may be ABI-old).
    missing_model = str(path.with_name(".offline_provider_no_model"))
    previous_model = os.environ.get("PUFFERLIB_MODEL_PATH")
    os.environ["PUFFERLIB_MODEL_PATH"] = missing_model
    try:
        spec.loader.exec_module(module)
    finally:
        if previous_model is None:
            os.environ.pop("PUFFERLIB_MODEL_PATH", None)
        else:
            os.environ["PUFFERLIB_MODEL_PATH"] = previous_model
    return module


def _strip_path(value: str) -> str:
    value = str(value).strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "'\"":
        value = value[1:-1]
    return value


def _resolve_path(raw: str, league: pathlib.Path | None = None) -> pathlib.Path:
    value = pathlib.Path(_strip_path(raw)).expanduser()
    if value.is_absolute():
        return value
    roots = [pathlib.Path.cwd()]
    if league is not None:
        # League files in this repository store paths relative to the repo
        # root (``saved/...``), while copied archives may live below a backup
        # root.  Search the league directory and its ancestors so both forms
        # resolve without rewriting the league file.
        roots.extend([league.parent, *league.parent.parents])
    candidates = []
    for root in roots:
        candidate = root / value
        if candidate not in candidates:
            candidates.append(candidate)
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    return candidates[0].resolve()


def policy_paths_from_league(
    league: str | pathlib.Path | None = None,
    policy_models: list[str] | tuple[str, ...] | None = None,
) -> list[pathlib.Path]:
    """Resolve explicit checkpoints and enabled ``[policy.*]`` league paths."""

    paths: list[pathlib.Path] = []
    for value in policy_models or ():
        # Accept comma-separated values as a convenience for shell scripts,
        # while repeated ``--policy-model`` flags remain unambiguous.
        paths.extend(_resolve_path(item) for item in str(value).split(",") if item.strip())
    if league:
        league_path = pathlib.Path(league).expanduser().resolve()
        parser = configparser.ConfigParser(interpolation=None)
        if not parser.read(league_path):
            raise FileNotFoundError(f"league file not found: {league_path}")
        for section in parser.sections():
            if not section.startswith("policy."):
                continue
            if parser.getint(section, "enabled", fallback=1) == 0:
                continue
            raw = parser.get(section, "path", fallback="").strip()
            if raw:
                paths.append(_resolve_path(raw, league_path))
    unique: list[pathlib.Path] = []
    seen: set[pathlib.Path] = set()
    for path in paths:
        resolved = path.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)
        if not resolved.is_file():
            raise FileNotFoundError(f"learned opponent checkpoint not found: {resolved}")
        unique.append(resolved)
    if not unique:
        raise ValueError("learned opponent mode requires --policy-model or --league")
    return unique


@dataclass
class NativePolicyProvider:
    """Run learned native MinGRU checkpoints as branch continuations.

    A replay-bank state does not include the policy's recurrent hidden state.
    This adapter therefore treats each saved state as a snapshot-local decision
    and resets the MinGRU before each action.  It is a learned-opponent stressor,
    not a claim of recurrent-history parity.  The selected league member is
    fixed for an episode and copied into every candidate branch.
    """

    paths: tuple[str, ...] | list[str]
    deterministic: bool = True
    seed: int = 707
    policy_weights: tuple[float, ...] | list[float] | None = None
    # Offline replay snapshots do not carry a recurrent hidden state, so the
    # historical provider resets before every action.  A live episode runner
    # sets this false and keeps one MinGRU state across turns instead.
    snapshot_local: bool = True
    backend: str = "numpy"
    device: str = "cuda"

    def __post_init__(self) -> None:
        import numpy as np

        self.paths = tuple(str(pathlib.Path(path).resolve()) for path in self.paths)
        if not self.paths:
            raise ValueError("NativePolicyProvider requires at least one checkpoint")
        self._module = _submission_module()
        if self.backend not in {"numpy", "torch"}:
            raise ValueError("backend must be 'numpy' or 'torch'")
        if self.backend == "torch":
            self._models = [
                _TorchNativeMinGRU(self._module, path, self.device)
                for path in self.paths
            ]
        else:
            self._models = [self._module.NativeMinGRU(path) for path in self.paths]
        if self.policy_weights is None:
            self._weights = [1.0] * len(self._models)
        else:
            self._weights = [float(value) for value in self.policy_weights]
            if len(self._weights) != len(self._models) or any(value < 0 for value in self._weights):
                raise ValueError("policy_weights must match checkpoints and be nonnegative")
            if not any(self._weights):
                raise ValueError("policy_weights must contain a positive value")
        self._selected = 0
        self._rng = np.random.default_rng(self.seed)

    def begin_episode(self, episode_id: str, turn: int) -> None:
        digest = hashlib.sha256(f"{self.seed}:{episode_id}:{turn}".encode("utf-8")).digest()
        unit = int.from_bytes(digest[:8], "little") / float(2**64)
        total = sum(self._weights)
        cursor = 0.0
        self._selected = len(self._weights) - 1
        for index, weight in enumerate(self._weights):
            cursor += weight / total
            if unit < cursor:
                self._selected = index
                break
        # A provider can be reused for many independent games.  Reset every
        # loaded model, not only the selected member, so a later league choice
        # cannot inherit hidden state from a previous episode.
        for model in self._models:
            model.reset()

    def fork(self) -> "NativePolicyProvider":
        # Models are read-only after construction and each action resets its
        # selected model.  Copy the RNG state for stochastic branch parity.
        child = copy.copy(self)
        import numpy as np

        # Snapshot-local branches can safely share immutable weights, but a
        # live recurrent provider must own its mutable hidden states.  The
        # model clone helper preserves the selected policy's exact state.
        if not self.snapshot_local:
            child._models = [model.clone() for model in self._models]
        child._rng = np.random.default_rng()
        child._rng.bit_generator.state = copy.deepcopy(self._rng.bit_generator.state)
        return child

    @staticmethod
    def _sample_heads(module, logits, mask, deterministic: bool, rng):
        import numpy as np

        actions = np.zeros(len(module.HEAD_SIZES), dtype=np.int32)

        def sample(head: int) -> None:
            start, end = module.HEAD_OFFSETS[head], module.HEAD_OFFSETS[head + 1]
            legal = np.flatnonzero(mask[start:end])
            if legal.size == 0:
                actions[head] = 0
                return
            values = logits[start:end][legal]
            if deterministic:
                actions[head] = legal[int(np.argmax(values))]
                return
            values = values - np.max(values)
            probabilities = np.exp(values)
            total = float(np.sum(probabilities))
            if not total or not np.isfinite(total):
                actions[head] = legal[int(np.argmax(values))]
                return
            probabilities /= total
            actions[head] = legal[int(rng.choice(legal.size, p=probabilities))]

        for head in range(module.UNIT_HEADS):
            sample(head)
        for slot in range(module.MARKET_SLOTS):
            continuation = module.UNIT_HEADS + 3 * slot
            sample(continuation)
            if actions[continuation] == 0:
                break
            sample(continuation + 1)
            if actions[continuation + 1] < 19:
                sample(continuation + 2)
        return actions

    def action(self, snapshot: dict[str, Any], player: int) -> dict[str, Any]:
        import numpy as np

        view = dict(snapshot)
        view["player"] = int(player)
        privates = snapshot.get("privates") or []
        view["private"] = privates[player] if 0 <= player < len(privates) else {}
        module = self._module
        model = self._models[self._selected]
        if self.snapshot_local:
            model.reset()
        encoded = module.encode_observation(view)
        mask = module.action_mask(view)
        logits = model.forward(encoded)[:module.MASK_SIZE]
        action_ids = self._sample_heads(
            module, logits, np.asarray(mask, dtype=bool), self.deterministic, self._rng,
        )
        return module.decode_actions(view, action_ids)

    def action_batch(
        self, snapshots: list[dict[str, Any]], players: list[int] | tuple[int, ...],
    ) -> list[dict[str, Any]]:
        """Decode actions for independent snapshot-local states in one batch.

        This is the performance path used by the counterfactual writer.  A
        live recurrent provider falls back to serial ``action`` calls because
        each stream owns different hidden state.  The deterministic output of
        the batch path matches serial inference; stochastic rows consume the
        provider RNG in row order.
        """
        import numpy as np

        if len(snapshots) != len(players):
            raise ValueError("snapshots and players must have equal length")
        if not snapshots:
            return []
        if self.backend != "torch" or not self.snapshot_local:
            return [self.action(snapshot, int(player))
                    for snapshot, player in zip(snapshots, players)]
        module = self._module
        views: list[dict[str, Any]] = []
        encoded: list[Any] = []
        masks: list[Any] = []
        for snapshot, player in zip(snapshots, players):
            view = dict(snapshot)
            view["player"] = int(player)
            privates = snapshot.get("privates") or []
            view["private"] = privates[player] if 0 <= int(player) < len(privates) else {}
            views.append(view)
            encoded.append(module.encode_observation(view))
            masks.append(np.asarray(module.action_mask(view), dtype=bool))
        logits = self._models[self._selected].forward_batch(np.stack(encoded, axis=0))
        return [
            module.decode_actions(
                view,
                self._sample_heads(module, logits[index], masks[index], self.deterministic, self._rng),
            )
            for index, view in enumerate(views)
        ]

    def native_action(self, lib, state, player: int) -> dict[str, Any]:
        """Run one recurrent policy step from the exact native view.

        Unlike :meth:`native_action_batch`, this preserves the selected
        model's mutable recurrent state and is therefore suitable for live
        episode streams and MPC fallback branches.  It uses the same C
        observation/mask writers as the trainer, avoiding a JSON round-trip
        that could otherwise hide an encoder mismatch.
        """

        import ctypes
        import numpy as np

        if not (
            hasattr(lib, "kg_policy_observation")
            and hasattr(lib, "kg_policy_action_mask")
            and hasattr(lib, "kg_policy_hand_count")
        ):
            from replay_native import c_snapshot

            return self.action(c_snapshot(lib, state), int(player))
        module = self._module
        observation = np.empty(module.OBS_SIZE, dtype=np.uint8)
        mask = np.empty(module.MASK_SIZE, dtype=np.uint8)
        lib.kg_policy_observation(
            state, int(player),
            observation.ctypes.data_as(ctypes.c_void_p), module.OBS_SIZE,
        )
        lib.kg_policy_action_mask(
            state, int(player),
            mask.ctypes.data_as(ctypes.c_void_p), module.MASK_SIZE,
        )
        model = self._models[self._selected]
        if self.snapshot_local:
            model.reset()
        logits = model.forward(observation)[:module.MASK_SIZE]
        action_ids = self._sample_heads(
            module, logits, mask.astype(bool), self.deterministic, self._rng,
        )
        hand_count = int(lib.kg_policy_hand_count(state, int(player)))
        # decode_actions needs only observer-relative hand cardinality.  The
        # native mask already handled all state-dependent legality.
        view = {
            "player": 0,
            "farms": [{"hands": [None] * hand_count}, {"hands": []}],
        }
        return module.decode_actions(view, action_ids)

    def native_action_batch(self, lib, states, players) -> list[dict[str, Any]]:
        """Infer directly from native KGState pointers when the fast view exists.

        The C helper is the production observation/mask writer, so this path
        removes both JSON serialization and the Python feature walk from large
        counterfactual batches.  It is restricted to snapshot-local Torch
        providers; live recurrent streams intentionally keep the ordinary
        per-snapshot path.
        """
        import ctypes
        import numpy as np

        if len(states) != len(players):
            raise ValueError("states and players must have equal length")
        if not states:
            return []
        if (
            self.backend != "torch" or not self.snapshot_local
            or not hasattr(lib, "kg_policy_observation")
            or not hasattr(lib, "kg_policy_action_mask")
            or not hasattr(lib, "kg_policy_hand_count")
        ):
            snapshots = [
                __import__("replay_native", fromlist=["c_snapshot"]).c_snapshot(lib, state)
                for state in states
            ]
            return self.action_batch(snapshots, players)
        module = self._module
        observations = np.empty((len(states), module.OBS_SIZE), dtype=np.uint8)
        masks = np.empty((len(states), module.MASK_SIZE), dtype=np.uint8)
        hand_counts: list[int] = []
        for index, (state, player) in enumerate(zip(states, players)):
            observation_ptr = observations[index].ctypes.data_as(ctypes.c_void_p)
            mask_ptr = masks[index].ctypes.data_as(ctypes.c_void_p)
            lib.kg_policy_observation(state, int(player), observation_ptr, module.OBS_SIZE)
            lib.kg_policy_action_mask(state, int(player), mask_ptr, module.MASK_SIZE)
            hand_counts.append(int(lib.kg_policy_hand_count(state, int(player))))
        logits = self._models[self._selected].forward_batch(observations)
        actions: list[dict[str, Any]] = []
        for index, player in enumerate(players):
            action_ids = self._sample_heads(
                module, logits[index], masks[index].astype(bool),
                self.deterministic, self._rng,
            )
            # decode_actions only needs the observer-relative hand cardinality;
            # all legality decisions have already been made by the exact C mask.
            view = {
                "player": 0,
                "farms": [
                    {"hands": [None] * hand_counts[index]},
                    {"hands": []},
                ],
            }
            actions.append(module.decode_actions(view, action_ids))
        return actions


def learned_provider(
    *, league: str | pathlib.Path | None = None,
    policy_models: list[str] | tuple[str, ...] | None = None,
    deterministic: bool = True, seed: int = 707, snapshot_local: bool = True,
    backend: str = "numpy", device: str = "cuda",
) -> NativePolicyProvider:
    paths = policy_paths_from_league(league, policy_models)
    return NativePolicyProvider(
        tuple(str(path) for path in paths), deterministic=deterministic,
        seed=seed, snapshot_local=snapshot_local, backend=backend, device=device,
    )
