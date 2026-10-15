"""External full-information Oak benchmark, using native observations for our policy.

Oak 1.1.4 (published wheel) API. Search RNG is internally random_device seeded;
fixed battle/draft seeds do not guarantee identical search decisions on rerun.
"""
import argparse
import ctypes as C
import hashlib
import json
import re
import statistics
import time
import urllib.request
from pathlib import Path

import oak
import oak.search as search

ROOT = Path(__file__).resolve().parents[3]
PANEL = {
    "zam_egg_mie": "https://pokepast.es/2c3090c9fe06d6a0/raw",
    "jynx_mie_don": "https://pokepast.es/3e7096e479e03605/raw",
    "gar_egg_zap": "https://pokepast.es/2e85771afa971c5f/raw",
}

def norm(value):
    return re.sub(r"[^a-z0-9]", "", value.lower())

def load_team(name, output):
    rules = json.loads((ROOT / "ocean/pokemon/data/freepick.json").read_text())
    species = {norm(x["name"]): x["id"] for x in rules["species"]}
    moves = {norm(x["name"]): x["id"] for x in rules["moves"]}
    cached = output / (name + ".paste.txt")
    if not cached.exists():
        cached.write_bytes(urllib.request.urlopen(PANEL[name], timeout=30).read())
    text = cached.read_text().replace("\r", "")
    team = []
    for block in re.split(r"\n\s*\n", text.strip()):
        lines = [line.strip() for line in block.splitlines()]
        mon = species[norm(re.sub(r" \([MF]\)$", "", lines[0]))]
        chosen = [moves[norm(line[2:])] for line in lines if line.startswith("- ")]
        if len(chosen) != 4:
            raise ValueError("Expected four sourced moves")
        team.append([mon, *chosen])
    if len(team) != 6:
        raise ValueError("Expected six sourced species")
    return team, hashlib.sha256(cached.read_bytes()).hexdigest()

def adapter(library="benchmark_adapter.so"):
    lib = C.CDLL(str(ROOT / "build/pokemon" / library))
    signatures = {
        "bench_create": ([C.c_char_p, C.c_int], C.c_void_p),
        "bench_fixed_learner": ([C.c_void_p,C.c_int],None),
        "bench_reset": ([C.c_void_p, C.c_uint64, C.c_int, C.c_void_p], C.c_int),
        "bench_state": ([C.c_void_p, C.c_void_p], C.c_int),
        "bench_fields": ([C.c_void_p, C.c_void_p], None),
        "bench_map": ([C.c_void_p, C.c_int], C.c_int),
        "bench_legal": ([C.c_void_p, C.c_int, C.c_void_p], C.c_int),
        "bench_step": ([C.c_void_p, C.c_int, C.POINTER(C.c_int)], C.c_int),
        "bench_policy_seconds": ([C.c_void_p], C.c_double),
        "bench_team": ([C.c_void_p, C.c_void_p], None),
        "bench_destroy": ([C.c_void_p], None),
    }
    for name, (args, result) in signatures.items():
        fn = getattr(lib, name)
        fn.argtypes = args
        fn.restype = result
    return lib

def timing_summary(samples):
    ordered = sorted(samples)
    if not ordered:
        return {"calls": 0, "total_seconds": 0}
    return {"calls":len(samples), "total_seconds":sum(samples),
        "mean_ms":statistics.mean(samples)*1000,
        "median_ms":statistics.median(samples)*1000,
        "p95_ms":ordered[max(0, int(len(ordered)*.95+.999999)-1)]*1000}

