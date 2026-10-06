#!/usr/bin/env python3
"""Pinned original weather accessors and complete weather/stat event calls."""
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
const id=(family,name)=>cat.ids[family].map[name]||0;
const abi=['chlorophyll','swiftswim','sandrush','slushrush','solarpower','orichalcumpulse',
 'heavymetal','lightmetal','unburden','megasol','noability','klutz'];
const weathers=['','sunnyday','raindance','sandstorm','snowscape','hail','desolateland','primordialsea'];
const groups=[];
for(let scenario=0;scenario<768;scenario++){
 const variant=Math.floor(scenario/96),ability=abi[Math.floor(scenario/8)%12];
 const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
 const raw={species:'Mew',moves:['tackle'],ability:'No Ability'};
 b.sides[0]=new Side('one',b,0,[structuredClone(raw)]);
 b.sides[1]=new Side('two',b,1,[structuredClone(raw)]);
 const [s0,s1]=b.sides;s0.foe=s1;s1.foe=s0;const p=s0.pokemon[0],q=s1.pokemon[0];
 const special=new Map([[p,[1,576]],[q,[1,1344]],[s0,[2,0]],[s1,[2,1]],[b.field,[3,0]],[b,[4,0]]]);
 const state=(name,target)=>({id:name,target,effectOrder:7});
 for(const mon of [p,q]){mon.isActive=true;mon.side.active[0]=mon;mon.ability='noability';mon.item='';
  mon.hp=150;mon.maxhp=300;mon.status=variant%2?'par':'';mon.statusState=state(mon.status,mon);
  mon.volatiles={};mon.weighthg=[0,1,3,101,9999,65535,1048576,4294967295][scenario%8];
  mon.storedStats={atk:101,def:73,spa:337,spd:199,spe:scenario%5===0?9999:101};mon.speed=77;
  mon.abilityState=state(mon.ability,mon);mon.itemState=state(mon.item,mon);}
 p.ability=ability;p.abilityState=state(ability,p);
 if(variant===1||variant===2||variant===3||variant===6)p.item='utilityumbrella';
 if(variant===2)b.field.pseudoWeather.magicroom=state('magicroom',b.field);
 if(variant===3||variant===7)q.ability='neutralizinggas';
 if(variant===4){q.ability='airlock';p.item='abilityshield';}
 if(variant===5){q.ability='cloudnine';q.volatiles.gastroacid=state('gastroacid',q);}
 if(variant===6){p.volatiles.gastroacid=state('gastroacid',p);p.volatiles.embargo=state('embargo',p);
  p.isActive=false;}
 if(variant===7){q.abilityState.ending=true;p.item='choicescarf';}
 if(ability==='unburden'||variant===0)p.volatiles.unburden=state('unburden',p);
 if(ability==='heavymetal'||ability==='lightmetal')p.item='floatstone';
 p.itemState=state(p.item,p);q.abilityState={...q.abilityState,id:q.ability};
 b.field.weather=weathers[scenario%8];b.field.weatherState=state(b.field.weather,b.field);
 b.activePokemon=p;b.activeTarget=q;b.activeMove={ignoreAbility:variant%3===0};b.effectOrder=17;b.speedOrder=[0,1];
 const refs=new Map(),objects=[];
 function register(obj){if(!refs.has(obj)){refs.set(obj,refs.size+1);objects.push(obj);}return refs.get(obj);}
 const roots=[];function root(at,obj){roots.push([at,register(obj)]);}
 root(524,b.formatData);root(525,b.field.weatherState);root(526,b.field.terrainState);
 root(527,b.effectState);root(529,b.field.pseudoWeather);
 for(const mon of [p,q])for(const [offset,key]of [[89,'speciesState'],[90,'statusState'],[91,'abilityState'],[92,'itemState'],[93,'volatiles']])
  root(special.get(mon)[1]+offset,mon[key]);
 for(const side of b.sides){root(4192+side.n*8,side.sideConditions);root(4193+side.n*8,side.slotConditions[0]);}
 for(let i=0;i<objects.length;i++)for(const v of Object.values(objects[i]))
  if(v&&typeof v==='object'&&!special.has(v))register(v);
 function encode(v){if(v===undefined)return [0,0,0,0];if(v===null)return [1,0,0,0];
  if(typeof v==='boolean')return [v?3:2,0,0,0];if(typeof v==='string')return [9,0,id('strings',v),0];
  if(typeof v==='object')return [10,...(special.get(v)||[7,refs.get(v)]),0];
  if(typeof v==='number')return [v<0?5:4,0,Math.abs(v),1];throw Error('Unknown value');}
 const prop={id:1,target:2,effectOrder:3,counter:4,duration:5,source:6,sourceEffect:7,started:8,
  ending:16,sourceSlot:14,isSlotCondition:15,atk:32,def:33,spa:34,spd:35,spe:36,accuracy:37,evasion:38};
 function fields(obj){return Object.entries(obj).map(([key,value])=>{
  const keyID=prop[key]||65536+id('conditions',key);
  if(!prop[key]&&!id('conditions',key))throw Error('Unknown property '+key);
  return [keyID,...encode(value)];});}
 const patches=[...roots,[522,0],[523,0],[4160,576],[4166,1344],[548,576],[535,1344],
  [549,+b.activeMove.ignoreAbility],[530,2],[531,0],[532,1],[533,id('conditions',b.field.weather)]];
 for(const mon of [p,q]){const at=special.get(mon)[1];patches.push([at+2,id('species',mon.baseSpecies.id)],
  [at+5,id('abilities',mon.ability)],[at+6,id('items',mon.item)],[at+7,mon.hp],[at+8,mon.maxhp],
  [at+13,id('conditions',mon.status)],[at+22,mon.weighthg],[at+23,+mon.isActive|64|
   (mon.volatiles.gastroacid?4:0)|(mon.volatiles.embargo?8:0)|
   ((mon===q?!mon.abilityState.ending:mon.abilityState.ending)?32:0)],[at+95,mon.speed]);
  ['atk','def','spa','spd','spe'].forEach((key,i)=>patches.push([at+17+i,mon.storedStats[key]]));
  for(let i=0;i<7;i++)patches.push([at+24+i,6]);}
 const initial=objects.map((obj,i)=>[i+1,fields(obj)]),parentState=refs.get(b.effectState);
 function current(family,name){return [family,id(['moves','abilities','items','conditions'][family]||'species',name),parentState,
   13,10,1,576,0,10,1,1344,0,0,0,0,0,1,6144,0];}
 function parent(effect,family,name){b.effect=effect;b.effectState=objects[parentState-1];b.eventDepth=0;
  b.event={id:'Parent13',target:p,source:q,effect:undefined,modifier:1.5};return current(family,name);}
 const effects=[[null,4,''],[dex.moves.get('tackle'),0,'tackle'],[dex.conditions.getByID('sandstorm'),3,'sandstorm'],
  [dex.abilities.get('megasol'),1,'megasol'],[dex.abilities.get('chlorophyll'),1,'chlorophyll'],
  [dex.items.get('utilityumbrella'),2,'utilityumbrella'],[dex.conditions.getByID('ability:megasol'),3,'ability:megasol']];
 const queries=[];b.resetRNG([1,2,3,4]);
 for(const [effect,family,name]of effects){const context=parent(effect,family,name);
  queries.push({input:[0,576,0],ids:[],context,expected:id('conditions',b.field.effectiveWeather())});
  for(const mon of [p,q])queries.push({input:[1,special.get(mon)[1],0],ids:[],context,
   expected:id('conditions',mon.effectiveWeather())});
  for(const names of [[],[''],['Sunny Day','Desolate Land'],['Rain Dance','Primordial Sea'],['Hail','Snowscape'],['Sandstorm']])
   queries.push({input:[2,576,names.length],ids:names.map(name=>id('conditions',name.toLowerCase().replace(/[^a-z0-9]/g,''))),
    context,expected:+b.field.isWeather(names)});
 }
 if(b.prng.getSeed()!=='1,2,3,4')throw Error('Weather query consumed RNG');
 const originalRun=b.runEvent;
 b.runEvent=function(event,...args){if(event==='ModifyBoost')register(args[3]);return originalRun.call(this,event,...args);};
 const originalInit=b.initEffectState;
 b.initEffectState=function(obj,order){const out=originalInit.call(this,obj,order);register(out);return out;};
 const calls=[];
 for(const mon of [p,q])for(const mode of [0,2,3,5]){
  const context=parent(dex.conditions.getByID('trickroom'),3,'trickroom');b.resetRNG([1,2,3,4]);
  const stat=ability==='solarpower'?2:ability==='orichalcumpulse'?0:4;
  const result=mode===0?mon.getStat(['atk','def','spa','spd','spe'][stat]):mode===2?mon.getActionSpeed():
   mode===3?(mon.updateSpeed(),mon.speed):mon.getWeight();
  if(b.effectState!==objects[parentState-1]||b.eventDepth!==0||b.event.modifier!==1.5)throw Error('Parent scope');
  calls.push({input:[mode,special.get(mon)[1],stat,0,0,4096,0],context,expected:result,rng:b.prng.getSeed(),
   next:objects.length+1,counter:b.effectOrder,speeds:[p.speed,q.speed],snapshots:objects.map((obj,i)=>[i+1,fields(obj)])});
 }
 // Mutate the original retained ability object after complete helper calls.
 // Native receives only the corresponding property operation, never source
 // post-state. The cached row deliberately remains contradictory/stale.
 const transitions=[],qState=refs.get(q.abilityState);
 for(const [remove,value]of [[false,true],[false,false],[false,1],[false,0],[false,-1],
  [false,null],[false,''],[false,'tackle'],[false,p.abilityState],[false,undefined],[true,undefined],[false,false]]){
  if(remove)delete q.abilityState.ending;else q.abilityState.ending=value;
  const context=parent(dex.conditions.getByID('trickroom'),3,'trickroom');b.resetRNG([1,2,3,4]);
  transitions.push({remove,value:encode(value),context,state:fields(q.abilityState),
   weather:[id('conditions',b.field.effectiveWeather()),id('conditions',p.effectiveWeather()),
    id('conditions',q.effectiveWeather())],
   suppression:[+p.ignoringAbility(),+p.ignoringItem(),+p.ignoringItem(true),
    +b.field.suppressingWeather(),+!!b.suppressingAbility(p)]});
  if(b.prng.getSeed()!=='1,2,3,4')throw Error('Ending transition consumed RNG');
 }
 groups.push({scenario,patches,initial,queries,calls,transitions,qState,
  final:objects.map((obj,i)=>[i+1,fields(obj)])});b.destroy();
}
console.log(JSON.stringify(groups));
"""


def main():
    groups = json.loads(subprocess.check_output(["node", "-e", SCRIPT], cwd=ROOT))
    cat = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * cat["word_count"])()
    data = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, data, len(data))
    words[2], words[4] = 1, 0
    heap = EffectHeap(lib, words)
    queries = calls = transitions = 0
    for group in groups:
        heap.reset(17)
        words[512:8192] = [0] * (8192 - 512)
        for reference, fields in group["initial"]:
            heap.import_raw(reference, fields)
        for address, value in group["patches"]:
            words[address] = value
        before = list(words[512:8192])
        for case in group["queries"]:
            words[0], words[32:35] = 47, case["input"]
            words[160:160 + len(case["ids"])] = case["ids"]
            words[64:83], words[8:12] = case["context"], [1, 2, 3, 4]
            lib.pg9_execute(words)
            assert words[1] == 0, (group["scenario"], case, words[1])
            assert words[16] == case["expected"], (group["scenario"], case, words[16])
            assert list(words[64:83]) == case["context"]
            assert list(words[512:8192]) == before
            assert list(words[8:12]) == [1, 2, 3, 4]
            queries += 1
        for reference, fields in group["initial"]:
            meta, actual = heap.inspect(reference)
            assert meta[1:] == [len(group["initial"]) + 1, 17]
            assert actual == fields
        for case in group["calls"]:
            words[0], words[32:39], words[64:83], words[8:12] = 46, case["input"], case["context"], [1, 2, 3, 4]
            lib.pg9_execute(words)
            assert words[1] == 0, (group["scenario"], case["input"], words[1])
            assert words[16] == case["expected"], (group["scenario"], case["input"], words[16], case["expected"])
            assert list(words[10216:10235]) == case["context"], (group["scenario"], "scope")
            assert list(words[8:12]) == [int(x) for x in case["rng"].split(",")], (group["scenario"], "rng")
            assert [words[671], words[1439]] == case["speeds"]
            if case["input"][0] == 3:
                before[case["input"][1] + 95 - 512] = words[case["input"][1] + 95]
            assert list(words[512:8192]) == before, (group["scenario"], "private mutation")
            for reference, fields in case["snapshots"]:
                meta, actual = heap.inspect(reference)
                assert meta[1:] == [case["next"], case["counter"]]
                assert actual == fields, (group["scenario"], reference, actual, fields)
            calls += 1
        for case in group["transitions"]:
            words[0], words[32:35] = 40, [4 if case["remove"] else 3, group["qState"], 16]
            words[64:68] = case["value"]
            lib.pg9_execute(words)
            assert words[1] == 0, (group["scenario"], "ending mutation", words[1])
            for mode, at, expected in zip([0, 1, 1], [576, 576, 1344], case["weather"]):
                words[0], words[32:35], words[64:83], words[8:12] = 47, [mode, at, 0], case["context"], [1, 2, 3, 4]
                lib.pg9_execute(words)
                assert words[1] == 0 and words[16] == expected, (group["scenario"], "live ending weather", case, words[16])
                assert list(words[64:83]) == case["context"]
                assert list(words[8:12]) == [1, 2, 3, 4]
                assert list(words[512:8192]) == before
            words[0], words[32:36], words[8:12] = 28, [0, 0, 1, words[549]], [1, 2, 3, 4]
            lib.pg9_execute(words)
            assert words[1] == 0 and list(words[16:21]) == case["suppression"], (group["scenario"], "live ending suppression", case, list(words[16:21]))
            assert list(words[8:12]) == [1, 2, 3, 4]
            assert list(words[512:8192]) == before
            meta, actual = heap.inspect(group["qState"])
            assert meta[1:] == [group["calls"][-1]["next"], group["calls"][-1]["counter"]]
            assert actual == case["state"], (group["scenario"], "ending aliases/property order", actual, case["state"])
            transitions += 1
        for reference, fields in group["final"]:
            _, actual = heap.inspect(reference)
            assert actual == fields, (group["scenario"], "ending final heap", reference, actual, fields)
    for mode, at, count in [(3, 576, 0), (0, 575, 0), (2, 576, 9)]:
        words[0], words[32:35] = 47, [mode, at, count]
        lib.pg9_execute(words)
        assert words[1] == 3
    words[0], words[32:35], words[160] = 47, [2, 576, 1], len(cat["ids"]["conditions"]["names"])
    lib.pg9_execute(words)
    assert words[1] == 3
    # Reject a diagnostic magnitude outside the native Nat domain before
    # multiplying. Legal Pokemon weights are integers stored in a U32 row.
    words[0], words[32:37] = 34, [1, cat["ids"]["abilities"]["map"]["heavymetal"],
                                cat["ids"]["callbacks"]["map"]["onmodifyweight"], 0, 0]
    words[64:83] = groups[-1]["calls"][0]["context"]
    for value in [[4, 65535, 4294967295, 1], [4, 0, 3, 2]]:
        words[88:92] = value
        lib.pg9_execute(words)
        assert words[1] == 3
    print(f"PASS: {queries} original field/Pokemon weather queries, {calls} full stat/action-speed/weight calls, "
          f"{transitions} persistent ending transitions with weather and five suppression checks; "
          "all eight weather values, six weather stat bodies, Unburden, Heavy/Light Metal and Float Stone, "
          "Umbrella/Magic Room, Air Lock/Cloud Nine/Gas/Gastro Acid/Shield/ending/inactive states, "
          "Mega Sol source effects, exact persistent mutations, parent scope and RNG")


if __name__ == "__main__":
    main()
