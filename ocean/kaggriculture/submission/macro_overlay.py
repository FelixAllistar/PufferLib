"""Submission-safe strategic overlay for the Kaggriculture export.

The offline evaluator can use the native simulator to branch a complete hidden
state.  A Kaggle submission cannot: it receives one player's observation and
does not have the opponent's private inventory or a native state snapshot.
This module therefore ports the deployable part of that controller: the
learned macro value model and the deterministic macro executor.  The proven
top hybrid bot remains the primitive executor, so a bad model proposal never
turns the submission into a PASS policy.  Explicit macros own only the farmer
and market fields they actually use; top-bot maintenance/harvest work on the
other hands is retained.
"""

from __future__ import annotations

import copy
import importlib.util
import os
import pathlib
import sys
from typing import Any


_HERE = pathlib.Path(__file__).resolve().parent
# In the source tree the macro modules live one directory above submission;
# packaged archives copy them next to main.py.
for _path in (_HERE, _HERE.parent):
    if str(_path) not in sys.path:
        sys.path.insert(0, str(_path))

try:
    from macro_actions import candidate_actions
    from macro_value_model import MacroValueModel
except Exception:  # pragma: no cover - export without optional overlay files
    candidate_actions = None
    MacroValueModel = None


def _load_top_bot():
    """Load the pure deterministic executor without importing main.py."""

    candidates = (_HERE / "top_bot.py", _HERE / "top_bot" / "main.py")
    source = next((path for path in candidates if path.is_file()), None)
    if source is None:
        return None
    spec = importlib.util.spec_from_file_location("kag_submission_top_bot", source)
    if spec is None or spec.loader is None:
        return None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _snapshot(obs: dict[str, Any]) -> dict[str, Any]:
    """Convert one Kaggle observation to the public macro snapshot contract."""

    player = int(obs.get("player", 0))
    private = obs.get("private") or {}
    # Only the acting player's private fields are present.  Keeping the other
    # entry empty is deliberate: macro_actions.public_features never reads
    # opponent private inventory.
    privates = [{}, {}]
    if player in (0, 1):
        privates[player] = private
    return {
        "player": player,
        "step": int(obs.get("step", int(obs.get("day", 0)) * 24 + int(obs.get("hour", 0)))),
        "day": int(obs.get("day", 0)),
        "hour": int(obs.get("hour", 0)),
        "farms": obs.get("farms") or [],
        "privates": privates,
        "market": obs.get("market") or {},
        "town": obs.get("town") or {},
    }


def _merge(macro: dict[str, Any], fallback: dict[str, Any]) -> dict[str, Any]:
    """Overlay a macro while retaining fallback routine work.

    The offline evaluator's merge intentionally replaces an empty market list,
    because its PPO continuation is the branch baseline.  In the submission,
    an empty macro market means "no strategic order"; retaining top-bot market
    orders is essential for selling and feed purchases during a route plan.
    """

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
    market = macro.get("market")
    if isinstance(market, list) and market:
        # A strategic plan may need one purchase, but the deterministic
        # executor may simultaneously have a feed/sell order queued.  Keep
        # the strategic orders first, then append distinct fallback orders up
        # to the native ten-order limit instead of silently deleting routine
        # economy work.
        merged_market: list[Any] = []
        seen: set[tuple[Any, ...]] = set()
        for order in [*market, *(fallback.get("market") or [])]:
            if not isinstance(order, list) or not order:
                continue
            key = tuple(order)
            if key in seen:
                continue
            seen.add(key)
            merged_market.append(copy.deepcopy(order))
            if len(merged_market) >= 10:
                break
        result["market"] = merged_market
    return result


