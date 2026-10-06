#!/usr/bin/env python3
"""Persistent indexed-array ownership against independent JS builtins.

The admitted model has own indexed cells and length, ordinary Array.prototype,
and no named properties/accessors. Initial sparse arrays are imported once;
subsequent output arrays/mutations are produced independently on each side.
"""
import ctypes
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = r"""
const cat=require('./build/pokemon_gen9/catalog/catalog.json');
const objects=[null],refs=new WeakMap(),cases=[];
let random=0x865fad31;
const pick=n=>{random=(Math.imul(random,1664525)+1013904223)>>>0;return random%n;};
const strings=cat.ids.strings.names;
const texts=['','tackle','bide','rollout'].filter(s=>Object.hasOwn(cat.ids.strings.map,s));
if(texts.length<3)throw Error('Missing fixture strings');
function encode(v){
 if(v===undefined)return [0,0,0,0];if(v===null)return [1,0,0,0];
 if(typeof v==='boolean')return [v?3:2,0,0,0];
 if(typeof v==='string'){
  if(!Object.hasOwn(cat.ids.strings.map,v))throw Error('Unknown string '+v);
  return [9,0,cat.ids.strings.map[v],0];
 }
 if(typeof v==='object'){
  if(!refs.has(v))throw Error('Unregistered fixture object');
  return [10,Array.isArray(v)?8:7,refs.get(v),0];
 }
 if(Number.isNaN(v))return [6,0,0,0];if(v===Infinity)return [7,0,0,0];if(v===-Infinity)return [8,0,0,0];
 const sign=v<0||Object.is(v,-0)?5:4,n=Math.abs(v);
 return Number.isInteger(v)?[sign,Math.floor(n/2**32),n>>>0,1]:[sign,0,n*2,2];
}
function register(obj){const ref=objects.length;objects.push(obj);refs.set(obj,ref);return ref;}
function snapshot(ref){
 const obj=objects[ref];
 if(!Array.isArray(obj))return [[21,...encode(obj.held)]];
 const fields=Object.keys(obj).map(k=>[Number(k),...encode(obj[k])]);
 fields.push([4294967295,...encode(obj.length)]);return fields;
}
function inspect(ref){cases.push({kind:'inspect',ref,fields:snapshot(ref),next:objects.length});}
function initial(arr){
 const ref=register(arr),fields=snapshot(ref);
 cases.push({kind:'import',ref,fields,next:objects.length});return ref;
}
function command(mode,ref=0,argument=0,value=undefined,values=[]){
 const input=encode(value),inputs=values.map(encode);let output,result=ref;
 if(mode===0){result=register(values.slice());output=objects[result];}
 else if(mode===1)output=objects[ref].length;
 else if(mode===2)output=objects[ref][argument];
 else if(mode===3)output=objects[ref].push(value);
 else if(mode===4){result=register(objects[ref].filter(t=>t!==strings[argument]));output=objects[result];}
 else if(mode===5){result=register(objects[ref].concat([value]));output=objects[result];}
 else if(mode===6)output=objects[ref].includes(strings[argument]);
 else if(mode===7){result=register(objects[ref].slice());output=objects[result];}
 else if(mode===8){objects[ref][argument]=value;output=value;}
 else throw Error('Fixture mode');
 cases.push({kind:'array',mode,ref,argument:mode===0?values.length:argument,
  input,inputs,result,output:encode(output),fields:snapshot(result),next:objects.length});
 return result;
}
// Preserve holes versus present undefined, nested aliases and cyclic values.
const plain=register({held:7});cases.push({kind:'import',ref:plain,fields:snapshot(plain),next:objects.length});
const empty=command(0),nested=command(0,0,0,undefined,[objects[plain],undefined]);
command(3,nested,0,objects[nested]);
const sparse=new Array(9);sparse[1]=undefined;sparse[4]='tackle';sparse[8]=objects[nested];
const ref=initial(sparse);
for(let i=0;i<=10;i++)command(2,ref,i);
command(2,ref,4294967295);command(1,ref);
const filtered=command(4,ref,cat.ids.strings.map.tackle);
const copied=command(7,ref),joined=command(5,ref,0,objects[nested]);
command(3,nested,0,'bide');inspect(ref);inspect(filtered);inspect(copied);inspect(joined);
// Mutating the original changes neither its filter/concat/slice result's cells.
command(3,ref,0,'rollout');inspect(filtered);inspect(copied);inspect(joined);
for(const text of texts){command(6,empty,cat.ids.strings.map[text]);command(6,ref,cat.ids.strings.map[text]);}
const onlyHoles=initial(new Array(6));command(4,onlyHoles,cat.ids.strings.map.tackle);
command(5,onlyHoles,0,undefined);command(7,onlyHoles);
// Numeric key enumeration is ascending, regardless of insertion order.
const shuffled=new Array(8);shuffled[7]='bide';shuffled[2]=null;shuffled[0]=false;
initial(shuffled);
const primitives=[undefined,null,false,true,0,-0,1,-1,0.5,-0.5,4294967295,
 NaN,Infinity,-Infinity,...texts,objects[plain],objects[nested]];
command(0,0,0,undefined,primitives);
for(let n=0;n<1600;n++){
 const arrays=objects.flatMap((v,i)=>Array.isArray(v)&&v.length<40?[i]:[]);
 const at=arrays[pick(arrays.length)],mode=pick(8),text=cat.ids.strings.map[texts[pick(texts.length)]];
 const value=pick(5)===0?objects[arrays[pick(arrays.length)]]:primitives[pick(primitives.length)];
 if(mode===0){const vals=Array.from({length:pick(15)},()=>primitives[pick(primitives.length)]);command(0,0,0,undefined,vals);}
 else if(mode===2)command(mode,at,pick(objects[at].length+3));
 else if(mode===4||mode===6)command(mode,at,text);
 else command(mode,at,0,value);
 if(n%100===0){inspect(ref);inspect(filtered);inspect(copied);inspect(joined);inspect(nested);}
}
// Preserve the entire previous operation/snapshot corpus before adding writes.
for(let i=1;i<objects.length;i++)inspect(i);
for(let n=0;n<512;n++){
 const candidates=objects.flatMap((v,i)=>Array.isArray(v)&&v.length<40?[i]:[]);
 const at=candidates[pick(candidates.length)],index=pick(objects[at].length+5);
 command(8,at,index,primitives[pick(primitives.length)]);
 if(n%64===0){inspect(ref);inspect(filtered);inspect(copied);inspect(joined);inspect(nested);}
}
// Real JS boundary assignment stays sparse and updates the last real index.
const boundary=initial(new Array(4294967295));
command(8,boundary,4294967294,'Normal');command(1,boundary);command(2,boundary,4294967294);
for(let i=1;i<objects.length;i++)inspect(i);
console.log(JSON.stringify({cases,objects:objects.length-1}));
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
    private = [0xFACA0000 + i for i in range(512)]
    words[2176:2688] = private
    words[0], words[32:38] = 40, [0, 37, 0, 0, 0, 0]
    lib.pg9_execute(words)
    assert words[1] == 0
    for index, case in enumerate(fixture["cases"]):
        if case["kind"] == "array":
            words[0], words[32:35] = 49, [case["mode"], case["ref"], case["argument"]]
            words[88:92] = case["input"]
            for i, value in enumerate(case["inputs"]):
                words[160 + i * 4:164 + i * 4] = value
        else:
            importing = case["kind"] == "import"
            words[0], words[32:38] = 40, [8 if importing else 7, case["ref"], len(case["fields"]), 0, 0, 0]
            if importing:
                for i, field in enumerate(case["fields"]):
                    words[8192 + i * 5:8197 + i * 5] = field
        lib.pg9_execute(words)
        assert words[1] == 0, (index, case, words[1])
        ref = case.get("result", case["ref"])
        assert list(words[16:20]) == [ref, case["next"], 37, len(case["fields"])], (index, case, list(words[16:20]))
        actual = [list(words[8192 + i * 5:8197 + i * 5]) for i in range(words[19])]
        assert actual == case["fields"], (index, case, actual)
        if case["kind"] == "array":
            assert list(words[64:68]) == case["output"], (index, case, list(words[64:68]))
        assert list(words[8:12]) == [1, 2, 3, 4]
        assert list(words[2176:2688]) == private

    def import_fields(ref, fields):
        words[0], words[32:38] = 40, [8, ref, len(fields), 0, 0, 0]
        for i, field in enumerate(fields):
            words[8192 + i * 5:8197 + i * 5] = field
        lib.pg9_execute(words)
        assert words[1] == 0

    def inspect(ref):
        words[0], words[32:38] = 40, [7, ref, 0, 0, 0, 0]
        lib.pg9_execute(words)
        assert words[1] == 0
        return list(words[16:20]), list(words[8192:8192 + words[19] * 5])

    # Reject noncanonical imported shapes without mutating them or allocating.
    bad_shapes = [[], [[4294967295, 0, 0, 0, 0]], [[4294967295, 5, 0, 1, 1]],
                  [[4294967295, 4, 0, 1, 2]],
                  [[1, 3, 0, 0, 0], [0, 2, 0, 0, 0], [4294967295, 4, 0, 2, 1]],
                  [[2, 3, 0, 0, 0], [4294967295, 4, 0, 2, 1]],
                  [[4294967295, 4, 0, 1, 1], [0, 3, 0, 0, 0]]]
    for offset, fields in enumerate(bad_shapes):
        ref = 10000 + offset
        import_fields(ref, fields)
        before = inspect(ref)
        for mode in range(1, 9):
            words[0], words[32:35] = 49, [mode, ref, 0]
            words[88:92] = [0, 0, 0, 0]
            lib.pg9_execute(words)
            assert words[1] == 3, (fields, mode, words[1])
            assert inspect(ref) == before
    # JS array length's exact U32 boundary is representable without billions
    # of allocated cells. Growth beyond it is outside the admitted model;
    # this domain error does not emulate JS push's named-property fault side effect.
    high = 11000
    import_fields(high, [[4294967295, 4, 0, 4294967295, 1]])
    before = inspect(high)
    for mode, argument, expected in [(1, 0, [4, 0, 4294967295, 1]),
                                     (2, 4294967295, [0, 0, 0, 0]),
                                     (2, 4294967294, [0, 0, 0, 0])]:
        words[0], words[32:35] = 49, [mode, high, argument]
        lib.pg9_execute(words)
        assert words[1] == 0 and list(words[64:68]) == expected
    for mode in (3, 5):
        words[0], words[32:35] = 49, [mode, high, 0]
        words[88:92] = [0, 0, 0, 0]
        lib.pg9_execute(words)
        assert words[1] == 3 and inspect(high) == before
    # The last real array index remains indexed; MAX itself is a named property
    # and remains outside the admitted own-indexed array model.
    value = [9, 0, cat["ids"]["strings"]["map"]["Normal"], 0]
    words[0], words[32:35], words[88:92] = 49, [8, high, 4294967294], value
    lib.pg9_execute(words)
    assert words[1] == 0 and list(words[64:68]) == value
    high_fields = inspect(high)[1]
    assert high_fields == [4294967294, *value, 4294967295, 4, 0, 4294967295, 1]
    before = inspect(high)
    words[0], words[32:35], words[88:92] = 49, [8, high, 4294967295], value
    lib.pg9_execute(words)
    assert words[1] == 3 and inspect(high) == before
    for mode, ref, argument in [(99, high, 0), (0, 0, 65),
                                (4, high, len(cat["ids"]["strings"]["names"])),
                                (6, high, len(cat["ids"]["strings"]["names"]))]:
        words[0], words[32:35] = 49, [mode, ref, argument]
        lib.pg9_execute(words)
        assert words[1] == 3 and inspect(high) == before
    for mode in range(1, 9):
        words[0], words[32:35] = 49, [mode, 999999, 0]
        lib.pg9_execute(words)
        assert words[1] == 3
    print(f"PASS: {len(fixture['cases'])} independent JavaScript array operations/snapshots; "
          f"{fixture['objects']} retained objects, sparse/dense cells, fresh filters/concat/clones, "
          "undefined versus holes, cyclic/shallow aliases, exact property order and unchanged RNG; "
          "invalid shapes/references and domain limits rejected")


if __name__ == "__main__":
    main()
