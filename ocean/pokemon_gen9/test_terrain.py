#!/usr/bin/env python3
"""Original terrain queries, nested stat events and closed Dex inventory."""
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
const records=[...dex.moves.all(),...dex.abilities.all(),...dex.items.all(),...dex.species.all(),
 ...cat.ids.conditions.names.slice(1).map(name=>dex.conditions.getByID(name)),dex.formats.get('gen9randombattle')];
const terrainKeys=/^on(?:Any|Ally|Foe|Source)?TryTerrain(?:Priority|SubOrder|Order)?$/;
for(const effect of records)for(const key of Object.keys(effect))
 if(terrainKeys.test(key)&&effect[key]!==undefined)throw Error('TryTerrain closure changed: '+effect.id+'.'+key);
const abilities=['noability','surgesurfer','hadronengine','grasspelt'];
const terrains=['','electricterrain','grassyterrain','mistyterrain','psychicterrain'],groups=[];
for(let scenario=0;scenario<160;scenario++){
 const variant=Math.floor(scenario/20),ability=abilities[Math.floor(scenario/5)%4];
 const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
 const raw={species:'Mew',moves:['tackle'],ability:'No Ability'};
 b.sides[0]=new Side('one',b,0,[structuredClone(raw)]);b.sides[1]=new Side('two',b,1,[structuredClone(raw)]);
 const [s0,s1]=b.sides;s0.foe=s1;s1.foe=s0;const p=s0.pokemon[0],q=s1.pokemon[0];
 const special=new Map([[p,[1,576]],[q,[1,1344]],[s0,[2,0]],[s1,[2,1]],[b,[4,0]],[b.field,[3,0]]]);
 const state=(name,target)=>({id:name,target,effectOrder:7});
 for(const mon of [p,q]){mon.isActive=true;mon.side.active[0]=mon;mon.ability='noability';mon.item='';
  mon.volatiles={};mon.abilityState=state(mon.ability,mon);mon.itemState=state('',mon);
  mon.storedStats={atk:101,def:73,spa:337,spd:199,spe:101};mon.speed=77;}
 p.ability=ability;p.abilityState=state(ability,p);
 if([1,2,3,7].includes(variant))q.ability='neutralizinggas';
 q.abilityState=state(q.ability,q);if(variant===7)q.abilityState.ending=true;
 if(variant===2||variant===3)p.item='abilityshield';
 p.itemState=state(p.item,p);if(variant===3)b.field.pseudoWeather.magicroom=state('magicroom',b.field);
 if(variant===4)p.volatiles.gastroacid=state('gastroacid',p);if(variant===5)p.isActive=false;
 b.field.terrain=terrains[scenario%5];b.field.terrainState=state(b.field.terrain,b.field);
 b.activePokemon=variant===6?q:p;b.activeTarget=p;b.activeMove={ignoreAbility:variant===6};
 b.effectOrder=17;b.speedOrder=[0,1];
 const refs=new Map(),objects=[];function register(obj){if(!refs.has(obj)){refs.set(obj,refs.size+1);objects.push(obj);}return refs.get(obj);}
 const roots=[];function root(at,obj){roots.push([at,register(obj)]);}
 for(const [at,obj]of [[524,b.formatData],[525,b.field.weatherState],[526,b.field.terrainState],
  [527,b.effectState],[529,b.field.pseudoWeather]])root(at,obj);
 for(const mon of [p,q])for(const [offset,key]of [[89,'speciesState'],[90,'statusState'],[91,'abilityState'],[92,'itemState'],[93,'volatiles']])root(special.get(mon)[1]+offset,mon[key]);
 for(const side of b.sides){root(4192+side.n*8,side.sideConditions);root(4193+side.n*8,side.slotConditions[0]);}
 for(let i=0;i<objects.length;i++)for(const value of Object.values(objects[i]))if(value&&typeof value==='object'&&!special.has(value))register(value);
 function encode(value){if(value===undefined)return [0,0,0,0];if(value===null)return [1,0,0,0];
  if(typeof value==='boolean')return [value?3:2,0,0,0];if(typeof value==='string')return [9,0,id('strings',value),0];
  if(typeof value==='number')return [value<0?5:4,0,Math.abs(value),1];
  if(typeof value==='object')return [10,...(special.get(value)||[7,refs.get(value)]),0];throw Error('Value');}
 const prop={id:1,target:2,effectOrder:3,counter:4,duration:5,source:6,sourceEffect:7,started:8,
  sourceSlot:14,isSlotCondition:15,ending:16,atk:32,def:33,spa:34,spd:35,spe:36,accuracy:37,evasion:38};
 function fields(obj){return Object.entries(obj).map(([key,value])=>{if(!prop[key]&&!id('conditions',key))throw Error('Property '+key);
  return [prop[key]||65536+id('conditions',key),...encode(value)];});}
 const patches=[...roots,[522,0],[523,0],[4160,576],[4166,1344],[548,special.get(b.activePokemon)[1]],
  [535,576],[549,+b.activeMove.ignoreAbility],[530,2],[531,0],[532,1],[534,id('conditions',b.field.terrain)]];
 for(const mon of [p,q]){const at=special.get(mon)[1];patches.push([at+1,id('species',mon.species.id)],
  [at+2,id('species',mon.baseSpecies.id)],[at+5,id('abilities',mon.ability)],[at+6,id('items',mon.item)],
  [at+7,mon.hp],[at+8,mon.maxhp],[at+23,+mon.isActive|64|(mon.volatiles.gastroacid?4:0)|
   ((mon===q?!mon.abilityState.ending:mon.abilityState.ending)?32:0)],[at+95,mon.speed]);
  ['atk','def','spa','spd','spe'].forEach((key,i)=>patches.push([at+17+i,mon.storedStats[key]]));
  for(let i=0;i<7;i++)patches.push([at+24+i,6]);}
 const initial=objects.map((obj,i)=>[i+1,fields(obj)]),parentState=refs.get(b.effectState);
 function parent(target,depth){b.effect=dex.conditions.getByID('trickroom');b.effectState=objects[parentState-1];b.eventDepth=depth;
  b.event={id:'Parent13',target,source:q,effect:undefined,modifier:1.5};b.resetRNG([1,2,3,4]);
  return [3,id('conditions','trickroom'),parentState,13,...encode(target),...encode(q),0,0,0,0,1,6144,depth];}
 const queries=[];
 for(const depth of [0,7,8])for(const inherited of [p,q,s0,s1,b,null,undefined])
 for(const requested of [undefined,null,p,q,s0,s1,b])for(const names of [null,[],[''],['Electric Terrain'],['Grassy Terrain','Misty Terrain'],['Psychic Terrain']]){
  const context=parent(inherited,depth),mode=names===null?0:1;let expected,error=0;
  try{expected=mode===0?id('conditions',b.field.effectiveTerrain(requested)):+b.field.isTerrain(names,requested);}catch(e){if(depth<8||e.message!=='Stack overflow')throw e;error=5;}
  if(b.prng.getSeed()!=='1,2,3,4')throw Error('Query RNG');
  queries.push({mode,ids:(names||[]).map(name=>id('conditions',name.toLowerCase().replace(/[^a-z0-9]/g,''))),
   context,requested:encode(requested),expected,error});
 }
 // Source uses JavaScript truthiness, rather than just null/undefined tests.
 for(const inherited of [false,0,'',p])for(const requested of [false,0,'']){
  const context=parent(inherited,0);
  queries.push({mode:0,ids:[],context,requested:encode(requested),
   expected:id('conditions',b.field.effectiveTerrain(requested)),error:0});
 }
 const originalRun=b.runEvent;b.runEvent=function(event,...args){if(event==='ModifyBoost')register(args[3]);return originalRun.call(this,event,...args);};
 const originalInit=b.initEffectState;b.initEffectState=function(obj,order){const out=originalInit.call(this,obj,order);register(out);return out;};
 const calls=[];
 for(const depth of [0,6,7])for(const mode of [0,2,3]){
  const context=parent(p,depth),stat=ability==='hadronengine'?2:ability==='grasspelt'?1:4;let expected,error=0;
  try{expected=mode===0?p.getStat(['atk','def','spa','spd','spe'][stat]):mode===2?p.getActionSpeed():(p.updateSpeed(),p.speed);}
  catch(e){if(depth!==7||e.message!=='Stack overflow')throw e;error=5;}
  if(!error&&(b.effectState!==objects[parentState-1]||b.eventDepth!==depth||b.event.modifier!==1.5))throw Error('Scope');
  calls.push({input:[mode,576,stat,0,0,4096,0],context,expected,error,rng:b.prng.getSeed(),speed:p.speed,
   next:objects.length+1,counter:b.effectOrder,snapshots:objects.map((obj,i)=>[i+1,fields(obj)])});
 }
 groups.push({scenario,patches,initial,queries,calls});b.destroy();
}
console.log(JSON.stringify({records:records.length,groups}));
"""


def main():
    fixture = json.loads(subprocess.check_output(["node", "-e", SCRIPT], cwd=ROOT))
    cat = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * cat["word_count"])()
    data = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, data, len(data))
    words[2], words[4] = 1, 0
    heap = EffectHeap(lib, words)
    queries = calls = faults = 0
    for group in fixture["groups"]:
        heap.reset(17)
        words[512:8192] = [0] * (8192 - 512)
        for reference, fields in group["initial"]:
            heap.import_raw(reference, fields)
        for address, value in group["patches"]:
            words[address] = value
        before = list(words[512:8192])
        for case in group["queries"]:
            words[0], words[32:34], words[64:83], words[88:92], words[8:12] = 48, [case["mode"], len(case["ids"])], case["context"], case["requested"], [1, 2, 3, 4]
            words[160:160 + len(case["ids"])] = case["ids"]
            lib.pg9_execute(words)
            assert words[1] == case["error"], (group["scenario"], case, words[1])
            if not case["error"]:
                assert words[16] == case["expected"], (group["scenario"], case, words[16])
            assert list(words[64:83]) == case["context"]
            assert list(words[8:12]) == [1, 2, 3, 4]
            assert list(words[512:8192]) == before
            queries += 1
        for reference, fields in group["initial"]:
            _, actual = heap.inspect(reference)
            assert actual == fields
        for case in group["calls"]:
            words[0], words[32:39], words[64:83], words[8:12] = 46, case["input"], case["context"], [1, 2, 3, 4]
            lib.pg9_execute(words)
            assert words[1] == case["error"], (group["scenario"], case["input"], case["context"], words[1], case["error"])
            if not case["error"]:
                assert words[16] == case["expected"], (group["scenario"], case, words[16])
                assert list(words[10216:10235]) == case["context"]
            assert list(words[8:12]) == [int(x) for x in case["rng"].split(",")]
            assert words[671] == case["speed"]
            before[671 - 512] = case["speed"]
            assert list(words[512:8192]) == before
            for reference, fields in case["snapshots"]:
                meta, actual = heap.inspect(reference)
                assert meta[1:] == [case["next"], case["counter"]]
                assert actual == fields, (group["scenario"], "heap", reference, actual, fields)
            calls += 1
            faults += bool(case["error"])
    before = list(words[512:8192])
    words[64:83] = fixture["groups"][-1]["queries"][0]["context"]
    for mode, count, requested in [(2, 0, [0, 0, 0, 0]), (0, 9, [0, 0, 0, 0]),
                                   (0, 0, [10, 1, 575, 0]), (0, 0, [10, 2, 2, 0])]:
        words[0], words[32:34], words[88:92] = 48, [mode, count], requested
        lib.pg9_execute(words)
        assert words[1] == 3 and list(words[512:8192]) == before
    words[0], words[32:34], words[88:92], words[160] = 48, [1, 1], [0, 0, 0, 0], len(cat["ids"]["conditions"]["names"])
    lib.pg9_execute(words)
    assert words[1] == 3 and list(words[512:8192]) == before
    print(f"PASS: {queries} original terrain queries, {calls} full stat/speed calls ({faults} nested depth faults); "
          f"{fixture['records']} resolved effects without TryTerrain listeners; target inheritance, all terrain values, "
          "Surge Surfer/Hadron Engine/Grass Pelt, suppression, persistent objects, scopes and RNG")


if __name__ == "__main__":
    main()
