#!/usr/bin/env python3
"""Resolved callback metadata, constants and getCallback aliases from Showdown."""
import ctypes
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]

SCRIPT = """
const {Dex,oracle}=require('./ocean/pokemon_gen9/reference.cjs');
const {Battle}=require(oracle+'/dist/sim/battle');
const {Side}=require(oracle+'/dist/sim/side');
const cat=require('./build/pokemon_gen9/catalog/catalog.json'),dex=Dex.mod('gen9');
const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
const p=new Side('fixture',b,0,[{species:'Mew',moves:['psychic'],ability:'No Ability'}]).pokemon[0];
const families=['moves','abilities','items','conditions',null,'species'];
const aliases=[],raw=[],immunities=[];
const code=key=>cat.ids.callbacks.map[key.toLowerCase()];
function tag(value) {
 if(value===undefined)return [4,0];
 if(typeof value==='function')return [0,0];
 if(typeof value==='boolean')return [1,+value];
 if(typeof value==='number')return Number.isInteger(value)&&value>=0?[2,value]:[5,value*10+32768];
 if(typeof value==='string')return [3,cat.ids.strings.map[value]];
 throw Error('unexpected handler value');
}
function descriptor(effect,key,value,nativeKey=key) {
 return [code(nativeKey),...tag(value),Number(effect[key+'Order']||0),
   (effect[key+'Priority']||0)*10+32768,(effect[key+'SubOrder']||0)+32768];
}
function packed(values){return [values[0],values[1],values[3],values[4],values[5],values[2]];}
for(let kind=0;kind<families.length;kind++) {
 const family=families[kind];
 if(!family)continue;
 for(let id=1;id<cat.ids[family].names.length;id++) {
  const name=cat.ids[family].names[id],effect=dex[family].get(name);
  for(const key of Object.keys(effect))if(typeof effect[key]==='function'&&
    (key.startsWith('on')||key.endsWith('Callback'))&&!code(key))
    throw Error('Executable callback missing from catalog: '+family+'.'+name+'.'+key);
  // Each exported event name is queried using the original Dex property/API.
  for(const key of cat.ids.callbacks.original.slice(1)) {
   if(effect[key]!==undefined||effect[key+'Order']!==undefined||
      effect[key+'Priority']!==undefined||effect[key+'SubOrder']!==undefined)
    raw.push({input:[kind,id,code(key),0],expected:packed(descriptor(effect,key,effect[key]))});
  }
  for(const key of ['onSwitchIn','onStart','onAnySwitchIn'])for(const mon of [false,true]) {
   const value=b.getCallback(mon?p:b,effect,key);
   const nativeKey=effect[key]===undefined&&value!==undefined?'onStart':key;
   aliases.push({input:[kind,id,code(key),+mon],expected:value===undefined?null:
     packed(descriptor(effect,key,value,nativeKey))});
  }
 }
}
for(let a=1;a<cat.ids.types.names.length;a++)for(let d=1;d<cat.ids.types.names.length;d++) {
 const at=dex.types.get(cat.ids.types.names[a]).name,dt=dex.types.get(cat.ids.types.names[d]).name;
 immunities.push([a,d,+dex.getImmunity(at,dt)]);
}
b.destroy();console.log(JSON.stringify({raw,aliases,immunities}));
"""


def main():
    fixtures = json.loads(subprocess.check_output(["node", "-e", SCRIPT], cwd=ROOT))
    cat = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * cat["word_count"])()
    binary = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, binary, len(binary))
    words[2] = 1
    for op, cases in ((22, fixtures["raw"]), (23, fixtures["aliases"])):
        for case in cases:
            words[0], words[8:12], words[32:36] = op, [1, 2, 3, 4], case["input"]
            lib.pg9_execute(words)
            assert words[1] == 0
            assert words[16] == int(case["expected"] is not None), (case, words[16])
            if case["expected"] is not None:
                assert list(words[64:70]) == case["expected"], (case, list(words[64:70]))
            assert list(words[8:12]) == [1, 2, 3, 4]
    start = cat["regions"]["immunity"]["start"]
    # Inspect the imported catalog separately from native callback results.
    for a, d, expected in fixtures["immunities"]:
        assert words[start + a * 20 + d] == expected
    for kind, id in [(4, 1), (0, 0), (0, cat["regions"]["moves"]["count"]), (3, 0xFFFFFFFF)]:
        words[0], words[32:36] = 22, [kind, id, 1, 0]
        lib.pg9_execute(words)
        assert words[1] == 3
    print(f"PASS: {len(fixtures['raw'])} callback descriptors/constants, "
          f"{len(fixtures['aliases'])} getCallback aliases with requested-event metadata; "
          f"{len(fixtures['immunities'])} type immunities and invalid-effect rejection")


if __name__ == "__main__":
    main()
