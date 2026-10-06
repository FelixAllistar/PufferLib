#!/usr/bin/env python3
"""Original isGrounded with retained Type graphs and live condition presence."""
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
const inventory=require('./ocean/pokemon_gen9/source-map.json').generator_inventory;
const id=(family,name)=>cat.ids[family].map[name]||0;
const prop={id:1,target:2,effectOrder:3,counter:4,duration:5,source:6,sourceEffect:7,started:8,
 sourceSlot:14,isSlotCondition:15,ending:16,typeWas:18};
const groups=[],counts={queries:0,mutations:0,errors:0,outcomes:{true:0,false:0,null:0}};
let scenario=0;
function fixture(plan,variant=0,mutations=false){
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
  mon.speed=77;
 }
 p.types=plan.types?plan.types.slice():p.baseSpecies.types;
 if(plan.shared)q.types=p.types;
 p.addedType=plan.added||'';p.terastallized=plan.tera||'';p.transformed=!!plan.transformed;
 p.ability=plan.ability||'noability';p.item=plan.item||'';
 if(plan.roost)p.volatiles.roost=state('roost',p);
 if(variant===1)b.field.pseudoWeather.gravity=state('gravity',b.field);
 if(variant===2)p.volatiles.ingrain=state('ingrain',p);
 if(variant===3)p.volatiles.smackdown=state('smackdown',p);
 if(variant===4)p.volatiles.magnetrise=state('magnetrise',p);
 if(variant===5)p.volatiles.telekinesis=state('telekinesis',p);
 if(variant===6)p.volatiles.roost=state('roost',p);
 if([7,8,9,17,18,19].includes(variant))q.ability='neutralizinggas';
 if(variant===8)q.abilityState.ending=true;
 if(variant===9)q.volatiles.gastroacid=state('gastroacid',q);
 if(variant===10)p.volatiles.gastroacid=state('gastroacid',p);
 if(variant===11)p.volatiles.embargo=state('embargo',p);
 if(variant===12)b.field.pseudoWeather.magicroom=state('magicroom',b.field);
 if(variant===13)p.isActive=false;
 if(variant===17)q.transformed=true;
 if(variant===18){q.fainted=true;q.hp=0;}
 if(variant===19)p.volatiles.commanding=state('commanding',p);
 if(variant===20)p.volatiles.roost=false;
 if(variant===21)p.volatiles.magnetrise=undefined;
 if(variant===22)p.volatiles.telekinesis=null;
 p.abilityState={...p.abilityState,id:p.ability};q.abilityState={...q.abilityState,id:q.ability};
 p.itemState=state(p.item,p);
 b.activePokemon=variant===15?p:variant===23?null:q;b.activeTarget=b.activePokemon===p?q:p;
 b.activeMove={ignoreAbility:[14,15,16,23].includes(variant)};
 if(variant===16)q.isActive=false;
 b.effectOrder=17;b.speedOrder=[0,1];
 const refs=new Map(),objects=[],roots=[];
 function register(obj){if(!refs.has(obj)){refs.set(obj,objects.length+1);objects.push(obj);}return refs.get(obj);}
 function root(at,obj){roots.push([at,register(obj)]);}
 for(const [at,obj]of [[524,b.formatData],[525,b.field.weatherState],[526,b.field.terrainState],[527,b.effectState],[529,b.field.pseudoWeather]])root(at,obj);
 for(const mon of [p,q])for(const [offset,key]of [[89,'speciesState'],[90,'statusState'],[91,'abilityState'],[92,'itemState'],[93,'volatiles'],[96,'types']])root(special.get(mon)[1]+offset,mon[key]);
 for(const side of b.sides){root(4192+side.n*8,side.sideConditions);root(4193+side.n*8,side.slotConditions[0]);}
 for(let i=0;i<objects.length;i++)for(const v of Object.values(objects[i]))
  if(v&&typeof v==='object'&&!special.has(v))register(v);
 function encode(v){
  if(v===undefined)return [0,0,0,0];if(v===null)return [1,0,0,0];if(typeof v==='boolean')return [v?3:2,0,0,0];
  if(typeof v==='number')return Number.isNaN(v)?[6,0,0,0]:[v<0?5:4,0,Math.abs(v),1];
  if(typeof v==='string'){if(!Object.hasOwn(cat.ids.strings.map,v))throw Error('Text '+v);return [9,0,id('strings',v),0];}
  const ref=special.get(v)||(refs.has(v)?[Array.isArray(v)?8:7,refs.get(v)]:null);
  if(!ref)throw Error('Unobserved allocation');return [10,...ref,0];
 }
 function fields(obj){
  if(Array.isArray(obj))return [...Object.keys(obj).map(key=>[Number(key),...encode(obj[key])]),[4294967295,...encode(obj.length)]];
  return Object.entries(obj).map(([key,v])=>{const k=prop[key]||65536+id('conditions',key);
   if(!prop[key]&&!id('conditions',key))throw Error('Field '+key);return [k,...encode(v)];});
 }
 const patches=[...roots,[522,0],[523,0],[4160,576],[4166,1344],[548,b.activePokemon?special.get(b.activePokemon)[1]:0],
  [535,special.get(b.activeTarget)[1]],[549,+b.activeMove.ignoreAbility],[530,2],[531,0],[532,1]];
 for(const mon of [p,q]){
  const at=special.get(mon)[1];patches.push([at+1,id('species',mon.species.id)],[at+2,id('species',mon.baseSpecies.id)],
   [at+5,id('abilities',mon.ability)],[at+6,id('items',mon.item)],[at+7,mon.hp],[at+8,mon.maxhp],
   [at+23,+mon.isActive|64|(+mon.transformed*2)|(+!!mon.volatiles.gastroacid*4)|(+!!mon.volatiles.embargo*8)|
    (+!!mon.volatiles.commanding*16)|(mon===q&&!q.abilityState.ending?32:0)],
   [at+81,+mon.fainted],[at+95,mon.speed],[at+97,id('strings',mon.addedType)],[at+98,id('strings',mon.terastallized)]);
 }
 const initial=objects.map((obj,i)=>[i+1,fields(obj)]),parentState=refs.get(b.effectState),parentObject=b.effectState;
 function capture(result){
  for(const obj of objects)if(obj.typeWas)register(obj.typeWas);
  if(result&&typeof result==='object'&&!special.has(result))register(result);return result;
 }
 const originalRun=b.runEvent;b.runEvent=function(event,...args){const out=originalRun.call(this,event,...args);return event==='Type'?capture(out):out;};
 for(const mon of [p,q]){const original=mon.getTypes;mon.getTypes=function(...args){return capture(original.apply(this,args));};}
 const originalInit=b.initEffectState;b.initEffectState=function(obj,order){const out=originalInit.call(this,obj,order);register(out);return out;};
 function parent(depth){
  b.effect=dex.conditions.getByID('trickroom');b.effectState=parentObject;b.eventDepth=depth;
  b.event={id:'Parent13',target:p,source:q,effect:undefined,modifier:1.5};b.resetRNG([1,2,3,4]);
  return [3,id('conditions','trickroom'),parentState,13,...encode(p),...encode(q),0,0,0,0,1,6144,depth];
 }
 const steps=[];
 function snapshot(step){steps.push({...step,next:objects.length+1,counter:b.effectOrder,
  snapshots:objects.map((obj,i)=>[i+1,fields(obj)]),rng:b.prng.getSeed()});}
 function query(negate,depth=0){
  const context=parent(depth);let result,error=0;
  try{result=p.isGrounded(negate);}catch(e){if(e.message!=='Stack overflow'||b.eventDepth<8)throw e;error=5;counts.errors++;}
  if(!error){if(b.effectState!==parentObject||b.eventDepth!==depth||b.event.modifier!==1.5)throw Error('Scope');counts.outcomes[String(result)]++;}
  snapshot({kind:'query',op:52,args:[576,+negate],context,error,expected:error?null:encode(result)});counts.queries++;
 }
 for(const depth of [0,7,8])for(const negate of [false,true])query(negate,depth);
 if(mutations){
  // Repeated presence writes/deletes change only the retained dictionary.
  // Native receives the property operation, never an oracle post-state.
  for(const [map,key]of [[b.field.pseudoWeather,'gravity'],[p.volatiles,'ingrain'],[p.volatiles,'smackdown'],
    [p.volatiles,'magnetrise'],[p.volatiles,'telekinesis']]){
   for(const value of [undefined,null,false,0,NaN,'']){
    map[key]=value;const context=parent(0);
    snapshot({kind:'mutation',op:40,args:[3,refs.get(map),65536+id('conditions',key)],context,value:encode(value),error:0});counts.mutations++;
    query(false);query(true);
   }
   delete map[key];const context=parent(0);
   snapshot({kind:'mutation',op:40,args:[4,refs.get(map),65536+id('conditions',key)],context,error:0});counts.mutations++;
   query(false);query(true);
  }
 }
 groups.push({scenario:scenario++,plan:plan.name||plan.species||plan.ability,variant,patches,initial,steps});b.destroy();
}
for(const types of [['Psychic'],['Flying'],['???','Flying'],[]])
 for(const ability of ['noability','levitate','eelevate','klutz'])
  for(const item of ['','ironball','airballoon','abilityshield','redorb','blueorb'])
   fixture({name:'type/ability/item matrix',types,ability,item});
