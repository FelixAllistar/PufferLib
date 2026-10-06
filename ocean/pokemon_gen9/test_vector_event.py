#!/usr/bin/env python3
"""Independent array-target runEvent, retaining the source relay-array rules."""
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
const manifest=require('./ocean/pokemon_gen9/PORTED_CALLBACKS.json');
const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
const set={species:'Mew',moves:['tackle'],ability:'No Ability'};
b.sides[0]=new Side('one',b,0,[structuredClone(set)]);
b.sides[1]=new Side('two',b,1,[structuredClone(set)]);
const p=b.sides[0].pokemon[0],q=b.sides[1].pokemon[0];
b.sides[0].foe=b.sides[1];b.sides[1].foe=b.sides[0];
const originalDiscovery=b.findEventHandlers;
for(const mon of [p,q]){mon.isActive=true;mon.side.active[0]=mon;mon.status='par';
 mon.abilityState={effectOrder:17};mon.volatiles={};}
p.ability='quickfeet';q.ability='levitate';p.item='assaultvest';q.item='';
p.maxhp=300;p.hp=200;q.maxhp=301;q.hp=100;
const families=['moves','abilities','items','conditions'];
const id=(family,name)=>cat.ids[family].map[name]||0;
const effect=(family,name)=>family==='conditions'?dex.conditions.getByID(name):dex[family].get(name);
function encode(v){
 if(v===undefined)return [0,0,0,0];if(v===null)return [1,0,0,0];
 if(typeof v==='boolean')return [v?3:2,0,0,0];if(typeof v==='string')return [9,0,id('strings',v),0];
 if(typeof v==='object')return [10,v===p||v===q?1:v===p.side||v===q.side?2:v===b.field?3:4,
  v===p?576:v===q?1344:v===q.side?1:0,0];
 if(Number.isNaN(v))return [6,0,0,0];if(v===Infinity)return [7,0,0,0];if(v===-Infinity)return [8,0,0,0];
 const sign=v<0||Object.is(v,-0)?5:4,a=Math.abs(v);
 return Number.isInteger(v)?[sign,Math.floor(a/2**32),a>>>0,1]:[sign,0,Math.round(a*10),10];
}
const literals=[];
for(const family of families)for(const name of cat.ids[family].names.slice(1)){
 const e=effect(family,name);
 for(const callback of cat.ids.callbacks.original.slice(1))
  if(['boolean','number','string'].includes(typeof e[callback]))literals.push({family,id:name,callback});
}
const numeric=manifest.bodies.filter(entry=>!['boost_mutation','type_array','type_weather_direct'].includes(entry.operation));
const scalar=[...numeric.filter(x=>x.operation==='chain'),...literals];
numeric.push(...literals.filter(x=>{const v=effect(x.family,x.id)[x.callback];
 return v===false||typeof v==='number'&&v>=0&&Number.isInteger(v);}));
let random=0x2947b81e;
const pick=n=>{random=(Math.imul(random,1664525)+1013904223)>>>0;return random%n;};
const events=['ModifySpe','TryHit','Invulnerability','DamagingHit','EntryHazard'];
const cases=[];
function fixture(n,size,count,event,fast,mode,depth=0){
 const bytes=Buffer.alloc(16);bytes.writeUInt32LE(n);
 b.resetRNG(n%2?'sodium,'+bytes.toString('hex'):[n,7,11,23]);
 const before=b.prng.getSeed();
 const targets=Array.from({length:size},(_,i)=>i%2?q:p);
 const initial=mode===0?Array.from({length:size},(_,i)=>[0,1,101,255,65535][(n+i)%5]):
  mode===1?Array.from({length:size},(_,i)=>[false,0,-0,null,undefined,NaN,true,Infinity,-Infinity][(n+i)%9]):
  Array(size).fill(true);
 const relay=mode<2?initial.slice():mode===2?undefined:mode===3?null:73;
 const hasRelay=relay!==undefined&&relay!==null;
 const context=[3,7,11,13,10,1,576,0,10,1,1344,0,0,0,0,0,1,6144,depth,0,0,0,0,0];
 b.effect={id:'parent',effectType:'Condition'};b.effectState={nativeState:11};b.eventDepth=depth;
 b.event={id:'Parent13',target:p,source:q,effect:undefined,modifier:1.5};
 const parent=b.event,parentEffect=b.effect,parentState=b.effectState;
 const rows=[],visited=[],pool=mode===0?numeric:scalar;
 const handlers=Array.from({length:count},(_,index)=>{
  const entry=pool[pick(pool.length)],e=effect(entry.family,entry.id),original=e[entry.callback];
  const targetIndex=pick(size),target=targets[targetIndex];
  const plain=entry.family==='conditions'&&entry.id!=='par';
  const holder=plain?b:index%3?q:p,state={nativeState:200+index};
  const priority=pick(5)-2,speed=plain?0:50+pick(3),order=[0,1,2,10][pick(4)],sub=pick(3),creation=pick(3);
  rows.push([families.indexOf(entry.family),id(entry.family,entry.id),id('callbacks',entry.callback.toLowerCase()),
   state.nativeState,holder===b?0:holder===p?576:1344,+!plain,order,32768+priority*10,32768+speed*2,
   32768+sub,creation,plain?0:18,targetIndex,0,0,0]);
  const h={effect:e,state,effectHolder:holder,order,priority,speed,subOrder:sub,effectOrder:creation,index:targetIndex,target};
  let first=true;Object.defineProperty(h,'callback',{get(){if(first){visited.push(index);first=false;}
   if(b.event.target!==target)throw Error('Target binding mismatch in source');return original;}});
  return h;
 });
 b.findEventHandlers=(target,name,source)=>name===event?handlers:originalDiscovery.call(b,target,name,source);
 const inputValues=initial.map(encode);
 let value,error=0;
 try{value=b.runEvent(event,targets,q,undefined,relay,false,fast);}
 catch(err){if(err.message==='Stack overflow'&&b.eventDepth>=8)error=5;else throw err;}
 if(!error&&(b.event!==parent||b.effect!==parentEffect||b.effectState!==parentState||b.eventDepth!==depth))
  throw Error('Source failed to restore scope');
 cases.push({rows,event:id('callbacks',('on'+event).toLowerCase()),fast:+fast,hasRelay:+hasRelay,context,
  targets:targets.map(mon=>mon===p?576:1344),values:inputValues,expected:error?[]:value.map(encode),
  visited,error,before,after:b.prng.getSeed(),
  states:Object.fromEntries(handlers.map(h=>[h.state.nativeState,
   Object.entries(h.state).map(([key,v])=>[key==='target'?2:13,...encode(v)])]))});
}
for(let n=0;n<320;n++){
 const size=n%17===0?12:1+pick(2),count=n<65?n:pick(65);
 fixture(n,size,count,events[n%5],n%2===0,n%5);
}
for(const depth of [7,8,9])for(const mode of [0,1,2,3,4])
 fixture(500+depth,2,12,'DamagingHit',mode%2===0,mode,depth);
