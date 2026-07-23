#!/usr/bin/env python3
"""Stream Sanderling events.jsonl into compact simulator calibration data."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path


ACTIVE_ROOMS = {"RoomActive1": 1, "RoomActive2": 2, "RoomActive3": 3}


def compact_entity(entity: dict) -> dict:
    rel = entity.get("rel") or {}
    return {
        "id": entity.get("id"),
        "item_id": entity.get("item_id"),
        "type_id": entity.get("type_id"),
        "role": entity.get("role"),
        "name": entity.get("name"),
        "distance_m": entity.get("distance"),
        "lock_state": entity.get("lock_state"),
        "weapon_assignment": entity.get("weapon_assignment"),
        "attacking_player": entity.get("attacking_player"),
        "position_m": [rel.get("x"), rel.get("y"), rel.get("z")],
        "velocity_mps": [rel.get("vx"), rel.get("vy"), rel.get("vz")],
        "closing_mps": rel.get("closing"),
        "tangential_mps": rel.get("tangential"),
        "radius_m": rel.get("radius"),
    }


def compact_frame(payload: dict) -> dict:
    canonical = payload["canonical"]
    ship = canonical.get("ship") or {}
    navigation = canonical.get("navigation") or {}
    return {
        "mono_ms": payload.get("sample_mono_ms", payload.get("mono_ms")),
        "scene_generation": canonical.get("scene_generation"),
        "room_index": canonical.get("room_index"),
        "stage": canonical.get("inferred_stage"),
        "ship": {
            "shield": ship.get("shield"),
            "armor": ship.get("armor"),
            "hull": ship.get("hull"),
            "cap": ship.get("cap"),
            "speed_mps": ship.get("speed"),
        },
        "navigation": {"mode": navigation.get("mode"), "target": navigation.get("target")},
        "entities": [compact_entity(e) for e in canonical.get("entities", [])],
        "modules": [
            {
                "ship_slot": m.get("ship_slot"),
                "hotkeys": m.get("hotkeys"),
                "cycle": m.get("cycle"),
                "overload": m.get("overload"),
                "quantity": m.get("quantity"),
            }
            for m in canonical.get("modules", [])
        ],
    }


def extract(source: Path, destination: Path, interval_ms: int) -> dict:
    destination.mkdir(parents=True, exist_ok=True)
    frames_path = destination / "frames.jsonl"
    episodes_path = destination / "episodes.json"
    names = Counter()
    episodes: list[dict] = []
    current: dict | None = None
    last_room = 0
    last_emit_ms: dict[tuple[int, int], int] = {}
    frame_count = 0

    with source.open("r", encoding="utf-8-sig") as src, frames_path.open("w", encoding="utf-8") as out:
        for line in src:
            try:
                event = json.loads(line)
            except (json.JSONDecodeError, UnicodeDecodeError):
                continue
            if event.get("type") != "observation":
                continue
            payload = event.get("payload") or {}
            canonical = payload.get("canonical") or {}
            if not canonical.get("is_authoritative"):
                continue
            room = ACTIVE_ROOMS.get(canonical.get("room_phase"))
            if room is None:
                if current is not None and canonical.get("room_phase") in {"RunComplete", "OutsideAbyss"}:
                    current["complete"] = len(current["rooms"]) == 3
                continue
            scene = int(canonical.get("scene_generation") or 0)
            mono_ms = int(payload.get("sample_mono_ms") or 0)

            if current is None or (room == 1 and last_room == 3) or (room == 1 and scene != current["scene_generation"]):
                current = {
                    "episode_index": len(episodes),
                    "scene_generation": scene,
                    "start_mono_ms": mono_ms,
                    "end_mono_ms": mono_ms,
                    "complete": False,
                    "rooms": {},
                }
                episodes.append(current)
            last_room = room
            current["end_mono_ms"] = mono_ms
            room_summary = current["rooms"].setdefault(str(room), {"first_mono_ms": mono_ms, "entities": {}})

            for entity in canonical.get("entities", []):
                role = entity.get("role")
                if role == "HostileNpc" and entity.get("name"):
                    names[entity["name"]] += 1
                key = entity.get("id") or f'{role}:{entity.get("name")}:{entity.get("item_id")}'
                compact = compact_entity(entity)
                previous = room_summary["entities"].get(key)
                # Early room frames are often only partially fused. Keep replacing a
                # partial entity until native vectors and a semantic role arrive.
                if previous is None or (
                    previous.get("role") == "Unknown" and compact.get("role") != "Unknown"
                ) or (
                    previous.get("position_m", [None])[0] is None
                    and compact.get("position_m", [None])[0] is not None
                ):
                    room_summary["entities"][key] = compact

            key = (current["episode_index"], room)
            if mono_ms - last_emit_ms.get(key, -interval_ms) >= interval_ms:
                out.write(json.dumps(compact_frame(payload), separators=(",", ":")) + "\n")
                last_emit_ms[key] = mono_ms
                frame_count += 1

    for episode in episodes:
        episode["rooms"] = [
            {"room_index": int(room), "first_mono_ms": data["first_mono_ms"], "entities": list(data["entities"].values())}
            for room, data in sorted(episode["rooms"].items())
        ]
        episode["duration_seconds"] = (episode["end_mono_ms"] - episode["start_mono_ms"]) / 1000.0
        episode["complete"] = len(episode["rooms"]) == 3

    summary = {
        "source": str(source),
        "episodes": len(episodes),
        "complete_episodes": sum(bool(e["complete"]) for e in episodes),
        "rooms": sum(len(e["rooms"]) for e in episodes),
        "frames": frame_count,
        "hostile_names": dict(names.most_common()),
    }
    episodes_path.write_text(json.dumps({"summary": summary, "episodes": episodes}, indent=2) + "\n", encoding="utf-8")
    (destination / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return summary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("events", type=Path)
    parser.add_argument("--output", type=Path, default=Path("ocean/abyss/data/recorded"))
    parser.add_argument("--interval-ms", type=int, default=1000)
    args = parser.parse_args()
    print(json.dumps(extract(args.events, args.output, args.interval_ms), indent=2))


if __name__ == "__main__":
    main()
