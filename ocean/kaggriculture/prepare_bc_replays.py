#!/usr/bin/env python3
"""Inventory demos cheaply; optionally cache parity-checked primitive action tapes.

This is data preparation, NOT a BC encoder or trainer. A tape contains the seed,
game configuration and BOTH primitive action streams. Replaying it through the
matching native core reconstructs states without reparsing the large official
JSON. Macro labels, policy observations and reward targets are deliberately NOT
cached here: they must be regenerated for the requested controller/reward ABI.

The inventory only reads metadata prefixes. Counts are available trajectories,
not leaderboard ranks or proof of a stable submission revision. The bounded
cache pass checks every frame against the official replay before publishing.
"""

from __future__ import annotations

import argparse
import collections
import csv
import ctypes
import gzip
import hashlib
import json
import math
import os
import pathlib
import re
import statistics
import tempfile
import zipfile

try:
    from . import scan_replay_identities as identities
    from . import replay_native as native
    from .index_replay_states import validate_episode, version_tuple
except ImportError:
    import scan_replay_identities as identities
    import replay_native as native
    from index_replay_states import validate_episode, version_tuple


FORMAT = "kaggriculture_primitive_tape_v1"
FIELDS = ("episode_id", "archive", "member", "crc32", "day", "player",
          "display_name", "agent_name", "module_version", "final_money",
          "opponent_money", "split")
REWARDS = re.compile(rb'"rewards"\s*:\s*(\[[^\]]*\])')


def split_for(episode_id: str, fraction: float, seed: int) -> str:
    # Both seats, all controllers, and repeated imports share a single split.
    key = hashlib.sha256(f"{seed}:{episode_id}".encode()).digest()
    return "holdout" if int.from_bytes(key[:8], "little") / 2**64 < fraction else "train"


def catalog(paths, *, version="1.32.7", fraction=0.15, seed=20260920):
    rows = {}
    counts = collections.Counter()
    for path in sorted(set(pathlib.Path(p).resolve() for p in paths)):
        with zipfile.ZipFile(path) as archive:
            for member in sorted(archive.infolist(), key=lambda item: item.filename):
                if member.is_dir() or not member.filename.endswith(".json"):
                    continue
                counts["members"] += 1
                with archive.open(member) as stream:
                    prefix = stream.read(8192)
                meta = identities._prefix_metadata(prefix)
                reward_match = REWARDS.search(prefix)
                if not meta or not meta["episode_id"] or len(meta["team_names"]) != 2 or not reward_match:
                    counts["missing_prefix_metadata"] += 1
                    continue
                if meta["module_version"] != version:
                    counts["other_module_version"] += 1
                    continue
                try:
                    rewards = [float(value) for value in json.loads(reward_match[1])]
                except (ValueError, TypeError):
                    counts["bad_rewards"] += 1
                    continue
                if len(rewards) != 2 or not all(math.isfinite(value) for value in rewards):
                    counts["bad_rewards"] += 1
                    continue
                for player, name in enumerate(meta["team_names"]):
                    agent_names = meta["agent_names"]
                    row = dict(zip(FIELDS, (
                        meta["episode_id"], str(path), member.filename,
                        f"{member.CRC:08x}", identities._source_day(path), player,
                        name, (agent_names[player] if player < len(agent_names) else "") or name,
                        meta["module_version"], rewards[player], rewards[1 - player],
                        split_for(meta["episode_id"], fraction, seed),
                    )))
                    key = (row["episode_id"], player)
                    if key in rows:
                        previous = rows[key]
                        if any(previous[field] != row[field] for field in (
                            "display_name", "agent_name", "module_version", "final_money", "opponent_money",
                        )):
                            raise ValueError(f"conflicting metadata for episode/player {key}")
                        counts["duplicate_player_streams"] += 1
                        continue
                    rows[key] = row
    return list(rows.values()), dict(counts)


def summarize(rows):
    groups = collections.defaultdict(list)
    for row in rows:
        groups[(row["display_name"], row["agent_name"])].append(row)
    result = []
    for (display, agent), group in groups.items():
        result.append({
            "display_name": display, "agent_name": agent,
            "player_streams": len(group),
            "unique_episodes": len({r["episode_id"] for r in group}),
            "days": dict(sorted(collections.Counter(r["day"] for r in group).items())),
            "split": dict(collections.Counter(r["split"] for r in group)),
            "cash_mean": statistics.fmean(r["final_money"] for r in group),
            "cash_median": statistics.median(r["final_money"] for r in group),
        })
    return sorted(result, key=lambda r: (-r["player_streams"], r["display_name"], r["agent_name"]))