def main():
    p = argparse.ArgumentParser()
    p.add_argument("checkpoint")
    p.add_argument("--team", choices=PANEL, default="zam_egg_mie")
    p.add_argument("--games", type=int, default=128)
    p.add_argument("--budget", type=int, default=4096)
    p.add_argument("--seed", type=int, default=20261014)
    p.add_argument("--deterministic", action="store_true")
    p.add_argument("--fixed-team",action="store_true",help="Force identical sourced teams/moves on both sides; tests battle play")
    p.add_argument("--fast",action="store_true",help="Use the separately parity-tested single-threaded BLAS adapter")
    p.add_argument("--validate", action="store_true", help="Compare every transition with Oak's engine")
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    if a.games < 2 or a.games % 2 or a.budget < 1:
        p.error("Need an even positive game count and positive search budget")
    a.output.mkdir(parents=True, exist_ok=True)
    team, team_sha = load_team(a.team, a.output)
    team_bytes = (C.c_uint8 * 30)(*[v for mon in team for v in mon])
    library="benchmark_adapter_blas.so" if a.fast else "benchmark_adapter.so"
    lib = adapter(library)
    handle = lib.bench_create(str(Path(a.checkpoint).resolve()).encode(), a.deterministic)
    lib.bench_fixed_learner(handle,a.fixed_team)
    agent = search.Agent()
    agent.budget = str(a.budget)
    agent.bandit = "ucb-1.0"
    agent.eval = "fp"
    buffer = (C.c_uint8 * 384)()
    choices_buffer = (C.c_uint8 * 256)()
    records = []
    transitions = 0
    rng_only_differences = 0
    shadow_state_differences = 0
    fields_buffer = (C.c_uint16 * 192)()
    search_seconds = []
    policy_seconds = []
    start = time.monotonic()
    key = Path(a.checkpoint).resolve().parents[3].name + "-" + Path(a.checkpoint).parent.parent.name + "-" + a.team + "-" + str(a.budget) + ("-argmax" if a.deterministic else "-sample") + ("-fixed" if a.fixed_team else "")
    try:
        for game in range(a.games):
            learner = game % 2
            seed = (a.seed + game * 0x9E3779B97F4A7C15) % (1 << 64)
            result = lib.bench_reset(handle, seed, learner, team_bytes)
            if result != 0:
                raise RuntimeError("Invalid draft/reset: " + str(result))
            durations = oak.Durations()
            sets = (C.c_uint8 * 30)()
            lib.bench_team(handle, sets)
            step = 0
            while result == 0:
                encoded_result = lib.bench_state(handle, buffer)
                battle = oak.Battle(bytes(buffer))
                lib.bench_fields(handle,fields_buffer)
                fields=[]
                for side in range(2):
                    for i in range(6):
                        mon=battle.side(side).pokemon(i);stats=mon.stats()
                        fields.extend([mon.species,mon.hp,mon.level,mon.status,stats.hp,stats.atk,stats.def_,stats.spe])
                        for j in range(4):fields.extend([mon.move(j).id,mon.move(j).pp])
                if fields!=list(fields_buffer):
                    raise RuntimeError('Oak decoded native species/stats/HP/status/PP incorrectly')
                native_choices = []
                for side in range(2):
                    n = lib.bench_legal(handle, side, choices_buffer)
                    native_choices.append(list(choices_buffer)[:n])
                oak_choices = oak.choices(battle, encoded_result)
                if any(sorted(x) != sorted(y) for x, y in zip(native_choices, oak_choices)):
                    raise RuntimeError("Engine legal-choice mismatch")
                opponent = 1 - learner
                if a.validate:
                    choice = oak_choices[opponent][step % len(oak_choices[opponent])]
                elif len(oak_choices[opponent]) == 1:
                    choice = oak_choices[opponent][0]
                else:
                    before = battle.bytes()
                    heap = search.Heap()
                    search_start = time.perf_counter()
                    output = search.search(battle, durations, encoded_result, heap, agent)
                    search_seconds.append(time.perf_counter()-search_start)
                    if battle.bytes() != before:
                        raise RuntimeError("Search mutated the real game")
                    probabilities = output.p1_empirical if opponent == 0 else output.p2_empirical
                    index = max(range(len(oak_choices[opponent])), key=lambda i: probabilities[i])
                    choice = oak_choices[opponent][index]
                action = lib.bench_map(handle, choice)
                selected = C.c_int(-1)
                result = lib.bench_step(handle, action, C.byref(selected))
                policy_seconds.append(lib.bench_policy_seconds(handle))
                if result < 0 or result == 4:
                    raise RuntimeError("Illegal action/native engine error: " + str(result))
                c1, c2 = (selected.value, choice) if learner == 0 else (choice, selected.value)
                shadow_result = oak.update(battle, durations, c1, c2)
                native_result = lib.bench_state(handle, buffer)
                # Our pinned engine and Oak's chance-instrumented fork can
                # consume a different number of RNG draws on no-effect turns.
                # The real game follows the native engine. Record the first
                # shadow divergence; refresh from native state next decision.
                if battle.bytes()[:-8] != bytes(buffer)[:-8] or shadow_result != native_result:
                    if shadow_state_differences == 0:
                        (a.output / (key + "-bridge-mismatch.json")).write_text(json.dumps({
                        "game":game,"step":step,"native_result":native_result,"oak_result":shadow_result,
                        "choices":[c1,c2],"native":bytes(buffer).hex(),"oak":battle.bytes().hex(),
                        "different_bytes":[[i,x,y] for i,(x,y) in enumerate(zip(bytes(buffer),battle.bytes())) if x!=y],
                            "native_text":str(oak.Battle(bytes(buffer))),"oak_text":str(battle)},indent=2))
                    shadow_state_differences += 1
                rng_only_differences += battle.bytes()[-8:] != bytes(buffer)[-8:]
                step += 1
                transitions += 1
            winner = result - 1 if result in (1, 2) else None
            records.append({"game": game, "seat": learner, "seed": seed,
                "score": 0.5 if winner is None else float(winner == learner),
                "timeout": result == 5, "decisions": step,
                "team": [list(sets[i:i+5]) for i in range(0, 30, 5)]})
            if (game + 1) % 16 == 0:
                print(f"{key} {game+1}/{a.games} score={sum(r['score'] for r in records)/len(records):.3f}", flush=True)
    finally:
        lib.bench_destroy(handle)
    report = {"checkpoint": str(Path(a.checkpoint).resolve()), "checkpoint_sha256": hashlib.sha256(Path(a.checkpoint).read_bytes()).hexdigest(),
        "oak_version": oak.__version__, "opponent": "oak-fp-ucb-1.0", "information": "Oak full state; learned policy private native observations",
        "adapter_library":library,"adapter_sha256":hashlib.sha256((ROOT/'build/pokemon'/library).read_bytes()).hexdigest(),
        "search_reproducibility": "Oak search RNG internally seeded; battle and draft seeds recorded", "team": a.team, "source": PANEL[a.team],
        "team_sha256": team_sha, "budget": a.budget, "deterministic": a.deterministic, "fixed_team":a.fixed_team, "validate": a.validate,
        "games": len(records), "wins": sum(r['score'] == 1 for r in records), "draws": sum(r['score'] == .5 for r in records),
        "losses": sum(r['score'] == 0 for r in records), "timeouts": sum(r['timeout'] for r in records), "transitions_checked": transitions,
        "rng_only_differences": rng_only_differences,
        "shadow_state_differences":shadow_state_differences,
        "engine_note":"Native pinned engine is authoritative. Oak fork prediction is not byte-identical; every native snapshot is freshly decoded and legal choice sets verified.",
        "elapsed_seconds": time.monotonic()-start, "records": records}
    report["timing"] = {"oak_search":timing_summary(search_seconds),
        "policy_inference":timing_summary(policy_seconds),
        "forced_oak_choices":transitions-len(search_seconds),
        "scope":"Battle decisions only. Oak timing excludes state conversion and heap allocation; policy timing includes network, masking and action selection, not simulator update.",
        "oak_search_seconds":search_seconds, "policy_inference_seconds":policy_seconds}
    destination = a.output / (key + ("-validation" if a.validate else "") + ".json")
    temporary = destination.with_suffix(".tmp")
    temporary.write_text(json.dumps(report, indent=2)+"\n")
    temporary.replace(destination)
    printable = {k:v for k,v in report.items() if k not in ("records", "timing")}
    printable["timing"] = {k:v for k,v in report["timing"].items() if not k.endswith("_seconds")}
    print(json.dumps(printable), flush=True)

if __name__ == "__main__":
    main()
