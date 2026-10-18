#!/usr/bin/env python3
"""Offline re-analysis of relocated Slumbot evidence without editing its manifest.

The original manifest and execution JSONL stay byte-for-byte unchanged. Only
log lookup is relocated beneath --report-root; recorded SHA256 values bind
the manifest to its execution and each included log to its completed job.
This program does not import eval.py, load a bot library, or make requests.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path, PurePosixPath
import sys

from poker_audit import difference, estimate, fingerprint
from slumbot_series import check_job, execution_audit, load_manifest, write_new


def resolve_log(recorded: str, report_root: Path,
                recorded_report_root: str | None = None) -> Path:
    """Resolve a recorded reports-relative suffix, refusing traversal/escape."""
    path = PurePosixPath(recorded)
    if recorded_report_root:
        relative = path.relative_to(PurePosixPath(recorded_report_root))
    else:
        matches = [i for i, part in enumerate(path.parts) if part == "reports"]
        if len(matches) != 1:
            raise ValueError("Recorded path needs one reports/ anchor or --recorded-report-root")
        relative = PurePosixPath(*path.parts[matches[0] + 1:])
    if not relative.parts or any(part in (".", "..") for part in relative.parts):
        raise ValueError("Invalid report-relative log path")
    root = report_root.resolve()
    resolved = (root / Path(*relative.parts)).resolve()
    if not resolved.is_relative_to(root):
        raise ValueError("Resolved log escapes the report root")
    return resolved


def analyze_archive(manifest_path: Path, report_root: Path,
                    recorded_report_root: str | None = None,
                    expected_manifest_sha256: str | None = None) -> dict:
    manifest = load_manifest(manifest_path)
    manifest_hash = fingerprint(manifest_path)
    if expected_manifest_sha256 and manifest_hash != expected_manifest_sha256:
        raise ValueError("Manifest differs from the explicitly expected SHA256")
    jobs = []
    for job in manifest["jobs"]:
        jobs.append({**job, "recorded_output": job["output"],
                     "output": str(resolve_log(job["output"], report_root, recorded_report_root))})
    if len({job["output"] for job in jobs}) != len(jobs):
        raise ValueError("Relocation maps multiple planned jobs to the same log")
    relocated = {**manifest, "jobs": jobs}
    execution = execution_audit(manifest_path, relocated)
    execution_path = manifest_path.with_suffix(".execution.jsonl")
    events = []
    if execution_path.exists():
        try:
            events = [json.loads(line) for line in execution_path.read_text().splitlines()]
            if not all(isinstance(event, dict) for event in events):
                raise ValueError("Execution events must be JSON objects")
        except (OSError, ValueError):
            events = []
    finished = {}
    for event in events:
        if "job_finished" in event:
            finished.setdefault(event["job_finished"], []).append(event)
    blockers = [{"execution": execution["errors"]}] if execution["errors"] else []
    arms = {name: {"raw": [], "adjusted": [], "positions": Counter(),
                   "terminal": Counter(), "clean_jobs": 0}
            for name in ("control", "candidate")}
    reports = []
    for job in jobs:
        audit, errors = check_job(job, manifest)
        provenance = finished.get(job["id"], [])
        if len(provenance) != 1:
            errors.append("missing or duplicated completed-job provenance")
        elif audit and provenance[0].get("log_sha256") != audit["sha256"]:
            errors.append("log SHA256 differs from completed-job provenance")
        report = {"id": job["id"], "arm": job["arm"],
                  "recorded_output": job["recorded_output"], "resolved_output": job["output"],
                  "eligible_job": not errors, "errors": errors}
        if audit:
            report.update(sha256=audit["sha256"], raw=audit["estimate"],
                          adjusted=audit["server_baseline"]["server_relative_estimate"],
                          audit_state=audit["audit"]["source_state"],
                          position_counts={key: value["hands"] for key, value in audit["by_position"].items()},
                          baseline_coverage_hands=audit["server_baseline"]["coverage_hands"])
        reports.append(report)
        if errors:
            blockers.append({"job": job["id"], "errors": errors})
            continue
        arm, terminal = arms[job["arm"]], Counter()
        with Path(job["output"]).open() as stream:
            for line in stream:
                record = json.loads(line)
                if "winnings_chips" not in record:
                    continue
                arm["raw"].append(record["winnings_chips"] / 100)
                arm["adjusted"].append(record["baseline_winnings_chips"] / 100)
                arm["positions"][str(record.get("position", "unknown"))] += 1
                verified = record.get("native_terminal_verified")
                terminal["verified" if verified is True else
                         "not_verifiable" if verified is False else "unrecorded"] += 1
        if fingerprint(Path(job["output"])) != audit["sha256"]:
            raise ValueError(f"Log changed during analysis: {job['id']}")
        arm["clean_jobs"] += 1
        arm["terminal"].update(terminal)
        report["terminal_verification"] = dict(terminal)
    summaries = {name: {"raw": estimate(arm["raw"]), "adjusted": estimate(arm["adjusted"]),
                        "clean_jobs": arm["clean_jobs"], "position_counts": dict(arm["positions"]),
                        "terminal_verification": dict(arm["terminal"])}
                 for name, arm in arms.items()}
    raw = adjusted = None
    if all(len(arm["raw"]) > 1 for arm in arms.values()):
        raw = difference(summaries["candidate"]["raw"], summaries["control"]["raw"])
        adjusted = difference(summaries["candidate"]["adjusted"], summaries["control"]["adjusted"])
    complete = not blockers and all(len(arm["raw"]) == manifest["hands_per_arm"] for arm in arms.values())
    conclusion = "incomplete_not_confirmatory"
    if complete:
        low, high = raw["ci95_bb100"]
        conclusion = "raw_improvement_supported" if low > 0 else "raw_deterioration_supported" if high < 0 else "raw_difference_unresolved"
    if fingerprint(manifest_path) != manifest_hash or \
            (execution_path.exists() and fingerprint(execution_path) != execution.get("sha256")):
        raise ValueError("Manifest or execution record changed during analysis")
    return {"schema": 1, "analysis": "offline_archived_series", "manifest": str(manifest_path.resolve()),
            "manifest_sha256": manifest_hash, "report_root": str(report_root.resolve()),
            "all_planned_jobs_complete_and_eligible": complete, "conclusion": conclusion,
            "execution": execution, "eligibility_blockers": blockers, "jobs": reports, "arms": summaries,
            "primary_raw_difference": raw, "secondary_adjusted_difference": adjusted,
            "interpretation": "Only report-relative log lookup was relocated. The original manifest and execution bytes were not modified. Every included log must match completed-job SHA256 provenance. All planned jobs are required for confirmation; incomplete-series statistics are descriptive, never a favorable prefix.",
            "ci_method": "Approximate normal 95% interval of independent hand returns, pooling identical frozen arms by hand. Raw gain is primary; the already-adjusted server field is secondary and is not subtracted again."}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--report-root", type=Path, required=True,
                        help="Local reports/ directory containing decompressed hand logs")
    parser.add_argument("--recorded-report-root", help="Explicit original reports root when its path lacks a unique reports/ anchor")
    parser.add_argument("--expected-manifest-sha256")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        result = analyze_archive(args.manifest, args.report_root, args.recorded_report_root,
                                 args.expected_manifest_sha256)
        if args.output:
            write_new(args.output, result)
            print(args.output.resolve())
        else:
            print(json.dumps(result, indent=2, sort_keys=True, allow_nan=False))
        return 0 if result["all_planned_jobs_complete_and_eligible"] else 2
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"archive audit error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
