#!/usr/bin/env python3
"""Exact seeded set fixtures from the original generator, not its Bend port."""
import ctypes
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def decode(words, at):
    return {
        "mon": words[at], "display": words[at + 1], "role": words[at + 2],
        "tera": words[at + 3], "ability": words[at + 4], "item": words[at + 5],
        "level": words[at + 6], "gender": words[at + 7], "shiny": words[at + 8],
        "moves": list(words[at + 10:at + 10 + words[at + 9]]),
        "evs": list(words[at + 16:at + 22]), "ivs": list(words[at + 22:at + 28]),
    }


NORMALIZE_JS = """
const norm=s=>s.toLowerCase().replace(/[^a-z0-9]/g,'');
function packed(s) {
 const id=(kind,name)=>{
  if(name==='')return 0;
  const found=cat.ids[kind].map[norm(name)];
  if(found===undefined)throw Error('Unknown '+kind+' name: '+name);
  return found;
 };
 return {mon:id('species',s.speciesId),display:id('species',s.species),
  role:id('roles',s.role),tera:id('types',s.teraType),ability:id('abilities',s.ability),
  item:id('items',s.item),level:s.level,gender:({M:1,F:2,N:3})[s.gender],shiny:+s.shiny,
  moves:s.moves.map(m=>id('moves',m)),evs:['hp','atk','def','spa','spd','spe'].map(k=>s.evs[k]),
  ivs:['hp','atk','def','spa','spd','spe'].map(k=>s.ivs[k])};
}
"""


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
const g=Teams.getGenerator('gen9randombattle','1,2,3,4');
const names=['rain','sun','sand','snow','statusCure','spikes','toxicSpikes','stealthRock',
 'stickyWeb','defog','rapidSpin','screens','teraBlast'];
""" + NORMALIZE_JS + """
const result=[];
for(const sid of Object.keys(g.randomSets))for(let n=0;n<4;n++) {
 const team=names.map((name,i)=>n%3===0?0:(n%3===1?(name==='spikes'?2:1):(i%2)));
 const details=Object.fromEntries(names.map((name,i)=>[name,team[i]]));
 const seed=n===0?[1,2,3,4]:n===1?[65535,65535,65535,65535]:n===2?[0,0,0,0]:[cat.ids.species.map[sid],3,5,7];
 const lead=n%2===0;
 g.setSeed(seed.join(','));
 let error=false,set=null,raw=null;
 try {raw=g.randomSet(sid,details,lead,false);}catch(e){error=true;}
 if(raw)set=packed(raw);
 result.push({sid:cat.ids.species.map[sid],seed,lead,team,error,set,
   after:g.prng.getSeed().split(',').map(Number)});
}
console.log(JSON.stringify(result));
"""
    cases = json.loads(subprocess.check_output(["node", "-e", script], cwd=ROOT))
    errors = 0
    for case in cases:
        words[0], words[12], words[15] = 10, case["sid"], case["lead"]
        words[48:61], words[8:12] = case["team"], case["seed"]
        native.pg9_execute(words)
        assert list(words[8:12]) == case["after"], (case, list(words[8:12]))
        if case["error"]:
            errors += 1
            assert words[1] == 2, (case, words[1])
        else:
            assert words[1] == 0, (case, words[1])
            assert decode(words, 64) == case["set"], (case, decode(words, 64))
    print(f"PASS: {len(cases)} full seeded sets and RNG states ({errors} matching source errors)")


if __name__ == "__main__":
    main()
