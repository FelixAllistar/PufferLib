#!/usr/bin/env python3
"""Original weather defense bodies and joined stat events with live Type state."""
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
const weathers=['','sunnyday','raindance','sandstorm','snowscape','hail','desolateland','primordialsea'];
const definitions=[['sandstorm','Rock','Ice','ModifySpD',3],['snowscape','Ice','Rock','ModifyDef',1]];
const prop={id:1,target:2,effectOrder:3,counter:4,duration:5,source:6,sourceEffect:7,started:8,
 sourceSlot:14,isSlotCondition:15,ending:16,typeWas:18,
 atk:32,def:33,spa:34,spd:35,spe:36,accuracy:37,evasion:38};
const groups=[],counts={direct:0,single:0,event:0,stat:0,errors:0};
let scenario=0;
function fixture(definition,plan,variant,weather){
 const [name,required,opposite,eventName,stat]=definition;
 const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
 const raw={species:plan.species||'Mew',moves:['tackle'],ability:'No Ability'};
 b.sides[0]=new Side('one',b,0,[structuredClone(raw)]);
 b.sides[1]=new Side('two',b,1,[{species:'Mew',moves:['tackle'],ability:'No Ability'}]);
 const [s0,s1]=b.sides;s0.foe=s1;s1.foe=s0;const p=s0.pokemon[0],q=s1.pokemon[0];
 const special=new Map([[p,[1,576]],[q,[1,1344]],[s0,[2,0]],[s1,[2,1]],[b.field,[3,0]],[b,[4,0]]]);
 const state=(name,target)=>({id:name,target,effectOrder:7});
 for(const mon of [p,q]){
  mon.isActive=true;mon.side.active[0]=mon;mon.ability='noability';mon.item='';mon.volatiles={};
  mon.abilityState=state(mon.ability,mon);mon.itemState=state('',mon);mon.speciesState=state(mon.species.id,mon);
  mon.status='';mon.statusState=state('',mon);mon.speed=77;
  mon.storedStats={atk:101,def:101,spa:101,spd:101,spe:101};
  for(const key of ['atk','def','spa','spd','spe','accuracy','evasion'])mon.boosts[key]=0;
 }
 p.types=plan.frozen?p.baseSpecies.types:(plan.types||[required]).slice();
 q.types=plan.shared?p.types:q.types;p.addedType=plan.added||'';p.terastallized=plan.tera||'';
 p.transformed=!!plan.transformed;p.ability=plan.ability||'noability';p.item=plan.item||'';
 if(plan.roost)p.volatiles.roost=state('roost',p);
 if(variant===1)q.ability='airlock';
 if(variant===2)q.ability='cloudnine';
 if(variant===3){q.ability='airlock';q.volatiles.gastroacid=state('gastroacid',q);}
 if(variant===4)q.ability='neutralizinggas';
 if(variant===5){q.ability='neutralizinggas';q.abilityState.ending=true;}
 if(variant===6)p.volatiles.gastroacid=state('gastroacid',p);
 if(variant===7){p.volatiles.embargo=state('embargo',p);b.field.pseudoWeather.magicroom=state('magicroom',b.field);}
 if(variant===8)p.isActive=false;
 if(variant===9)q.ability='megasol';
 p.abilityState={...p.abilityState,id:p.ability};q.abilityState={...q.abilityState,id:q.ability};
 p.itemState=state(p.item,p);
 b.field.weather=weather;b.field.weatherState=state(weather,b.field);b.effectOrder=17;b.speedOrder=[0,1];
 b.activePokemon=q;b.activeTarget=p;b.activeMove={ignoreAbility:false};
 const refs=new Map(),objects=[],roots=[];
 function register(obj){if(!refs.has(obj)){refs.set(obj,objects.length+1);objects.push(obj);}return refs.get(obj);}
 function root(at,obj){roots.push([at,register(obj)]);}
 for(const [at,obj]of [[524,b.formatData],[525,b.field.weatherState],[526,b.field.terrainState],[527,b.effectState],[529,b.field.pseudoWeather]])root(at,obj);
 for(const mon of [p,q])for(const [offset,key]of [[89,'speciesState'],[90,'statusState'],[91,'abilityState'],[92,'itemState'],[93,'volatiles'],[96,'types']])root(special.get(mon)[1]+offset,mon[key]);
 for(const side of b.sides){root(4192+side.n*8,side.sideConditions);root(4193+side.n*8,side.slotConditions[0]);}
 const childState={target:q,counter:3};register(childState);
 for(let i=0;i<objects.length;i++)for(const v of Object.values(objects[i]))
  if(v&&typeof v==='object'&&!special.has(v))register(v);
 function encode(v){
  if(v===undefined)return [0,0,0,0];if(v===null)return [1,0,0,0];if(typeof v==='boolean')return [v?3:2,0,0,0];
  if(typeof v==='string'){if(!Object.hasOwn(cat.ids.strings.map,v))throw Error('Text '+v);return [9,0,id('strings',v),0];}
  if(typeof v==='number')return [v<0?5:4,Math.floor(Math.abs(v)/2**32),Math.abs(v)>>>0,1];
  const ref=special.get(v)||(refs.has(v)?[Array.isArray(v)?8:7,refs.get(v)]:null);
  if(!ref)throw Error('Unobserved allocation');return [10,...ref,0];
 }
 function fields(obj){
  if(Array.isArray(obj))return [...Object.keys(obj).map(key=>[Number(key),...encode(obj[key])]),[4294967295,...encode(obj.length)]];
  return Object.entries(obj).map(([key,v])=>{const k=prop[key]||65536+id('conditions',key);
   if(!prop[key]&&!id('conditions',key))throw Error('Field '+key);return [k,...encode(v)];});
 }
 const patches=[...roots,[522,0],[523,0],[4160,576],[4166,1344],[548,1344],[535,576],
  [530,2],[531,0],[532,1],[533,id('conditions',weather)]];
 for(const mon of [p,q]){
  const at=special.get(mon)[1];patches.push([at+1,id('species',mon.species.id)],[at+2,id('species',mon.baseSpecies.id)],
   [at+5,id('abilities',mon.ability)],[at+6,id('items',mon.item)],[at+7,mon.hp],[at+8,mon.maxhp],
   [at+23,+mon.isActive|64|(+mon.transformed*2)|(+!!mon.volatiles.gastroacid*4)|(+!!mon.volatiles.embargo*8)|
    (mon===q&&!q.abilityState.ending?32:0)],
   [at+95,mon.speed],[at+97,id('strings',mon.addedType)],[at+98,id('strings',mon.terastallized)]);
  for(let i=0;i<5;i++)patches.push([at+17+i,101]);
  for(let i=0;i<7;i++)patches.push([at+24+i,6]);
 }
 const initial=objects.map((obj,i)=>[i+1,fields(obj)]),parentState=refs.get(b.effectState),parentObject=b.effectState;
 function capture(result){
  if(p.volatiles.roost?.typeWas)register(p.volatiles.roost.typeWas);
  if(result&&typeof result==='object'&&!special.has(result))register(result);return result;
 }
 const originalRun=b.runEvent;b.runEvent=function(event,...args){
  if(event==='ModifyBoost')register(args[3]);
  const out=originalRun.call(this,event,...args);return event==='Type'?capture(out):out;
 };
 for(const mon of [p,q]){const original=mon.getTypes;mon.getTypes=function(...args){return capture(original.apply(this,args));};}
 const originalInit=b.initEffectState;b.initEffectState=function(obj,order){const out=originalInit.call(this,obj,order);register(out);return out;};
 function parent(depth,move=false){
  b.effect=move?dex.moves.get('tackle'):dex.conditions.getByID('trickroom');b.effectState=parentObject;b.eventDepth=depth;
  b.event={id:'Parent13',target:p,source:q,effect:undefined,modifier:1.5};b.resetRNG([1,2,3,4]);
  return [move?0:3,id(move?'moves':'conditions',move?'tackle':'trickroom'),parentState,13,...encode(p),...encode(q),0,0,0,0,1,6144,depth];
 }
 const effect=dex.conditions.getByID(name),callback=id('callbacks','on'+eventName.toLowerCase());
 if(effect['on'+eventName+'Priority']!==10)throw Error('Weather defense priority');
 const relay=[0,1,101,199,9999,65535,1048576,4294967295][scenario%8],steps=[];
 function record(kind,op,args,run,depth=0,move=false,value=relay){
  const context=parent(depth,move);let result,error=0;
  try{result=run();}catch(e){if(e.message!=='Stack overflow'||b.eventDepth<8)throw e;error=5;counts.errors++;}
  if(!error&&(b.effectState!==parentObject||b.eventDepth!==depth||b.event.modifier!==1.5))throw Error('Parent scope');
  steps.push({kind,op,args,relay:encode(value),event:[callback,...encode(p),...encode(null),...encode(null)],
   context,error,expected:error?null:encode(result),next:objects.length+1,counter:b.effectOrder,
   snapshots:objects.map((obj,i)=>[i+1,fields(obj)]),rng:b.prng.getSeed()});counts[kind]++;
 }
 for(const depth of [0,7,8]){
  record('direct',34,[3,id('conditions',name),callback,0,0],()=>effect['on'+eventName].call(b,relay,p),depth);
  record('single',35,[3,id('conditions',name),callback,refs.get(childState),0,0],
   ()=>b.singleEvent(eventName,effect,childState,p,null,null,relay),depth);
  record('event',43,[0,0,4,0,0,0],()=>b.runEvent(eventName,p,null,null,relay),depth);
  record('stat',46,[0,576,stat,0,0,4096,0],()=>p.getStat(['atk','def','spa','spd','spe'][stat]),depth);
 }
 // In direct calls Mega Sol sees the caller's effect; single/run handlers
 // instead bind the weather effect. Preserve both source contexts.
 record('direct',34,[3,id('conditions',name),callback,0,0],()=>effect['on'+eventName].call(b,relay,p),0,true);
 // An absent qualifying weather returns undefined without converting the
 // relay. Native integer-domain validation must remain inside the true branch.
 if(!weather)record('direct',34,[3,id('conditions',name),callback,0,0],
  ()=>effect['on'+eventName].call(b,null,p),0,false,null);
 groups.push({scenario:scenario++,name,plan:plan.name,variant,weather,patches,initial,steps});b.destroy();
}
for(const definition of definitions){
 const [name,required,opposite]=definition;
 for(const type of [...dex.types.all().map(t=>t.name),'???','Bird'])for(const weather of weathers)
  fixture(definition,{name:'plain '+type,types:[type]},0,weather);
 const typed=required==='Rock'?['stoneplate','rockmemory']:['icicleplate','icememory'];
 const plans=[
  {name:'roost dual',types:['Flying',required],roost:true},
  {name:'roost empty filter',types:['Flying'],roost:true},
  {name:'shared empty',types:[],shared:true},
  {name:'added type',types:['Psychic'],added:required},
  {name:'ordinary tera',types:[required],tera:'Fire'},
  {name:'matching tera',types:['Psychic'],tera:required},
  {name:'stellar',types:['Psychic'],added:required,tera:'Stellar'},
  {name:'arceus raw plate',species:'Arceus',ability:'multitype',item:typed[0],roost:true},
  {name:'silvally raw memory',species:'Silvally',ability:'rkssystem',item:typed[1],roost:true},
  {name:'transformed arceus',species:'Arceus',ability:'multitype',item:typed[0],types:[opposite],transformed:true},
  {name:'wrong raw ability',species:'Arceus',ability:'noability',item:typed[0],types:[opposite]},
  {name:'source frozen array',species:required==='Rock'?'Tyranitar':'Glaceon',frozen:true},
  {name:'assault vest chain',item:'assaultvest'},
  {name:'eviolite chain',species:'Pikachu',item:'eviolite'},
  {name:'utility umbrella',item:'utilityumbrella'},
 ];
 for(const plan of plans)for(let variant=0;variant<10;variant++)
  for(const weather of [name,name==='sandstorm'?'snowscape':'sandstorm',''])fixture(definition,plan,variant,weather);
}
console.log(JSON.stringify({groups,counts}));
"""


def main():
    source = subprocess.run(["node", "-e", SCRIPT], cwd=ROOT, stdout=subprocess.PIPE)
    if source.returncode:
        raise RuntimeError(f"Original weather defense fixture failed ({source.returncode})")
    fixture = json.loads(source.stdout)
    cat = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * cat["word_count"])()
    binary = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, binary, len(binary))
    words[2] = 1
    heap = EffectHeap(lib, words)
    actions = [0xFAAA0000 + i for i in range(512)]
    total = 0
    for group in fixture["groups"]:
        heap.reset(17)
        words[512:8192] = [0] * (8192 - 512)
        for ref, fields in group["initial"]:
            heap.import_raw(ref, fields)
        for at, value in group["patches"]:
            words[at] = value
        before = list(words[512:8192])
        for step in group["steps"]:
            words[0], words[32:32 + len(step["args"])], words[8:12] = step["op"], step["args"], [1, 2, 3, 4]
            words[64:83], words[88:92], words[104:117] = step["context"], step["relay"], step["event"]
            words[2176:2688] = actions
            lib.pg9_execute(words)
            label = (group["scenario"], group["name"], group["plan"], group["variant"], group["weather"], step["kind"], step["context"][-1])
            assert words[1] == step["error"], (label, "error", words[1], step["error"])
            if not step["error"]:
                actual = list(words[96:100]) if step["op"] != 46 else [4, 0, words[16], 1]
                assert actual == step["expected"], (label, actual, step["expected"])
                assert list(words[10216:10235]) == step["context"], (label, "scope")
            assert list(words[512:2176]) == before[:2176 - 512], (label, "private rows")
            assert list(words[2688:8192]) == before[2688 - 512:], (label, "private roots")
            assert list(words[2176:2688]) == actions, (label, "queue")
            assert list(words[8:12]) == [int(x) for x in step["rng"].split(",")], (label, "rng")
            assert heap.inspect(0)[0] == [0, step["next"], step["counter"]], (label, "allocation")
            for ref, fields in step["snapshots"]:
                assert heap.inspect(ref)[1] == fields, (label, "retained graph", ref, heap.inspect(ref)[1], fields)
            total += 1
    print(f"PASS: {total} original weather defense transitions in {len(fixture['groups'])} retained sequences; "
          f"counts {fixture['counts']}; direct/singleEvent/native-collected runEvent/getStat, live Type events, "
          "all imported types/eight weather values, Roost/shared empty/added/Tera/Stellar/Plate/Memory, "
          "suppression and Mega Sol caller effects, priority-10 direct rounding before item chains, "
          "exact retained mutations/allocations/private rows/queue/scopes/RNG and original nested depth faults")


if __name__ == "__main__":
    main()
