#!/usr/bin/env python3
"""Original scalar runEvent after collection; source body/order/RNG snapshots."""
import ctypes
import json
from pathlib import Path
import subprocess
from native_test_helpers import EffectHeap

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = r"""
const {oracle,Dex}=require('./ocean/pokemon_gen9/reference.cjs');
const {Battle}=require(oracle+'/dist/sim/battle');
const {Side}=require(oracle+'/dist/sim/side');
const cat=require('./build/pokemon_gen9/catalog/catalog.json');
const manifest=require('./ocean/pokemon_gen9/PORTED_CALLBACKS.json');
const dex=Dex.mod('gen9'),b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
const raw={species:'Mew',moves:['tackle'],ability:'No Ability'};
const s0=new Side('one',b,0,[structuredClone(raw)]),s1=new Side('two',b,1,[structuredClone(raw)]);
b.sides[0]=s0;b.sides[1]=s1;s0.foe=s1;s1.foe=s0;const p=s0.pokemon[0],q=s1.pokemon[0];
const originalDiscovery=b.findEventHandlers;
const families=['moves','abilities','items','conditions'];
const id=(family,name)=>cat.ids[family].map[name]||0;
const effect=(family,name)=>family==='moves'?dex.moves.get(name):family==='abilities'?dex.abilities.get(name):
 family==='items'?dex.items.get(name):dex.conditions.getByID(name);
const decode=w=>w[0]===0?undefined:w[0]===1?null:w[0]===2?false:w[0]===3?true:w[0]===6?NaN:
 w[0]===7?Infinity:w[0]===8?-Infinity:(w[0]===5?-1:1)*(w[1]*2**32+w[2])/w[3];
const encode=v=>{
 if(v===undefined)return [0,0,0,0];if(v===null)return [1,0,0,0];
 if(typeof v==='boolean')return [v?3:2,0,0,0];if(typeof v==='string')return [9,0,id('strings',v),0];
 if(typeof v==='object')return [10,v===p||v===q?1:v===s0||v===s1?2:v===b.field?3:4,
  v===p?576:v===q?1344:v===s1?1:0,0];
 if(Number.isNaN(v))return [6,0,0,0];if(v===Infinity)return [7,0,0,0];if(v===-Infinity)return [8,0,0,0];
 const negative=v<0||Object.is(v,-0),magnitude=Math.abs(v);
 return Number.isInteger(v)?[negative?5:4,Math.floor(magnitude/2**32),magnitude>>>0,1]:
  [negative?5:4,0,Math.round(magnitude*10),10];
};
const numericBodies=manifest.bodies.filter(entry=>!['boost_mutation','type_array','type_weather_direct'].includes(entry.operation));
const numeric=numericBodies.slice();
for(const entry of numericBodies)if(entry.family==='abilities'||entry.family==='items')
 numeric.push({...entry,family:'conditions',id:(entry.family==='abilities'?'ability:':'item:')+entry.id});
const literals=[];
for(const family of families)for(const name of cat.ids[family].names.slice(1)){
 const e=effect(family,name);
 for(const key of cat.ids.callbacks.original.slice(1))
  if(['boolean','number','string'].includes(typeof e[key]))literals.push({family,id:name,callback:key,value:e[key]});
}
const seen=new Set();
const distinct=literals.filter(entry=>{const key=typeof entry.value+':'+entry.value;
 if(seen.has(key))return false;seen.add(key);return true;});
const scalarPool=[...numeric.filter(entry=>entry.operation==='chain'),...distinct];
const numericPool=[...numeric,...literals.filter(entry=>entry.value===false||
 typeof entry.value==='number'&&Number.isInteger(entry.value)&&entry.value>=0)];
let random=0xabe99175;
const pick=n=>{random=(Math.imul(random,1664525)+1013904223)>>>0;return random%n;};
const cases=[];
function fixture(n,count,event,fast,sodium=false,scalar=false,depth=0){
 const seed=[n,7,11,23];
 if(sodium){const bytes=Buffer.alloc(16);bytes.writeUInt32LE(n);b.resetRNG('sodium,'+bytes.toString('hex'));}
 else b.resetRNG(seed);
 const before=b.prng.getSeed();
 const flags=[65,64,67,69,73,81,97,105][n%8];
 const ability=n%3===0?'quickfeet':n%3===1?'levitate':'klutz';
 const item=n%3===0?'abilityshield':'assaultvest',other=n%2?'neutralizinggas':'noability';
 function set(mon,side,abi,it,f,creation){
  mon.ability=abi;mon.item=it;mon.isActive=!!(f&1);mon.transformed=!!(f&2);mon.fainted=false;
  mon.volatiles={};mon.abilityState={ending:!!(f&32),effectOrder:creation};
  for(const [key,bit] of [['gastroacid',4],['embargo',8],['commanding',16]])if(f&bit)mon.volatiles[key]={};
  side.active[0]=f&64?mon:null;
 }
 set(p,s0,ability,item,flags,17);set(q,s1,other,'',65,23);
 p.maxhp=q.maxhp=300;p.hp=[0,150,151,300][n%4];q.hp=[300,151,150,0][n%4];
 p.status=n%3===0?'par':'tox';q.status=n%4===0?'par':'tox';
 b.field.pseudoWeather=n%5===0?{magicroom:{}}:{};
 b.activePokemon=n%3===0?p:n%3===1?q:null;b.activeMove=n%2?{ignoreAbility:true}:null;
 b.effect={id:'parent',effectType:'Condition'};b.effectState={nativeState:11};b.eventDepth=depth;
 b.event={id:'Parent13',target:p,source:q,effect:undefined,modifier:1.5};
 const parent=b.event,parentEffect=b.effect,parentState=b.effectState;
 const context=[3,7,11,13,10,1,576,0,10,1,1344,0,0,0,0,0,1,6144,depth,0,0,0,0,0];
 const relay=scalar?[[0,0,0,0],[1,0,0,0],[2,0,0,0],[3,0,0,0],[4,0,0,1],[6,0,0,0],
   [7,0,0,0],[8,0,0,0]][n%8]:[4,0,[0,1,101,255,65535,1048576,4294967295][n%7],1];
 const target=scalar&&n%4===0?null:p;
 const pool=scalar?scalarPool:numericPool,rows=[],visited=[];
 const handlers=Array.from({length:count},(_,index)=>{
  const entry=pool[pick(pool.length)],e=effect(entry.family,entry.id),original=e[entry.callback];
  const nonPokemon=entry.family==='conditions'&&!entry.id.startsWith('ability:')&&!entry.id.startsWith('item:')&&entry.id!=='par';
  const holder=nonPokemon?b:index%3===0?q:p,at=holder===b?0:holder===p?576:1344;
  const state={nativeState:200+index};
  const priority=n%4===0?0:pick(5)-2,speed=nonPokemon?0:n%4===0?50:50+pick(3);
  const order=n%4===0?0:[0,1,2,10][pick(4)],sub=n%4===0?0:pick(3),creation=n%4===0?0:pick(3);
  rows.push([families.indexOf(entry.family),id(entry.family,entry.id),id('callbacks',entry.callback.toLowerCase()),
   state.nativeState,at,+!nonPokemon,order,32768+priority*10,32768+speed*2,32768+sub,creation,
   nonPokemon?0:holder.abilityState.effectOrder+1,0,0,0,0]);
  const h={effect:e,state,effectHolder:holder,order,priority,speed,subOrder:sub,effectOrder:creation};
  let first=true;
  Object.defineProperty(h,'callback',{get(){if(first){visited.push(index);first=false;}return original;}});
  return h;
 });
 // Replace collection only; original ordering, guards, actual callback bodies,
 // argument relays, modifier finalization and scope restoration execute.
 b.findEventHandlers=(target,name,source)=>name===event?handlers:originalDiscovery.call(b,target,name,source);
 b.log=Array(n%3===0?1200:0).fill('fixture');b.sentLogPos=0;
 let value,error=0;
 try{value=b.runEvent(event,target,q,undefined,decode(relay),false,fast);}
 catch(err){if(err.message==='Stack overflow'&&b.eventDepth>=8)error=5;else throw err;}
 if(!error&&(b.event!==parent||b.effect!==parentEffect||b.effectState!==parentState||b.eventDepth!==depth))
  throw Error('Source failed to restore scope');
 cases.push({rows,event:id('callbacks',('on'+event).toLowerCase()),fast:+fast,context,relay,
  target:target?[10,1,576,0]:[1,0,0,0],expected:encode(value),visited,error,before,after:b.prng.getSeed(),
  p:[id('abilities',ability),id('items',item),flags,id('conditions',p.status)],
  q:[id('abilities',other),0,65,id('conditions',q.status)],magic:+!!b.field.pseudoWeather.magicroom,
  states:Object.fromEntries(handlers.map(h=>[h.state.nativeState,
   Object.entries(h.state).map(([key,v])=>[key==='target'?2:13,...encode(v)])])),
  actor:n%3===0?576:n%3===1?1344:0,ignore:+!!b.activeMove,lines:b.log.length-b.sentLogPos,
  species:id('species','mew'),hp:[p.hp,q.hp],maxhp:300});
}
const events=['ModifySpe','TryHit','Invulnerability','DamagingHit','EntryHazard'];
for(let n=0;n<256;n++)fixture(n,n<65?n:pick(65),events[n%5],n%2===0,n%3===0,false);
for(let n=0;n<128;n++)fixture(n+300,pick(25),events[n%5],n%2===0,n%3===0,true);
for(const depth of [7,8,9])for(const scalar of [false,true])for(const fast of [false,true])
 fixture(700+depth,12,'ModifySpe',fast,true,scalar,depth);
console.log(JSON.stringify(cases));b.destroy();
"""


