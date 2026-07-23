#!/usr/bin/env python3
"""Normalize the exported Abyss NPC CSV for simulator/runtime use."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path


FIELDS = {
    "name": "type",
    "hull_class": "hull_class",
    "signature_radius_m": "sig_radius",
    "shield_hp": "shield_hp",
    "armor_hp": "armor_hp",
    "structure_hp": "structure_hp",
    "max_speed_mps": "Enemy Max Velocity",
    "orbit_speed_mps": "Enemy Orbit Velocity",
    "orbit_range_m": "max_combat_orbit_range",
    "turret_optimal_m": "Weapon Optimal",
    "turret_falloff_m": "Weapon Falloff",
    "turret_tracking": "Weapon Tracking",
    "turret_cycle_s": "rof",
    "dps": "Total DPS",
}


def number(value: str | None) -> float:
    if value is None or not value.strip():
        return 0.0
    try:
        return float(value)
    except ValueError:
        return 0.0


def build(source: Path, destination: Path) -> list[dict]:
    with source.open("r", encoding="utf-8-sig", newline="") as handle:
        rows = list(csv.DictReader(handle))
    catalog = []
    for row in rows:
        item = {}
        for output, column in FIELDS.items():
            item[output] = row.get(column, "") if output in {"name", "hull_class"} else number(row.get(column))
        item["shield_resists"] = [number(row.get(f"shield_{d}")) / 100.0 for d in ("em", "thermal", "kinetic", "exp")]
        item["armor_resists"] = [number(row.get(f"armor_{d}")) / 100.0 for d in ("em", "thermal", "kinetic", "exp")]
        item["structure_resists"] = [number(row.get(f"structure_{d}")) / 100.0 for d in ("em", "thermal", "kinetic", "exp")]
        turret_dps = number(row.get("Weapon DPS"))
        missile_dps = number(row.get("Missile DPS"))
        if not item["dps"]:
            item["dps"] = turret_dps + missile_dps
        turret_raw = [number(row.get(f"ammo_{d}")) for d in ("em", "thermal", "kinetic", "exp")]
        turret_total = sum(turret_raw)
        turret_mix = [value / turret_total if turret_total else 0.0 for value in turret_raw]
        missile_raw = [0.0, 0.0, 0.0, 0.0]
        damage_index = {"EM": 0, "THE": 1, "KIN": 2, "EXP": 3}
        for amount, damage_type in re.findall(r"([0-9.]+)\s+(EM|THE|KIN|EXP)", row.get("missile_dmg", "")):
            missile_raw[damage_index[damage_type]] += float(amount)
        missile_total = sum(missile_raw)
        missile_mix = [value / missile_total if missile_total else 0.0 for value in missile_raw]
        combined_dps = turret_dps + missile_dps
        item["damage_mix"] = [
            (turret_mix[i] * turret_dps + missile_mix[i] * missile_dps) / combined_dps
            if combined_dps else 0.0
            for i in range(4)
        ]
        item["neutralizer_gj_per_s"] = number(row.get("GJ Neutralized Per Second"))
        item["local_repair_hp_per_s"] = number(row.get("Local Repair Per Second"))
        item["remote_repair_hp_per_s"] = number(row.get("Remote Repair Per Second"))
        catalog.append(item)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(catalog, indent=2) + "\n", encoding="utf-8")
    return catalog


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path, default=Path("ocean/abyss/data/npc_catalog.json"))
    args = parser.parse_args()
    print(f"wrote {len(build(args.source, args.output))} NPC definitions to {args.output}")


if __name__ == "__main__":
    main()
