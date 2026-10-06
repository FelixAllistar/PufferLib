#!/usr/bin/env python3
"""Execute pinned source functions and native bodies independently."""
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
const id=(family,name)=>cat.ids[family].map[name]||0;
const values=[[4,0,0,1],[4,0,1,1],[4,0,101,1],[4,0,255,1],[4,0,65535,1],
 [4,0,1048576,1],[4,0,4294967295,1],[5,0,1,1],[6,0,0,0],[7,0,0,0],[8,0,0,0]];
const decode=w=>w[0]===0?undefined:w[0]===1?null:w[0]===2?false:w[0]===3?true:
 w[0]===6?NaN:w[0]===7?Infinity:w[0]===8?-Infinity:(w[0]===5?-1:1)*w[2];
const encode=v=>{
 if(v===undefined)return [0,0,0,0];if(v===null)return [1,0,0,0];
 if(typeof v==='boolean')return [v?3:2,0,0,0];
 if(typeof v==='string')return [9,0,id('strings',v),0];
 if(Number.isNaN(v))return [6,0,0,0];if(v===Infinity)return [7,0,0,0];if(v===-Infinity)return [8,0,0,0];
 const magnitude=Math.abs(v),negative=v<0||Object.is(v,-0);
 if(!Number.isInteger(v))return [negative?5:4,0,Math.round(magnitude*10),10];
 return [negative?5:4,Math.floor(magnitude/2**32),magnitude>>>0,1];
};
const effect=(family,name)=>family==='moves'?dex.moves.get(name):family==='abilities'?dex.abilities.get(name):
 family==='items'?dex.items.get(name):dex.conditions.getByID(name);
const cases=[];
function fixture(family,name,key,relay,mod,index=0,single=false,override=''){
 const kind=['moves','abilities','items','conditions'].indexOf(family),eid=id(family,name),e=effect(family,name);
 const flags=[65,64,67,69,73,81,97,105][index%8] | (index%3===2?256:0);
 const ability=index%3===0?'quickfeet':index%3===1?'levitate':'klutz';
 const item=index%3===0?'abilityshield':'';
 const other=index%2?'neutralizinggas':'noability';
 const set=(mon,side,abi,it,f)=>{
  mon.ability=abi;mon.item=it;mon.isActive=!!(f&1);mon.transformed=!!(f&2);
  mon.fainted=false;mon.abilityState={ending:!!(f&32)};mon.volatiles={};
  for(const [name,bit] of [['gastroacid',4],['embargo',8],['commanding',16],['dynamax',256]])if(f&bit)mon.volatiles[name]={};
  side.active[0]=f&64?mon:null;
 };
 set(p,s0,ability,item,flags);set(q,s1,other,'',65);
 p.status=family==='conditions'&&name==='par'&&(!single||index%2===0)?'par':index%4===1?'':'tox';
 const species=['Mew','Pikachu','Pikachu-Original','Raichu','Charmander','Charizard','Ditto','Clamperl'];
 p.baseSpecies=dex.species.get(species[index%species.length]);
 p.species=dex.species.get(species[(index+3)%species.length]);
 p.maxhp=[300,301,0,1,4294967295][index%5];
 p.hp=[0,Math.floor(p.maxhp/2),Math.ceil(p.maxhp/2),p.maxhp,Math.floor(p.maxhp/2)+1][index%5];
 b.field.pseudoWeather=index%5===0?{magicroom:{}}:{};
 const counterValues=[undefined,0,5,-1,NaN,Infinity,null,false,true,-0];
 const counter=counterValues[index%counterValues.length],childCounter=counterValues[(index+3)%counterValues.length];
 const childState={nativeState:123,counter:childCounter};
 b.effect=e;b.effectState={nativeState:11,counter};b.eventDepth=single?index%9:1;
 const lines=single&&index>0&&index%13===0?1001:0;
 b.log=Array(lines).fill('fixture');b.sentLogPos=0;
 b.event={id:'Fixture13',target:p,source:q,effect:undefined};
 if(mod!==null)b.event.modifier=mod/4096;
 const context=[kind,eid,11,13,10,1,576,0,10,1,1344,0,0,0,0,0,+(mod!==null),mod||0,b.eventDepth,0,0,0,0,0];
 const handler=e[key];let result,error=0;
 try{
  result=single?b.singleEvent(key.slice(2),e,childState,p,q,undefined,decode(relay),override?e[override]:undefined):
   typeof handler==='function'?handler.call(b,decode(relay),p,q,{id:'tackle',category:'Physical',type:'Normal'}):handler;
 }catch(err){if(b.eventDepth>=8)error=5;else if(single&&lines>1000)error=6;else throw err;}
 const post=context.slice();post[16]=+(b.event.modifier!==undefined);
 post[17]=post[16]?Math.trunc(b.event.modifier*4096)>>>0:0;
 if(b.prng.getSeed()!=='1,2,3,4')throw Error('Numeric body consumed RNG');
 cases.push({kind,eid,key:id('callbacks',key.toLowerCase()),override:override?id('callbacks',override.toLowerCase()):0,
  context,relay,post,counter:encode(counter),childCounter:encode(childCounter),
  expected:encode(result),truthy:+!!result,single,error,lines,
  status:id('conditions',p.status),facts:[id('species',p.species.id),id('species',p.baseSpecies.id),p.hp,p.maxhp],
  p:[id('abilities',ability),id('items',item),flags],q:[id('abilities',other),0,65],magic:+!!b.field.pseudoWeather.magicroom});
}
for(const entry of manifest.bodies.filter(entry=>['conditional_chain','effect_counter_chain'].includes(entry.operation)))
 for(let i=0;i<80;i++)for(const mod of [null,2048,4096,6144]){
  fixture(entry.family,entry.id,entry.callback,values[i%7],mod,i);
  fixture(entry.family,entry.id,entry.callback,values[i%7],mod,i,true);
 }
