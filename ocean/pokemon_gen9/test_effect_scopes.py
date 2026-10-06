#!/usr/bin/env python3
"""Original event bodies over persistent state, advanced independently."""
import ctypes
import json
from pathlib import Path
import subprocess
from native_test_helpers import EffectHeap

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = r"""
const {oracle,Dex}=require('./ocean/pokemon_gen9/reference.cjs');
const {Battle}=require(oracle+'/dist/sim/battle'),{Side}=require(oracle+'/dist/sim/side');
const cat=require('./build/pokemon_gen9/catalog/catalog.json'),dex=Dex.mod('gen9');
const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
const set={species:'Mew',moves:['tackle'],ability:'No Ability'};
b.sides[0]=new Side('one',b,0,[structuredClone(set)]);
b.sides[1]=new Side('two',b,1,[structuredClone(set)]);
const p=b.sides[0].pokemon[0],q=b.sides[1].pokemon[0];
for(const mon of [p,q]){
 mon.isActive=true;mon.side.active[0]=mon;mon.ability='noability';mon.item='';
 mon.abilityState={};mon.volatiles={};mon.hp=200;mon.maxhp=300;
}
const special=new Map([[p,[1,576]],[q,[1,1344]],[p.side,[2,0]],
 [q.side,[2,1]],[b.field,[3,0]],[b,[4,0]]]);
const keys={id:1,target:2,effectOrder:3,counter:4,nested:10,nativeState:13};
const id=(family,name)=>cat.ids[family].map[name]||0;
let objects,references,next;
function encode(v){
 if(v===undefined)return [0,0,0,0];if(v===null)return [1,0,0,0];
 if(typeof v==='boolean')return [v?3:2,0,0,0];
 if(typeof v==='string')return [9,0,id('strings',v),0];
 if(typeof v==='object')return [10,...(special.get(v)||[7,references.get(v)]),0];
 if(Number.isNaN(v))return [6,0,0,0];if(v===Infinity)return [7,0,0,0];if(v===-Infinity)return [8,0,0,0];
 const n=Math.abs(v),sign=v<0||Object.is(v,-0)?5:4;
 return Number.isInteger(v)?[sign,Math.floor(n/2**32),n>>>0,1]:[sign,0,Math.round(n*10),10];
}
const fields=obj=>Object.entries(obj).map(([key,value])=>[keys[key],...encode(value)]);
const originalInit=b.initEffectState;
b.initEffectState=function(obj,order){
 const value=originalInit.call(this,obj,order);
 if(!references.has(value)){references.set(value,next);objects.set(next++,value);}
 return value;
};
const context=[3,7,11,13,10,1,576,0,10,1,1344,0,0,0,0,0,1,6144,0,0,0,0,0,0];
const groups=[];
function resetScope(depth=0,lines=0,suppressed=false){
 b.effect=dex.conditions.getByID(cat.ids.conditions.names[7]);b.effectState=objects.get(11);
 b.event={id:'Parent13',target:p,source:q,effect:undefined,modifier:1.5};b.eventDepth=depth;
 b.log=Array(lines).fill('fixture');b.sentLogPos=0;
 p.volatiles=suppressed?{gastroacid:{}}:{};
}
function record(group,input,work,vector=false){
 let result,error=0;
 try{result=work();}catch(err){
  if(input.depth>=8)error=5;else if(input.lines>1000)error=6;else throw err;
 }
 if(b.effectState!==objects.get(11)||b.eventDepth!==(input.depth||0))throw Error('Scope was not restored');
 const parent=context.slice();parent[18]=input.depth||0;
 group.steps.push({...input,expected:error?[]:vector?result.map(encode):encode(result),error,parent,
  next,counter:b.effectOrder,snapshots:[...objects].map(([ref,obj])=>[ref,fields(obj)]),
  after:b.prng.getSeed()});
}
function single(group,name,key,ref,target=p,depth=0,lines=0,suppressed=false){
 const effect=dex.abilities.get(name),callback=id('callbacks',key.toLowerCase());
 resetScope(depth,lines,suppressed);
 record(group,{op:35,args:[1,id('abilities',name),callback,ref,0,lines],
  event:[callback,...encode(target),...encode(q),0,0,0,0],relay:encode(101),depth,lines,suppressed:+suppressed},
  ()=>b.singleEvent(key.slice(2),effect,ref?objects.get(ref):null,target,q,undefined,101));
}
function run(group,list,vector=false,event='ModifyAtk',fast=false,values=[101,0]){
 resetScope();
 const rows=[],handlers=list.map(([name,key,ref,holder,index=0],i)=>{
  const effect=dex.abilities.get(name),pokemon=holder===p||holder===q,
   holderID=holder===p?576:holder===q?1344:0;
  const [family,identity]=special.get(holder);
  rows.push([1,id('abilities',name),id('callbacks',key.toLowerCase()),ref,holderID,+pokemon,
    i+1,32768,32768,32768,i,0,index,pokemon?0:family,pokemon?0:identity,0]);
  return {effect,callback:effect[key],state:ref?objects.get(ref):null,effectHolder:holder,
   order:i+1,priority:0,speed:0,subOrder:0,effectOrder:i,
   ...(vector?{index,target:index?q:p}:{})};
 });
 // Discovery alone is supplied; pinned runEvent executes all guards,
 // state binding, actual callbacks, ordering, relays and restoration.
 b.findEventHandlers=()=>handlers;
 const callback=id('callbacks',('on'+event).toLowerCase());
 record(group,{op:vector?38:37,args:vector?[rows.length,+fast,2,1]:[rows.length,+fast],rows,
  event:[callback,...(vector?[10,6,0,0]:encode(p)),...encode(q),0,0,0,0],
  relay:encode(101),values:values.map(encode),targets:[576,1344]},
  ()=>b.runEvent(event,vector?[p,q]:p,q,undefined,vector?values.slice():101,false,fast),vector);
}
const counters=[undefined,null,false,true,0,-0,1,5,-1,NaN,Infinity,-Infinity,0.5];
const holders=[p,q,p.side,q.side,b.field,b];
for(let n=0;n<78;n++){
 objects=new Map();references=new WeakMap();next=22;b.effectOrder=17;b.resetRNG([1,2,3,4]);
 const parent={nativeState:11,counter:5},a={nativeState:20,counter:counters[n%counters.length]},
  z={nativeState:21,target:holders[(n+2)%holders.length],counter:counters[(n+4)%counters.length]};
 if(n%3!==0)a.target=holders[n%holders.length];
 if(n%4===0)a.effectOrder=3;
 if(n%5===0)a.id='bide';
 z.nested=a;
 for(const [ref,obj]of [[11,parent],[20,a],[21,z]]){objects.set(ref,obj);references.set(obj,ref);}
 const group={initial:[...objects].map(([ref,obj])=>[ref,fields(obj)]),steps:[]};
 single(group,'slowstart','onModifyAtk',20,holders[(n+1)%holders.length]);
 run(group,[['slowstart','onModifySpe',20,holders[n%holders.length]]]);
 resetScope();record(group,{op:40,args:[3,20,4],value:encode(n%2?0:5)},()=>{a.counter=n%2?0:5;});
 single(group,'slowstart','onModifySpe',20,q);
 run(group,[['battlearmor','onCriticalHit',20,b.field]]);
 run(group,[['slowstart','onModifyAtk',20,p.side]]);
 run(group,[['hugepower','onModifyAtk',20,p,0],['slowstart','onModifySpe',20,q,1]],true,
  n%2?'DamagingHit':'ModifyAtk',false,[101,0]);
 resetScope();record(group,{op:40,args:[5,20,0]},()=>b.clearEffectState(a));
 single(group,'slowstart','onModifyAtk',20,p);
 run(group,[['slowstart','onModifySpe',20,q.side]]);
 single(group,'hugepower','onModifyAtk',0,b.field);
 single(group,'battlearmor','onCriticalHit',0,p);
 single(group,'hugepower','onModifySpA',0,p); // missing: no allocation
 single(group,'hugepower','onModifyAtk',0,p,0,0,true); // suppressed: no allocation
 run(group,[['battlearmor','onCriticalHit',0,b]]); // literal: no allocation
 run(group,[['hugepower','onModifyAtk',0,b.field]]); // empty state + field target
 run(group,[['slowstart','onModifySpe',0,p.side]]); // empty state + side target
 run(group,[['battlearmor','onCriticalHit',21,b],['hugepower','onModifyAtk',20,p]],false,'TryHit',true);
 single(group,'hugepower','onModifyAtk',0,p,8);
 single(group,'hugepower','onModifyAtk',0,p,0,1001);
 groups.push(group);
}
console.log(JSON.stringify(groups));b.destroy();
"""