fixture(999,0,0,'TryHit',false,2);
console.log(JSON.stringify(cases));b.destroy();
"""


def seed_limbs(seed):
    data = bytes.fromhex(seed.split(",")[1])
    return [int.from_bytes(data[i:i + 4], "little") for i in range(0, 32, 4)]


def main():
    cases = json.loads(subprocess.check_output(["node", "-e", SCRIPT], cwd=ROOT))
    cat = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * cat["word_count"])()
    binary = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, binary, len(binary))
    words[2] = 1
    heap = EffectHeap(lib, words)
    actions = [0xA117C000 + i for i in range(512)]
    executions = 0
    for case in cases:
        heap.reset()
        heap.import_raw(11, [[13, 4, 0, 11, 1]])
        for row in case["rows"]:
            heap.import_raw(row[3], [[13, 4, 0, row[3], 1]])
        if case["before"].startswith("sodium,"):
            words[0], words[8:16] = 24, seed_limbs(case["before"])
            lib.pg9_execute(words)
            assert words[1] == 0
        else:
            words[4], words[8:12] = 0, [int(x) for x in case["before"].split(",")]
        for index in range(12):
            at = 576 + index * 128
            words[at + 5] = cat["ids"]["abilities"]["map"]["quickfeet" if index == 0 else "levitate"]
            words[at + 6] = cat["ids"]["items"]["map"]["assaultvest"] if index == 0 else 0
            words[at + 13] = cat["ids"]["conditions"]["map"]["par"]
            words[at + 23], words[at + 81] = (65 if index in (0, 6) else 0), 0
            words[at + 1], words[at + 2] = [cat["ids"]["species"]["map"]["mew"]] * 2
            words[at + 7], words[at + 8] = (200, 300) if index == 0 else (100, 301)
        words[544], words[548], words[549] = 0, 0, 0
        words[0], words[32:36] = 38, [len(case["rows"]), case["fast"], len(case["targets"]), case["hasRelay"]]
        words[64:83] = case["context"][:19]
        words[104:117] = [case["event"], 10, 6, 0, 0, 10, 1, 1344, 0, 0, 0, 0, 0]
        words[144:144 + len(case["targets"])] = case["targets"]
        for index, value in enumerate(case["values"]):
            words[160 + index * 4:164 + index * 4] = value
        for index, row in enumerate(case["rows"]):
            words[8192 + index * 16:8208 + index * 16] = row
        words[2176:2688] = actions
        lib.pg9_execute(words)
        assert words[1] == case["error"], (case, words[1])
        assert list(words[2176:2688]) == actions
        if not case["error"]:
            actual = [list(words[96 + index * 4:100 + index * 4]) for index in range(words[18])]
            assert actual == case["expected"], (case, actual)
            assert list(words[10216:10240]) == case["context"], (case, list(words[10216:10240]))
            assert words[16] == len(case["visited"]), (case, words[16])
            assert list(words[13824:13824 + words[16]]) == case["visited"]
            executions += words[16]
        if case["after"].startswith("sodium,"):
            assert list(words[560:568]) == seed_limbs(case["after"])
        else:
            assert list(words[8:12]) == [int(x) for x in case["after"].split(",")]
        for row in case["rows"]:
            fields = case["states"][str(row[3])]
            assert heap.inspect(row[3])[1] == fields, (row, fields)
    for count, size, flag in [(65, 2, 1), (0, 13, 1), (0, 2, 2)]:
        words[0], words[32:36] = 38, [count, 0, size, flag]
        lib.pg9_execute(words)
        assert words[1] == 3
    print(f"PASS: {len(cases)} original array-target runEvent cases, {executions} exact listener executions; "
          "indexed relays, DamagingHit zero exception, all sort modes, target binding, both RNGs and scope restoration")


if __name__ == "__main__":
    main()