class PortableMacroOverlay:
    """Greedy macro controller usable from the Kaggle Python callback."""

    def __init__(self, model_path: pathlib.Path | None):
        self.model = None
        self.top_bot = _load_top_bot()
        if (candidate_actions is not None and MacroValueModel is not None
                and model_path is not None and model_path.is_file()):
            try:
                self.model = MacroValueModel.load(model_path)
            except Exception:
                # A missing/corrupt optional artifact must not prevent the
                # ordinary top-bot export from loading.
                self.model = None
        self.interval = max(1, int(os.environ.get("PUFFERLIB_MACRO_INTERVAL", "24")))
        # The model is a proposal generator trained against a scripted
        # continuation.  A modest positive margin keeps weak extrapolations
        # from displacing the proven executor.
        self.min_prediction = float(os.environ.get("PUFFERLIB_MACRO_MIN_PREDICTION", "250"))
        self.episode_steps = 720
        self.shed_capacity = 100
        self._queues: dict[int, list[dict[str, Any]]] = {}
        self._next: dict[int, int] = {}
        self._last_step: dict[int, int] = {}

    @property
    def enabled(self) -> bool:
        return self.model is not None

    def fallback(self, obs: dict[str, Any], learned_fallback) -> dict[str, Any]:
        """Use the top hybrid executor; neural policy is emergency fallback."""

        if self.top_bot is not None:
            try:
                return self.top_bot.agent(obs)
            except Exception:
                pass
        return learned_fallback()

    def _reset_if_needed(self, player: int, step: int) -> None:
        previous = self._last_step.get(player)
        if step == 0 or (previous is not None and step <= previous):
            self._queues[player] = []
            self._next[player] = 0
        self._last_step[player] = step

    def _choose(self, snapshot: dict[str, Any], player: int, step: int):
        candidates = candidate_actions(
            snapshot, player, include_strategic=True,
            episode_steps=self.episode_steps, shed_capacity=self.shed_capacity,
        )
        if not candidates:
            return None
        predictions = self.model.predict_candidates(
            snapshot, player, candidates,
            episode_steps=self.episode_steps, turns_per_day=24,
            shed_capacity=self.shed_capacity,
        )
        # The top hybrid executor already owns routine maintenance, harvest,
        # and liquidation.  Only let the learned layer propose investments or
        # planting plans; otherwise a model trained on counterfactual branches
        # can steal the farmer from an urgent chore without seeing hidden state.
        strategic_kinds = {"PLANT", "BUILD_ANIMAL", "BUY_LAND"}
        allowed = {
            i for i, candidate in enumerate(candidates)
            if candidate.kind == "HOLD" or candidate.kind in strategic_kinds
        }
        # Near the terminal, no new investment can realize reliably.  The
        # executor continues selling/harvesting normally and the overlay stays
        # out of the way.
        remaining = max(0, self.episode_steps - step)
        if remaining <= 96:
            allowed = {
                i for i, candidate in enumerate(candidates)
                if candidate.kind == "HOLD"
            }
        if not allowed:
            return None
        hold = next((i for i, candidate in enumerate(candidates)
                     if candidate.kind == "HOLD"), None)
        best = max(allowed, key=lambda i: (float(predictions[i]), -i))
        candidate = candidates[best]
        # A plan must leave enough time for its first production/maintenance
        # cycle.  This prevents late-game planting/expansion from consuming
        # cash while the top bot is already liquidating.
        minimum_time = {
            "PLANT": 120,
            "BUILD_ANIMAL": 180,
            "BUY_LAND": 240,
        }.get(candidate.kind, 0)
        if candidate.kind != "HOLD" and remaining < minimum_time:
            return None
        if hold is not None and candidates[best].kind != "HOLD":
            # The ridge model predicts candidate-minus-baseline cash.  Do not
            # spend/route merely because extrapolation is positive by a tiny
            # amount; top-bot remains the routine baseline.
            if float(predictions[best]) <= max(
                    float(self.min_prediction), float(predictions[hold])):
                return None
        return candidate

    def action(self, obs: dict[str, Any], fallback_action: dict[str, Any]) -> dict[str, Any]:
        if not self.enabled:
            return fallback_action
        player = int(obs.get("player", 0))
        step = int(obs.get("step", int(obs.get("day", 0)) * 24 + int(obs.get("hour", 0))))
        self._reset_if_needed(player, step)
        queue = self._queues.setdefault(player, [])
        # Preserve the known high-quality opening tape.  It establishes the
        # initial mixed crop/animal economy before learned macro decisions.
        if step < 26:
            return fallback_action
        if queue:
            return _merge(queue.pop(0), fallback_action)
        if step < self._next.get(player, 0):
            return fallback_action
        try:
            snapshot = _snapshot(obs)
            selected = self._choose(snapshot, player, step)
        except Exception:
            selected = None
        self._next[player] = step + self.interval
        if selected is None or selected.kind == "HOLD":
            return fallback_action
        queue.extend(selected.action_sequence())
        return _merge(queue.pop(0), fallback_action)


def make_overlay() -> PortableMacroOverlay | None:
    # Enhanced exports default to the proven deterministic top-bot executor.
    # The public-observation ridge proposal layer is opt-in because exact MPC
    # safety guards require the native hidden state unavailable to a Kaggle
    # callback.  ``PUFFERLIB_MACRO_OVERLAY=1`` enables that experimental layer
    # for local A/B runs; ``0`` keeps the raw neural export for compatibility.
    mode = os.environ.get("PUFFERLIB_MACRO_OVERLAY", "topbot").lower()
    if mode in {"0", "off", "raw"}:
        return None
    if mode in {"topbot", "deterministic", "safe"}:
        return PortableMacroOverlay(None)
    override = os.environ.get("PUFFERLIB_MACRO_MODEL_PATH")
    path = pathlib.Path(override) if override else _HERE / "macro_learned_72_ridge.npz"
    return PortableMacroOverlay(path)
