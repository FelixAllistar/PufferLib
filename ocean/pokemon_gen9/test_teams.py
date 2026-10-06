#!/usr/bin/env python3
"""Whole-team oracle comparisons include rejection sampling and lead insertion."""
import ctypes
import json
from pathlib import Path
import subprocess

from test_sets import NORMALIZE_JS, decode

ROOT = Path(__file__).resolve().parents[2]


def main():
    catalog = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    native = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    native.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    native.pg9_execute.restype = None
    words = (ctypes.c_uint32 * catalog["word_count"])()
    data = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, data, len(data))
    words[2] = 1
    script = """
const {Teams}=require('./ocean/pokemon_gen9/reference.cjs');
const cat=require('./build/pokemon_gen9/catalog/catalog.json');
""" + NORMALIZE_JS + """
const result=[];
const seeds=[[0,0,0,0],[1,2,3,4],[65535,65535,65535,65535],[0,0,0,1]];
for(let n=0;n<252;n++)seeds.push([n,65535-n,(n*47111)&65535,(n*32117)&65535]);
const g=Teams.getGenerator('gen9randombattle','1,2,3,4');
for(const seed of seeds) {
 g.setSeed(seed.join(','));
 let error=false,sets=[],raw=null;
 try {raw=g.randomTeam();}catch(e){error=true;}
 if(raw)sets=raw.map(packed);
 result.push({seed,error,sets,after:g.prng.getSeed().split(',').map(Number)});
}
console.log(JSON.stringify(result));
"""
    cases = json.loads(subprocess.check_output(["node", "-e", script], cwd=ROOT))
    errors = 0
    for case in cases:
        words[0], words[8:12] = 11, case["seed"]
        native.pg9_execute(words)
        assert list(words[8:12]) == case["after"], (case, list(words[8:12]))
        if case["error"]:
            errors += 1
            assert words[1] == 2, (case, words[1])
        else:
            assert words[1] == 0, (case, words[1])
            assert words[16] == 6, (case, words[16])
            assert [decode(words, 64 + i * 32) for i in range(6)] == case["sets"], (
                case, [decode(words, 64 + i * 32) for i in range(6)])
    print(f"PASS: {len(cases)} complete seeded teams and RNG states ({errors} matching source errors)")


if __name__ == "__main__":
    main()
