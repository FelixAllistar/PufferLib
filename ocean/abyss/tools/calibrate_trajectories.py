#!/usr/bin/env python3
"""Fit conservative NPC steering parameters from recorded native-vector frames."""

from __future__ import annotations

import argparse
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path


def finite(value: object) -> bool:
    return isinstance(value, (int, float)) and math.isfinite(value)


def median(values: list[float], default: float) -> float:
    return statistics.median(values) if values else default


def build(frames_path: Path, catalog_path: Path, output: Path) -> None:
    catalog = {row["name"]: row for row in json.loads(catalog_path.read_text())}
    samples: dict[str, dict[str, list[float]]] = defaultdict(lambda: defaultdict(list))

    with frames_path.open(encoding="utf-8") as frames:
        for line in frames:
            frame = json.loads(line)
            for entity in frame.get("entities", []):
                if entity.get("role") != "HostileNpc" or entity.get("name") not in catalog:
                    continue
                name = entity["name"]
                definition = catalog[name]
                maximum = max(float(definition["max_speed_mps"]), 1.0)
                distance = entity.get("distance_m")
                closing = entity.get("closing_mps")
                tangential = entity.get("tangential_mps")
                velocity = entity.get("velocity_mps") or []
                speed = None
                if len(velocity) == 3 and all(finite(v) for v in velocity):
                    speed = math.sqrt(sum(float(v) ** 2 for v in velocity))

                # Native/UI joins occasionally produce one-frame multi-km/s spikes.
                # Dark T0 permits 1.5x base maximum; retain modest measurement slack.
                physical_limit = maximum * 1.8
                if finite(speed) and speed <= physical_limit:
                    samples[name]["speed"].append(float(speed))
                if finite(tangential) and abs(float(tangential)) <= physical_limit:
                    samples[name]["tangential"].append(abs(float(tangential)))
                if finite(closing) and abs(float(closing)) <= physical_limit:
                    samples[name]["closing"].append(float(closing))
                    if finite(distance):
                        error = float(distance) - float(definition["orbit_range_m"])
                        if abs(error) >= 1000 and float(closing) * error > 0:
                            samples[name]["gain"].append(abs(float(closing) / error))

    rows = {}
    for name, definition in catalog.items():
        observed = samples[name]
        base_orbit = max(float(definition["orbit_speed_mps"]), 1.0)
        raw_orbit_scale = median([x / base_orbit for x in observed["tangential"] if x > 5], 1.0)
        # Tangential velocity is relative to the player, so shrink its fit toward
        # the published orbit speed instead of treating it as NPC ground truth.
        orbit_weight = len(observed["tangential"]) / (len(observed["tangential"]) + 100.0)
        orbit_scale = 1.0 + (raw_orbit_scale - 1.0) * orbit_weight
        raw_radial_gain = median(observed["gain"], 0.7)
        radial_weight = len(observed["gain"]) / (len(observed["gain"]) + 50.0)
        radial_gain = 0.7 + (raw_radial_gain - 0.7) * radial_weight
        rows[name] = {
            "radial_gain": min(1.5, max(0.08, radial_gain)),
            "orbit_speed_scale": min(1.25, max(0.75, orbit_scale)),
            "physical_speed_samples": len(observed["speed"]),
            "radial_samples": len(observed["gain"]),
            "tangential_samples": len(observed["tangential"]),
        }
    output.write_text(json.dumps(rows, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--frames", type=Path, default=Path("ocean/abyss/data/recorded/frames.jsonl"))
    parser.add_argument("--catalog", type=Path, default=Path("ocean/abyss/data/npc_catalog.json"))
    parser.add_argument("--output", type=Path, default=Path("ocean/abyss/data/trajectory_calibration.json"))
    args = parser.parse_args()
    build(args.frames, args.catalog, args.output)
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
