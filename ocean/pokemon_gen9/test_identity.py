#!/usr/bin/env python3
"""Exact resolved IDs and display text, separate from numeric catalog IDs."""
import ctypes
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = r"""
const {Dex}=require('./ocean/pokemon_gen9/reference.cjs');
const dex=Dex.mod('gen9'),cat=require('./build/pokemon_gen9/catalog/catalog.json'),cases=[];
for(const [kind,family]of ['moves','abilities','items','conditions',null,'species'].entries()){
 if(!family)continue;
 for(let id=1;id<cat.ids[family].names.length;id++){
  const effect=family==='conditions'?dex.conditions.getByID(cat.ids[family].names[id]):dex[family].get(cat.ids[family].names[id]);
  const expected=['id','name'].map(key=>{
   const text=effect[key]||'',value=cat.ids.strings.map[text];
   if(value===undefined||cat.ids.strings.names[value]!==text)throw Error('Identity text missing: '+text);
   return value;
  });
  cases.push({kind,id,family,expected});
 }
}
if(cat.schema_version!==12)throw Error('Expected species-type identity catalog schema');
const stable=['','bide','iceball','recharge','rollout','uproar'];
if(JSON.stringify(cat.ids.strings.names.slice(0,stable.length))!==JSON.stringify(stable))throw Error('Literal IDs moved');
console.log(JSON.stringify(cases));
"""


def main():
    cases = json.loads(subprocess.check_output(["node", "-e", SCRIPT], cwd=ROOT))
    cat = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * cat["word_count"])()
    binary = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, binary, len(binary))
    words[2], words[8:12] = 1, [1, 2, 3, 4]
    actions = [0x1DE10000 + i for i in range(512)]
    words[2176:2688] = actions
    for case in cases:
        region = cat["regions"][case["family"] + "Text"]
        at = region["start"] + case["id"] * region["stride"]
        assert list(words[at:at + 2]) == case["expected"]
        words[0], words[32:34] = 41, [case["kind"], case["id"]]
        lib.pg9_execute(words)
        assert words[1] == 0, (case, words[1])
        assert list(words[16:18]) == case["expected"], (case, list(words[16:18]))
        assert list(words[8:12]) == [1, 2, 3, 4]
        assert list(words[2176:2688]) == actions
    for family, id in ((0, 0), (4, 1), (6, 1), (5, cat["regions"]["species"]["count"]), (3, 4294967295)):
        words[0], words[32:34] = 41, [family, id]
        lib.pg9_execute(words)
        assert words[1] == 3
    print(f"PASS: {len(cases)} resolved effect identity/display pairs, canonical/cosmetic species, "
          "prefixed effects, stable literal text IDs, native table lookups, exact RNG and rejected inputs")


if __name__ == "__main__":
    main()