for(const ability of cat.ids.abilities.names.slice(1))fixture({name:'ability inventory '+ability,types:['Normal'],ability});
for(const species of inventory.species_or_form_ids)fixture({species});
const plans=[
 {name:'ordinary Flying',types:['Flying'],ability:'levitate',item:'airballoon'},
 {name:'non-Flying Levitate',types:['Normal'],ability:'levitate',item:'abilityshield'},
 {name:'non-Flying Eelevate',types:['Normal'],ability:'eelevate',item:'ironball'},
 {name:'shared empty',types:[],shared:true},
 {name:'Roost',types:['Flying'],roost:true},
 {name:'added Flying',types:['Normal'],added:'Flying'},
 {name:'ordinary Tera',types:['Normal'],tera:'Flying'},
 {name:'Stellar Tera',types:['Flying'],tera:'Stellar'},
 {name:'Arceus raw Sky Plate',species:'Arceus',ability:'multitype',item:'skyplate',roost:true},
 {name:'Silvally raw Flying Memory',species:'Silvally',ability:'rkssystem',item:'flyingmemory',roost:true},
 {name:'Arceus repeated Type events',species:'Arceus',ability:'multitype',item:'skyplate'},
 {name:'Silvally repeated Type events',species:'Silvally',ability:'rkssystem',item:'flyingmemory'},
 {name:'transformed Arceus',species:'Arceus',ability:'multitype',item:'skyplate',types:['Normal'],transformed:true},
 ];
