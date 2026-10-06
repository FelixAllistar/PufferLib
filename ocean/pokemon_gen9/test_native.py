#!/usr/bin/env python3
"""Independent integer checks and seeded draws from the real Showdown PRNG."""
import ctypes
import json
from pathlib import Path
import random
import subprocess

ROOT = Path(__file__).resolve().parents[2]
WORDS = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())["word_count"]
MASK = (1 << 32) - 1


def main():
    native = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    native.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    native.pg9_execute.restype = None
    words = (ctypes.c_uint32 * WORDS)()
    script = """
const {PRNG}=require('./ocean/pokemon_gen9/reference.cjs');
const seeds=['0,0,0,0','1,2,3,4','65535,65535,65535,65535','0,65535,0,65535'];
const bounds=[0,1,2,16,19,509,1024,65535,4294967295];
const cases=[];
for(const seed of seeds) {
 const rng=new PRNG(seed);
 for(let i=0;i<256;i++) {
  const before=rng.getSeed().split(',').map(Number);
  const bound=bounds[i%bounds.length];
  const result=rng.random(bound);
  cases.push({before,bound,result,after:rng.getSeed().split(',').map(Number)});
 }
}
console.log(JSON.stringify(cases));
"""
    cases = json.loads(subprocess.check_output(["node", "-e", script], cwd=ROOT))
    for case in cases:
        words[0] = 1
        words[8:12] = case["before"]
        words[12] = case["bound"]
        native.pg9_execute(words)
        assert words[1] == 0
        assert list(words[16:20]) == case["after"], case
        assert words[20] == case["result"], (case, words[20])
    rng = random.Random(9069)
    values = [(0, 0), (MASK, MASK), (MASK, 1024), (0x80000000, 2)]
    values += [(rng.randrange(1 << 32), rng.randrange(1 << 32)) for _ in range(512)]
    for a, b in values:
        words[0] = 2
        words[8], words[9] = a, b
        native.pg9_execute(words)
        assert words[16] == a * b >> 32, (a, b, words[16])
        # modify has a U32-truncated product, followed by a non-wrapping add.
        assert words[18] == ((a * b & MASK) + 2047) // 4096, (a, b, words[18])
    for _ in range(256):
        a, b = rng.randrange(256, 32769), rng.randrange(256, 32769)
        words[8], words[9] = a, b
        native.pg9_execute(words)
        assert words[17] == (a * b + 2048) >> 12
    signed_chains = json.loads(subprocess.check_output(["node", "-e", """
const {oracle}=require('./ocean/pokemon_gen9/reference.cjs');
const {Battle}=require(oracle+'/dist/sim/battle');
const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
const cases=[];
for(const a of [0,1,4096,32768,65535,65536,1048575,16777215])
 for(const n of [0,1,4096,32768,65535,65536,1048575,16777215])
  cases.push([a,n,(b.chain(a/4096,n/4096)*4096)>>>0]);
console.log(JSON.stringify(cases));b.destroy();
"""], cwd=ROOT))
    for a, b, expected in signed_chains:
        words[0], words[8], words[9] = 2, a, b
        native.pg9_execute(words)
        assert words[17] == expected, (a, b, words[17], expected)
    # Reset/operation results cannot depend on dirty transport memory.
    for i in range(WORDS):
        words[i] = MASK
    words[0] = 1
    words[8:12] = [0, 0, 0, 0]
    words[12] = 1024
    native.pg9_execute(words)
    assert words[1] == 0 and list(words[16:21]) == [0, 0, 38, 40643, 0]
    words[0] = 999
    native.pg9_execute(words)
    assert words[1] == 1
    catalog = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    binary = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, binary, len(binary))
    words[2] = 1
    words[0] = 3
    species_cases = json.loads(subprocess.check_output(["node", "-e", """
const {Dex}=require('./ocean/pokemon_gen9/reference.cjs');
const sets=require('./build/pokemon_gen9/oracle/dist/data/random-battles/gen9/sets.json');
console.log(JSON.stringify(Object.keys(sets).map(id=>{
 const s=Dex.mod('gen9').species.get(id);
 return {id,base:s.baseSpecies,types:s.types,stats:['hp','atk','def','spa','spd','spe'].map(k=>s.baseStats[k]),
  gender:s.gender||'',level:sets[id].level,templates:sets[id].sets.length,nfe:s.nfe,
  weaknesses:[0,...require('./build/pokemon_gen9/catalog/catalog.json').ids.types.names.slice(1)
   .map(t=>Dex.mod('gen9').getEffectiveness(Dex.types.get(t).name,s)+8)]};
})));
"""], cwd=ROOT))
    for case in species_cases:
        key = lambda s: "".join(x for x in s.lower() if x.isascii() and x.isalnum())
        sid = catalog["ids"]["species"]["map"][case["id"]]
        words[12] = sid
        native.pg9_execute(words)
        assert words[1] == 0 and words[2] == 0
        expected = [sid, catalog["ids"]["species"]["map"][key(case["base"])],
                    catalog["ids"]["types"]["map"][key(case["types"][0])],
                    catalog["ids"]["types"]["map"][key(case["types"][1])] if len(case["types"]) == 2 else 0,
                    *case["stats"], {"M": 1, "F": 2, "N": 3, "": 0}[case["gender"]],
                    case["level"], case["templates"]]
        assert list(words[16:29]) == expected, (case, list(words[16:29]))
        assert words[31] == int(case["nfe"])
        assert list(words[64:84]) == case["weaknesses"], (case, list(words[64:84]))
    print(f"PASS: {len(cases)} Showdown RNG draws, {len(values)} full-word products/modifiers, "
          f"{256 + len(signed_chains)} modifier chains including signed shift, "
          f"{len(species_cases)} species imports, dirty buffers and rejected operations")


if __name__ == "__main__":
    main()
