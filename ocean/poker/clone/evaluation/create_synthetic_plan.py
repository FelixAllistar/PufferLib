#!/usr/bin/env python3
"""Create one fixed offline plan from explicit build and control identities.

Only source and metadata are interpreted. Native libraries and external model
inputs are hashed as opaque files. No evaluator, HTTP client or native library
is imported or run. The resulting synthetic plan cannot authorize live play.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys

PACKAGE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE))
from package_io import (BASE_COMMIT, EVALUATION_COMMIT, canonical, descriptor, fresh_output, model_binding,
                        package_inventory, read_object, require, save_new, verify_descriptor)


EVALUATOR_FILES = {"clone_eval.py", "clone_adapter.py", "clone_audit.py", "clone_series.py",
                   "test_clone_pipeline.py", "test_clone_contract.py", "test_clone_preflight.py"}
BUILD_FIELDS = {"schema", "status", "created_utc", "base_commit", "evaluation_commit", "compiler", "original_flags",
                "sampler_flags", "players", "batch", "ndebug", "river_cfr", "inputs", "package_sources",
                "staged_sources", "objects", "candidate", "model_binding", "model_binding_file",
                "commands", "poker_source", "context_source", "native_library_loads", "game_initializations",
                "policy_calls", "HTTP_calls", "play_qualified", "scope"}


def now():
    return datetime.now(timezone.utc).isoformat()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-manifest", type=Path, required=True)
    parser.add_argument("--expected-build-manifest-sha256", required=True)
    parser.add_argument("--control", type=Path, required=True)
    parser.add_argument("--expected-control-sha256", required=True)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--output", type=Path, required=True, help="A new directory for this plan")
    args = parser.parse_args()
    require(type(args.run_id) is str and 1 <= len(args.run_id) <= 96
            and all(character in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"
                    for character in args.run_id), "run ID must be a short safe namespace")
    manifest_ref = descriptor(args.build_manifest, args.expected_build_manifest_sha256)
    build = read_object(args.build_manifest, args.expected_build_manifest_sha256)
    require(set(build) == BUILD_FIELDS and build["schema"] == "poker-clone-portable-build-v1"
            and build["status"] == "built" and build["base_commit"] == BASE_COMMIT
            and build["evaluation_commit"] == EVALUATION_COMMIT,
            "an exact portable clone build manifest is required")
    require(type(build["players"]) is int and build["players"] == 2
            and type(build["batch"]) is int and build["batch"] == 64
            and build["ndebug"] is False and build["river_cfr"] is False
            and build["play_qualified"] is False, "build configuration differs")
    for name in ("native_library_loads", "game_initializations", "policy_calls", "HTTP_calls"):
        require(type(build[name]) is int and build[name] == 0, "build unexpectedly executed " + name)
    binding = model_binding(build["model_binding"])
    build_root = Path(manifest_ref["path"]).parent
    require(not (build_root / "failure.json").exists(), "build has a preserved failure")
    require(build["poker_source"] == str(build_root / "evaluation_support")
            and build["context_source"] == str(build_root / "source"), "staged source locations differ")
    require(type(build["inputs"]) is dict and set(build["inputs"]) == {
        "model_binding", "header", "standalone_module", "standalone_source_closure"},
        "external build inputs are incomplete")
    require(type(build["objects"]) is dict and set(build["objects"]) == {"bridge", "sampler"},
            "separately compiled objects are required")
    require(type(build["package_sources"]) is list and type(build["staged_sources"]) is list,
            "complete source descriptor lists are required")
    rows = [manifest_ref, *package_inventory(), *build["package_sources"], *build["staged_sources"],
            *build["inputs"].values(), *build["objects"].values(), build["candidate"],
            build["commands"], build["model_binding_file"]]
    checked = {}
    for row in rows:
        row = verify_descriptor(row)
        previous = checked.setdefault(row["path"], row)
        require(previous == row, "conflicting source or artifact descriptor")
    require(build["candidate"]["path"] == str(build_root / "libpoker2_clone.so"),
            "candidate is not the newly linked bridge")
    require(build["inputs"]["standalone_module"]["sha256"] == binding["module_binary_sha256"]
            and build["inputs"]["standalone_source_closure"]["sha256"] == binding["module_source_closure_sha256"],
            "standalone module identity differs from the external binding")
    for row in (build["inputs"]["model_binding"], build["model_binding_file"]):
        require(canonical(model_binding(read_object(row["path"], row["sha256"]))) == canonical(binding),
                "model binding changed")
    evaluator = Path(__file__).resolve().parent
    require(all(str(evaluator / name) in checked for name in EVALUATOR_FILES),
            "complete evaluator source is absent from the package inventory")
    control = descriptor(args.control, args.expected_control_sha256)
    require(control["sha256"] != build["candidate"]["sha256"], "control and candidate are the same library")
    checked[control["path"]] = control
    fixture_path = evaluator / "fixtures/synthetic_decks.json"
    require(str(fixture_path) in checked, "literal synthetic fixture is absent from the source inventory")
    fixtures = read_object(fixture_path, checked[str(fixture_path)]["sha256"])
    require(set(fixtures) == {"schema", "decks", "orientation", "primary_seed", "uncertain_action_seed",
                             "redirected_action_seed", "scope"}
            and fixtures["schema"] == "poker-clone-synthetic-decks-v1", "unexpected synthetic fixture schema")
    decks = fixtures["decks"]
    require(type(decks) is list and len(decks) == 4, "four literal synthetic decks are required")
    for deck in decks:
        require(type(deck) is list and len(deck) == 9 and len(set(deck)) == 9
                and all(type(card) is int and 0 <= card < 52 for card in deck), "invalid literal deck")
    for name, expected in (("primary_seed", 202610186500), ("uncertain_action_seed", 202610186599),
                           ("redirected_action_seed", 202610186598)):
        require(type(fixtures[name]) is int and fixtures[name] == expected,
                "fixture seed differs from the fixed pipeline source")

    def job(name, arm, hands, seed):
        return {"id": name, "arm": arm, "hands": hands, "games": 1, "seed": seed, "run_id": args.run_id}

    plan = {"schema": "clone-series-v2", "mode": "synthetic", "created_utc": now(),
        "run_id": args.run_id, "no_stakes": True, "resume_allowed": False,
        "replacement_hands_allowed": False, "policy_id": 7, "equity_samples": 0,
        "analysis": "actual_full_hand_raw_v2", "primary_metric": "raw", "contact": "offline-synthetic",
        "poker_source": build["poker_source"], "context_source": build["context_source"],
        "evaluator_source": str(evaluator), "proof_decks": decks,
        "jobs": [job("00_control", "control", 8, fixtures["primary_seed"]),
                 job("01_candidate", "candidate", 8, fixtures["primary_seed"])],
        "failure_jobs": [job("uncertain_action", "candidate", 2, fixtures["uncertain_action_seed"]),
                         job("redirected_action", "candidate", 2, fixtures["redirected_action_seed"])],
        "libraries": {"candidate": {key: build["candidate"][key] for key in ("path", "sha256")},
                      "control": {key: control[key] for key in ("path", "sha256")}},
        "model_binding": binding, "model_binding_sha256": hashlib.sha256(canonical(binding)).hexdigest(),
        "source_sha256": {path: row["sha256"] for path, row in sorted(checked.items())},
        "prerequisites": {"portable_build_manifest": manifest_ref}, "next_stage_authorized": False,
        "scope": "Fixed16 offline synthetic native hands and two interruption jobs; no real HTTP or strength claim."}
    output = fresh_output(args.output)
    save_new(output / "started.json", {"created_utc": now(), "build_manifest": manifest_ref,
                                      "policy_calls": 0, "HTTP_calls": 0})
    try:
        for row in checked.values():
            verify_descriptor(row)
        save_new(output / "synthetic_plan.json", plan)
        result = {"schema": "poker-clone-synthetic-plan-creation-v1", "status": "created",
                  "plan": descriptor(output / "synthetic_plan.json"), "policy_calls": 0,
                  "native_library_loads": 0, "HTTP_calls": 0, "next_stage_authorized": False}
        save_new(output / "creation.json", result)
        print(json.dumps(result, sort_keys=True))
    except BaseException as error:
        save_new(output / "failure.json", {"status": "failed", "error_type": type(error).__name__,
                  "error": str(error), "preserved_output": str(output), "retry_performed": False})
        raise


if __name__ == "__main__":
    main()