for(const plan of plans)for(let variant=0;variant<24;variant++){
 // The falsy Roost case observes temporary initialized callback states on Mew.
 if(variant===20&&plan.species)continue;
 fixture(plan,variant);
}
for(const ability of ['noability','levitate','eelevate'])
 for(const types of [[],['Flying'],['Normal']])fixture({name:'retained presence mutations',ability,types,shared:true},0,true);
console.log(JSON.stringify({groups,counts,abilities:cat.ids.abilities.names.length-1,species:inventory.species_or_form_ids.length}));
"""


def main():
    source = subprocess.run(["node", "-e", SCRIPT], cwd=ROOT, stdout=subprocess.PIPE)
    if source.returncode:
        raise RuntimeError(f"Original grounded fixture failed ({source.returncode})")
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
            words[64:83] = step["context"]
            if step["kind"] == "mutation":
                words[64:68] = step.get("value", [0, 0, 0, 0])
            words[2176:2688] = actions
            lib.pg9_execute(words)
            label = (group["scenario"], group["plan"], group["variant"], step["kind"], step["args"], step["context"][-1])
            assert words[1] == step["error"], (label, "error", words[1], step["error"])
            if step["kind"] == "query" and not step["error"]:
                assert list(words[64:68]) == step["expected"], (label, list(words[64:68]), step["expected"])
                assert list(words[10216:10235]) == step["context"], (label, "scope")
            assert list(words[512:2176]) == before[:2176 - 512], (label, "private rows")
            assert list(words[2688:8192]) == before[2688 - 512:], (label, "private roots")
            assert list(words[2176:2688]) == actions, (label, "queue")
            assert list(words[8:12]) == [int(x) for x in step["rng"].split(",")], (label, "rng")
            assert heap.inspect(0)[0] == [0, step["next"], step["counter"]], (label, "allocation")
            for ref, fields in step["snapshots"]:
                assert heap.inspect(ref)[1] == fields, (label, "retained graph", ref, heap.inspect(ref)[1], fields)
    last = fixture["groups"][-1]["steps"][-1]
    before = heap.inspect(0)[0]
    for at, negate in [(575, 0), (577, 0), (576, 2)]:
        words[0], words[32:34], words[64:83] = 52, [at, negate], last["context"]
        lib.pg9_execute(words)
        assert words[1] == 3 and heap.inspect(0)[0] == before
    print(f"PASS: {fixture['counts']['queries']} original isGrounded calls in {len(fixture['groups'])} retained sequences, "
          f"{fixture['counts']['mutations']} independent dictionary mutations; {fixture['abilities']} abilities and {fixture['species']} generated species/forms; "
          f"counts {fixture['counts']}; true/false/null, source early returns, property presence even for falsy values, "
          "Flying/Roost/added types/Tera/Stellar/Plate/Memory, Levitate/Eelevate, item and ability suppression, "
          "breaker/Shield/actor states, exact retained graphs/allocations/private rows/queue/scopes/RNG and original depth faults")


if __name__ == "__main__":
    main()
