#!/usr/bin/env python3
"""Default Showdown RNG: complete key/output traces and independent teams."""
import ctypes
import json
from pathlib import Path
import subprocess

from test_sets import NORMALIZE_JS, decode

ROOT = Path(__file__).resolve().parents[2]


def limbs(seed):
    raw = bytes.fromhex(seed.split(",")[1])
    return [int.from_bytes(raw[i:i + 4], "little") for i in range(0, 32, 4)]


def main():
    catalog = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    native = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    native.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    native.pg9_execute.restype = None
    words = (ctypes.c_uint32 * catalog["word_count"])()
    binary = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, binary, len(binary))
    words[2] = 1
    script = """
const {PRNG,Teams}=require('./ocean/pokemon_gen9/reference.cjs');
const cat=require('./build/pokemon_gen9/catalog/catalog.json');
""" + NORMALIZE_JS + """
const seeds=['sodium,','sodium,'+'0'.repeat(64),'sodium,'+'f'.repeat(64),
 'sodium,000102030405060708090a0b0c0d0e0f'];
for(let n=0;n<60;n++)seeds.push('sodium,'+Buffer.from(Array.from({length:32},
 (_,i)=>(n*73+i*29+i*n)&255)).toString('hex'));
const bounds=[0,1,2,3,16,19,509,65535,2147483648,4294967295];
const draws=[],teams=[];
const g=Teams.getGenerator('gen9randombattle',seeds[0]);
for(const seed of seeds){
 const rng=new PRNG(seed);
 for(let i=0;i<32;i++){
  const before=rng.getSeed(),bound=bounds[i%bounds.length];
  const raw=rng.rng.next(),after=rng.getSeed();
  const scaled=new PRNG(before).random(bound);
  draws.push({before,bound,raw,scaled,after});
 }
 g.setSeed(seed);
 let error=false,raw=null;
 try{raw=g.randomTeam();}catch(e){error=true;}
 teams.push({seed:new PRNG(seed).getSeed(),error,
  sets:raw?raw.map(packed):[],after:g.prng.getSeed()});
}
console.log(JSON.stringify({draws,teams}));
"""
    cases = json.loads(subprocess.check_output(["node", "-e", script], cwd=ROOT))

    def seed(value):
        words[0], words[8:16] = 24, limbs(value)
        native.pg9_execute(words)
        assert words[1] == 0 and words[4] == 1
        assert list(words[560:568]) == limbs(value)

    for case in cases["draws"]:
        seed(case["before"])
        words[0] = 26
        native.pg9_execute(words)
        assert words[1] == 0 and words[16] == case["raw"], (case, words[16])
        assert list(words[560:568]) == limbs(case["after"]), case
        seed(case["before"])
        words[0], words[12] = 25, case["bound"]
        native.pg9_execute(words)
        assert words[1] == 0 and words[16] == case["scaled"], (case, words[16])
        assert list(words[560:568]) == limbs(case["after"]), case
    errors = 0
    for case in cases["teams"]:
        seed(case["seed"])
        words[0] = 11
        native.pg9_execute(words)
        assert list(words[560:568]) == limbs(case["after"]), case
        if case["error"]:
            errors += 1
            assert words[1] == 2, (case, words[1])
        else:
            assert words[1] == 0 and words[16] == 6, (case, words[1])
            assert [decode(words, 64 + i * 32) for i in range(6)] == case["sets"], case
    # Constructor reset preserves the selected stream without advancing it.
    seed(cases["teams"][-1]["seed"])
    before = list(words[560:568])
    cache = catalog["species_type_cache"]
    words[cache["start"]:cache["end"]] = [0xDEADBA5E] * (cache["end"] - cache["start"])
    words[0] = 12
    native.pg9_execute(words)
    assert words[1] == 0 and list(words[560:568]) == before
    roots = {524: 1, 525: 2, 526: 3, 527: 5, 529: 4, 4160: 576, 4192: 12, 4193: 13}
    # The constructor now attaches its shared species type array after the
    # thirteen selected effect states/maps. Every other cache root must reset.
    group = catalog["species"][words[576 + 77]]["type_array"]
    roots[cache["start"] + group] = 14
    assert list(words[576 + 96:576 + 99]) == [14, 0, 0]
    assert list(words[576 + 102:576 + 104]) == [15, 16]
    count = words[576 + 14]
    assert list(words[576 + 107:576 + 109]) == [18 + count, 17 + count]
    assert words[576 + 109] == 19 + count
    assert {at: words[at] for at in roots} == roots
    assert all(words[i] == 0 for i in range(512, catalog["mutable_words"])
               if i not in range(560, 568) and i not in range(576, 704) and i not in roots)
    # The old two-team operation accepts explicit four-limb seeds only.
    words[0] = 14
    native.pg9_execute(words)
    assert words[1] == 3 and list(words[560:568]) == before
    words[0], words[4] = 25, 2
    native.pg9_execute(words)
    assert words[1] == 3 and list(words[560:568]) == before
    print(f"PASS: {len(cases['draws'])} raw/scaled Sodium draws with full keys, "
          f"{len(cases['teams'])} complete teams ({errors} matching source errors); "
          "reset stream preservation and invalid mode rejection")


if __name__ == "__main__":
    main()
