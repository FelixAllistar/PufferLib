#!/usr/bin/env python3
"""Independent TS/Bend comparisons of ordered pool edits and early returns."""
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
 const selected=t.movepool.slice(0,n).map(m=>Dex.mod('gen9').moves.get(m).id);
 const moveSet=new Set(selected);
 const pool=t.movepool.map(m=>Dex.mod('gen9').moves.get(m).id).filter(m=>!moveSet.has(m));
 const team=names.map((name,i)=>(tid+n)%3===0?0:((tid+n)%3===1?(name==='spikes'?2:1):(i%2)));
 const details=Object.fromEntries(names.map((name,i)=>[name,team[i]]));
 const counter=g.queryMoves(moveSet,s,t.teraTypes[0],t.abilities);
 let error=false;
 try {g.cullMovePool(new Set(s.types),moveSet,t.abilities,counter,pool,details,s,
   false,t.teraTypes[0],t.role,false);}catch(e){error=true;}
 result.push({tid,sid:cat.ids.species.map[t.species],tera:cat.ids.types.map[norm(t.teraTypes[0])],
  moves:selected.map(id=>cat.ids.moves.map[id]),team,error,pool:pool.map(id=>cat.ids.moves.map[id])});
}
console.log(JSON.stringify(result));
"""
    cases = json.loads(subprocess.check_output(["node", "-e", script], cwd=ROOT))
    errors = 0
    for case in cases:
        words[0] = 8
        words[12], words[13], words[14] = case["sid"], case["tid"], case["tera"]
        words[32] = len(case["moves"])
        words[33:33 + len(case["moves"])] = case["moves"]
        words[48:61] = case["team"]
        words[8:12] = [1, 2, 3, 4]
        native.pg9_execute(words)
        assert list(words[8:12]) == [1, 2, 3, 4]
        if case["error"]:
            errors += 1
            assert words[1] == 2, (case, words[1])
        else:
            assert words[1] == 0, (case, words[1])
            assert words[16] == len(case["pool"]), (case, words[16])
            assert list(words[64:64 + words[16]]) == case["pool"], (case, list(words[64:64 + words[16]]))
    print(f"PASS: {len(cases)} ordered move-pool culls ({errors} matching source errors)")


if __name__ == "__main__":
    main()
