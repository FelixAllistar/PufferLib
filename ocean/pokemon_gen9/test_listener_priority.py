#!/usr/bin/env python3
"""Actual resolved Dex getCallback/resolvePriority, including species handlers."""
import ctypes
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = r"""
const {oracle,Dex}=require('./ocean/pokemon_gen9/reference.cjs');
const {Battle}=require(oracle+'/dist/sim/battle'),{Side}=require(oracle+'/dist/sim/side');
const cat=require('./build/pokemon_gen9/catalog/catalog.json'),dex=Dex.mod('gen9');
const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
const set={species:'Mew',moves:['tackle'],ability:'No Ability'};
b.sides[0]=new Side('one',b,0,[structuredClone(set)]);
b.sides[1]=new Side('two',b,1,[structuredClone(set)]);
const p=b.sides[0].pokemon[0],q=b.sides[1].pokemon[0];p.storedStats.spe=123;
const families=['moves','abilities','items','conditions',null,'species'],cases=[];
function tag(v){if(typeof v==='function')return [0,0];if(typeof v==='boolean')return [1,+v];
 if(typeof v==='string')return [3,cat.ids.strings.map[v]];
 if(typeof v==='number')return Number.isInteger(v)&&v>=0?[2,v]:[5,v*10+32768];
 return [4,0];}
let serial=0;
function fixture(kind,id,key,pokemon,scope,rank){
 const family=families[kind],effect=family==='conditions'?dex.conditions.getByID(cat.ids[family].names[id]):
  dex[family].get(cat.ids[family].names[id]);
 const holder=pokemon?p:b,callback=b.getCallback(holder,effect,key);
 const creation=17+serial%3,abilityOrder=23+serial%5,cached=serial%7===0?0:42+serial%11;
 const state={target:scope===1||scope===2?p.side:scope===3?b.field:scope===0?p:b,
  isSlotCondition:scope===2,effectOrder:creation};
 p.abilityState.effectOrder=abilityOrder;p.speed=cached;
 b.speedOrder=rank===0?[]:rank===1?[p.getFieldPositionValue(),q.getFieldPositionValue()]:
  [q.getFieldPositionValue(),p.getFieldPositionValue()];
 const out=callback===undefined?null:b.resolvePriority({effect,callback,state,effectHolder:holder},key);
 const nativeKey=effect[key]===undefined&&callback!==undefined?'onStart':key;
 const [t,v]=tag(callback);
 cases.push({input:[kind,id,cat.ids.callbacks.map[key.toLowerCase()],101,scope,creation,
  pokemon?576:0,+pokemon,cached,123,rank,abilityOrder,serial%2],
  keys:out?[0,Number(out.order||0),(out.priority||0)*10+32768,(out.speed||0)*2+32768,
   out.subOrder+32768,out.effectOrder||0,pokemon?abilityOrder+1:0,serial%2]:null,
  descriptor:out?[cat.ids.callbacks.map[nativeKey.toLowerCase()],t,Number(effect[key+'Order']||0),
   (effect[key+'Priority']||0)*10+32768,(effect[key+'SubOrder']||0)+32768,v]:null});
 serial++;
}
for(let kind=0;kind<families.length;kind++){
 const family=families[kind];if(!family)continue;
 for(let id=1;id<cat.ids[family].names.length;id++){
  const effect=family==='conditions'?dex.conditions.getByID(cat.ids[family].names[id]):dex[family].get(cat.ids[family].names[id]);
  for(const key of cat.ids.callbacks.original.slice(1))if(effect[key]!==undefined)
   for(const pokemon of [false,true])fixture(kind,id,key,pokemon,serial%5,serial%3);
  for(const key of ['onSwitchIn','onAnySwitchIn'])for(const pokemon of [false,true])
   fixture(kind,id,key,pokemon,serial%5,serial%3);
 }
}
for(const callback of ['onAllyTryHitSide','onSwitchIn','onFoeSwitchIn','onRedirectTarget'])
 for(const rank of [0,1,2])for(const scope of [0,1,2,3,4])
  if(cat.ids.callbacks.map[callback.toLowerCase()])fixture(1,cat.ids.abilities.map.magicbounce,callback,true,scope,rank);
b.destroy();console.log(JSON.stringify(cases));
"""


def main():
    cases = json.loads(subprocess.check_output(["node", "-e", SCRIPT], cwd=ROOT))
    cat = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * cat["word_count"])()
    data = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, data, len(data))
    words[2] = 1
    for case in cases:
        words[0], words[8:12], words[32:45] = 39, [1, 2, 3, 4], case["input"]
        lib.pg9_execute(words)
        assert words[1] == 0, (case, words[1])
        assert words[16] == int(case["keys"] is not None), (case, words[16])
        if case["keys"] is not None:
            assert list(words[64:72]) == case["keys"], (case, list(words[64:72]))
            assert list(words[72:78]) == case["descriptor"], (case, list(words[72:78]))
        assert list(words[8:12]) == [1, 2, 3, 4]
    # Independently verify prefix/suffix tables against literal property names.
    region = cat["regions"]["callbackInfo"]
    for index, key in enumerate(cat["ids"]["callbacks"]["original"]):
        at = region["start"] + index * region["stride"]
        flags = int(key.endswith("SwitchIn")) | (int(key.endswith("RedirectTarget")) << 1) | (int(key == "onAllyTryHitSide") << 2)
        assert words[at] == flags
        for offset, prefix in enumerate(["onAlly", "onAny", "onFoe", "onSource"], 1):
            expected = cat["ids"]["callbacks"]["map"].get((prefix + key[2:]).lower(), 0) if key.startswith("on") else 0
            assert words[at + offset] == expected
    for family, cached in [(4, 42), (6, 42), (1, 16384)]:
        words[0], words[32:45] = 39, [family, 1, 1, 101, 0, 17, 576, 1, cached, 123, 0, 23, 0]
        lib.pg9_execute(words)
        assert words[1] == 3
    print(f"PASS: {len(cases)} resolved listener-priority cases, exact callback aliases/metadata, "
          "species descriptors, all scopes, switch-in speed bias, Magic Bounce and prefix/suffix tables")


if __name__ == "__main__":
    main()
