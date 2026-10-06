#!/usr/bin/env python3
"""Showdown's scalar relay decisions, preserving each JavaScript value kind."""
import ctypes
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = r"""
const {oracle}=require('./ocean/pokemon_gen9/reference.cjs');
const {Battle}=require(oracle+'/dist/sim/battle');
const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
const values=[[0,0,0,0],[1,0,0,0],[2,0,0,0],[3,0,0,0],
 [4,0,0,1],[5,0,0,1],[4,0,1,1],[5,0,1,1],[4,0,1,2],[5,0,1,2],
 [4,0,3,2],[4,0,2147483648,1],[4,0,4294967295,1],[4,1,1,1],
 [4,65535,4294967295,1],[4,0,1048576,1],[6,0,0,0],[7,0,0,0],[8,0,0,0],
 [9,0,0,0],[9,0,1,0],[10,0,0,0],[10,3,17,0]];
const decode=w=>{
 switch(w[0]){
 case 0:return undefined;case 1:return null;case 2:return false;case 3:return true;
 case 4:case 5:return (w[0]===5?-1:1)*(w[1]*2**32+w[2])/w[3];
 case 6:return NaN;case 7:return Infinity;case 8:return -Infinity;
 case 9:return w[2]?'text'+w[2]:'';case 10:return {family:w[1],id:w[2]};
 }};
const encode=v=>{
 if(v===undefined)return {tag:0};if(v===null)return {tag:1};
 if(typeof v==='boolean')return {tag:v?3:2};
 if(typeof v==='number'){
  if(Number.isNaN(v))return {tag:6};if(v===Infinity)return {tag:7};if(v===-Infinity)return {tag:8};
  return {tag:v<0||Object.is(v,-0)?5:4,value:Math.abs(v)};
 }
 if(typeof v==='string')return {tag:9,id:v?Number(v.slice(4)):0};
 return {tag:10,family:v.family,id:v.id};
};
const effect={id:'fixture',effectType:'Condition',onFixture:undefined};
const originalModify=b.modify;
const cases=[];
function fixture(mode,old,returned,fast=0,modifier=4096,tag=4,payload=0){
 let entered=0,result;
 b.eventDepth=0;b.log=[];b.sentLogPos=0;
 b.modify=mode===4?originalModify:x=>x;
 if(mode===0||mode===2){
  effect.onFixture=()=>mode===0?undefined:decode(returned);
  result=b.singleEvent('Fixture',effect,{},b,null,null,decode(old));
 }else{
  const first=()=>{entered++;if(mode===4)b.event.modifier=modifier/4096;return decode(returned);};
  const second=()=>{entered++;return undefined;};
  b.findEventHandlers=()=>mode===1?[]:[{effect,callback:first,state:{},effectHolder:b,priority:2},
   {effect,callback:second,state:{},effectHolder:b,priority:1}];
  result=b.runEvent('Fixture',b,null,null,decode(old),false,!!fast);
 }
 cases.push({mode,old:mode===4?(returned[0]===0?values[3]:returned):old,returned,fast,modifier,tag,payload,expected:encode(result),
  stopped:mode===3?+(entered===1):0,truthy:+!!result});
}
for(const value of values){fixture(0,value,values[0]);fixture(1,value,values[0]);}
for(const old of values){
 const singleOld=old[0]===0?values[3]:old;
 const runOld=old[0]<=1?values[3]:old;
 for(const returned of values){fixture(2,singleOld,returned);
  for(let fast=0;fast<2;fast++)fixture(3,runOld,returned,fast);
 }
}
for(const value of values)for(const modifier of [0,1,2048,2049,4095,4096,6144,8192,32768,65535,4294967295])
 fixture(4,values[3],value,0,modifier);
let state=0x9ab34567;
const word=()=>state=(Math.imul(state,1664525)+1013904223)>>>0;
for(let i=0;i<256;i++){
 const value=[4,word()&65535,word(),1];
 for(const q of [word(),4096,32768,4294967295])fixture(4,values[3],value,0,q);
}
// Raw constants really found in this revision, not guessed callback results.
const cat=require('./build/pokemon_gen9/catalog/catalog.json');
const data=require(oracle+'/dist/data/abilities').Abilities;
for(const [name,entry] of Object.entries(data))for(const [key,value] of Object.entries(entry)){
 if(!key.startsWith('on')||typeof value==='function'||key.endsWith('Order')||
  (key.endsWith('Priority')&&key!=='onFractionalPriority'&&key!=='onModifyPriority'))continue;
 let tag,payload;
 if(typeof value==='boolean'){tag=1;payload=+value;}
 else if(typeof value==='number'){tag=value<0?5:2;payload=value<0?32768+Math.round(value*10):value;}
 else if(typeof value==='string'){tag=3;payload=cat.ids.strings.map[value];}
 else continue;
 effect.onFixture=value;
 const result=b.singleEvent('Fixture',effect,{},b,null,null,true);
 const expected=typeof value==='string'?{tag:9,id:payload}:encode(result);
 cases.push({mode:5,old:values[3],returned:values[0],fast:0,modifier:4096,tag,payload,
  expected,stopped:0,truthy:+!!result});
}
console.log(JSON.stringify(cases));b.destroy();
"""


def decoded(words):
    tag, high, low, denominator = words
    if tag in (4, 5):
        return {"tag": tag, "value": (high * 2**32 + low) / denominator}
    if tag == 9:
        return {"tag": tag, "id": low}
    if tag == 10:
        return {"tag": tag, "family": high, "id": low}
    return {"tag": tag}


def main():
    cases = json.loads(subprocess.check_output(["node", "-e", SCRIPT], cwd=ROOT))
    cat = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * cat["word_count"])()
    data = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, data, len(data))
    words[2], words[8:12] = 1, [1, 2, 3, 4]
    for case in cases:
        words[0] = 32
        words[32:37] = [case[key] for key in ("mode", "fast", "modifier", "tag", "payload")]
        words[64:68], words[68:72] = case["old"], case["returned"]
        lib.pg9_execute(words)
        assert words[1] == 0, (case, words[1])
        actual = decoded(list(words[64:68]))
        assert actual == case["expected"], (case, actual)
        assert words[16] == case["stopped"] and words[17] == case["truthy"], (case, list(words[16:18]))
        assert list(words[8:12]) == [1, 2, 3, 4]
    for mode, fast, tag, payload, code in [(6, 0, 4, 0, 3), (0, 2, 4, 0, 3),
                                            (5, 0, 0, 0, 4), (5, 0, 6, 0, 3)]:
        words[0], words[32:37], words[64:72] = 32, [mode, fast, 4096, tag, payload], [3, 0, 0, 0] * 2
        lib.pg9_execute(words)
        assert words[1] == code
    for value in [[11, 0, 0, 0], [4, 65536, 0, 1], [5, 0, 1, 0]]:
        words[0], words[32:37], words[64:68], words[68:72] = 32, [0, 0, 4096, 4, 0], value, [0, 0, 0, 0]
        lib.pg9_execute(words)
        assert words[1] == 3
    print(f"PASS: {len(cases)} source scalar event values, falsy distinctions, undefined continuation, "
          "fast exit, signed zero, non-finite/fractional/final modifiers and raw constants; unsupported body rejection")


if __name__ == "__main__":
    main()
