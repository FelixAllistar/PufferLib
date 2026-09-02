#!/usr/bin/env python3
"""Generate native-simulator counterfactual macro-action labels.

This is an offline tool.  It restores complete, parity-verified KGState
snapshots, applies one candidate action to one player, continues both branches
with an explicit continuation provider, and records the cash difference versus
the same-state baseline.  It never changes the live PPO action ABI.

The default ``pass`` continuation is a smoke-test baseline, not a PvP oracle.
Use ``expert_first`` for the recorded next joint action, ``trace`` with a JSONL
action trace, or ``rule`` for the reactive native scripted baseline.  The rule
provider is deliberately named as scripted rather than learned: it is a useful
PvP stressor, while a learned league adapter can implement the same provider
interface later.
"""

from __future__ import annotations

import argparse
import csv
import ctypes
import json
import os
import pathlib
import struct
import time
from typing import Any

from build_replay_state_bank import BANK_FORMAT_VERSION, BANK_HEADER, BANK_MAGIC
from macro_actions import (
    FEATURE_FIELDS, candidate_actions, merge_macro_action, pass_action,
)
from replay_native import CAction, CConfig, c_action, c_snapshot, clone_action, load_core
from state_sampling import SELECTIONS, select_rows, shard_rows


DEFAULT_LIB = pathlib.Path(__file__).with_name("build") / "libkaggriculture.so"

DATASET_FIELDS = (
    "state_id", "record_index", "episode_id", "source", "seed", "turn", "player",
    "agent", "opponent_mode", "horizon", "horizon_steps", "horizon_terminal",
    "state_sha256", "candidate_id", "candidate_kind", "candidate_kind_code",
    "candidate_item", "candidate_item_code", "candidate_quantity",
    "candidate_executable", "candidate_note", "candidate_action_json",
    "candidate_plan_json", "candidate_plan_steps",
    "candidate_effective", "baseline_first_money", "candidate_first_money",
    "candidate_first_delta_money", "baseline_final_money", "candidate_final_money",
    "delta_money", "delta_money_normalized", "baseline_terminal",
    "candidate_terminal", "expert_action_json",
    *(f"feature_{name}" for name in FEATURE_FIELDS),
)


def _default_config(lib) -> CConfig:
    config = CConfig()
    # The function is exported by the core and keeps this tool synchronized
    # with future default changes.  Keep a fallback for older shared objects.
    if hasattr(lib, "kg_config_default"):
        lib.kg_config_default.argtypes = [ctypes.POINTER(CConfig)]
        lib.kg_config_default.restype = None
        lib.kg_config_default(ctypes.byref(config))
    else:
        config.episode_steps = 720
        config.board_size = 10
        config.starting_money = 3000
        config.max_market_orders_per_turn = 10
        config.turns_per_day = 24
        config.shed_capacity = 100
        config.weed_spawn_chance = 0.005
        config.town_shop_unlock_interval = 3
        config.town_shop_sell_interval = 4
        config.town_center_sell_interval = 24
        config.farm_hand_cost_mult = 1
        config.seed = 0
    return config


def _restore(lib, payload: bytes, config: CConfig):
    state = lib.kg_create(ctypes.byref(config))
    if not state:
        raise RuntimeError("kg_create failed while restoring a counterfactual")
    buffer = ctypes.create_string_buffer(payload, len(payload))
    if not lib.kg_state_deserialize(state, buffer, len(payload)):
        lib.kg_destroy(state)
        raise RuntimeError("kg_state_deserialize rejected a state-bank record")
    return state


def _pass_pair() -> list[dict[str, Any]]:
    return [pass_action(), pass_action()]


def _parse_pair(value: str | None) -> list[dict[str, Any]] | None:
    if not value:
        return None
    try:
        pair = json.loads(value)
    except json.JSONDecodeError as error:
        raise ValueError(f"invalid expert action JSON: {error}") from error
    if not isinstance(pair, list) or len(pair) != 2:
        raise ValueError("expert action JSON must be a two-player list")
    return [item if isinstance(item, dict) else pass_action() for item in pair]


