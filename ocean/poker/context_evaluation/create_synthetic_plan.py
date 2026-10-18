#!/usr/bin/env python3
"""Bind the fixed offline pipeline test to explicit local source and binaries.

This command writes one new synthetic plan. It never starts a game, contacts a
server, or creates a live-execution authorization.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path


HERE = Path(__file__).resolve().parent
POKER = HERE.parent
DECKS = POKER / "continuation_tests/fixtures/synthetic_decks.json"
DECKS_SHA256 = "82577847ce10ce4ca294e0065ef12cbff09d61216027960bdb75025d4b3eff5f"


def digest(path):
    hasher = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            hasher.update(block)
    return hasher.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--control", type=Path, required=True,
                        help="explicit original HU policy library")
    parser.add_argument("--candidate", type=Path, required=True,
                        help="explicit retained-context HU library")
    parser.add_argument("--poker-source", type=Path, default=POKER,
                        help="directory containing the compatible eval.py")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    poker_source = args.poker_source.resolve()
    if digest(DECKS) != DECKS_SHA256:
        raise ValueError("fixed synthetic deck manifest changed")
    decks = json.loads(DECKS.read_text())["decks"][:4]
    if len(decks) != 4 or any(len(deck) != 9 or len(set(deck)) != 9
                              or any(type(card) is not int or not 0 <= card < 52
                                     for card in deck) for deck in decks):
        raise ValueError("invalid fixed synthetic deck manifest")
    source_paths = list(HERE.glob("*.py")) + [DECKS, poker_source / "eval.py",
                                             POKER / "continuation_transport.py"]
    # Native headers are bound as source inputs in addition to the selected
    # binary identities. Build provenance for a historical control is external.
    source_paths += list(POKER.glob("*.h")) + [POKER / "native.c", POKER / "continuation.c"]
    libraries = {arm: {"path": str(path.resolve()), "sha256": digest(path)}
                 for arm, path in (("control", args.control), ("candidate", args.candidate))}
    run_id = "context-pipeline-synthetic-v1"
    plan = {
        "schema": "context-series-recovery-v1", "mode": "synthetic",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "run_id": run_id, "no_stakes": True, "resume_allowed": False,
        "replacement_hands_allowed": False, "policy_id": 7, "equity_samples": 0,
        "analysis": "actual_full_hand_then_separate_common_prefix_river_v1",
        "primary_metric": "first_divergence",
        "contact": "offline synthetic test; no HTTP requests",
        "poker_source": str(poker_source), "context_source": str(POKER),
        "libraries": libraries, "proof_decks": decks,
        "source_sha256": {str(path.resolve()): digest(path) for path in sorted(set(source_paths))},
        "jobs": [{"id": f"{index:02d}_{arm}", "arm": arm, "games": 1, "hands": 8,
                  "seed": 202610187500, "run_id": run_id}
                 for index, arm in enumerate(("control", "candidate"))],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(plan, stream, sort_keys=True, indent=2, allow_nan=False)
        stream.write("\n")
    print(json.dumps({"plan": str(args.output.resolve()), "sha256": digest(args.output),
                      "mode": "synthetic", "network_calls": 0}))


if __name__ == "__main__":
    main()
