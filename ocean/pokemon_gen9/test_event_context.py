#!/usr/bin/env python3
"""Observe original nested callbacks; compare every scope boundary separately."""
import ctypes
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = r"""
const {oracle}=require('./ocean/pokemon_gen9/reference.cjs');
const {Battle}=require(oracle+'/dist/sim/battle');
const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
b.nativeFamily=4;b.nativeID=0;
const cases=[];
function value(x){
 if(x===undefined)return [0,0,0,0];if(x===null)return [1,0,0,0];
 if(typeof x==='boolean')return [x?3:2,0,0,0];
 return [10,x.nativeFamily,x.nativeID,0];
}
function effect(family,id){return {nativeFamily:family,nativeID:id,id:'scope'+id,
 effectType:['Move','Ability','Item','Condition','Condition'][family],flags:{}};}
for(let fixture=0;fixture<192;fixture++){
 let commands=[],snapshots=[],frames=0,last=0,serial=30;
 const initialDepth=fixture%8;
 const rootEffect=effect(fixture%5,fixture%5===4?0:7);
 b.effect=rootEffect;b.effectState={nativeState:11};b.eventDepth=initialDepth;
 b.event={id:'Fixture13',target:fixture%3?b:null,source:null,effect:undefined};
 if(fixture%2)b.event.modifier=fixture%4===1?1:1.5;
 b.log=[];b.sentLogPos=0;
 function fields(){
  const e=b.event,present=+(e.modifier!==undefined);
  return [b.effect.nativeFamily,b.effect.nativeID,b.effectState.nativeState,Number(e.id.slice(7)),
   ...value(e.target),...value(e.source),...value(e.effect),present,
   present?Math.trunc(e.modifier*4096)>>>0:0,b.eventDepth,frames,last,0,0,0];
 }
 const initial=fields();
 const capture=()=>snapshots.push(fields());
 const record=(mode,first=0)=>{const row=Array(24).fill(0);row[0]=mode;row[1]=first;commands.push(row);};
 function inside(level){
  const q=[0,2048,4096,6144,8192,32768,65536][(fixture+level)%7];
  b.chainModify(q/4096);record(6,q);capture();
  const number=[0,1,101,255,65535,1048576][(fixture+level)%6];
  last=b.finalModify(number);record(7,number);capture();
  if(level>0)call((fixture+level)%2,level-1);
  b.chainModify(1.5);record(6,6144);capture();
 }
 function call(run,level){
  const id=serial++,newEffect=effect((fixture+id)%4,id),state={nativeState:id+100};
  const event='Fixture'+id,target=run&&fixture%3===0?null:b;
  const source=fixture%2?false:{nativeFamily:1,nativeID:1344};
  const sourceEffect=fixture%3?{nativeFamily:8,nativeID:id+200}:null;
  const row=[run?1:0,newEffect.nativeFamily,id,state.nativeState,id,
   ...value(target),...value(source),...value(sourceEffect),0,0,0,0,0,0,0];
  commands.push(row);frames++;
  if(!run){
   b.singleEvent(event,newEffect,state,target,source,sourceEffect,undefined,()=>{capture();inside(level);});
  }else{
   const previousFind=b.findEventHandlers;
   let firstRead=0,secondRead=0;
   const callback=()=>{capture();inside(level);};
   const first={effect:newEffect,state,effectHolder:b,priority:2};
   Object.defineProperty(first,'callback',{get(){
    if(firstRead++===0){capture();record(2);const c=commands.at(-1);
     c[1]=newEffect.nativeFamily;c[2]=id;c[3]=state.nativeState;frames++;}
    return callback;
   }});
   const second={effect:newEffect,state,effectHolder:b,priority:1};
   Object.defineProperty(second,'callback',{get(){
    if(secondRead++===0){frames--;record(5);capture();}
    return undefined;
   }});
   b.findEventHandlers=()=>[first,second];
   b.runEvent(event,target,source,sourceEffect);
   b.findEventHandlers=previousFind;
  }
  frames--;record(run?4:3);capture();
 }
 call(fixture%2,Math.min(2,7-initialDepth));
 if(commands.length>64||commands.length!==snapshots.length)throw Error('Bad fixture transport');
 if(b.prng.getSeed()!=='1,2,3,4')throw Error('Context consumed RNG');
 cases.push({initial,commands,snapshots,error:0});
}
for(const mode of [0,1])for(const depth of [0,7,8,9])for(const lines of [0,1000,1001]){
 b.effect=effect(3,7);b.effectState={nativeState:11};b.event={id:'Fixture13',target:b,source:null,effect:undefined};
 b.eventDepth=depth;b.log=Array(lines).fill('fixture');b.sentLogPos=0;
 const initial=[3,7,11,13,...value(b),...value(null),...value(undefined),0,0,depth,0,0,0,0,0];
 const command=[mode,3,30,130,30,...value(b),...value(null),...value(undefined),lines,0,0,0,0,0,0];
 let error=0;
 const e=effect(3,30);
 try{
  if(mode){b.findEventHandlers=()=>[];b.runEvent('Fixture30',b,null,undefined);}
  else b.singleEvent('Fixture30',e,{nativeState:130},b,null,undefined,undefined,()=>{});
 }catch(err){error=depth>=8?5:6;}
 // Successful cases were already checked with callbacks above; isolate source faults.
 if(error)cases.push({initial,commands:[command],snapshots:[],error});
}
console.log(JSON.stringify(cases));b.destroy();
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
    words[2], words[8:12] = 1, [1, 2, 3, 4]
    boundaries = 0
    for case in cases:
        words[0], words[32], words[64:83] = 33, len(case["commands"]), case["initial"][:19]
        for index, row in enumerate(case["commands"]):
            assert len(row) == 24, row
            words[8192 + index * 24:8192 + (index + 1) * 24] = row
        lib.pg9_execute(words)
        assert words[1] == case["error"], (case, words[1])
        assert list(words[10216:10240]) == case["initial"]
        assert words[16] == len(case["snapshots"])
        for index, expected in enumerate(case["snapshots"]):
            actual = list(words[10240 + index * 24:10240 + (index + 1) * 24])
            assert actual == expected, (case, index, actual, expected)
            boundaries += 1
        assert list(words[8:12]) == [1, 2, 3, 4]
    initial = cases[0]["initial"]
    for mode in (3, 4, 5, 8):
        words[0], words[32], words[64:83] = 33, 1, initial[:19]
        words[8192:8216] = [mode] + [0] * 23
        lib.pg9_execute(words)
        assert words[1] == 3 and words[16] == 0
    run_entry = next(row for case in cases for row in case["commands"] if row[0] == 1)
    words[0], words[32], words[64:83] = 33, 2, initial[:19]
    words[8192:8216], words[8216:8240] = run_entry, [3] + [0] * 23
    lib.pg9_execute(words)
    assert words[1] == 3 and words[16] == 1
    words[0], words[32] = 33, 65
    lib.pg9_execute(words)
    assert words[1] == 3
    print(f"PASS: {len(cases)} source nested event traces, {boundaries} scope/modifier boundaries; "
          "single/run/handler restoration, source fault precedence, empty/mismatched frame rejection and exact RNG")


if __name__ == "__main__":
    main()
