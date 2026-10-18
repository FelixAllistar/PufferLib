"""Frozen, append-only orchestration and raw audit for a qualified V2 clone.

Only the explicit run command can issue requests. Each library and source is
verified before the series, before every job, and before offline analysis.
An interruption invalidates the planned series; there is no resume command.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
from types import SimpleNamespace

import clone_eval as runner
import clone_audit as audit
import clone_adapter as adapter


def now():
    return datetime.now(timezone.utc).isoformat()


def save_new(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, sort_keys=True, indent=2, allow_nan=False)
        stream.write("\n")


def load_plan(path, expected):
    path = Path(path)
    audit.require(runner.sha256(path) == expected, "frozen plan digest differs")
    plan = runner.decode(path.read_bytes())
    audit.require(plan.get("schema") == "clone-series-v2", "unsupported series schema")
    audit.require(plan.get("mode") in ("synthetic", "integration", "confirmatory"),
                  "unsupported series mode")
    audit.require(plan.get("no_stakes") is True and plan.get("resume_allowed") is False
                  and plan.get("replacement_hands_allowed") is False, "invalid execution constraints")
    audit.require(plan.get("policy_id") == 7 and plan.get("equity_samples") == 0,
                  "unfrozen policy configuration")
    audit.require(plan.get("analysis") == "actual_full_hand_raw_v2" and plan.get("primary_metric") == "raw",
                  "invalid analysis contract")
    runner.validate_model_binding(plan.get("model_binding"))
    source_digests = plan.get("source_sha256")
    audit.require(type(source_digests) is dict, "source closure must be an explicit mapping")
    source_directory = Path(__file__).resolve().parent
    required_sources = {Path(__file__).resolve(),
        (Path(plan["poker_source"]) / "eval.py").resolve(),
        (Path(plan["context_source"]) / "clone_transport.py").resolve(),
        (Path(plan["context_source"]) / "clone_public_replay.py").resolve()}
    for name, module in (("clone_eval.py", runner), ("clone_audit.py", audit),
                         ("clone_adapter.py", adapter)):
        actual = Path(module.__file__).resolve()
        audit.require(actual == source_directory / name, "imported evaluator source differs: " + name)
        required_sources.add(actual)
    audit.require(all(str(path) in source_digests for path in required_sources),
                  "required evaluator/transport source is absent from the frozen closure")
    for name, digest in plan["source_sha256"].items():
        audit.require(type(name) is str and Path(name).is_absolute()
                      and str(Path(name).resolve()) == name, "source path is not canonical and absolute")
        audit.require(runner.sha256(name) == digest, "source changed: " + name)
    for arm in audit.ARMS:
        audit.require(runner.sha256(plan["libraries"][arm]["path"])
                      == plan["libraries"][arm]["sha256"], "native library changed: " + arm)
    jobs = plan["jobs"]
    audit.require(type(jobs) is list and jobs and len({job["id"] for job in jobs}) == len(jobs),
                  "invalid planned job identities")
    for job in jobs:
        audit.require(type(job["id"]) is str and 1 <= len(job["id"]) <= 96
                      and all(char in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"
                              for char in job["id"]), "job ID is not a safe fixed slot name")
        audit.require(job["arm"] in audit.ARMS and job["run_id"] == plan["run_id"], "job identity differs")
        runner.quotas(job["hands"], job["games"])
        runner.integer(job["seed"], 0, runner.U64_MAX, "job master seed")
    if plan.get("mode") == "confirmatory":
        order = ["control", "candidate", "candidate", "control", "candidate", "control", "control", "candidate"]
        audit.require(len(jobs) == 8 and [job["arm"] for job in jobs] == order,
                      "confirmatory allocation differs from the prospective V2 proposal")
        for index, job in enumerate(jobs):
            audit.require(job["hands"] == 8000 and job["games"] == 5
                          and job["seed"] == 202610186100 + index // 2,
                          "confirmatory count, concurrency or seed differs")
    return plan


def arguments(plan, job, plan_hash, output):
    library = plan["libraries"][job["arm"]]
    control = plan["libraries"]["control"]
    candidate = plan["libraries"]["candidate"]
    return SimpleNamespace(style=7, samples=0, arm=job["arm"],
        hands=job["hands"], games=job["games"], seed=job["seed"],
        library=Path(library["path"]), expected_library_sha256=library["sha256"],
        control_library=Path(control["path"]), expected_control_sha256=control["sha256"],
        context_library=Path(candidate["path"]), expected_context_sha256=candidate["sha256"],
        poker_source=Path(plan["poker_source"]), context_source=Path(plan["context_source"]),
        output=Path(output), job=job["id"], run_id=plan["run_id"],
        manifest_sha256=plan_hash, contact=plan["contact"], model_binding=plan["model_binding"])


def require_play_qualification(plan):
    """Bind completed predictive and implementation checks before any HTTP."""
    reference = plan.get("qualification")
    audit.require(type(reference) is dict and set(reference) == {"path", "sha256"},
                  "completed qualification receipt required")
    path = Path(reference["path"]).resolve()
    audit.require(plan["source_sha256"].get(str(path)) == reference["sha256"]
                  and runner.sha256(path) == reference["sha256"], "qualification receipt changed")
    receipt = runner.decode(path.read_bytes())
    audit.require(receipt.get("approved_by") == "/root"
                  and receipt.get("schema") == "clone-pre-play-qualification-v2"
                  and receipt.get("model_binding_sha256") == runner.model_binding_digest(plan["model_binding"])
                  and receipt.get("candidate_library_sha256") == plan["libraries"]["candidate"]["sha256"],
                  "qualification belongs to another candidate or model")
    for name in ("fresh_validation_all26_passed", "fresh_assessment_all26_passed",
                 "native_sampler_parity_and_cost_passed", "native_bridge_verification_passed",
                 "actual_model_synthetic_pipeline_passed"):
        audit.require(receipt.get(name) is True, "unfinished pre-play qualification: " + name)
    if plan["mode"] == "confirmatory":
        audit.require(receipt.get("live_integration_passed") is True,
                      "confirmatory play requires the complete fixed live integration check")


def authorized_output(authorization, plan_hash, mode):
    audit.require(authorization.get("plan_sha256") == plan_hash
                  and authorization.get("authorized") is True
                  and authorization.get("mode") == mode, "plan is not authorized for this mode")
    value = authorization.get("output_directory")
    audit.require(type(value) is str and Path(value).is_absolute()
                  and str(Path(value).resolve()) == value,
                  "execution authorization must bind one canonical absolute output directory")
    return Path(value)


def run_series(plan_path, expected, authorization_path, authorization_sha256, output):
    plan = load_plan(plan_path, expected)
    audit.require(plan.get("mode") in ("integration", "confirmatory"),
                  "generic live runner refuses synthetic or unknown plans")
    require_play_qualification(plan)
    authorization_path, output = Path(authorization_path), Path(output)
    audit.require(runner.sha256(authorization_path) == authorization_sha256,
                  "source-bound execution authorization changed")
    authorization = runner.decode(authorization_path.read_bytes())
    audit.require(authorized_output(authorization, expected, plan["mode"]) == output.resolve(),
                  "series output differs from the authorized directory")
    output.mkdir(parents=True, exist_ok=False)
    start = {"schema": "clone-series-start-v2", "created_utc": now(), "plan_sha256": expected,
             "authorization_sha256": authorization_sha256, "jobs": [job["id"] for job in plan["jobs"]]}
    save_new(output / "start.json", start)
    receipts = []
    for job in plan["jobs"]:
        load_plan(plan_path, expected)
        receipt = {"id": job["id"], "started_utc": now(), "complete": False}
        save_new(output / (job["id"] + ".started.json"), receipt)
        log = output / (job["id"] + ".jsonl")
        try:
            code = runner.run(arguments(plan, job, expected, log), live_execution={
                "plan_path": str(Path(plan_path).resolve()), "plan_sha256": expected,
                "authorization_path": str(authorization_path.resolve()),
                "authorization_sha256": authorization_sha256})
            receipt.update(exit_code=code, complete=code == 0, interrupted=code != 0,
                           finished_utc=now(), log_sha256=runner.sha256(log), log=str(log.resolve()))
        except BaseException as exc:
            receipt.update(complete=False, interrupted=True, error_type=type(exc).__name__, finished_utc=now())
            if log.is_file():
                receipt.update(log_sha256=runner.sha256(log), log=str(log.resolve()))
            save_new(output / (job["id"] + ".receipt.json"), receipt)
            receipts.append(receipt)
            save_new(output / "series_receipt.json", {"complete": False, "plan_sha256": expected,
                                                      "attempts": receipts, "closed_utc": now()})
            raise
        save_new(output / (job["id"] + ".receipt.json"), receipt)
        receipts.append(receipt)
        if not receipt["complete"]:
            break
    complete = len(receipts) == len(plan["jobs"]) and all(row["complete"] for row in receipts)
    save_new(output / "series_receipt.json", {"complete": complete, "plan_sha256": expected,
                                              "attempts": receipts, "closed_utc": now()})
    return 0 if complete else 2


def read_completed_job(plan, job, receipt, expected):
    path = Path(receipt["log"])
    audit.require(runner.sha256(path) == receipt["log_sha256"], "completed job log changed")
    records = [runner.decode(line) for line in path.read_bytes().splitlines() if line.strip()]
    audit.require(records and all(type(row) is dict for row in records), "invalid log objects")
    audit.require("metadata" in records[0] and "summary" in records[-1]
                  and sum("metadata" in row for row in records) == 1
                  and sum("summary" in row for row in records) == 1, "invalid metadata/summary coverage")
    audit.require(not any(any(key in row for key in ("interrupted", "resumed", "worker_error"))
                          for row in records), "job contains an interruption or worker error")
    metadata = records[0]["metadata"]
    quotas = runner.quotas(job["hands"], job["games"])
    expected_metadata = {"log_schema": 6, "run_id": plan["run_id"], "job_id": job["id"],
        "arm": job["arm"], "style": 7, "equity_samples": 0, "manifest_sha256": expected,
        "library_sha256": plan["libraries"][job["arm"]]["sha256"],
        "settlement_library_sha256": plan["libraries"]["control"]["sha256"],
        "context_library_sha256": plan["libraries"]["candidate"]["sha256"],
        "model_binding": plan["model_binding"],
        "model_binding_sha256": runner.model_binding_digest(plan["model_binding"]),
        "context_protocol": "behavior_clone_bridge_v2", "no_stakes": True, "starting_bb": 200,
        "hands_requested": job["hands"], "concurrent_games": job["games"], "seed": job["seed"],
        "hand_reservation_quotas": quotas, "policy_rng_mode": "independent_hand_seeds_v1",
        "hand_seed_derivation": "sha256_poker_policy_hand_seed_v1",
        "hand_reservation_scheme": "game_blocks_without_overflow_v2",
        "wager_retry_count": 0, "replacement_hands_allowed": False, "resume_allowed": False}
    expected_metadata.update(opponent="synthetic_local_opponent" if plan["mode"] == "synthetic" else "slumbot",
                             transport_mode="synthetic_injected" if plan["mode"] == "synthetic" else "live")
    for key, value in expected_metadata.items():
        audit.require(metadata.get(key) == value, "job metadata differs: " + key)
    expected_sources = {name: plan["source_sha256"][str(Path(__file__).with_name(name).resolve())]
                        for name in ("clone_eval.py", "clone_audit.py", "clone_adapter.py")}
    expected_sources["eval.py"] = plan["source_sha256"][str((Path(plan["poker_source"]) / "eval.py").resolve())]
    expected_sources["clone_transport.py"] = plan["source_sha256"][
        str((Path(plan["context_source"]) / "clone_transport.py").resolve())]
    expected_sources["clone_public_replay.py"] = plan["source_sha256"][
        str((Path(plan["context_source"]) / "clone_public_replay.py").resolve())]
    audit.require(metadata.get("source_sha256") == expected_sources, "live runner/source receipt differs")
    summary = records[-1]["summary"]
    audit.require(summary.get("complete") is True and summary.get("hands") == job["hands"]
                  and summary.get("failures") == 0 and summary.get("worker_counts") == quotas,
                  "incomplete job summary")
    reservations, finished, hands, positions = {}, {}, [], [[] for _ in quotas]

    def identity(record):
        game = audit.integer(record.get("game"), 0, job["games"] - 1, "reservation worker")
        attempt = audit.integer(record.get("game_hand_attempt"), 0, quotas[game] - 1, "worker attempt")
        index = audit.integer(record.get("hand_reservation_index"), 0, job["hands"] - 1, "reservation ID")
        audit.require(index == sum(quotas[:game]) + attempt, "reservation outside fixed worker block")
        seed = runner.hand_seed(job["seed"], index)
        audit.require(record.get("policy_seed_at_hand_start") == seed
                      and record.get("context_generation") == attempt + 1, "hand seed/generation differs")
        return index, (game, attempt, seed)

    for row in records:
        if "hand_reserved" in row:
            index, signature = identity(row["hand_reserved"])
            audit.require(index not in reservations, "duplicate reservation")
            reservations[index] = signature
        elif "winnings_chips" in row:
            index, signature = identity(row)
            audit.require(index in reservations and reservations[index] == signature and index not in finished,
                          "terminal lacks unique prior reservation")
            audit.require(row.get("hand") == len(hands), "terminal hand order differs")
            game, attempt, seed = signature
            seat = audit.integer(row.get("position"), 0, 1, "terminal seat")
            positions[game].append((attempt, seat))
            if row.get("decisions"):
                audit.require(row["decisions"][0].get("policy_rng_before") == seed, "first RNG differs from seed")
            else:
                audit.require(row.get("policy_rng") == seed, "no-decision hand changed RNG")
            finished[index] = signature
            hands.append(row)
        else:
            audit.require("metadata" in row or "summary" in row, "unknown log record type")
    audit.require(set(reservations) == set(finished) == set(range(job["hands"])), "incomplete reserved hands")
    for game, entries in enumerate(positions):
        entries.sort()
        audit.require([attempt for attempt, _ in entries] == list(range(quotas[game])), "worker attempt order differs")
        audit.require(all(entries[i][1] != entries[i - 1][1] for i in range(1, len(entries))),
                      "server seat did not alternate in an uninterrupted session")
    return hands


def analyze(plan_path, expected, directory, report_path):
    plan = load_plan(plan_path, expected)
    directory = Path(directory)
    receipt_path = directory / "series_receipt.json"
    receipt = runner.decode(receipt_path.read_bytes())
    audit.require(receipt.get("complete") is True and receipt.get("plan_sha256") == expected,
                  "series did not complete the frozen plan")
    audit.require_complete_slots([job["id"] for job in plan["jobs"]], receipt["attempts"])
    evaluation = runner.load_module("context_analysis_original", Path(plan["poker_source"]) / "eval.py")
    transport = runner.load_transport("clone_analysis_transport", Path(plan["context_source"]))
    candidate, control = (plan["libraries"][arm]["path"] for arm in ("candidate", "control"))
    sessions = adapter.factories(evaluation, transport, candidate, control, plan["model_binding"],
                                 plan["libraries"]["candidate"]["sha256"])
    truth = adapter.TruthCallbacks(evaluation, transport, candidate, control)
    identities = {arm: {"policy_id": 7, "library_sha256": plan["libraries"][arm]["sha256"]}
                  for arm in audit.ARMS}
    rows = []
    for job, completed in zip(plan["jobs"], receipt["attempts"]):
        hands = read_completed_job(plan, job, completed, expected)
        for hand in hands:
            rows.append(audit.audit_hand(hand, job, identities, sessions, truth.verify))
            if len(rows) % 250 == 0:
                print(json.dumps({"audited_hands": len(rows)}), flush=True)
    counts = [sum(job["hands"] for job in plan["jobs"] if job["arm"] == arm) for arm in audit.ARMS]
    audit.require(counts[0] == counts[1], "unbalanced frozen arm denominators")
    result = audit.summarize(rows, counts[0])
    primary = result["differences"][plan["primary_metric"]]
    result.update(schema="clone-series-analysis-v2", mode=plan["mode"],
        plan_sha256=expected, series_receipt_sha256=runner.sha256(receipt_path),
        model_binding_sha256=runner.model_binding_digest(plan["model_binding"]),
        primary_metric=plan["primary_metric"], completed_utc=now(),
        relative_improvement_supported=plan["mode"] == "confirmatory" and primary["ci95_bb100"][0] > 0,
        absolute_slumbot_win_supported=plan["mode"] == "confirmatory"
            and result["arms"]["candidate"]["raw"]["ci95_bb100"][0] > 0,
        limitations=["approximate normal intervals over complete whole hands",
                     "paired policy seeds are not duplicate Slumbot server deals",
                     "no claim of full-game exploitability, six-player strength or PPO training"],
        rows=rows)
    save_new(report_path, result)
    print(json.dumps({"mode": plan["mode"], "hands": len(rows), "report_sha256": runner.sha256(report_path),
                      "relative_improvement_supported": result["relative_improvement_supported"],
                      "absolute_slumbot_win_supported": result["absolute_slumbot_win_supported"]}), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("run", "analyze"))
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--expected-plan-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--authorization", type=Path)
    parser.add_argument("--authorization-sha256")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.command == "run":
        audit.require(args.authorization is not None and args.authorization_sha256 is not None,
                      "explicit source-bound execution authorization required")
        return run_series(args.plan, args.expected_plan_sha256, args.authorization,
                          args.authorization_sha256, args.output)
    audit.require(args.report is not None, "analysis report path required")
    analyze(args.plan, args.expected_plan_sha256, args.output, args.report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