def _load_trace(path: pathlib.Path | None) -> dict[tuple[str, int], list[dict[str, Any]]]:
    if path is None:
        return {}
    trace: dict[tuple[str, int], list[dict[str, Any]]] = {}
    with path.open(encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(f"invalid trace JSON on line {line_number}: {error}") from error
            if not isinstance(record, dict):
                raise ValueError(f"trace line {line_number} is not an object")
            episode = str(record.get("episode_id", ""))
            turn = int(record.get("turn", -1))
            actions = record.get("actions")
            if not episode or turn < 0 or not isinstance(actions, list) or len(actions) != 2:
                raise ValueError(
                    f"trace line {line_number} needs episode_id, nonnegative turn, and two actions"
                )
            trace[(episode, turn)] = [
                action if isinstance(action, dict) else pass_action() for action in actions
            ]
    return trace


def _read_manifest(path: pathlib.Path, limit: int) -> tuple[list[dict[str, str]], int]:
    with path.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    total = len(rows)
    if limit > 0:
        rows = rows[:limit]
    if not rows:
        raise ValueError(f"state-bank manifest is empty: {path}")
    required = {"record_index", "byte_offset", "byte_size", "sha256", "episode_id", "turn", "players"}
    missing = required.difference(rows[0])
    if missing:
        raise ValueError(f"manifest is missing required fields: {sorted(missing)}")
    return rows, total


def _read_record(stream, row: dict[str, str], state_size: int) -> bytes:
    byte_size = int(row["byte_size"])
    if byte_size != state_size:
        raise ValueError(
            f"record {row['record_index']} has size {byte_size}, expected {state_size}"
        )
    stream.seek(int(row["byte_offset"]))
    payload = stream.read(byte_size)
    if len(payload) != byte_size:
        raise ValueError(f"truncated state-bank record {row['record_index']}")
    import hashlib

    digest = hashlib.sha256(payload).hexdigest()
    if digest != row["sha256"]:
        raise ValueError(f"checksum mismatch for state-bank record {row['record_index']}")
    return payload


def _players(row: dict[str, str], requested: str) -> tuple[int, ...]:
    available = tuple(sorted({int(value) for value in row["players"].split(",") if value != ""}))
    if requested == "both":
        return available or (0, 1)
    player = int(requested)
    if player not in available:
        raise ValueError(f"player {player} is not present in state-bank row {row['record_index']}")
    return (player,)


def _money(snapshot: dict[str, Any], player: int) -> float:
    farms = snapshot.get("farms") or []
    if player >= len(farms) or not isinstance(farms[player], dict):
        return 0.0
    try:
        return float(farms[player].get("money", 0.0))
    except (TypeError, ValueError):
        return 0.0


def _initial_pair(
    row: dict[str, str], mode: str, trace: dict[tuple[str, int], list[dict[str, Any]]],
    snapshot: dict[str, Any] | None = None, provider: Any = None,
    lib: Any = None, native_state: Any = None,
) -> list[dict[str, Any] | CAction]:
    if mode == "pass":
        return _pass_pair()
    if mode == "expert_first":
        return _parse_pair(row.get("expert_actions")) or _pass_pair()
    if mode == "rule":
        if provider is None or snapshot is None:
            raise ValueError("rule continuation requires a provider and snapshot")
        if lib is not None and native_state is not None:
            actions = (CAction * 2)()
            liquidation = int(getattr(provider, "liquidation_turns", 48))
            if hasattr(lib, "kg_rule_action_ex"):
                lib.kg_rule_action_ex(native_state, 0, liquidation, ctypes.byref(actions[0]))
                lib.kg_rule_action_ex(native_state, 1, liquidation, ctypes.byref(actions[1]))
            else:
                lib.kg_rule_action(native_state, 0, ctypes.byref(actions[0]))
                lib.kg_rule_action(native_state, 1, ctypes.byref(actions[1]))
            return [actions[0], actions[1]]
        return [provider.action(snapshot, player) for player in (0, 1)]
    if mode == "learned":
        if provider is None or snapshot is None:
            raise ValueError("learned continuation requires a provider and snapshot")
        return [provider.action(snapshot, player) for player in (0, 1)]
    return trace.get((str(row["episode_id"]), int(row["turn"])), _pass_pair())


def _continuation_pair(
    lib, state,
    episode_id: str, turn: int, mode: str,
    trace: dict[tuple[str, int], list[dict[str, Any]]], provider: Any = None,
) -> list[dict[str, Any] | CAction]:
    if mode == "trace":
        return trace.get((episode_id, turn), _pass_pair())
    if mode == "rule":
        if provider is None:
            raise ValueError("rule continuation requires a provider")
        # The scripted rule policy is implemented in the exact native core so
        # each branch does not serialize a 38-KB JSON snapshot at every turn.
        actions = (CAction * 2)()
        liquidation = int(getattr(provider, "liquidation_turns", 48))
        if hasattr(lib, "kg_rule_action_ex"):
            lib.kg_rule_action_ex(state, 0, liquidation, ctypes.byref(actions[0]))
            lib.kg_rule_action_ex(state, 1, liquidation, ctypes.byref(actions[1]))
        else:
            lib.kg_rule_action(state, 0, ctypes.byref(actions[0]))
            lib.kg_rule_action(state, 1, ctypes.byref(actions[1]))
        return [actions[0], actions[1]]
    if mode == "learned":
        if provider is None:
            raise ValueError("learned continuation requires a provider")
        # Keep the planner/brancher on the same exact native observation path
        # as the batched dataset writer whenever the provider is the
        # snapshot-local Torch adapter.  The two player calls are one GPU
        # batch; this avoids serializing a 38-KB JSON snapshot for every
        # transition while preserving action/RNG order.
        if (
            getattr(provider, "backend", "numpy") == "torch"
            and bool(getattr(provider, "snapshot_local", False))
            and hasattr(provider, "native_action_batch")
        ):
            return provider.native_action_batch(lib, [state, state], [0, 1])
        snapshot = c_snapshot(lib, state)
        return [provider.action(snapshot, player) for player in (0, 1)]
    return _pass_pair()


def _step_pair(lib, state, pair: list[dict[str, Any] | CAction]) -> None:
    actions = (CAction * 2)()
    actions[0] = pair[0] if isinstance(pair[0], CAction) else c_action(pair[0])
    actions[1] = pair[1] if isinstance(pair[1], CAction) else c_action(pair[1])
    lib.kg_step(state, actions)


def _serialize(lib, state, state_size: int) -> bytes:
    buffer = (ctypes.c_ubyte * state_size)()
    if not lib.kg_state_serialize(state, buffer, state_size):
        raise RuntimeError("native state serialization failed during branching")
    return bytes(buffer)


def _branch_with_first(
    lib,
    payload: bytes,
    config: CConfig,
    state_size: int,
    initial_step: int,
    player: int,
    first_pair: list[dict[str, Any]],
    planned_actions: tuple[dict[str, Any], ...] | None,
    episode_id: str,
    mode: str,
    trace: dict[tuple[str, int], list[dict[str, Any]]],
    provider: Any,
    horizon: int | None,
) -> tuple[float, float, bytes, bool]:
    """Return (final own money, first own money, first bytes, terminal)."""
    result = branch_payload(
        lib, payload, config, state_size, initial_step, player, first_pair,
        planned_actions, episode_id, mode, trace, provider, horizon,
    )
    return (
        float(result["final_money"]), float(result["first_money"]),
        result["first_bytes"], bool(result["terminal"]),
    )


def branch_payload(
    lib,
    payload: bytes,
    config: CConfig,
    state_size: int,
    initial_step: int,
    player: int,
    first_pair: list[dict[str, Any]],
    planned_actions: tuple[dict[str, Any], ...] | None,
    episode_id: str,
    mode: str,
    trace: dict[tuple[str, int], list[dict[str, Any]]],
    provider: Any,
    horizon: int | None,
) -> dict[str, Any]:
    """Branch a state and return auditable terminal bytes and cash metrics.

    This is the C2 planner's shared execution primitive.  ``planned_actions``
    includes the first action at index zero; after the plan is exhausted the
    selected player is continued by the requested provider, exactly like the
    baseline branch.  Returning serialized bytes rather than a JSON snapshot
    keeps search on the native ABI and lets callers evaluate another candidate
    from the same leaf without exposing private fields to a model.
    """

    state = _restore(lib, payload, config)
    branch_provider = (
        provider.fork() if provider is not None and hasattr(provider, "fork") else provider
    )
    try:
        _step_pair(lib, state, first_pair)
        first_after = _serialize(lib, state, state_size)
        first_money = float(lib.kg_player_money(state, player))
        steps = 1
        while not bool(lib.kg_done(state)) and (horizon is None or steps < horizon):
            turn = initial_step + steps
            pair = _continuation_pair(
                lib, state, episode_id, turn, mode, trace, branch_provider,
            )
            if planned_actions is not None and steps < len(planned_actions):
                # A macro is a strategic overlay: preserve the continuation
                # policy's unspecified unit commands (especially hired-hand
                # maintenance) while replacing only the explicit farmer/
                # market fields.  This is the same merge used by the live
                # macro evaluator, keeping labels faithful to deployment.
                if isinstance(pair[player], dict):
                    pair[player] = merge_macro_action(
                        planned_actions[steps], pair[player],
                    )
                else:
                    # Native rule/legacy providers return CAction objects;
                    # retain their original behavior until a structured
                    # fallback action is available.
                    pair[player] = planned_actions[steps]
            _step_pair(lib, state, pair)
            steps += 1
        final_bytes = _serialize(lib, state, state_size)
        return {
            "final_money": float(lib.kg_player_money(state, player)),
            "first_money": first_money,
            "first_bytes": first_after,
            "final_bytes": final_bytes,
            "steps": steps,
            "terminal": bool(lib.kg_done(state)),
        }
    finally:
        lib.kg_destroy(state)


def _branch_candidates_batch_serial(
    lib,
    payload: bytes,
    config: CConfig,
    state_size: int,
    initial_step: int,
    player: int,
    candidates: list[Any],
    first_pair: list[dict[str, Any] | CAction],
    episode_id: str,
    provider: Any,
    horizon: int | None,
    batch_size: int = 128,
) -> list[dict[str, Any]]:
    """Evaluate candidate branches in batches of snapshot-local PPO calls.

    Native states still advance one exact transition at a time, but all
    independent learned-opponent observations at a transition are encoded and
    forwarded together.  Replay-bank snapshots have no recurrent hidden state,
    so this is semantically equivalent to the serial learned path while
    avoiding one Python/Torch launch per branch.  The helper intentionally
    accepts only learned providers with ``action_batch``; rule/trace branches
    continue through :func:`branch_payload` as their native fast paths.
    """
    if not candidates:
        return []
    if not hasattr(provider, "action_batch"):
        raise TypeError("batched candidate evaluation requires a learned action_batch provider")
    batch_size = max(1, int(batch_size))
    results: list[dict[str, Any]] = []
    for offset in range(0, len(candidates), batch_size):
        batch = candidates[offset:offset + batch_size]
        states: list[Any] = []
        steps = [0] * len(batch)
        first_money: list[float] = []
        first_bytes: list[bytes] = []
        sequences = [candidate.action_sequence() for candidate in batch]
        try:
            for candidate in batch:
                state = _restore(lib, payload, config)
                pair = [clone_action(first_pair[0]), clone_action(first_pair[1])]
                if isinstance(pair[player], dict):
                    pair[player] = merge_macro_action(
                        candidate.payload(), pair[player],
                    )
                else:
                    pair[player] = candidate.payload()
                _step_pair(lib, state, pair)
                states.append(state)
                steps[len(states) - 1] = 1
                first_bytes.append(_serialize(lib, state, state_size))
                first_money.append(float(lib.kg_player_money(state, player)))
            active = list(range(len(batch)))
            while active:
                active = [
                    index for index in active
                    if not bool(lib.kg_done(states[index]))
                    and (horizon is None or steps[index] < horizon)
                ]
                if not active:
                    break
                def _actions(indices: list[int], action_player: int) -> list[dict[str, Any]]:
                    if hasattr(provider, "native_action_batch"):
                        return provider.native_action_batch(
                            lib, [states[index] for index in indices],
                            [action_player] * len(indices),
                        )
                    snapshots = [c_snapshot(lib, states[index]) for index in indices]
                    return provider.action_batch(
                        snapshots, [action_player] * len(indices),
                    )

                opponent_actions = _actions(active, 1 - player)
                # Advance an independent fallback action for every branch at
                # every transition.  Sequence actions then overlay it, which
                # preserves ordinary PPO hand work during farmer-only plans.
                own_actions = dict(zip(
                    active,
                    _actions(active, player),
                ))
                for position, index in enumerate(active):
                    step = steps[index]
                    if step < len(sequences[index]):
                        own_action = merge_macro_action(
                            sequences[index][step], own_actions[index],
                        )
                    else:
                        own_action = own_actions[index]
                    pair: list[dict[str, Any] | CAction] = [
                        pass_action(), pass_action(),
                    ]
                    pair[player] = own_action
                    pair[1 - player] = opponent_actions[position]
                    _step_pair(lib, states[index], pair)
                    steps[index] += 1
            for index, state in enumerate(states):
                results.append({
                    "final_money": float(lib.kg_player_money(state, player)),
                    "first_money": first_money[index],
                    "first_bytes": first_bytes[index],
                    "final_bytes": _serialize(lib, state, state_size),
                    "steps": steps[index],
                    "terminal": bool(lib.kg_done(state)),
                })
        finally:
            for state in states:
                lib.kg_destroy(state)
    return results


def _branch_candidates_batch(
    lib,
    payload: bytes,
    config: CConfig,
    state_size: int,
    initial_step: int,
    player: int,
    candidates: list[Any],
    first_pair: list[dict[str, Any] | CAction],
    episode_id: str,
    provider: Any,
    horizon: int | None,
    batch_size: int = 128,
    workers: int = 1,
) -> list[dict[str, Any]]:
    """Evaluate candidate branches with optional deterministic CPU workers.

    Each worker owns its native states and a forked provider.  Snapshot-local
    Torch models share immutable GPU weights, while each fork gets an
    independent RNG for stochastic sampling.  Results are concatenated in
    candidate order, so worker scheduling cannot change the dataset schema or
    deterministic labels.  ``workers=1`` is the reference implementation.
    """
    if not candidates:
        return []
    workers = max(1, int(workers))
    if workers == 1 or len(candidates) <= max(1, int(batch_size)):
        return _branch_candidates_batch_serial(
            lib, payload, config, state_size, initial_step, player, candidates,
            first_pair, episode_id, provider, horizon, batch_size,
        )
    from concurrent.futures import ThreadPoolExecutor

    batch_size = max(1, int(batch_size))
    chunks = [
        candidates[offset:offset + batch_size]
        for offset in range(0, len(candidates), batch_size)
    ]
    workers = min(workers, len(chunks))

    def run(index: int, chunk: list[Any]) -> list[dict[str, Any]]:
        child = provider.fork() if hasattr(provider, "fork") else provider
        # The deterministic path does not consume this RNG.  Advancing each
        # stochastic fork gives reproducible, non-overlapping streams when a
        # caller explicitly requests stochastic offline labels.
        if hasattr(child, "_rng") and not getattr(child, "deterministic", True):
            child._rng.random(index * 100003)
        return _branch_candidates_batch_serial(
            lib, payload, config, state_size, initial_step, player, chunk,
            first_pair, episode_id, child, horizon, len(chunk),
        )

    with ThreadPoolExecutor(max_workers=workers) as executor:
        futures = [executor.submit(run, index, chunk) for index, chunk in enumerate(chunks)]
        return [result for future in futures for result in future.result()]


def _row_features(
    snapshot: dict[str, Any], player: int, episode_steps: int, turns_per_day: int,
    shed_capacity: int,
) -> dict[str, Any]:
    # Import here so the public feature function remains independently usable.
    from macro_actions import public_features

    features = public_features(
        snapshot, player, episode_steps=episode_steps,
        turns_per_day=turns_per_day, shed_capacity=shed_capacity,
    )
    return {f"feature_{name}": features.get(name, 0) for name in FEATURE_FIELDS}


def generate(args: argparse.Namespace) -> dict[str, Any]:
    bank = pathlib.Path(args.bank)
    manifest_path = pathlib.Path(args.manifest or f"{bank}.manifest.tsv")
    # Read the complete manifest before sampling so ``stratified`` can see all
    # seasons/scenario tags.  The bank/manifest count check must still use the
    # untruncated total.
    rows, manifest_count = _read_manifest(manifest_path, 0)
    rows = select_rows(
        rows, args.limit_states, strategy=args.state_selection,
        seed=args.state_seed, episode_steps=args.episode_steps,
    )
    rows = shard_rows(rows, args.state_shard_index, args.state_shard_count)
    with bank.open("rb") as stream:
        header = stream.read(BANK_HEADER.size)
    if len(header) != BANK_HEADER.size:
        raise ValueError(f"truncated state-bank header: {bank}")
    magic, bank_version, state_version, state_size, record_count, _reserved = BANK_HEADER.unpack(header)
    if magic != BANK_MAGIC or bank_version != BANK_FORMAT_VERSION:
        raise ValueError(f"unsupported state-bank format: {bank}")
    if record_count != manifest_count:
        # The explicit count check catches an accidentally paired manifest from
        # a different bank before any expensive branches run.
        raise ValueError(f"manifest/bank record count mismatch for {bank}")

    lib = load_core(pathlib.Path(args.lib))
    actual_size = int(lib.kg_state_serialized_size())
    actual_version = int(lib.kg_state_serialization_version())
    if actual_size != state_size or actual_version != state_version:
        raise ValueError(
            f"native ABI mismatch: bank version/size={state_version}/{state_size}, "
            f"library={actual_version}/{actual_size}"
        )
    config = _default_config(lib)
    trace = _load_trace(pathlib.Path(args.trace) if args.trace else None)
    if args.opponent_mode == "trace" and not trace:
        raise ValueError("--opponent-mode trace requires a non-empty --trace JSONL")
    provider = None
    if args.opponent_mode == "rule":
        from opponent_providers import RuleProvider

        provider = RuleProvider(
            episode_steps=args.episode_steps,
            liquidation_turns=args.rule_liquidation_turns,
        )
    elif args.opponent_mode == "learned":
        from opponent_providers import learned_provider

        provider = learned_provider(
            league=args.league, policy_models=args.policy_model,
            deterministic=not args.policy_stochastic, seed=args.state_seed,
            backend=args.policy_backend, device=args.policy_device,
        )
    horizon = None if args.horizon == "terminal" else int(args.horizon)
    if horizon is not None and horizon < 1:
        raise ValueError("--horizon must be a positive integer or terminal")
    output = pathlib.Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    counts = {"states": 0, "players": 0, "candidates": 0, "effective": 0, "terminal": 0}
    effective_by_kind: dict[str, int] = {}
    started = time.monotonic()
    with bank.open("rb") as bank_stream, output.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=DATASET_FIELDS, delimiter="\t")
        writer.writeheader()
        for row in rows:
            payload = _read_record(bank_stream, row, state_size)
            restored = _restore(lib, payload, config)
            try:
                snapshot = c_snapshot(lib, restored)
                if provider is not None and hasattr(provider, "begin_episode"):
                    provider.begin_episode(str(row["episode_id"]), int(row["turn"]))
                base_pair = _initial_pair(
                    row, args.opponent_mode, trace, snapshot=snapshot, provider=provider,
                    lib=lib, native_state=restored,
                )
            finally:
                lib.kg_destroy(restored)
            initial_step = int(snapshot.get("step", 0))
            counts["states"] += 1
            for player in _players(row, args.players):
                counts["players"] += 1
                features = _row_features(
                    snapshot, player, args.episode_steps,
                    args.turns_per_day, args.shed_capacity,
                )
                candidates = candidate_actions(
                    snapshot, player, max_candidates=args.max_candidates,
                    include_strategic=not args.direct_only,
                    episode_steps=args.episode_steps,
                    shed_capacity=args.shed_capacity,
                )
                baseline_money_final, baseline_money_first, baseline_first, baseline_terminal = _branch_with_first(
                    lib, payload, config, state_size, initial_step, player, base_pair,
                    None, str(row["episode_id"]), args.opponent_mode, trace, provider, horizon,
                )
                batched_results = None
                if (
                    args.opponent_mode == "learned"
                    and getattr(provider, "backend", "numpy") == "torch"
                    and bool(getattr(provider, "snapshot_local", False))
                ):
                    batched_results = _branch_candidates_batch(
                        lib, payload, config, state_size, initial_step, player,
                        candidates, base_pair, str(row["episode_id"]), provider,
                        horizon, args.branch_batch_size,
                        args.branch_workers,
                    )
                for candidate_index, candidate in enumerate(candidates):
                    candidate_pair = [clone_action(base_pair[0]), clone_action(base_pair[1])]
                    if isinstance(candidate_pair[player], dict):
                        candidate_pair[player] = merge_macro_action(
                            candidate.payload(), candidate_pair[player],
                        )
                    else:
                        candidate_pair[player] = candidate.payload()
                    if batched_results is not None:
                        candidate_result = batched_results[candidate_index]
                        candidate_money_final = float(candidate_result["final_money"])
                        candidate_money_first = float(candidate_result["first_money"])
                        candidate_first = candidate_result["first_bytes"]
                        candidate_terminal = bool(candidate_result["terminal"])
                    else:
                        candidate_money_final, candidate_money_first, candidate_first, candidate_terminal = _branch_with_first(
                            lib, payload, config, state_size, initial_step, player, candidate_pair,
                            candidate.action_sequence(), str(row["episode_id"]),
                            args.opponent_mode, trace, provider, horizon,
                        )
                    # The clock differs by design; compare candidate/baseline
                    # after the same transition using the macro module helper.
                    effective = int(baseline_first != candidate_first)
                    delta = candidate_money_final - baseline_money_final
                    horizon_terminal = int(baseline_terminal and candidate_terminal)
                    out = {
                        "state_id": f"{row['record_index']}:{player}",
                        "record_index": row["record_index"],
                        "episode_id": row["episode_id"],
                        "source": row.get("source", ""),
                        "seed": row.get("seed", ""),
                        "turn": row["turn"],
                        "player": player,
                        "agent": row.get("agent", ""),
                        "opponent_mode": args.opponent_mode,
                        "horizon": args.horizon,
                        "horizon_steps": args.horizon if horizon is not None else "terminal",
                        "horizon_terminal": horizon_terminal,
                        "state_sha256": row["sha256"],
                        **candidate.columns(),
                        "candidate_effective": effective,
                        "baseline_first_money": float(baseline_money_first),
                        "candidate_first_money": float(candidate_money_first),
                        "candidate_first_delta_money": candidate_money_first - baseline_money_first,
                        "baseline_final_money": baseline_money_final,
                        "candidate_final_money": candidate_money_final,
                        "delta_money": delta,
                        "delta_money_normalized": delta / max(1.0, float(config.starting_money)),
                        "baseline_terminal": int(baseline_terminal),
                        "candidate_terminal": int(candidate_terminal),
                        "expert_action_json": row.get("expert_actions", ""),
                        **features,
                    }
                    writer.writerow(out)
                    counts["candidates"] += 1
                    counts["effective"] += effective
                    counts["terminal"] += horizon_terminal
                    effective_by_kind[candidate.kind] = effective_by_kind.get(candidate.kind, 0) + effective
            if counts["states"] % 16 == 0 or counts["states"] == len(rows):
                elapsed = max(1e-6, time.monotonic() - started)
                print(
                    f"counterfactual states={counts['states']}/{len(rows)} "
                    f"players={counts['players']} candidates={counts['candidates']} "
                    f"elapsed={elapsed:.1f}s",
                    flush=True,
                )
    summary = {
        "format": "kaggriculture_counterfactual_dataset_v2",
        "bank": str(bank),
        "manifest": str(manifest_path),
        "output": str(output),
        "native_state_version": state_version,
        "native_state_size": state_size,
        "opponent_mode": args.opponent_mode,
        "horizon": args.horizon,
        "episode_steps": args.episode_steps,
        "turns_per_day": args.turns_per_day,
        "players": args.players,
        "max_candidates": args.max_candidates,
        "branch_batch_size": args.branch_batch_size,
        "branch_workers": args.branch_workers,
        "state_selection": args.state_selection,
        "state_seed": args.state_seed,
        "selected_state_rows": len(rows),
        "state_shard_index": args.state_shard_index,
        "state_shard_count": args.state_shard_count,
        "counts": counts,
        "effective_by_kind": effective_by_kind,
        "target": "candidate_final_money - baseline_final_money",
        "candidate_semantics": (
            "strategic overlay: explicit farmer/market fields replace the "
            "fallback action; unspecified hired-hand work is preserved"
        ),
        "normalization": "delta_money / state_config.starting_money",
        "observation_boundary": "opponent private inventories are never used in feature columns",
        "warning": (
            "pass/expert_first/trace continuations are not reactive opponent policies"
            if args.opponent_mode not in {"rule", "learned"} else
            (
                "rule continuation is reactive but scripted; validate against learned league policies"
                if args.opponent_mode == "rule" else
                "learned continuation uses snapshot-local policy inference; recurrent hidden history is unavailable"
            )
        ),
        "policy_paths": list(getattr(provider, "paths", ())) if provider is not None else [],
    }
    pathlib.Path(f"{output}.summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return summary


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bank", required=True, help="verified .kgb state bank")
    parser.add_argument("--manifest", help="bank manifest (default BANK.manifest.tsv)")
    parser.add_argument("--output", required=True, help="output candidate TSV")
    parser.add_argument("--lib", default=str(DEFAULT_LIB))
    parser.add_argument("--limit-states", type=int, default=0, help="0 = all manifest rows")
    parser.add_argument(
        "--state-selection", choices=SELECTIONS, default="first",
        help="how to choose a limited bank subset (default: first for compatibility)",
    )
    parser.add_argument("--state-seed", type=int, default=707)
    parser.add_argument(
        "--state-shard-index", type=int, default=0,
        help="zero-based worker shard after sampling (default: 0)",
    )
    parser.add_argument(
        "--state-shard-count", type=int, default=1,
        help="number of non-overlapping worker shards (default: 1)",
    )
    parser.add_argument("--max-candidates", type=int, default=0, help="0 = full core catalog")
    parser.add_argument(
        "--branch-batch-size", type=int, default=128,
        help="learned snapshot-local branch states per GPU inference batch",
    )
    parser.add_argument(
        "--branch-workers", type=int, default=max(1, min(4, os.cpu_count() or 1)),
        help="parallel native branch workers (deterministic labels preserve order)",
    )
    parser.add_argument(
        "--direct-only", action="store_true",
        help="omit multi-turn plant/build/service candidates (C0 compatibility mode)",
    )
    parser.add_argument("--players", choices=("both", "0", "1"), default="both")
    parser.add_argument("--horizon", default="1", help="positive steps or 'terminal'")
    parser.add_argument(
        "--opponent-mode", choices=("pass", "expert_first", "trace", "rule", "learned"), default="pass",
    )
    parser.add_argument("--trace", help="JSONL trace for --opponent-mode trace")
    parser.add_argument(
        "--rule-liquidation-turns", type=int, default=48,
        help="rule-provider liquidation window (default: 48)",
    )
    parser.add_argument("--episode-steps", type=int, default=720)
    parser.add_argument("--turns-per-day", type=int, default=24)
    parser.add_argument("--shed-capacity", type=int, default=100)
    parser.add_argument(
        "--policy-model", action="append",
        help="learned opponent checkpoint (repeatable or comma-separated)",
    )
    parser.add_argument("--league", help="league.ini containing enabled learned policies")
    parser.add_argument(
        "--policy-stochastic", action="store_true",
        help="sample learned policy logits instead of taking argmax",
    )
    parser.add_argument(
        "--policy-backend", choices=("numpy", "torch"), default="numpy",
        help="learned policy inference backend (torch enables GPU acceleration)",
    )
    parser.add_argument("--policy-device", default="cuda")
    args = parser.parse_args(argv)
    if (
        args.limit_states < 0 or args.max_candidates < 0 or args.branch_batch_size < 1
        or args.branch_workers < 1
        or args.rule_liquidation_turns < 0
        or args.state_shard_count < 1
        or args.state_shard_index < 0
        or args.state_shard_index >= args.state_shard_count
    ):
        parser.error("limits must be nonnegative")
    return args


def main(argv: list[str] | None = None) -> int:
    print(json.dumps(generate(parse_args(argv)), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
