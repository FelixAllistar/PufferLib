#!/usr/bin/env python3
"""Independent JS UTF-16 primitives and canonical text identities.

Text IDs are a serialization contract; oracle contents come from original JS
strings. Interning never imports or replaces any post-action native text state.
"""
import ctypes
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = r"""
const cat=require('./build/pokemon_gen9/catalog/catalog.json');
const names=cat.ids.strings.names,canonical=new Map(names.map((v,i)=>[v,i]));
const cases=[];let count=0;
const units=text=>Array.from({length:text.length},(_,i)=>text.charCodeAt(i));
const hash=text=>units(text).reduce((h,u)=>(Math.imul(h,31)+u)>>>0,0);
function output(text,id){return {id,units:units(text),hash:hash(text),count};}
function read(text){cases.push({mode:0,argument:canonical.get(text),...output(text,canonical.get(text))});}
function intern(text){
 let id=canonical.get(text);
 if(id===undefined){id=names.length+count++;canonical.set(text,id);}
 cases.push({mode:1,argument:text.length,input:units(text),...output(text,id)});
 return id;
}
for(const text of names){read(text);intern(text);}
// Equal hashes must retain distinct IDs; JS strings compare by all code units.
for(const text of ['Aa','BB','Aa','BB','AaAa','BBBB','AaBB','BBAa','',
 '\u0000','\ud800','\udfff','\ud83d\ude00','\ud83d\u0000\ude00',
 'é','e\u0301','Normal/Flying','Fire/Water','???/Bird','Rock','Normal',
 '汉字','A'.repeat(1024),'\u0000'.repeat(1024)])intern(text);
let seed=0x8539a917;
const pick=n=>{seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed%n;};
const retained=['Normal/Flying','Fire/Water','\ud800','\ud83d\ude00'];
for(let n=0;n<1400;n++){
 const length=pick(65),text=String.fromCharCode(...Array.from({length},()=>pick(65536)));
 intern(text);intern(text);read(text);
 if(n%100===0)for(const previous of retained)read(previous);
 if(n%17===0)retained.push(text);
}
for(const text of retained)read(text);
for(const text of canonical.keys())read(text);
console.log(JSON.stringify({cases,dynamic:count}));
"""


def main():
    fixture = json.loads(subprocess.check_output(["node", "-e", SCRIPT], cwd=ROOT))
    cat = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * cat["word_count"])()
    binary = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, binary, len(binary))
    words[2], words[8:12] = 1, [1, 2, 3, 4]
    sentinel = [0xDACA0000 + i for i in range(8192 - 512)]
    words[512:8192] = sentinel
    words[0], words[32:35] = 40, [0, 77, 0]
    lib.pg9_execute(words)
    assert words[1] == 0
    # An independently imported object survives string interning and vice versa.
    rock = cat["ids"]["strings"]["map"]["Rock"]
    words[0], words[32:35] = 40, [8, 1, 1]
    words[8192:8197] = [21, 9, 0, rock, 0]
    lib.pg9_execute(words)
    assert words[1] == 0
    held = rock
    for index, case in enumerate(fixture["cases"]):
        words[0], words[32:34] = 53, [case["mode"], case["argument"]]
        if case["mode"] == 1:
            words[160:160 + len(case["input"])] = case["input"]
        private_before = list(words[512:8192])
        lib.pg9_execute(words)
        assert words[1] == 0, (index, case, words[1])
        assert list(words[16:22]) == [case["id"], 2, 77, len(case["units"]),
                                       case["count"], case["hash"]], (index, case, list(words[16:22]))
        assert list(words[64:68]) == [9, 0, case["id"], 0]
        assert list(words[8192:8192 + words[19]]) == case["units"], (index, case)
        assert list(words[512:8192]) == private_before and list(words[8:12]) == [1, 2, 3, 4]
        if index % 179 == 0:
            words[0], words[32:35] = 40, [7, 1, 0]
            lib.pg9_execute(words)
            assert words[1] == 0 and list(words[16:20]) == [1, 2, 77, 1]
            assert list(words[8192:8197]) == [21, 9, 0, held, 0]
            # A normal object mutation must keep every previously interned string.
            held = case["id"]
            words[0], words[32:35], words[64:68] = 40, [3, 1, 21], [9, 0, held, 0]
            lib.pg9_execute(words)
            assert words[1] == 0
    last_count = fixture["dynamic"]
    for mode, argument, values in [(1, 1, [65536]), (1, 1025, []),
                                   (0, 0xFFFFFFFF, []), (0, len(cat["ids"]["strings"]["names"]) + last_count, []),
                                   (99, 0, [])]:
        words[0], words[32:34] = 53, [mode, argument]
        words[160:160 + len(values)] = values
        lib.pg9_execute(words)
        assert words[1] == 3, (mode, argument, words[1])
        words[0], words[32:34] = 53, [0, rock]
        lib.pg9_execute(words)
        assert words[1] == 0 and words[20] == last_count
        assert words[17] == 2 and words[18] == 77
    # Reset forgets dynamic primitives, while all static IDs remain valid.
    words[0], words[32:35] = 40, [0, 93, 0]
    lib.pg9_execute(words)
    assert words[1] == 0
    words[0], words[32:34] = 53, [0, len(cat["ids"]["strings"]["names"])]
    lib.pg9_execute(words)
    assert words[1] == 3
    words[0], words[32:34] = 53, [0, rock]
    lib.pg9_execute(words)
    assert words[1] == 0 and list(words[17:19]) == [1, 93] and words[20] == 0
    print(f"PASS: {len(fixture['cases'])} independent JS text transitions; "
          f"{len(cat['ids']['strings']['names'])} static and {fixture['dynamic']} dynamic texts; "
          "UTF-16 contents, hash collisions, surrogates/NUL, canonical IDs, retained snapshots, "
          "object/state/RNG preservation, invalid domains and reset")


if __name__ == "__main__":
    main()