def main():
    groups = json.loads(subprocess.check_output(["node", "-e", SCRIPT], cwd=ROOT))
    cat = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * cat["word_count"])()
    binary = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, binary, len(binary))
    words[2], words[4] = 1, 0
    heap = EffectHeap(lib, words)
    total = 0
    actions = [0xE5110000 + i for i in range(512)]
    for group in groups:
        words[8:12] = [1, 2, 3, 4]
        heap.reset(17)
        for reference, fields in group["initial"]:
            heap.import_raw(reference, fields)
        for step in group["steps"]:
            for index in range(12):
                at = 576 + index * 128
                words[at + 5] = cat["ids"]["abilities"]["map"]["noability"]
                words[at + 6], words[at + 13], words[at + 81] = 0, 0, 0
                words[at + 23] = (65 if index in (0, 6) else 0) | (4 if index == 0 and step.get("suppressed") else 0)
                words[at + 7], words[at + 8] = 200, 300
            words[537], words[544], words[548], words[549] = step.get("lines", 0), 0, 0, 0
            words[2176:2688] = actions
            words[0], words[32:32 + len(step["args"])] = step["op"], step["args"]
            if step["op"] == 40:
                words[64:68] = step.get("value", [0, 0, 0, 0])
            else:
                words[64:83], words[88:92], words[104:117] = step["parent"][:19], step["relay"], step["event"]
                for index, row in enumerate(step.get("rows", [])):
                    words[8192 + index * 16:8208 + index * 16] = row
                if step["op"] == 38:
                    words[144:146] = step["targets"]
                    for index, value in enumerate(step["values"]):
                        words[160 + index * 4:164 + index * 4] = value
            lib.pg9_execute(words)
            assert words[1] == step["error"], (step, words[1])
            assert list(words[2176:2688]) == actions
            if not step["error"] and step["op"] != 40:
                actual = ([list(words[96 + i * 4:100 + i * 4]) for i in range(2)]
                          if step["op"] == 38 else list(words[96:100]))
                assert actual == step["expected"], (step, actual)
                assert list(words[10216:10240]) == step["parent"], (step, list(words[10216:10240]))
            meta, _ = heap.inspect(0)
            assert meta == [0, step["next"], step["counter"]], (step, meta)
            for reference, expected in step["snapshots"]:
                assert heap.inspect(reference)[1] == expected, (step, reference, heap.inspect(reference)[1])
            assert list(words[8:12]) == [int(x) for x in step["after"].split(",")]
            total += 1
    # Invalid supplied function state fails explicitly instead of inventing an
    # object. Raw state import also rejects existing IDs, zero and overflow.
    for reference in (11, 0, 4294967295):
        words[0], words[32:35] = 40, [8, reference, 0]
        lib.pg9_execute(words)
        assert words[1] == 3
    words[0], words[32:38] = 35, [1, cat["ids"]["abilities"]["map"]["hugepower"],
        cat["ids"]["callbacks"]["map"]["onmodifyatk"], 999999, 0, 0]
    words[64:83], words[104:117], words[88:92] = groups[0]["steps"][0]["parent"][:19], groups[0]["steps"][0]["event"], [4, 0, 101, 1]
    lib.pg9_execute(words)
    assert words[1] == 3
    print(f"PASS: {total} original persistent effect-scope transitions in {len(groups)} independent sequences; "
          "single/run/array holder differences, raw states, aliases, counter changes/clears, literal/skipped handlers, "
          "fallback allocation, parent restoration and exact RNG")


if __name__ == "__main__":
    main()
