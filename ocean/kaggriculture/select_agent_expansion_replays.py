#!/usr/bin/env python3
"""Find exact-player replay trajectories with strong early farm expansion."""

from __future__ import annotations

import argparse
import csv
import json
import pathlib
import statistics
import zipfile
from typing import Any

from index_replay_states import (
    agent_names,
    expand_inputs,
    iter_replays,
)


FIELDS = (
    "episode_id", "source", "player", "agent", "module_version",
    "final_money", "winner", "land_60", "plants_60", "animals_60",
    "hands_60", "money_60", "land_200", "plants_200", "animals_200",
    "hands_200", "money_200", "selected",
)


def iter_agent_replays(
    paths: list[pathlib.Path], agent: str, selected_sources: set[str] | None,
):
    """Decode only archive members whose small metadata prefix names agent."""
    if selected_sources is not None:
        yield from iter_replays(paths, selected_sources)
        return
    needle = json.dumps(agent).encode("utf-8")
    zip_paths = [path for path in paths if path.suffix.lower() == ".zip"]
    other_paths = [path for path in paths if path.suffix.lower() != ".zip"]
    for path in zip_paths:
        with zipfile.ZipFile(path) as archive:
            for name in sorted(archive.namelist()):
                if not name.lower().endswith(".json") or name.endswith("/"):
                    continue
                with archive.open(name) as raw:
                    if needle not in raw.read(4096):
                        continue
                with archive.open(name) as raw:
                    episode = json.load(raw)
                yield f"{path}:{name}", episode
    yield from iter_replays(other_paths)


def farm_stats(episode: dict[str, Any], player: int, turn: int) -> dict[str, int]:
    steps = episode["steps"]
    index = min(max(turn, 0), len(steps) - 1)
    observation = steps[index][player]["observation"]
    farm = observation["farms"][player]
    plants = 0
    animals = 0
    for row in farm.get("tiles", []):
        for tile in row:
            if not isinstance(tile, dict):
                continue
            kind = str(tile.get("kind", "")).upper()
            if kind == "PLANT":
                plants += 1
            elif kind == "ANIMAL" or "animal" in tile:
                animals += 1
    return {
        "land": len(farm.get("unlocked_quadrants", [])),
        "plants": plants,
        "animals": animals,
        "hands": 1 + len(farm.get("hands", [])),
        "money": int(float(farm.get("money", 0))),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+")
    parser.add_argument("--agent", required=True)
    parser.add_argument(
        "--manifest", type=pathlib.Path,
        help="player manifest used to avoid decoding unrelated replay members",
    )
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--summary", type=pathlib.Path)
    parser.add_argument("--exact-version", default="1.32.7")
    parser.add_argument("--minimum-final-money", type=float, default=0)
    parser.add_argument("--land-turn", type=int, default=200)
    parser.add_argument("--minimum-land", type=int, default=3)
    args = parser.parse_args()

    paths = expand_inputs(args.inputs)
    selected_sources: set[str] | None = None
    if args.manifest is not None:
        by_archive: dict[str, set[str]] = {}
        with args.manifest.open(encoding="utf-8", newline="") as stream:
            manifest = csv.DictReader(stream, delimiter="\t")
            for row in manifest:
                if row.get("agent") != args.agent:
                    continue
                archive, _, member = str(row.get("source", "")).partition(":")
                if member:
                    by_archive.setdefault(pathlib.Path(archive).stem, set()).add(member)
        selected_sources = {
            f"{path}:{member}"
            for path in paths
            for member in by_archive.get(path.stem, set())
        }
        if not selected_sources:
            raise SystemExit(f"manifest contains no trajectories for {args.agent!r}")

    rows: list[dict[str, Any]] = []
    seen = 0
    for source, episode in iter_agent_replays(paths, args.agent, selected_sources):
        if str(episode.get("module_version", "")) != args.exact_version:
            continue
        names = agent_names(episode)
        if args.agent not in names:
            continue
        seen += 1
        player = names.index(args.agent)
        rewards = [float(value) for value in episode.get("rewards", [0, 0])]
        final_money = rewards[player]
        early = farm_stats(episode, player, 60)
        target = farm_stats(episode, player, args.land_turn)
        selected = (
            final_money >= args.minimum_final_money
            and target["land"] >= args.minimum_land
        )
        rows.append({
            "episode_id": str((episode.get("info") or {}).get(
                "EpisodeId", episode.get("id", ""))),
            "source": source,
            "player": player,
            "agent": args.agent,
            "module_version": episode.get("module_version", ""),
            "final_money": final_money,
            "winner": int(final_money == max(rewards)),
            "land_60": early["land"],
            "plants_60": early["plants"],
            "animals_60": early["animals"],
            "hands_60": early["hands"],
            "money_60": early["money"],
            "land_200": target["land"],
            "plants_200": target["plants"],
            "animals_200": target["animals"],
            "hands_200": target["hands"],
            "money_200": target["money"],
            "selected": int(selected),
        })

    selected_rows = [row for row in rows if row["selected"]]
    if not selected_rows:
        raise SystemExit(
            f"no {args.agent!r} trajectories reached {args.minimum_land} land "
            f"by turn {args.land_turn}"
        )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS, delimiter="\t")
        writer.writeheader()
        writer.writerows(selected_rows)

    summary = {
        "agent": args.agent,
        "exact_version": args.exact_version,
        "matching_episodes": seen,
        "selected_trajectories": len(selected_rows),
        "minimum_final_money": args.minimum_final_money,
        "minimum_land": args.minimum_land,
        "land_turn": args.land_turn,
        "sources": sorted({
            pathlib.Path(str(row["source"]).split(":", 1)[0]).stem
            for row in selected_rows
        }),
        "final_money_mean": statistics.fmean(
            float(row["final_money"]) for row in selected_rows
        ),
        "land_60_distribution": {
            str(land): sum(int(row["land_60"]) == land for row in selected_rows)
            for land in range(1, 5)
        },
        "land_200_distribution": {
            str(land): sum(int(row["land_200"]) == land for row in selected_rows)
            for land in range(1, 5)
        },
        "plants_60_mean": statistics.fmean(
            int(row["plants_60"]) for row in selected_rows
        ),
        "animals_60_mean": statistics.fmean(
            int(row["animals_60"]) for row in selected_rows
        ),
        "plants_200_mean": statistics.fmean(
            int(row["plants_200"]) for row in selected_rows
        ),
        "animals_200_mean": statistics.fmean(
            int(row["animals_200"]) for row in selected_rows
        ),
    }
    summary_path = args.summary or args.output.with_suffix(".summary.json")
    summary_path.write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