def compact_tape(episode):
    reason = validate_episode(episode, version_tuple(episode["module_version"]))
    if reason:
        raise ValueError(reason)
    steps = episode["steps"]
    if len(steps) != int(episode["configuration"]["episodeSteps"]):
        raise ValueError("incomplete episode")
    actions = []
    for index, frame in enumerate(steps):
        # Kaggle stores shared fields (step, farms, market, town) on seat 0;
        # seat 1 may contain only its private observation and player index.
        if int(frame[0]["observation"]["step"]) != index or any(
            "step" in record["observation"] and int(record["observation"]["step"]) != index
            for record in frame[1:]
        ):
            raise ValueError("non-contiguous replay steps")
        if index:
            pair = [record.get("action") for record in frame]
            if not all(isinstance(action, dict) for action in pair):
                raise ValueError("missing primitive action")
            actions.append(pair)
    return {
        "format": FORMAT, "configuration": episode["configuration"],
        "info": episode["info"], "module_version": episode["module_version"],
        "rewards": episode["rewards"], "frames": len(steps), "actions": actions,
        "action_alignment": "actions[t] takes native state t to state t+1",
        "bc_ready": False,
    }


def verify_tape(lib, tape, frames=None):
    """With frames, verify every official frame; without, check cached rollout end."""
    cfg = native.replay_config(tape)
    state = lib.kg_create(ctypes.byref(cfg))
    if not state:
        raise RuntimeError("kg_create failed")
    try:
        if tape["frames"] != len(tape["actions"]) + 1:
            raise ValueError("action/frame alignment mismatch")
        for turn in range(tape["frames"]):
            if frames is not None:
                difference = native.first_difference(
                    native.canonical_replay_frame(frames[turn]), native.c_snapshot(lib, state),
                )
                if difference:
                    raise ValueError(f"parity failure at turn {turn}: {difference}")
            if turn < len(tape["actions"]):
                pair = (native.CAction * 2)(*(native.c_action(a) for a in tape["actions"][turn]))
                lib.kg_step(state, pair)
        cash = [lib.kg_player_money(state, player) for player in range(2)]
        if cash != tape["rewards"] or not lib.kg_done(state):
            raise ValueError(f"terminal mismatch: cash={cash} expected={tape['rewards']}")
    finally:
        lib.kg_destroy(state)


def cache_one(lib, lib_hash, row, directory):
    # Changed archive/core => distinct cache. No stale binary-state ABI reuse.
    key = f"{row['episode_id']}_{row['crc32']}_{lib_hash[:16]}"
    path = directory / f"{key}.tape.json.gz"
    if path.exists():
        with gzip.open(path, "rt", encoding="utf-8") as stream:
            tape = json.load(stream)
        if tape.get("format") != FORMAT or tape.get("core_sha256") != lib_hash or tape.get("source_crc32") != row["crc32"]:
            raise ValueError(f"invalid existing cache: {path}")
        verify_tape(lib, tape)
        return path, True, tape["parity_frames"]
    with zipfile.ZipFile(row["archive"]) as archive:
        member = archive.getinfo(row["member"])
        if f"{member.CRC:08x}" != row["crc32"]:
            raise ValueError("archive changed after inventory")
        raw = archive.read(member)
    episode = json.loads(raw)
    if str(episode["info"]["EpisodeId"]) != row["episode_id"] or episode["module_version"] != row["module_version"]:
        raise ValueError("full replay metadata differs from inventory")
    tape = compact_tape(episode)
    verify_tape(lib, tape, episode["steps"])
    tape.update(core_sha256=lib_hash, source_sha256=hashlib.sha256(raw).hexdigest(),
                source_archive=row["archive"], source_member=row["member"],
                source_crc32=row["crc32"], parity_frames=len(episode["steps"]))
    directory.mkdir(parents=True, exist_ok=True)
    # Atomic, exclusive publication: interruption cannot create a valid-looking
    # partial tape, and another process's existing cache is never overwritten.
    payload = json.dumps(tape, separators=(",", ":"), allow_nan=False).encode()
    with tempfile.NamedTemporaryFile(dir=directory, delete=False) as destination:
        temporary = pathlib.Path(destination.name)
        destination.write(gzip.compress(payload, compresslevel=1, mtime=0))
    try:
        os.link(temporary, path)
    finally:
        temporary.unlink()
    return path, False, tape["parity_frames"]


