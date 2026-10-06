#!/usr/bin/env python3
"""Measure complete reset generation; these numbers are not battle-step rates."""
import ctypes
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    if (ROOT / "build/pokemon_gen9/native/defer-benchmark").exists():
        print("Benchmark deferred: shared machine is busy.")
        return
    catalog = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * catalog["word_count"])()
    data = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, data, len(data))
    words[0], words[2] = 11, 1
    seeds = [[n, 65535 - n, n * 47111 & 65535, n * 32117 & 65535] for n in range(256)]

    def run(batch):
        for seed in batch:
            words[8:12] = seed
            lib.pg9_execute(words)
            assert words[1] == 0 and words[16] == 6

    run(seeds[:32])
    timings, cpu_timings = [], []
    for _ in range(3):
        start = time.perf_counter()
        start_cpu = time.process_time()
        run(seeds)
        timings.append(time.perf_counter() - start)
        cpu_timings.append(time.process_time() - start_cpu)
    script = """
const {Teams}=require('./ocean/pokemon_gen9/reference.cjs');
const g=Teams.getGenerator('gen9randombattle','1,2,3,4');
const seeds=Array.from({length:256},(_,n)=>[n,65535-n,n*47111&65535,n*32117&65535].join(','));
function run(xs){for(const seed of xs){g.setSeed(seed);if(g.randomTeam().length!==6)throw Error('failed team');}}
run(seeds.slice(0,32));
const seconds=[],cpu_seconds=[];
for(let n=0;n<3;n++){const start=process.hrtime.bigint(),cpu=process.cpuUsage();run(seeds);
seconds.push(Number(process.hrtime.bigint()-start)/1e9);const used=process.cpuUsage(cpu);
cpu_seconds.push((used.user+used.system)/1e6);}
console.log(JSON.stringify({node:process.version,seconds,cpu_seconds}));
"""
    oracle = json.loads(subprocess.check_output(["node", "-e", script], cwd=ROOT))
    result = {
        "revision": catalog["revision"], "operation": "complete team generation",
        "teams_per_repeat": len(seeds), "warmup_teams": 32,
        "native_seconds": timings, "oracle_seconds": oracle["seconds"],
        "native_cpu_seconds": cpu_timings, "oracle_cpu_seconds": oracle["cpu_seconds"],
        "native_teams_per_second": len(seeds) / sorted(timings)[1],
        "oracle_teams_per_second": len(seeds) / sorted(oracle["seconds"])[1],
        "node": oracle["node"], "bend": "2.0.35", "cc": "clang-19 -O3",
        "transport": "ctypes call and mutable-prefix copying included in native timing",
        "cpu_quota": "100%",
    }
    out = ROOT / "build/pokemon_gen9/native/benchmark.json"
    out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
