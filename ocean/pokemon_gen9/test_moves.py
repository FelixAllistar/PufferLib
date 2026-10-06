#!/usr/bin/env python3
"""Compare native moveset enforcement, ordered choices and every RNG draw."""
import ctypes
import json
from pathlib import Path
import subprocess

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
const {Dex,Teams}=require('./ocean/pokemon_gen9/reference.cjs');
const cat=require('./build/pokemon_gen9/catalog/catalog.json');
const g=Teams.getGenerator('gen9randombattle','1,2,3,4');
const names=['rain','sun','sand','snow','statusCure','spikes','toxicSpikes','stealthRock',
 'stickyWeb','defog','rapidSpin','screens','teraBlast'];
const norm=s=>s.toLowerCase().replace(/[^a-z0-9]/g,'');
const result=[];
for(let tid=1;tid<cat.templates.length;tid++)for(let n=0;n<4;n++) {
 const t=cat.templates[tid],s=Dex.mod('gen9').species.get(t.species);
 const team=names.map((name,i)=>n%3===0?0:(n%3===1?(name==='spikes'?2:1):(i%2)));
 const details=Object.fromEntries(names.map((name,i)=>[name,team[i]]));
 const seed=n===0?[1,2,3,4]:n===1?[65535,65535,65535,65535]:n===2?[0,0,0,0]:[tid,3,5,7];
 const tera=t.teraTypes[n%t.teraTypes.length],lead=(tid+n)%3===0;
 g.setSeed(seed.join(','));
 let error=false,moves=[];
 try {moves=[...g.randomMoveset(new Set(s.types),t.abilities,details,s,lead,
   t.movepool.map(m=>Dex.mod('gen9').moves.get(m).id),tera,t.role,false)];}catch(e){error=true;}
 result.push({tid,sid:cat.ids.species.map[t.species],tera:cat.ids.types.map[norm(tera)],
  lead,seed,after:g.prng.getSeed().split(',').map(Number),
  team,error,moves:moves.map(id=>cat.ids.moves.map[id])});
}
console.log(JSON.stringify(result));
"""
    cases = json.loads(subprocess.check_output(["node", "-e", script], cwd=ROOT))
    errors = 0
    for case in cases:
        words[0] = 9
        words[12], words[13], words[14], words[15] = case["sid"], case["tid"], case["tera"], case["lead"]
        words[48:61] = case["team"]
        words[8:12] = case["seed"]
        native.pg9_execute(words)
        assert list(words[8:12]) == case["after"], (case, list(words[8:12]))
        if case["error"]:
            errors += 1
            assert words[1] == 2, (case, words[1])
        else:
            assert words[1] == 0, (case, words[1])
            assert list(words[64:64 + words[16]]) == case["moves"], (case, list(words[64:64 + words[16]]))
    print(f"PASS: {len(cases)} seeded movesets and RNG states ({errors} matching source errors)")


if __name__ == "__main__":
    main()
