#!/usr/bin/env python3
"""Inventory exact Kaggriculture replay identities and behavior evidence.

The replay metadata carries both ``info.TeamNames`` (display/team identity)
and ``info.Agents[*].Name`` (submission identity).  They are reported as
separate fields and never merged by fuzzy name matching.  Prefix scanning
counts every episode cheaply; ``--agent-name``/``--display-name`` then opt in
to full JSON parsing for selected identities so action counts and reward
distributions can be quantified without inflating unrelated episodes.

Typical refresh audit::

    python scan_replay_identities.py /workspace/elite_replays/raw/*/*.zip \
        --agent-name Yuan800 --full --output yuan800.json

Use ``--top 20 --full`` to parse the twenty most represented exact identities
after a prefix pass.  The output includes episode/player-stream counts by day,
final-money summaries, low-level action counts, and opening/turn-180 slices.
"""

from __future__ import annotations

import argparse
import collections
import datetime as dt
import glob
import io
import json
import math
import pathlib
import re
import statistics
import zipfile
from typing import Any, Iterable


_TEAM_RE = re.compile(r'"TeamNames"\s*:\s*(\[[^\]]*\])')
_AGENTS_RE = re.compile(r'"Agents"\s*:\s*(\[[^\]]*\])')
_EPISODE_RE = re.compile(r'"EpisodeId"\s*:\s*([^,}]+)')
_VERSION_RE = re.compile(r'"module_version"\s*:\s*"([^"]*)"')


def _expand(values: Iterable[str]) -> list[pathlib.Path]:
    result: list[pathlib.Path] = []
    seen: set[pathlib.Path] = set()
    for value in values:
        matches = glob.glob(value, recursive=True) or [value]
        for match in matches:
            path = pathlib.Path(match)
            paths = sorted(path.rglob("*.zip")) if path.is_dir() else [path]
            for candidate in paths:
                resolved = candidate.resolve()
                if resolved in seen:
                    continue
                if not candidate.is_file():
                    raise FileNotFoundError(candidate)
                seen.add(resolved)
                result.append(candidate)
    return result


def _prefix_metadata(raw: bytes) -> dict[str, Any] | None:
    text = raw.decode("utf-8", errors="replace")
    team_match = _TEAM_RE.search(text)
    if not team_match:
        return None
    try:
        teams = json.loads(team_match.group(1))
    except json.JSONDecodeError:
        return None
    episode_match = _EPISODE_RE.search(text)
    version_match = _VERSION_RE.search(text)
    episode_id = episode_match.group(1).strip() if episode_match else ""
    try:
        episode_id = json.loads(episode_id)
    except json.JSONDecodeError:
        episode_id = str(episode_id).strip('"')
    agent_match = _AGENTS_RE.search(text)
    agent_names = []
    if agent_match:
        try:
            agents = json.loads(agent_match.group(1))
            if isinstance(agents, list):
                agent_names = [
                    str(value.get("Name", "")) if isinstance(value, dict) else ""
                    for value in agents
                ]
        except json.JSONDecodeError:
            agent_names = []
    return {
        "team_names": [str(value) for value in teams] if isinstance(teams, list) else [],
        "agent_names": agent_names,
        "episode_id": str(episode_id),
        "module_version": version_match.group(1) if version_match else "",
    }


def _episode_identity(episode: dict[str, Any]) -> list[dict[str, Any]]:
    info = episode.get("info") or {}
    teams = info.get("TeamNames")
    agents = info.get("Agents")
    if not isinstance(teams, list):
        teams = []
    if not isinstance(agents, list):
        agents = []
    result = []
    for player in range(max(2, len(teams), len(agents))):
        team = str(teams[player]) if player < len(teams) else ""
        agent = ""
        if player < len(agents) and isinstance(agents[player], dict):
            agent = str(agents[player].get("Name", ""))
        # TeamNames is the exact fallback for older replay metadata, but when
        # both fields exist they remain distinct in the report.
        result.append({
            "player": player, "display_name": team,
            "agent_name": agent or team,
        })
    return result


def _source_day(path: pathlib.Path) -> str:
    match = re.search(r"(20\d\d-\d\d-\d\d)", str(path))
    return match.group(1) if match else "unknown"


def _new_record(
    identity: dict[str, Any], *, day: str, source: str, episode_id: str,
    module_version: str,
) -> dict[str, Any]:
    return {
        "display_name": identity["display_name"],
        "agent_name": identity["agent_name"],
        "episodes": 0,
        "player_streams": 0,
        "days": collections.Counter(),
        "sources": collections.Counter(),
        "episode_ids": [],
        "module_versions": collections.Counter(),
        "final_money": [],
        "wins": 0,
        "actions": collections.Counter(),
        "market_actions": collections.Counter(),
        "market_quantities": collections.Counter(),
        "opening_actions": collections.Counter(),
        "turn180_actions": collections.Counter(),
        "opening_market": collections.Counter(),
        "turn180_market": collections.Counter(),
    }