// Object-mutating callbacks are checked by test_stats with aliased relays.
for(const entry of manifest.bodies.filter(entry=>!['boost_mutation','type_array','type_weather_direct'].includes(entry.operation))){
 const variants=[entry];
 if(entry.family==='abilities'||entry.family==='items')variants.push({...entry,family:'conditions',
  id:(entry.family==='abilities'?'ability:':'item:')+entry.id});
 for(const v of variants)for(const relay of values)for(const mod of [null,0,2048,4096,6144,32768,65536]){
  fixture(v.family,v.id,v.callback,relay,mod);
  fixture(v.family,v.id,v.callback,relay,mod,0,true);
 }
}
for(const entry of manifest.bodies.filter(entry=>['weight_double','weight_half'].includes(entry.operation)))
 for(const relay of [[5,0,0,1],[5,0,3,1],[5,0,4294967295,1]])for(const single of [false,true])
  fixture(entry.family,entry.id,entry.callback,relay,4096,0,single);
for(let i=0;i<192;i++)for(const mod of [null,2048,4096,6144])
 fixture('conditions','par','onModifySpe',values[i%7],mod,i);
for(let i=0;i<192;i++)for(const mod of [null,2048,4096,6144])
 fixture('conditions','par','onModifySpe',values[i%7],mod,i,true);
for(const relay of [[0,0,0,0],[1,0,0,0],[2,0,0,0],[3,0,0,0],...values]){
 fixture('abilities','hugepower','onModifySpA',relay,4096,0,true);
 fixture('abilities','battlearmor','onModifyAtk',relay,4096,0,true,'onCriticalHit');
 fixture('abilities','battlearmor','onCriticalHit',relay,4096,0,true,'onModifyAtk');
 fixture('abilities','stall','onModifyAtk',relay,4096,0,true,'onFractionalPriority');
}
for(const relay of values)fixture('abilities','hustle','onModifySpe',relay,4096,0,true,'onModifyAtk');
// Exercise all resolved literal callbacks, including prefixed/nested effects.
for(const [family,kind] of ['moves','abilities','items','conditions'].map((f,i)=>[f,i])){
 for(const name of cat.ids[family].names.slice(1)){
  const e=effect(family,name);
  for(const key of cat.ids.callbacks.original.slice(1)){
   if(!['boolean','number','string'].includes(typeof e[key]))continue;
   // Query callback identities, including scalar item type-name callbacks.
   if(!cat.ids.callbacks.map[key.toLowerCase()])continue;
   fixture(family,name,key,values[2],4096);
   fixture(family,name,key,values[2],4096,0,true);
  }
 }
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
    heap = EffectHeap(lib, words)
    for case in cases:
        heap.reset()
        heap.import_raw(11, [[13, 4, 0, 11, 1], [4, *case["counter"]]])
        heap.import_raw(123, [[13, 4, 0, 123, 1], [4, *case["childCounter"]]])
        for index in range(12):
            at = 576 + index * 128
            words[at + 5], words[at + 6], words[at + 23] = case["p"] if index == 0 else case["q"] if index == 6 else [0, 0, 0]
            words[at + 81] = 0
        words[544], words[589] = case["magic"], case["status"]
        words[577], words[578], words[583], words[584] = case["facts"]
        if case["single"]:
            words[0], words[32:38] = 35, [case["kind"], case["eid"], case["key"], 123, case["override"], case["lines"]]
            words[104:117] = [case["key"], 10, 1, 576, 0, 10, 1, 1344, 0, 0, 0, 0, 0]
        else:
            words[0], words[32:37] = 34, [case["kind"], case["eid"], case["key"], 0, 0]
        words[64:83], words[88:92] = case["context"][:19], case["relay"]
        lib.pg9_execute(words)
        assert words[1] == case["error"], (case, words[1])
        if case["error"]:
            assert list(words[8:12]) == [1, 2, 3, 4]
            continue
        assert list(words[96:100]) == case["expected"], (case, list(words[96:100]))
        assert words[17] == case["truthy"]
        assert list(words[10216:10240]) == case["post"], (case, list(words[10216:10240]))
        assert list(words[8:12]) == [1, 2, 3, 4]
        assert heap.inspect(123)[1] == [[13, 4, 0, 123, 1], [4, *case["childCounter"]]]
    ability = cat["ids"]["abilities"]["map"]["levitate"]
    callback = cat["ids"]["callbacks"]["map"]["onimmunity"]
    words[0], words[32:37], words[64:83], words[88:92] = 34, [1, ability, callback, 0, 0], cases[0]["context"][:19], [4, 0, 100, 1]
    lib.pg9_execute(words)
    assert words[1] == 4
    hustle = cat["ids"]["abilities"]["map"]["hustle"]
    words[0], words[32:37], words[88:92] = 34, [1, hustle, cat["ids"]["callbacks"]["map"]["onmodifyatk"], 0, 0], [4, 0, 3, 2]
    lib.pg9_execute(words)
    assert words[1] == 3
    whole = sum(case["single"] for case in cases)
    body_count = sum(entry["operation"] not in ("boost_mutation", "type_array", "type_weather_direct") for entry in
                     json.loads((ROOT / "ocean/pokemon_gen9/PORTED_CALLBACKS.json").read_text())["bodies"])
    print(f"PASS: {len(cases) - whole} direct and {whole} original singleEvent callback cases, {body_count} named numeric bodies, "
          "prefixed aliases, relays/modifier scopes, paralysis/Quick Feet suppression and source faults; unsupported rejection")


if __name__ == "__main__":
    main()
