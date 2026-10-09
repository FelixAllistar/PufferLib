"""Reset-free empirical PSRO comparison; evaluation only, never launches training."""
import argparse
from array import array
from concurrent.futures import ThreadPoolExecutor
import hashlib
import importlib.util
import itertools
import json
import math
import os
from pathlib import Path
import time
import threading


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--workers", type=int, default=1)
    parser.add_argument("--population", type=Path)
    parser.add_argument("--cache", type=Path)
    parser.add_argument("--screen-only", action="store_true")
    args = parser.parse_args()
    root, output = args.root.resolve(), args.output.resolve()
    assert 1 <= args.workers <= 4
    assert not output.exists() or args.resume, "use a fresh report directory or --resume"
    output.mkdir(parents=True, exist_ok=args.resume)
    spec = importlib.util.spec_from_file_location("kag_run", root / "ocean/kaggriculture/run.py")
    run = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(run)
    for variable in ("KAG_CRITIC_MODE", "KAG_REWARD_WIN_LOSS_DRAW"):
        os.environ.pop(variable, None)
    # All actors share the unchanged ABI. Using the qualified original binary
    # avoids making match outcomes depend on how each stage's critic was trained.
    binary = root / "puffer"
    sources = {
        "paired_100M": "checkpoints/kaggriculture/1791156949292/0000000099614720.bin",
        "paired_42M": "checkpoints/kaggriculture/1791156949292/0000000041943040.bin",
        "paired_84M": "checkpoints/kaggriculture/1791156949292/0000000083886080.bin",
        "plain_wld_42M": "checkpoints/kaggriculture/1791155874853/0000000041943040.bin",
        "pre_wld_300M": "checkpoints/kaggriculture/1791148789764/0000000299892736.bin",
        "sweep51": "saved/kaggriculture/sweep51_final_20261004.bin",
        "previous_champion": "saved/kaggriculture/latest_champ_1790824202158.bin",
        **{f"fixed_bank_{i}": f"saved/kaggriculture/sweep_league_20260929/opponent_{i}.bin"
            for i in range(4)},
    }
    if args.population:
        sources = json.loads(args.population.read_text())
    league = {"contract": run.CONTRACT, "models": {}, "matches": {}, "strategy": {},
        "method": "Empirical PSRO population payoff and maximin mixture; no new response-oracle training",
        "reset_state_prob": 0, "balanced_seats": True,
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "screen_seeds": [707, 1707], "screen_games_per_seed": 128,
        "root": str(root), "created_ns": time.time_ns()}
    for name, relative in sources.items():
        path = root / relative
        data = path.read_bytes()
        assert len(data) == 4 * run.CONTRACT["parameters"], path
        assert all(math.isfinite(value) for value in array("f", data)), path
        league["models"][name] = {"path": str(path),
            "sha256": hashlib.sha256(data).hexdigest(), "finite": True}
    if args.resume:
        previous = json.loads((output / "league.json").read_text())
        assert previous["models"] == league["models"]
        assert previous["binary_sha256"] == league["binary_sha256"]
        league = previous
    elif args.cache:
        cached = json.loads(args.cache.read_text())
        assert cached["binary_sha256"] == league["binary_sha256"]
        assert cached["contract"] == league["contract"]
        assert cached["screen_seeds"] == league["screen_seeds"]
        assert cached["screen_games_per_seed"] == league["screen_games_per_seed"]
        for key, entry in cached["matches"].items():
            first, second = key.split("/")
            if first not in sources or second not in sources:
                continue
            assert cached["models"][first]["sha256"] == league["models"][first]["sha256"]
            assert cached["models"][second]["sha256"] == league["models"][second]["sha256"]
            assert entry["games"] == 256
            assert [result["seed"] for result in entry["seeds"]] == league["screen_seeds"]
            league["matches"][key] = entry
        league["cache_source"] = str(args.cache.resolve())
    run.save_league(output / "league.json", league)
    print(f"ALL {len(sources)} CHECKPOINTS FINITE; {len(league['matches'])} cached pairs; population comparison begins", flush=True)
    write_lock = threading.Lock()

    def solve():
        try:
            run.solve_league(league)
            league["solver"] = "SciPy linear programming"
        except ModuleNotFoundError as error:
            if error.name not in ("numpy", "scipy"):
                raise
            # A strict population winner is the unique exact maximin solution:
            # it guarantees >=0 against every pure opponent, whereas any mixture
            # containing another policy scores <0 against this winner.
            for candidate in sources:
                scores = []
                for opponent in sources:
                    if candidate == opponent:
                        continue
                    key = f"{candidate}/{opponent}"
                    score = (league["matches"][key]["score"] if key in league["matches"]
                        else 1 - league["matches"][f"{opponent}/{candidate}"]["score"])
                    scores.append(score)
                if all(score > .5 for score in scores):
                    league["strategy"] = {name: float(name == candidate) for name in sources}
                    league["champion"] = candidate
                    league["solver"] = "Strict population winner: exact analytical maximin strategy"
                    return
            league["solver"] = "Complete matrix saved; solve locally with SciPy"
            league["strategy"] = {}

    def match(first, second, games, seeds, stage):
        key = f"{first}/{second}"
        results = []
        for seed in seeds:
            destination = output / stage / f"{first}_vs_{second}_s{seed}.log"
            result = run.evaluate(binary, destination,
                Path(league["models"][first]["path"]),
                Path(league["models"][second]["path"]), games, seed)
            assert result["games"] >= games and 0 <= result["score"] <= 1
            results.append(result)
        total = sum(result["games"] for result in results)
        combined = {"score": sum(result["score"] * result["games"]
            for result in results) / total, "games": total, "seeds": results, "stage": stage}
        with write_lock:
            league["matches"][key] = combined
            run.save_league(output / "league.json", league)
        print(f"PAIR_RESULT {first} vs {second}: {combined['score']:.4%} over {total} games ({stage})", flush=True)

    priorities = [(name, "pre_wld_300M") for name in
        ("paired_100M", "paired_42M", "paired_84M", "plain_wld_42M")]
    priorities += [("paired_100M", "paired_42M"), ("paired_100M", "paired_84M")]
    if args.population:
        latest = next(iter(sources))
        priorities = [(latest, name) for name in sources if name != latest]
    order = priorities + list(itertools.combinations(sources, 2))
    seen = {frozenset(key.split("/")) for key in league["matches"]}
    pending = []
    for first, second in order:
        unordered = frozenset((first, second))
        if unordered in seen:
            continue
        seen.add(unordered)
        pending.append((first, second))
    screen_stage = f"screen_resume_{time.time_ns()}" if args.resume else "screen"
    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        tasks = [executor.submit(match, first, second, 128, [707, 1707], screen_stage)
            for first, second in pending]
        for task in tasks:
            task.result()
    # Normalize pair orientation to the solver's population order.
    def normalize():
        for first, second in itertools.combinations(sources, 2):
            reverse = f"{second}/{first}"
            if reverse in league["matches"]:
                entry = league["matches"].pop(reverse)
                league["matches"][f"{first}/{second}"] = dict(entry, score=1 - entry["score"],
                    reversed_from=reverse)
    normalize()
    solve()
    run.save_league(output / "league.json", league)
    print("SCREEN_MAXIMIN", json.dumps(league["strategy"]), flush=True)
    if args.screen_only or not league["strategy"]:
        print("MATRIX_COMPLETE", output, flush=True)
        return
    # Independent held-out seeds: confirm the collapse comparisons and top
    # mixture members against old baselines without reusing selection seeds.
    confirmation = list(priorities)
    candidates = {name for name, weight in league["strategy"].items() if weight > .01}
    candidates.add(league["champion"])
    for name in candidates:
        for baseline in ("pre_wld_300M", "previous_champion", "sweep51"):
            if name != baseline:
                confirmation.append((name, baseline))
    seen = set()
    pending = []
    for first, second in confirmation:
        unordered = frozenset((first, second))
        if unordered in seen:
            continue
        seen.add(unordered)
        pending.append((first, second))
    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        tasks = [executor.submit(match, first, second, 512, [2707, 3707], "confirm")
            for first, second in pending]
        for task in tasks:
            task.result()
    normalize()
    solve()
    league["strategy_source"] = "Complete reset-free seat-balanced empirical population matrix; targeted held-out confirmation"
    run.save_league(output / "league.json", league)
    print("FINAL_MAXIMIN", json.dumps(league["strategy"]), flush=True)
    print("FINAL_CHAMPION", league["champion"], flush=True)
    print("EVALUATION_COMPLETE", output, flush=True)


if __name__ == "__main__":
    main()