def _add_prefix_record(
    records: dict[tuple[str, str], dict[str, Any]], identity: dict[str, Any],
    *, day: str, source: str, episode_id: str, module_version: str,
) -> dict[str, Any]:
    key = (identity["display_name"], identity["agent_name"])
    record = records.setdefault(
        key,
        _new_record(identity, day=day, source=source,
                    episode_id=episode_id, module_version=module_version),
    )
    record["episodes"] += 1
    record["player_streams"] += 1
    record["days"][day] += 1
    record["sources"][source] += 1
    record["module_versions"][module_version] += 1
    if len(record["episode_ids"]) < 10000:
        record["episode_ids"].append(episode_id)
    return record


def _command_op(command: Any) -> str:
    return (
        str(command[0]).upper()
        if isinstance(command, (list, tuple)) and command else "INVALID"
    )


def _add_full_episode(
    record: dict[str, Any], episode: dict[str, Any], player: int,
) -> None:
    rewards = episode.get("rewards") or []
    if player < len(rewards):
        try:
            record["final_money"].append(float(rewards[player]))
            if rewards and float(rewards[player]) == max(float(value) for value in rewards):
                record["wins"] += 1
        except (TypeError, ValueError):
            pass
    steps = episode.get("steps") or []
    for turn, step in enumerate(steps):
        if not isinstance(step, list) or player >= len(step):
            continue
        row = step[player]
        if not isinstance(row, dict):
            continue
        action = row.get("action") or {}
        commands = [_command_op(action.get("farmer", ["PASS"]))]
        commands.extend(_command_op(command) for command in action.get("hands", ()))
        target = record["opening_actions"] if turn <= 60 else (
            record["turn180_actions"] if 168 <= turn <= 192 else record["actions"]
        )
        for op in commands:
            record["actions"][op] += 1
            target[op] += 1
        market = action.get("market") or []
        for order in market:
            op = _command_op(order)
            record["market_actions"][op] += 1
            market_target = record["opening_market"] if turn <= 60 else (
                record["turn180_market"] if 168 <= turn <= 192 else None
            )
            if market_target is not None:
                market_target[op] += 1
            if isinstance(order, (list, tuple)) and len(order) > 1:
                item = str(order[1]).upper()
                key = f"{op}:{item}"
                record["market_actions"][key] += 1
            if isinstance(order, (list, tuple)) and len(order) > 2:
                try:
                    amount = int(order[2])
                except (TypeError, ValueError, OverflowError):
                    amount = 0
                if amount > 0:
                    record["market_quantities"][op] += amount


def _summary(record: dict[str, Any], *, full: bool) -> dict[str, Any]:
    result = {
        "display_name": record["display_name"],
        "agent_name": record["agent_name"],
        "episodes": record["episodes"],
        "player_streams": record["player_streams"],
        "days": dict(sorted(record["days"].items())),
        "sources": dict(sorted(record["sources"].items())),
        "module_versions": dict(sorted(record["module_versions"].items())),
        "wins": record["wins"],
        "episode_ids": record["episode_ids"],
        "full_parse": full,
    }
    if full:
        values = sorted(record["final_money"])
        if values:
            percentile = lambda fraction: values[min(
                len(values) - 1, int(round(fraction * (len(values) - 1)))
            )]
            result["final_money"] = {
                "count": len(values), "min": values[0],
                "p25": percentile(.25), "median": percentile(.5),
                "p75": percentile(.75), "max": values[-1],
                "mean": statistics.fmean(values),
            }
        else:
            result["final_money"] = {"count": 0}
        result["actions"] = dict(record["actions"].most_common())
        result["market_actions"] = dict(record["market_actions"].most_common())
        result["market_quantities"] = dict(record["market_quantities"].most_common())
        result["opening_actions"] = dict(record["opening_actions"].most_common())
        result["opening_market"] = dict(record["opening_market"].most_common())
        result["turn180_actions"] = dict(record["turn180_actions"].most_common())
        result["turn180_market"] = dict(record["turn180_market"].most_common())
    return result


