#!/usr/bin/env python3
"""Run a complete native Kaggriculture episode with a macro controller.

This is the end-to-end comparison gate for the offline planning stack.  The
opponent and PPO baseline use the same native policy adapter and recurrent
state that the submission uses.  The macro side chooses a feasible strategic candidate
at sparse decision points, expands it into primitive actions with the
deterministic executor, and is evaluated against the exact native simulator.

The MPC lookahead is deliberately separate from the live episode state: each
candidate is restored from the identical serialized bytes and rolled forward
against a snapshot-local copy of the opponent policy.  This makes candidate
comparisons common-random-number and prevents lookahead from mutating the
actual recurrent opponent.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import pathlib
from dataclasses import dataclass
from typing import Any

from macro_actions import MacroAction, candidate_actions, merge_macro_action, pass_action
from macro_value_model import MacroValueModel, choose_best
from opponent_providers import NativePolicyProvider, policy_paths_from_league
from replay_native import CAction, CConfig, c_action, c_snapshot, clone_action, load_core


DEFAULT_LIB = pathlib.Path(__file__).with_name("build") / "libkaggriculture.so"


def _restore(lib, payload: bytes, config: CConfig):
    state = lib.kg_create(ctypes.byref(config))
    if not state:
        raise RuntimeError("kg_create failed while restoring an episode branch")
    buffer = ctypes.create_string_buffer(payload, len(payload))
    if not lib.kg_state_deserialize(state, buffer, len(payload)):
        lib.kg_destroy(state)
        raise RuntimeError("kg_state_deserialize rejected an episode branch")
    return state


def _serialize(lib, state, size: int) -> bytes:
    buffer = (ctypes.c_ubyte * size)()
    if not lib.kg_state_serialize(state, buffer, size):
        raise RuntimeError("kg_state_serialize failed during episode lookahead")
    return bytes(buffer)


def _step_pair(lib, state, left: dict[str, Any] | CAction, right: dict[str, Any] | CAction) -> None:
    actions = (CAction * 2)()
    actions[0] = left if isinstance(left, CAction) else c_action(left)
    actions[1] = right if isinstance(right, CAction) else c_action(right)
    lib.kg_step(state, actions)


def _empty_action() -> dict[str, Any]:
    return pass_action()


def _provider_action(
    provider: NativePolicyProvider, lib, state, snapshot: dict[str, Any], player: int,
) -> dict[str, Any]:
    """Prefer the production native observation path for one policy step."""

    if hasattr(provider, "native_action"):
        return provider.native_action(lib, state, player)
    return provider.action(snapshot, player)


def _branch_candidate(
    lib,
    payload: bytes,
    config: CConfig,
    state_size: int,
    player: int,
    candidate: MacroAction | None,
    opponent_path: pathlib.Path,
    fallback_provider: NativePolicyProvider | None = None,
    first_fallback_action: dict[str, Any] | None = None,
    *,
    horizon: int,
    episode_id: str,
    seed: int,
    deterministic: bool,
    policy_backend: str,
    policy_device: str,
    opponent_provider: NativePolicyProvider | None = None,
) -> float:
    """Roll one macro candidate against separate PPO continuation streams.

    The serialized branch has no recurrent history.  Offline calls therefore
    reset a snapshot-local opponent, while live calls pass the current
    opponent provider and fork its hidden state/RNG at the decision boundary.
    The controlled player's fallback PPO is cloned from the live episode when
    supplied.  Once the macro sequence ends, that clone must keep acting;
    emitting PASS here would evaluate a macro-plus-idle policy rather than a
    strategic overlay.
    """

    state = _restore(lib, payload, config)
    # A live controller must branch the opponent's current recurrent stream,
    # not construct a snapshot-local policy that resets its hidden state on
    # every turn.  The latter is useful for offline replay snapshots but can
    # make a lookahead approve a harmful action simply because the branch is
    # playing a different opponent. ``fork`` copies hidden state and RNG, so
    # candidate and baseline branches remain common-random-number exact.
    if opponent_provider is not None:
        opponent = opponent_provider.fork()
    else:
        opponent = NativePolicyProvider(
            (str(opponent_path),), deterministic=deterministic, seed=seed,
            snapshot_local=True, backend=policy_backend, device=policy_device,
        )
        opponent.begin_episode(f"{episode_id}:lookahead", int(lib.kg_state_step(state)))
    fallback = fallback_provider.fork() if fallback_provider is not None else None
    sequence = candidate.action_sequence() if candidate is not None else ()
    try:
        steps = 0
        while not bool(lib.kg_done(state)) and steps < max(1, int(horizon)):
            if (
                policy_backend == "torch"
                and bool(getattr(opponent, "snapshot_local", False))
                and hasattr(opponent, "native_action_batch")
            ):
                # Lookahead opponents are snapshot-local by construction. Use
                # the exact native observation/mask writer rather than
                # serializing a JSON snapshot at every branch turn.
                opponent_action = opponent.native_action_batch(
                    lib, [state], [1 - player],
                )[0]
            else:
                snapshot = c_snapshot(lib, state)
                opponent_action = _provider_action(
                    opponent, lib, state, snapshot, 1 - player,
                )
            if steps < len(sequence):
                if steps == 0 and first_fallback_action is not None:
                    fallback_action = first_fallback_action
                elif fallback is not None:
                    fallback_action = _provider_action(
                        fallback, lib, state, c_snapshot(lib, state), player,
                    )
                else:
                    fallback_action = _empty_action()
                own_action = merge_macro_action(sequence[steps], fallback_action)
            elif steps == 0 and first_fallback_action is not None:
                # ``first_fallback_action`` was already sampled by the live
                # controller before it entered selection.  Reuse it for the
                # baseline branch rather than consuming the same recurrent
                # policy/RNG stream a second time.
                own_action = first_fallback_action
            elif fallback is not None:
                # The branch owns this provider clone, so other candidates and
                # the live controller retain independent recurrent state.
                own_action = _provider_action(
                    fallback, lib, state, c_snapshot(lib, state), player,
                )
            else:
                own_action = _empty_action()
            pair: list[dict[str, Any] | CAction] = [_empty_action(), _empty_action()]
            pair[player] = own_action
            pair[1 - player] = opponent_action
            _step_pair(lib, state, pair[0], pair[1])
            steps += 1
        return float(lib.kg_player_money(state, player))
    finally:
        lib.kg_destroy(state)


@dataclass
class MacroEpisodeController:
    """Live macro controller with greedy or bounded exact-MPC selection."""

    lib: Any
    config: CConfig
    state_size: int
    model: MacroValueModel
    opponent_path: pathlib.Path
    player: int
    mode: str = "greedy"
    decision_interval: int = 24
    top_k: int = 4
    lookahead: int = 24
    # Optional longer proposal-safety horizon.  Zero means use ``lookahead``;
    # setting this to the episode horizon enables an exact terminal-cash gate
    # while preserving bounded top-K MPC ranking.
    guard_horizon: int = 0
    deterministic: bool = True
    episode_id: str = "episode"
    seed: int = 707
    episode_steps: int = 720
    turns_per_day: int = 24
    shed_capacity: int = 100
    policy_backend: str = "numpy"
    policy_device: str = "cuda"
    min_prediction: float = 0.0
    # The macro controller is a strategic overlay, not a replacement for the
    # primitive policy.  Once a one-turn macro has been issued (or while the
    # controller is between decision boundaries), the fallback PPO must keep
    # operating the farm.  Without this, the old implementation emitted PASS
    # for every non-decision turn and therefore measured a do-nothing policy,
    # not a hierarchical policy.
    fallback_provider: NativePolicyProvider | None = None
    # Current live opponent stream for exact lookahead.  Forking this provider
    # preserves recurrent hidden state and stochastic RNG at the decision
    # boundary; offline branches still construct snapshot-local providers.
    opponent_provider: NativePolicyProvider | None = None

    def __post_init__(self) -> None:
        self._queue: list[dict[str, Any]] = []
        self.decisions: list[dict[str, Any]] = []
        self._next_decision_step = 0

    def reset(self, episode_id: str, seed: int) -> None:
        self.episode_id = str(episode_id)
        self.seed = int(seed)
        self._queue = []
        self.decisions = []
        self._next_decision_step = 0

    def _select(
        self, snapshot: dict[str, Any], state, step: int,
        fallback_action: dict[str, Any] | None = None,
    ) -> tuple[MacroAction, float, float | None]:
        candidates = candidate_actions(
            snapshot, self.player, include_strategic=True,
            episode_steps=self.episode_steps, shed_capacity=self.shed_capacity,
        )
        if not candidates:
            raise RuntimeError("candidate catalog returned no HOLD action")
        predictions = self.model.predict_candidates(
            snapshot, self.player, candidates, episode_steps=self.episode_steps,
            turns_per_day=self.turns_per_day, shed_capacity=self.shed_capacity,
        )
        greedy_index, greedy_candidate, greedy_prediction = choose_best(candidates, predictions)
        selected_index = greedy_index
        selected_exact: float | None = None
        baseline_exact: float | None = None
        if self.mode == "mpc":
            ranked = sorted(
                range(len(candidates)),
                key=lambda index: (float(predictions[index]), -index),
                reverse=True,
            )[: max(1, min(int(self.top_k), len(candidates)))]
            payload = _serialize(self.lib, state, self.state_size)
            exact: dict[int, float] = {}
            for index in ranked:
                exact[index] = _branch_candidate(
                    self.lib, payload, self.config, self.state_size, self.player,
                    candidates[index], self.opponent_path, horizon=self.lookahead,
                    episode_id=self.episode_id, seed=self.seed,
                    deterministic=self.deterministic,
                    policy_backend=self.policy_backend,
                    policy_device=self.policy_device,
                    fallback_provider=self.fallback_provider,
                    first_fallback_action=fallback_action,
                    opponent_provider=self.opponent_provider,
                )
            selected_index = max(
                ranked,
                key=lambda index: (exact[index], float(predictions[index]), -index),
            )
            selected_exact = exact[selected_index]
        selected = candidates[selected_index]
        # Predictions are trained as candidate-minus-baseline cash.  Never
        # override a capable primitive PPO with a non-positive intervention;
        # fitted tree extrapolation can otherwise turn a late-game tie into a
        # stream of harmful one-unit sells/buys.  HOLD is the explicit zero
        # action, while ``min_prediction`` provides an optional safety margin.
        hold_index = next(
            (index for index, candidate in enumerate(candidates)
             if candidate.kind == "HOLD"),
            None,
        )
        hold_prediction = (
            float(predictions[hold_index]) if hold_index is not None else 0.0
        )
        if (
            hold_index is not None
            and selected.kind != "HOLD"
            and float(predictions[selected_index])
            <= max(0.0, hold_prediction + float(self.min_prediction))
        ):
            selected_index = hold_index
            selected = candidates[selected_index]
            selected_exact = None
        # A learned score is only a proposal.  Before applying a non-HOLD
        # proposal, compare it against the actual fallback PPO continuation
        # from the same native state and random stream.  This common-random-
        # number guard is what makes the adapter safe in stochastic episodes;
        # a model trained on deterministic snapshot-local labels must not be
        # allowed to spend cash when its exact short lookahead is worse.
        if selected.kind != "HOLD" and self.fallback_provider is not None:
            guard_horizon = max(1, int(self.guard_horizon or self.lookahead))
            if selected_exact is None:
                payload = _serialize(self.lib, state, self.state_size)
                selected_exact = _branch_candidate(
                    self.lib, payload, self.config, self.state_size, self.player,
                    selected, self.opponent_path, horizon=guard_horizon,
                    episode_id=self.episode_id, seed=self.seed,
                    deterministic=self.deterministic,
                    policy_backend=self.policy_backend,
                    policy_device=self.policy_device,
                    fallback_provider=self.fallback_provider,
                    first_fallback_action=fallback_action,
                    opponent_provider=self.opponent_provider,
                )
            payload = _serialize(self.lib, state, self.state_size)
            baseline_exact = _branch_candidate(
                self.lib, payload, self.config, self.state_size, self.player,
                None, self.opponent_path, horizon=guard_horizon,
                episode_id=self.episode_id, seed=self.seed,
                deterministic=self.deterministic,
                policy_backend=self.policy_backend,
                policy_device=self.policy_device,
                fallback_provider=self.fallback_provider,
                first_fallback_action=fallback_action,
                opponent_provider=self.opponent_provider,
            )
            if selected_exact <= baseline_exact:
                selected_index = hold_index if hold_index is not None else selected_index
                selected = candidates[selected_index]
                selected_exact = None
        self.decisions.append({
            "step": int(step),
            "candidate": selected.action_id,
            "candidate_kind": selected.kind,
            "candidate_quantity": int(selected.quantity),
            "candidate_count": len(candidates),
            "greedy_candidate": greedy_candidate.action_id,
            "greedy_prediction": float(greedy_prediction),
            "selected_prediction": float(predictions[selected_index]),
            "selected_exact_money": selected_exact,
            "baseline_exact_money": baseline_exact,
            "mode": self.mode,
            "fallback_used": int(selected.kind == "HOLD"),
        })
        return selected, float(predictions[selected_index]), selected_exact

    def action(self, snapshot: dict[str, Any], state) -> dict[str, Any]:
        step = int(snapshot.get("step", 0))
        # Advance the fallback recurrent policy on every real transition,
        # including turns whose action is overridden by a queued macro.  This
        # keeps its hidden state aligned with the actual episode while making
        # it available for ordinary primitive control between strategic
        # decisions.  The returned action is intentionally discarded when a
        # macro sequence is active.
        fallback_action = (
            _provider_action(
                self.fallback_provider, self.lib, state, snapshot, self.player,
            )
            if self.fallback_provider is not None else _empty_action()
        )
        # A direct candidate (HOLD/SELL/etc.) has a one-turn primitive
        # sequence, but it is still a strategic decision.  Keep it active
        # until the next scheduled boundary instead of re-scoring all ~1,000
        # primitive turns; multi-turn executor plans remain uninterrupted.
        if not self._queue and step >= getattr(self, "_next_decision_step", 0):
            selected, _prediction, _exact = self._select(
                snapshot, state, step, fallback_action,
            )
            # HOLD means "do not override the primitive policy", not "send a
            # PASS command once".  Injecting a PASS would desynchronize a
            # recurrent PPO and make the no-intervention comparison worse.
            self._queue = (
                [] if selected.kind == "HOLD" else list(selected.action_sequence())
            )
            self._next_decision_step = step + max(1, int(self.decision_interval))
        if self._queue:
            return merge_macro_action(self._queue.pop(0), fallback_action)
        return fallback_action


def _make_config(lib, episode_steps: int) -> CConfig:
    config = CConfig()
    lib.kg_config_default(ctypes.byref(config))
    config.episode_steps = int(episode_steps)
    return config


def _run_episode(
    lib,
    config: CConfig,
    state_size: int,
    *,
    baseline_path: pathlib.Path,
    opponent_path: pathlib.Path,
    macro_model: MacroValueModel | None,
    macro_player: int | None,
    macro_mode: str,
    decision_interval: int,
    top_k: int,
    lookahead: int,
    guard_horizon: int,
    deterministic: bool,
    seed: int,
    episode_id: str,
    episode_steps: int,
    turns_per_day: int,
    shed_capacity: int,
    policy_backend: str,
    policy_device: str,
    min_prediction: float,
) -> dict[str, Any]:
    episode_config = CConfig.from_buffer_copy(bytes(config))
    episode_config.seed = int(seed)
    state = lib.kg_create(ctypes.byref(episode_config))
    if not state:
        raise RuntimeError("kg_create failed for live episode")
    baseline = NativePolicyProvider(
        (str(baseline_path),), deterministic=deterministic,
        seed=seed + 11, snapshot_local=False,
        backend=policy_backend, device=policy_device,
    )
    opponent = NativePolicyProvider(
        (str(opponent_path),), deterministic=deterministic,
        seed=seed + 29, snapshot_local=False,
        backend=policy_backend, device=policy_device,
    )
    baseline.begin_episode(episode_id, 0)
    opponent.begin_episode(episode_id, 0)
    controller = None
    if macro_model is not None:
        if macro_player not in (0, 1):
            raise ValueError("macro_player must be 0 or 1 when a macro model is used")
        controller = MacroEpisodeController(
            lib, episode_config, state_size, macro_model, opponent_path,
            int(macro_player), mode=macro_mode, decision_interval=decision_interval,
            top_k=top_k, lookahead=lookahead, guard_horizon=guard_horizon,
            deterministic=deterministic,
            episode_id=episode_id, seed=seed, episode_steps=episode_steps,
            turns_per_day=turns_per_day, shed_capacity=shed_capacity,
            policy_backend=policy_backend, policy_device=policy_device,
            min_prediction=min_prediction,
            fallback_provider=NativePolicyProvider(
                (str(baseline_path),), deterministic=deterministic,
                # Use the same RNG stream as the standalone baseline so the
                # stochastic comparison is genuinely common-random-number.
                # (The macro path may override actions, but HOLD must be
                # exactly the baseline trajectory.)
                seed=seed + 11, snapshot_local=False,
                backend=policy_backend, device=policy_device,
            ),
            opponent_provider=opponent,
        )
        controller.reset(episode_id, seed)
        controller.fallback_provider.begin_episode(episode_id, 0)
    try:
        while not bool(lib.kg_done(state)):
            snapshot = c_snapshot(lib, state)
            if controller is not None:
                macro_action = controller.action(snapshot, state)
            else:
                macro_action = None
            if controller is None:
                # Keep the baseline and macro agent in the same seat so the
                # money/win comparison is meaningful for either macro_player.
                left = (
                    _provider_action(baseline, lib, state, snapshot, 0)
                    if macro_player == 0 else _provider_action(opponent, lib, state, snapshot, 0)
                )
                right = (
                    _provider_action(opponent, lib, state, snapshot, 1)
                    if macro_player == 0 else _provider_action(baseline, lib, state, snapshot, 1)
                )
            else:
                left = (
                    macro_action if macro_player == 0
                    else _provider_action(opponent, lib, state, snapshot, 0)
                )
                right = (
                    _provider_action(opponent, lib, state, snapshot, 1)
                    if macro_player == 0 else macro_action
                )
            _step_pair(lib, state, left, right)
        final = c_snapshot(lib, state)
        money = [float(lib.kg_player_money(state, player)) for player in (0, 1)]
        winner = (
            0 if money[0] > money[1] else
            1 if money[1] > money[0] else -1
        )
        result = {
            "episode_id": episode_id,
            "seed": int(seed),
            "macro": controller is not None,
            "macro_player": macro_player,
            "macro_mode": macro_mode if controller is not None else None,
            "money": money,
            "winner": winner,
            "steps": int(final.get("step", 0)),
        }
        if controller is not None:
            result["decisions"] = controller.decisions
        return result
    finally:
        lib.kg_destroy(state)


def _paths(args: argparse.Namespace) -> list[pathlib.Path]:
    values: list[str] = []
    for value in args.opponent_model or []:
        values.extend(item.strip() for item in str(value).split(",") if item.strip())
    if args.league:
        return policy_paths_from_league(args.league, values)
    return policy_paths_from_league(None, values)


def evaluate(args: argparse.Namespace) -> dict[str, Any]:
    lib = load_core(pathlib.Path(args.lib))
    state_size = int(lib.kg_state_serialized_size())
    config = _make_config(lib, args.episode_steps)
    opponents = _paths(args)
    baseline_paths = (
        policy_paths_from_league(None, args.baseline_model)
        if args.baseline_model else None
    )
    macro_model = MacroValueModel.load(args.macro_model) if args.macro_model else None
    output = pathlib.Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, Any]] = []
    for opponent_index, opponent_path in enumerate(opponents):
        baseline_path = (
            baseline_paths[opponent_index % len(baseline_paths)]
            if baseline_paths else opponent_path
        )
        for episode_index in range(args.episodes):
            seed = int(args.seed) + episode_index
            episode_id = f"{opponent_index}:{episode_index}"
            baseline = _run_episode(
                lib, config, state_size, baseline_path=baseline_path,
                opponent_path=opponent_path, macro_model=None,
                macro_player=args.macro_player,
                macro_mode=args.macro_mode, decision_interval=args.decision_interval,
                top_k=args.top_k, lookahead=args.lookahead,
                guard_horizon=args.guard_horizon,
                deterministic=not args.stochastic, seed=seed, episode_id=episode_id,
                episode_steps=args.episode_steps, turns_per_day=args.turns_per_day,
                shed_capacity=args.shed_capacity, policy_backend=args.policy_backend,
                policy_device=args.policy_device, min_prediction=args.min_prediction,
            )
            macro = _run_episode(
                lib, config, state_size, baseline_path=baseline_path,
                opponent_path=opponent_path, macro_model=macro_model,
                macro_player=args.macro_player, macro_mode=args.macro_mode,
                decision_interval=args.decision_interval, top_k=args.top_k,
                lookahead=args.lookahead, guard_horizon=args.guard_horizon,
                deterministic=not args.stochastic,
                seed=seed, episode_id=episode_id, episode_steps=args.episode_steps,
                turns_per_day=args.turns_per_day, shed_capacity=args.shed_capacity,
                policy_backend=args.policy_backend, policy_device=args.policy_device,
                min_prediction=args.min_prediction,
            )
            rows.append({
                "opponent": str(opponent_path),
                "baseline_model": str(baseline_path),
                "baseline": baseline,
                "macro": macro,
                "macro_money_delta": float(
                    macro["money"][args.macro_player] - baseline["money"][args.macro_player]
                ),
            })
    summaries: list[dict[str, Any]] = []
    for opponent_path in opponents:
        subset = [row for row in rows if row["opponent"] == str(opponent_path)]
        macro_money = [float(row["macro"]["money"][args.macro_player]) for row in subset]
        baseline_money = [
            float(row["baseline"]["money"][args.macro_player]) for row in subset
        ]
        summaries.append({
            "opponent": str(opponent_path),
            "episodes": len(subset),
            "baseline_mean_money": sum(baseline_money) / len(subset),
            "macro_mean_money": sum(macro_money) / len(subset),
            "macro_minus_baseline": (
                sum(macro_money) - sum(baseline_money)
            ) / len(subset),
            "baseline_win_rate": sum(
                row["baseline"]["winner"] == args.macro_player for row in subset
            ) / len(subset),
            "macro_win_rate": sum(
                row["macro"]["winner"] == args.macro_player for row in subset
            ) / len(subset),
        })
    summary = {
        "format": "kaggriculture_macro_episode_eval_v1",
        "lib": str(args.lib),
        "macro_model": args.macro_model,
        "macro_player": args.macro_player,
        "macro_mode": args.macro_mode,
        "decision_interval": args.decision_interval,
        "top_k": args.top_k,
        "lookahead": args.lookahead,
        "guard_horizon": args.guard_horizon or args.lookahead,
        "episodes_per_opponent": args.episodes,
        "seed": args.seed,
        "deterministic": not args.stochastic,
        "policy_backend": args.policy_backend,
        "policy_device": args.policy_device,
        "min_prediction": args.min_prediction,
        "episode_steps": args.episode_steps,
        "opponents": [str(path) for path in opponents],
        "summaries": summaries,
        "episodes": rows,
    }
    output.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return summary


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--macro-model", required=True)
    parser.add_argument("--opponent-model", action="append")
    parser.add_argument("--league")
    parser.add_argument("--baseline-model", action="append")
    parser.add_argument("--output", required=True)
    parser.add_argument("--lib", default=str(DEFAULT_LIB))
    parser.add_argument("--episodes", type=int, default=2)
    parser.add_argument("--seed", type=int, default=707)
    parser.add_argument("--macro-player", type=int, choices=(0, 1), default=0)
    parser.add_argument("--macro-mode", choices=("greedy", "mpc"), default="greedy")
    parser.add_argument("--decision-interval", type=int, default=24)
    parser.add_argument("--top-k", type=int, default=4)
    parser.add_argument("--lookahead", type=int, default=24)
    parser.add_argument(
        "--guard-horizon", type=int, default=0,
        help="proposal safety horizon in turns; 0 uses --lookahead",
    )
    parser.add_argument("--episode-steps", type=int, default=720)
    parser.add_argument("--turns-per-day", type=int, default=24)
    parser.add_argument("--shed-capacity", type=int, default=100)
    parser.add_argument("--stochastic", action="store_true")
    parser.add_argument(
        "--policy-backend", choices=("numpy", "torch"), default="numpy",
        help="inference backend for PPO checkpoints (torch enables GPU acceleration)",
    )
    parser.add_argument("--policy-device", default="cuda")
    parser.add_argument(
        "--min-prediction", type=float, default=0.0,
        help="minimum predicted delta over HOLD required for a macro override",
    )
    args = parser.parse_args(argv)
    if not args.opponent_model and not args.league:
        parser.error("provide --opponent-model or --league")
    if args.episodes < 1 or args.decision_interval < 1 or args.top_k < 1 or args.lookahead < 1:
        parser.error("episodes, decision interval, top-k, and lookahead must be positive")
    if args.guard_horizon < 0:
        parser.error("guard-horizon must be nonnegative")
    if args.baseline_model and not all(args.baseline_model):
        parser.error("baseline-model values must be nonempty")
    return args


def main(argv: list[str] | None = None) -> int:
    print(json.dumps(evaluate(parse_args(argv)), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
