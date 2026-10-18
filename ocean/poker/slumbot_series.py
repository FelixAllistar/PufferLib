#!/usr/bin/env python3
"""Freeze, run, and audit a serialized interleaved Slumbot comparison.

Only the explicit `run` command launches eval.py and its live requests.
`freeze`, `analyze`, and importing this module are entirely offline.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import json
from pathlib import Path
import signal
import subprocess
import sys

from poker_audit import difference, estimate, fingerprint, parse_log

ORDER = ("control", "candidate", "candidate", "control",
         "candidate", "control", "control", "candidate")
SEMANTICS = "already_adjusted_hero_minus_slumbot_selfplay"


def write_new(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x") as handle:
        json.dump(value, handle, indent=2, sort_keys=True, allow_nan=False)
        handle.write("\n")


def freeze(args) -> dict:
    runner = args.runner.resolve()
    directory = args.output_dir.resolve()
    libraries = {"control": args.control.resolve(), "candidate": args.candidate.resolve()}
    hashes = {arm: fingerprint(path) for arm, path in libraries.items()}
    samples = {"control": args.control_samples, "candidate": args.candidate_samples}
    jobs = []
    for i, arm in enumerate(ORDER):
        job_id = f"{i:02d}_{arm}"
        jobs.append({"id": job_id, "arm": arm, "library": str(libraries[arm]),
                     "library_sha256": hashes[arm], "seed": args.start_seed + i,
                     "quota": args.hands_per_job, "games": args.games, "policy_id": 7,
                     "equity_samples": samples[arm],
                     "output": str(directory / f"{job_id}.jsonl")})
    return {"schema": 1, "created_at_utc": datetime.now(timezone.utc).isoformat(),
            "runner": str(runner), "runner_sha256": fingerprint(runner),
            # Keep a venv executable's symlink spelling: resolving it would
            # invoke the system interpreter without that venv's dependencies.
            "python": str(args.python.absolute()), "contact": args.contact,
            "primary_metric": "raw_gain_bb100", "secondary_metric": "already_adjusted_gain_bb100",
            "baseline_winnings_semantics": SEMANTICS,
            "hands_per_arm": 4 * args.hands_per_job, "jobs": jobs,
            "require_baseline_coverage": True,
            "stopping_rule": "Execute all eight fixed jobs in order, one at a time. Stop further jobs on any dirty, incomplete, mismatched or failed job. Preserve every attempt. No score-based stopping, substitutions, extensions, or favorable prefixes.",
            "analysis_rule": "Combine all clean complete jobs of each identical frozen arm; confirmatory eligibility requires every planned job. Raw independent-arm difference is primary, server-adjusted difference secondary. Do not pool earlier development or unrelated logs."}


def load_manifest(path: Path) -> dict:
    manifest = json.loads(path.read_text())
    jobs = manifest["jobs"]
    if len(jobs) != 8 or tuple(job["arm"] for job in jobs) != ORDER:
        raise ValueError("Manifest must contain the frozen eight-job interleaved order")
    if len({job["seed"] for job in jobs}) != 8 or len({job["output"] for job in jobs}) != 8:
        raise ValueError("Every job needs a distinct seed and output")
    if len({job["id"] for job in jobs}) != 8:
        raise ValueError("Duplicate job IDs")
    if manifest.get("primary_metric") != "raw_gain_bb100" or \
            manifest.get("secondary_metric") != "already_adjusted_gain_bb100":
        raise ValueError("Unexpected metric definition")
    if manifest.get("baseline_winnings_semantics") != SEMANTICS:
        raise ValueError("Unverified baseline semantics")
    for arm in ("control", "candidate"):
        selected = [job for job in jobs if job["arm"] == arm]
        identity = {(job["library_sha256"], job["policy_id"], job["equity_samples"]) for job in selected}
        if len(identity) != 1:
            raise ValueError(f"{arm} changes binary, policy, or sample setting across jobs")
        if sum(job["quota"] for job in selected) != manifest["hands_per_arm"]:
            raise ValueError(f"{arm} planned counts disagree")
    for job in jobs:
        if job["quota"] < 2 or not 1 <= job["games"] <= 50:
            raise ValueError("Invalid quota or concurrency")
    return manifest


def check_job(job: dict, manifest: dict) -> tuple[dict | None, list[str]]:
    path = Path(job["output"])
    if not path.exists():
        return None, ["missing output"]
    try:
        audit = parse_log(path, details=False, baseline_mode="already_adjusted")
    except (ValueError, TypeError, KeyError, OSError) as exc:
        return None, [f"unreadable/invalid log: {exc}"]
    errors = []
    if not audit["window"]["clean"]:
        errors.append("dirty or undrained window")
    if audit["audit"]["source_state"] != "target_completed_clean":
        errors.append("job did not finish its target cleanly")
    if audit["estimate"]["hands"] != job["quota"]:
        errors.append("completed hand count differs from fixed quota")
    expected = {"library_sha256": job["library_sha256"], "style": job["policy_id"],
                "equity_samples": job["equity_samples"], "seed": job["seed"],
                "hands_requested": job["quota"], "concurrent_games": job["games"],
                "opponent": "slumbot.com", "starting_bb": 200,
                "baseline_winnings_semantics": SEMANTICS}
    metadata = audit["metadata"]
    for key, value in expected.items():
        if metadata.get(key, 0 if key == "equity_samples" else None) != value:
            errors.append(f"metadata mismatch: {key}")
    # Current runner records its source hash. Fail closed when absent or changed.
    recorded_runner = metadata.get("runner_sha256", metadata.get("runner_source_sha256"))
    if recorded_runner != manifest["runner_sha256"]:
        errors.append("runner source hash missing or different")
    if audit["audit"]["record_counts"].get("resumed", 0):
        errors.append("planned jobs must be single fresh runs, not resumed logs")
    if manifest.get("require_baseline_coverage", True) and not audit["server_baseline"]["complete_coverage"]:
        errors.append("incomplete server-baseline coverage")
    return audit, errors


def execution_audit(path: Path, manifest: dict) -> dict:
    """Require a hash-bound serialized execution record for confirmation."""
    execution = path.with_suffix(".execution.jsonl")
    errors = []
    if not execution.exists():
        return {"path": str(execution), "errors": ["missing frozen execution record"]}
    try:
        events = [json.loads(line) for line in execution.read_text().splitlines()]
    except (OSError, ValueError) as exc:
        return {"path": str(execution), "sha256": fingerprint(execution),
                "errors": [f"invalid execution record: {exc}"]}
    expected = [job["id"] for job in manifest["jobs"]]
    if not events or events[0].get("manifest_sha256") != fingerprint(path):
        errors.append("execution manifest hash mismatch or missing start")
    starts = [event["job_started"] for event in events if "job_started" in event]
    finishes = [event["job_finished"] for event in events if "job_finished" in event]
    if starts != expected or finishes != expected:
        errors.append("execution did not finish every planned job in order")
    if not events or not events[-1].get("all_jobs_finished"):
        errors.append("execution lacks successful series completion")
    by_id = {job["id"]: job for job in manifest["jobs"]}
    active = None
    for event in events:
        if "job_started" in event:
            if active is not None:
                errors.append("execution contains overlapping or unfinished jobs")
            active = event["job_started"]
        if "job_finished" not in event:
            continue
        job_id = event["job_finished"]
        if active != job_id:
            errors.append(f"unmatched execution finish: {job_id}")
        active = None
        if event.get("returncode") != 0 or event.get("errors"):
            errors.append(f"execution reported failure: {job_id}")
        if job_id in by_id:
            output = Path(by_id[job_id]["output"])
            if output.exists() and event.get("log_sha256") != fingerprint(output):
                errors.append(f"log differs from completed execution: {job_id}")
    return {"path": str(execution), "sha256": fingerprint(execution),
            "errors": errors, "jobs_started": starts, "jobs_finished": finishes}


def analyze(path: Path) -> dict:
    manifest = load_manifest(path)
    arms = {arm: {"raw": [], "adjusted": [], "positions": Counter(),
                  "terminal_verification": Counter(), "clean_jobs": 0}
            for arm in ("control", "candidate")}
    execution = execution_audit(path, manifest)
    reports = []
    blockers = ([{"execution": execution["errors"]}] if execution["errors"] else [])
    for job in manifest["jobs"]:
        audit, errors = check_job(job, manifest)
        report = {"id": job["id"], "arm": job["arm"], "output": job["output"],
                  "eligible_job": not errors, "errors": errors}
        if audit:
            report.update(sha256=audit["sha256"], raw=audit["estimate"],
                          adjusted=audit["server_baseline"]["server_relative_estimate"],
                          audit_state=audit["audit"]["source_state"],
                          position_counts={key: value["hands"] for key, value in audit["by_position"].items()})
        elif Path(job["output"]).exists():
            report["sha256"] = fingerprint(Path(job["output"]))
        reports.append(report)
        if errors:
            blockers.append({"job": job["id"], "errors": errors})
            continue
        arm = arms[job["arm"]]
        arm["clean_jobs"] += 1
        terminal = Counter()
        with Path(job["output"]).open() as handle:
            for line in handle:
                rec = json.loads(line)
                if "winnings_chips" not in rec:
                    continue
                arm["raw"].append(rec["winnings_chips"] / 100)
                if "baseline_winnings_chips" in rec:
                    arm["adjusted"].append(rec["baseline_winnings_chips"] / 100)
                arm["positions"][str(rec.get("position", "unknown"))] += 1
                verified = rec.get("native_terminal_verified")
                terminal["verified" if verified is True else
                         "not_verifiable" if verified is False else "unrecorded"] += 1
        report["terminal_verification"] = dict(terminal)
        report["baseline_coverage_hands"] = audit["server_baseline"]["coverage_hands"]
        arm["terminal_verification"].update(terminal)
        if fingerprint(Path(job["output"])) != audit["sha256"]:
            raise ValueError(f"Log changed during analysis: {job['id']}")
    summaries = {}
    for name, arm in arms.items():
        summaries[name] = {"raw": estimate(arm["raw"]), "adjusted": estimate(arm["adjusted"]),
                           "clean_jobs": arm["clean_jobs"], "position_counts": dict(arm["positions"]),
                           "terminal_verification": dict(arm["terminal_verification"])}
    raw_difference = adjusted_difference = None
    if all(len(arm["raw"]) > 1 for arm in arms.values()):
        raw_difference = difference(summaries["candidate"]["raw"], summaries["control"]["raw"])
    if all(len(arm["adjusted"]) > 1 for arm in arms.values()):
        adjusted_difference = difference(summaries["candidate"]["adjusted"], summaries["control"]["adjusted"])
    complete = not blockers and all(len(arm["raw"]) == manifest["hands_per_arm"] for arm in arms.values())
    conclusion = "incomplete_not_confirmatory"
    if complete:
        low, high = raw_difference["ci95_bb100"]
        conclusion = "raw_improvement_supported" if low > 0 else "raw_deterioration_supported" if high < 0 else "raw_difference_unresolved"
    return {"schema": 1, "manifest": str(path.resolve()), "manifest_sha256": fingerprint(path),
            "all_planned_jobs_complete_and_eligible": complete, "conclusion": conclusion,
            "eligibility_blockers": blockers, "execution": execution, "jobs": reports, "arms": summaries,
            "primary_raw_difference": raw_difference, "secondary_adjusted_difference": adjusted_difference,
            "interpretation": "Every planned job is required for confirmation. Any incomplete-series statistics describe all currently eligible complete jobs and are not a selected or confirmatory prefix. No developmental or dirty-job hands are pooled.",
            "ci_method": "Approximate normal 95% CI of independent hand returns; identical frozen arm jobs pooled by hand, not by unweighted job means. Interleaving addresses timing balance but does not prove statistical independence."}


def run(path: Path) -> int:
    manifest = load_manifest(path)
    manifest_hash = fingerprint(path)
    execution_path = path.with_suffix(".execution.jsonl")
    # Refuse implicit continuation after process interruption. Existing outputs
    # remain untouched and must be analyzed as the exact manifest they belong to.
    if execution_path.exists() or any(Path(job["output"]).exists() for job in manifest["jobs"]):
        raise ValueError("Series already started; refusing implicit resume or overwrite")
    execution_path.parent.mkdir(parents=True, exist_ok=True)
    child = None
    stopped = False
    def forward(signum, _frame):
        nonlocal stopped
        stopped = True
        if child is not None and child.poll() is None:
            child.send_signal(signum)
    old = {sig: signal.signal(sig, forward) for sig in (signal.SIGINT, signal.SIGTERM)}
    try:
        with execution_path.open("x") as execution:
            def record(value):
                execution.write(json.dumps({"at_utc": datetime.now(timezone.utc).isoformat(), **value}) + "\n")
                execution.flush()
            record({"started": True, "manifest_sha256": manifest_hash})
            for job in manifest["jobs"]:
                if stopped:
                    record({"stopped": "signal_before_next_job"})
                    return 2
                if fingerprint(path) != manifest_hash:
                    raise ValueError("Manifest changed after launch")
                if fingerprint(Path(manifest["runner"])) != manifest["runner_sha256"]:
                    raise ValueError("Frozen runner source changed")
                if fingerprint(Path(job["library"])) != job["library_sha256"]:
                    raise ValueError(f"Frozen {job['arm']} library changed")
                output = Path(job["output"])
                output.parent.mkdir(parents=True, exist_ok=True)
                command = [manifest["python"], manifest["runner"], "--library", job["library"],
                           "--style", str(job["policy_id"]), "--equity-samples", str(job["equity_samples"]),
                           "--hands", str(job["quota"]), "--games", str(job["games"]),
                           "--seed", str(job["seed"]), "--output", str(output)]
                if manifest.get("contact"):
                    command.extend(("--contact", manifest["contact"]))
                process_log = output.with_suffix(".process.log")
                record({"job_started": job["id"], "command": command, "process_log": str(process_log)})
                print(f"Starting {job['id']}: {job['quota']} hands", flush=True)
                with process_log.open("x") as log:
                    child = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
                    returncode = child.wait()
                    child = None
                audit, errors = check_job(job, manifest)
                if returncode:
                    errors.append(f"runner exit code {returncode}")
                if fingerprint(Path(job["library"])) != job["library_sha256"]:
                    errors.append("library changed during job")
                if fingerprint(Path(manifest["runner"])) != manifest["runner_sha256"]:
                    errors.append("runner source changed during job")
                record({"job_finished": job["id"], "returncode": returncode, "errors": errors,
                        "log_sha256": fingerprint(output) if output.exists() else None})
                if audit:
                    write_new(output.with_suffix(".audit.json"), audit)
                if errors or stopped:
                    record({"stopped": "dirty_incomplete_or_interrupted_job", "job": job["id"]})
                    print(f"Stopped after {job['id']}; all outputs preserved", flush=True)
                    return 2
            record({"all_jobs_finished": True})
    finally:
        for sig, handler in old.items():
            signal.signal(sig, handler)
    result = analyze(path)
    write_new(path.with_suffix(".analysis.json"), result)
    print(json.dumps({"complete": result["all_planned_jobs_complete_and_eligible"],
                      "conclusion": result["conclusion"], "primary": result["primary_raw_difference"],
                      "secondary": result["secondary_adjusted_difference"]}, indent=2), flush=True)
    return 0 if result["all_planned_jobs_complete_and_eligible"] else 2


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    create = sub.add_parser("freeze")
    create.add_argument("--control", type=Path, required=True)
    create.add_argument("--candidate", type=Path, required=True)
    create.add_argument("--runner", type=Path, required=True)
    create.add_argument("--python", type=Path, default=Path(sys.executable))
    create.add_argument("--output-dir", type=Path, required=True)
    create.add_argument("--manifest", type=Path, required=True)
    create.add_argument("--hands-per-job", type=int, default=2000)
    create.add_argument("--games", type=int, default=5)
    create.add_argument("--start-seed", type=int, default=202610185000)
    create.add_argument("--control-samples", type=int, choices=(0, 32, 64, 128), default=0)
    create.add_argument("--candidate-samples", type=int, choices=(0, 32, 64, 128), default=0)
    create.add_argument("--contact", default="https://github.com/FelixAllistar")
    launch = sub.add_parser("run", help="Launches LIVE Slumbot requests through the frozen eval.py")
    launch.add_argument("manifest", type=Path)
    report = sub.add_parser("analyze", help="Offline audit of every planned job")
    report.add_argument("manifest", type=Path)
    report.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "freeze":
            if args.hands_per_job < 2 or not 1 <= args.games <= 50:
                raise ValueError("Invalid quota or concurrency")
            write_new(args.manifest, freeze(args))
            load_manifest(args.manifest)
            print(args.manifest.resolve())
            return 0
        if args.command == "run":
            return run(args.manifest)
        result = analyze(args.manifest)
        if args.output:
            write_new(args.output, result)
            print(args.output.resolve())
        else:
            print(json.dumps(result, indent=2, sort_keys=True, allow_nan=False))
        return 0
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"series error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