def scan(
    paths: Iterable[pathlib.Path], *, prefix_bytes: int = 8192,
    agent_names: set[str] | None = None,
    display_names: set[str] | None = None,
    full: bool = False, top: int = 0,
) -> dict[str, Any]:
    paths = list(paths)
    records: dict[tuple[str, str], dict[str, Any]] = {}
    full_candidates: list[tuple[pathlib.Path, str, dict[str, Any], int]] = []
    for path in paths:
        day = _source_day(path)
        with zipfile.ZipFile(path) as archive:
            names = sorted(name for name in archive.namelist()
                           if name.lower().endswith(".json") and not name.endswith("/"))
            for member in names:
                with archive.open(member) as stream:
                    prefix = stream.read(prefix_bytes)
                metadata = _prefix_metadata(prefix)
                if metadata is None:
                    continue
                # A full parse is selected only by exact display/submission
                # identity; no case folding or substring aliases are used.
                try:
                    episode_id = metadata["episode_id"]
                    version = metadata["module_version"]
                    identities = []
                    for player, name in enumerate(metadata["team_names"]):
                        agents = metadata.get("agent_names", ())
                        identities.append({
                            "player": player, "display_name": name,
                            "agent_name": str(agents[player])
                            if player < len(agents) and agents[player] else name,
                        })
                    for identity in identities:
                        record = _add_prefix_record(
                            records, identity, day=day, source=str(path),
                            episode_id=episode_id, module_version=version,
                        )
                        wanted = (
                            (agent_names and identity["agent_name"] in agent_names)
                            or (display_names and identity["display_name"] in display_names)
                        )
                        if wanted:
                            full_candidates.append((path, member, identity, identity["player"]))
                except (TypeError, ValueError):
                    continue

    # Optional full pass: parse requested identities, or discover the top-N
    # exact names from the prefix inventory first.
    if top:
        ranked = sorted(records.values(), key=lambda value: (
            -value["episodes"], value["agent_name"], value["display_name"]
        ))[:top]
        selected = {(value["display_name"], value["agent_name"]) for value in ranked}
        # Retain explicitly requested exact identities while adding the top-N
        # set; ``--agent-name Yuan800 --top 20 --full`` should not silently
        # drop Yuan800 merely because it is not yet in the top twenty.
        retained_candidates = list(full_candidates)
        full_candidates = retained_candidates
        for path in paths:
            day = _source_day(path)
            with zipfile.ZipFile(path) as archive:
                for member in sorted(name for name in archive.namelist()
                                     if name.lower().endswith(".json") and not name.endswith("/")):
                    with archive.open(member) as stream:
                        prefix = stream.read(prefix_bytes)
                    metadata = _prefix_metadata(prefix)
                    if metadata is None:
                        continue
                    agents = metadata.get("agent_names", ())
                    for player, name in enumerate(metadata["team_names"]):
                        agent_name = str(agents[player]) if player < len(agents) and agents[player] else name
                        if (name, agent_name) in selected:
                            full_candidates.append((path, member, {
                                "display_name": name, "agent_name": agent_name,
                            }, player))
    if full and not full_candidates and not agent_names and not display_names and not top:
        # Explicit --full with no selector means all exact names. This is
        # intentionally opt-in because parsing hundreds of ~30 MB episodes is
        # much more expensive than the prefix inventory.
        for path in paths:
            with zipfile.ZipFile(path) as archive:
                for member in sorted(name for name in archive.namelist()
                                     if name.lower().endswith(".json") and not name.endswith("/")):
                    full_candidates.append((path, member, {}, 0))

    full_keys: set[tuple[str, str]] = set()
    for path, member, hinted, hinted_player in full_candidates:
        with zipfile.ZipFile(path) as archive:
            with archive.open(member) as stream:
                episode = json.load(io.TextIOWrapper(stream, encoding="utf-8"))
        day = _source_day(path)
        metadata = _episode_identity(episode)
        episode_id = str((episode.get("info") or {}).get("EpisodeId", ""))
        version = str(episode.get("module_version", ""))
        rewards = episode.get("rewards") or []
        for identity in metadata:
            key = (identity["display_name"], identity["agent_name"])
            selected = (
                (agent_names and identity["agent_name"] in agent_names)
                or (display_names and identity["display_name"] in display_names)
                or (top and key in {
                    (value["display_name"], value["agent_name"])
                    for value in sorted(records.values(), key=lambda value: (
                        -value["episodes"], value["agent_name"], value["display_name"]
                    ))[:top]
                })
                or (full and not agent_names and not display_names and not top)
            )
            if not selected or identity["player"] >= len(rewards):
                continue
            # Prefix records used team name as a fallback. If Agents.Name is
            # available, move this exact stream to its distinct identity key
            # before adding full metrics, avoiding accidental name merging.
            record = records.setdefault(
                key, _new_record(identity, day=day, source=str(path),
                                 episode_id=episode_id, module_version=version)
            )
            _add_full_episode(record, episode, identity["player"])
            full_keys.add(key)

    summaries = [
        _summary(record, full=(key in full_keys))
        for key, record in sorted(records.items(), key=lambda item: (
            -item[1]["episodes"], item[0][1], item[0][0]
        ))
    ]
    return {
        "files": [str(path) for path in paths],
        "prefix_bytes": prefix_bytes,
        "records": summaries,
        "full_identity_keys": [list(key) for key in sorted(full_keys)],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", help="daily ZIPs or globs")
    parser.add_argument("--prefix-bytes", type=int, default=8192)
    parser.add_argument("--agent-name", action="append", default=[])
    parser.add_argument("--display-name", action="append", default=[])
    parser.add_argument("--top", type=int, default=0)
    parser.add_argument("--full", action="store_true")
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    if args.prefix_bytes < 1024:
        parser.error("--prefix-bytes must be at least 1024")
    if args.top < 0:
        parser.error("--top must be non-negative")
    report = scan(
        _expand(args.inputs), prefix_bytes=args.prefix_bytes,
        agent_names=set(args.agent_name), display_names=set(args.display_name),
        full=args.full, top=args.top,
    )
    payload = json.dumps(report, indent=2, ensure_ascii=False, sort_keys=True)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(payload + "\n", encoding="utf-8")
    print(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
