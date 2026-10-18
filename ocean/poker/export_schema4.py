#!/usr/bin/env python3
"""Metadata-only schema4 adapter for the unchanged frozen range exporter.

Terminal records call the hero's seat `position`; decision records use
`client_pos`. The first VAL attempt stopped at this distinction before its
identity filter yielded any hand or accessed card/action fields. This adapter
adds the terminal-only alias in memory. It changes no source log, model,
scorer, cohort, split, or native computation. Its own hash and execution proof
supplement the frozen host-exporter provenance and are required before TEST.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import export_ranges as frozen

HOST_SHA = "9ba67df8b327d0ab806b60f603f1b808ca9caa20dd1f86da79881a7e43c40108"
SCORER_SHA = "de91cba4eab627afae3b11deddac84f1a9426a38c8a4c2bdbc10bfe27d8dc402"
MODEL_SHA = "c195e2766bec51174e581c8ace18d77d8e564d972f7851e2bfa9b70f6c00e5bd"


def schema4_loads(text, *args, **kwargs):
    value = json.loads(text, *args, **kwargs)
    if isinstance(value, dict) and "winnings_chips" in value:
        position = value["position"]
        if type(position) is not int or position not in (0, 1):
            raise ValueError("Invalid schema4 terminal seat")
        if "client_pos" in value and value["client_pos"] != position:
            raise ValueError("Conflicting terminal seat fields")
        value["client_pos"] = position
    return value


class Schema4JSON:
    loads = staticmethod(schema4_loads)

    def __getattr__(self, name):
        return getattr(json, name)


def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--partition", choices=("val", "test"), required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--choice", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args, _ = parser.parse_known_args()
    host = Path(frozen.__file__)
    scorer = host.parent / "fit_root_range.py"
    if frozen.sha(host) != HOST_SHA or frozen.sha(scorer) != SCORER_SHA or frozen.sha(args.model) != MODEL_SHA:
        raise ValueError("The original fitted model, host exporter and scorer must remain unchanged")
    adapter_hash = frozen.sha(Path(__file__))
    if args.partition == "test":
        if args.choice is None:
            raise ValueError("TEST requires the later exact-choice freeze")
        choice = json.loads(args.choice.read_text())
        if choice.get("schema_adapter_sha256") != adapter_hash:
            raise ValueError("TEST choice must explicitly bind this metadata adapter")
    frozen.json = Schema4JSON()
    frozen.main()
    if frozen.sha(Path(__file__)) != adapter_hash:
        raise ValueError("Schema adapter changed during export")
    proof = {"schema": 1, "phase": "metadata_only_schema4_terminal_seat_adapter",
        "partition": args.partition, "host_exporter_sha256": HOST_SHA,
        "schema_adapter_sha256": adapter_hash, "scorer_sha256": SCORER_SHA,
        "model_sha256": MODEL_SHA, "data_manifest_sha256": frozen.sha(args.output / "manifest.json"),
        "mapping": "For a terminal record identified by key presence only, add client_pos=position in memory; reject missing/invalid/conflicting seat metadata. All other fields and input bytes are unchanged.",
        "card_action_outcome_values_used_by_adapter": False,
        "cohort_split_model_scorer_and_gates_changed": False}
    frozen.write_json(args.output / "schema_adapter_execution.json", proof)
    print(json.dumps({"schema_adapter_sha256": adapter_hash,
        "adapter_execution_sha256": frozen.sha(args.output / "schema_adapter_execution.json")}, indent=2))


if __name__ == "__main__":
    main()