def limbs(seed):
    data = bytes.fromhex(seed.split(",")[1])
    return [int.from_bytes(data[i:i + 4], "little") for i in range(0, 32, 4)]


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
    heap = EffectHeap(lib, words)
    total = 0
    actions = [(0xAC10C000 + i) & 0xFFFFFFFF for i in range(512)]
    for case in cases:
        heap.reset()
        heap.import_raw(11, [[13, 4, 0, 11, 1]])
        for row in case["rows"]:
            heap.import_raw(row[3], [[13, 4, 0, row[3], 1]])
        if case["before"].startswith("sodium,"):
            words[0], words[8:16] = 24, limbs(case["before"])
            lib.pg9_execute(words)
            assert words[1] == 0
        else:
            words[4], words[8:12] = 0, [int(x) for x in case["before"].split(",")]
        for index in range(12):
            at = 576 + index * 128
            facts = case["p"] if index == 0 else case["q"] if index == 6 else [0, 0, 0, 0]
            words[at + 5], words[at + 6], words[at + 23], words[at + 13] = facts
            words[at + 81] = 0
            words[at + 1], words[at + 2], words[at + 7], words[at + 8] = (
                case["species"], case["species"], case["hp"][int(index == 6)] if index in (0, 6) else 0, case["maxhp"])
        words[537], words[544], words[548], words[549] = case["lines"], case["magic"], case["actor"], case["ignore"]
        words[0], words[32:34] = 37, [len(case["rows"]), case["fast"]]
        words[64:83], words[88:92] = case["context"][:19], case["relay"]
        words[104:117] = [case["event"], *case["target"], 10, 1, 1344, 0, 0, 0, 0, 0]
        words[2176:2688] = actions
        for index, row in enumerate(case["rows"]):
            words[8192 + index * 16:8192 + (index + 1) * 16] = row
        lib.pg9_execute(words)
        assert words[1] == case["error"], (case, words[1])
        assert list(words[2176:2688]) == actions
        if not case["error"]:
            assert list(words[96:100]) == case["expected"], (case, list(words[96:100]))
            assert list(words[10216:10240]) == case["context"], (case, list(words[10216:10240]))
            assert words[16] == len(case["visited"]), (case, words[16])
            assert list(words[13824:13824 + words[16]]) == case["visited"], (case, list(words[13824:13824 + words[16]]))
            total += words[16]
        if case["after"].startswith("sodium,"):
            assert list(words[560:568]) == limbs(case["after"]), (case, list(words[560:568]))
        else:
            assert list(words[8:12]) == [int(x) for x in case["after"].split(",")]
        for row in case["rows"]:
            fields = case["states"][str(row[3])]
            assert heap.inspect(row[3])[1] == fields, (row, fields)
    words[0], words[32:34] = 37, [65, 0]
    lib.pg9_execute(words)
    assert words[1] == 3
    print(f"PASS: {len(cases)} original scalar runEvent cases, {total} exact listener executions, "
          "all source sort modes, both RNG streams, suppression/early exits, scope restoration and preserved action queue")


if __name__ == "__main__":
    main()