def write_outputs(directory, rows, counts, cache_rows, rejected, options):
    directory.mkdir(parents=True, exist_ok=True)
    # The output directory is a new run, not a mutable training data location.
    with (directory / "catalog.tsv").open("x", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS, delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)
    report = {
        "prefix_inventory_only": True, "bc_ready": False,
        "leaderboard_rank_verified": False, "submission_revision_verified": False,
        "counts": counts, "unique_episodes": len({r["episode_id"] for r in rows}),
        "identities": summarize(rows), "cache": cache_rows,
        "rejected_cache_episodes": rejected, "options": options,
        "notes": ["Cash statistics are archive/opponent-biased, not a controlled ranking.",
                  "Holdout assignment is episode-level and shared by both seats.",
                  "Cached tapes need a controller-specific float observation/label adapter before BC."],
    }
    with (directory / "summary.json").open("x") as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write("\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+")
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--exact-version", default="1.32.7")
    parser.add_argument("--holdout-fraction", type=float, default=0.15)
    parser.add_argument("--seed", type=int, default=20260920)
    parser.add_argument("--teacher", help="exact display name; cache only this identity")
    parser.add_argument("--agent-name", help="optional exact replay agent name as a second condition")
    parser.add_argument("--cache-limit", type=int, default=0, help="max unique games to fully parse/verify")
    parser.add_argument("--cache-dir", type=pathlib.Path)
    parser.add_argument("--lib", type=pathlib.Path, help="native core; no GPU required")
    parser.add_argument("--skip-incompatible", action="store_true",
                        help="record and exclude parity/input failures instead of aborting")
    args = parser.parse_args()
    if not 0 < args.holdout_fraction < 1 or args.cache_limit < 0:
        parser.error("holdout fraction must be in (0,1); cache limit must be nonnegative")
    if args.cache_limit and not (args.teacher and args.lib):
        parser.error("caching requires --teacher and --lib")
    if args.output.exists():
        parser.error("output already exists; use a new inventory directory")
    rows, counts = catalog(identities._expand(args.inputs), version=args.exact_version,
                           fraction=args.holdout_fraction, seed=args.seed)
    if not rows:
        parser.error("no compatible replay metadata found")
    cached, rejected = [], []
    if args.cache_limit:
        selected = {r["episode_id"]: r for r in rows
                    if r["display_name"] == args.teacher and (not args.agent_name or r["agent_name"] == args.agent_name)}
        if not selected:
            parser.error("no matching teacher")
        lib = native.load_core(args.lib.resolve())
        lib_hash = hashlib.sha256(args.lib.read_bytes()).hexdigest()
        # Stable random sample, independent of cash/outcome and source ordering.
        selected = sorted(selected.values(), key=lambda r: hashlib.sha256(
            f"sample:{args.seed}:{r['episode_id']}".encode()).digest())[:args.cache_limit]
        directory = args.cache_dir or args.output / "tapes"
        for row in selected:
            try:
                path, reused, frames = cache_one(lib, lib_hash, row, directory)
            except (ValueError, KeyError, TypeError) as error:
                if not args.skip_incompatible:
                    raise
                item = {"episode_id": row["episode_id"], "archive": row["archive"],
                        "member": row["member"], "reason": str(error)}
                rejected.append(item)
                print(json.dumps({"rejected": item}), flush=True)
                continue
            item = {"episode_id": row["episode_id"], "split": row["split"],
                    "path": str(path), "reused": reused, "parity_frames": frames}
            cached.append(item)
            print(json.dumps(item), flush=True)
    options = {key: str(value) if isinstance(value, pathlib.Path) else value
               for key, value in vars(args).items()}
    report = write_outputs(args.output, rows, counts, cached, rejected, options)
    print(json.dumps({"unique_episodes": report["unique_episodes"],
                      "top_identities": report["identities"][:12], "cached": len(cached),
                      "rejected": len(rejected)}, indent=2))
    if args.cache_limit and not cached:
        raise SystemExit("No parity-compatible tapes; see summary.json for failures")


if __name__ == "__main__":
    main()
