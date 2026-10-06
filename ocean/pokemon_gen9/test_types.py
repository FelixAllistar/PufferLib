#!/usr/bin/env python3
"""Original Type bodies, discovery and Pokemon type methods; retained graphs."""
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
const expected=new Map(['roost','arceus','silvally'].map(name=>[dex.conditions.getByID(name).onType,name]));
if(expected.size!==3)throw Error('Type function identities changed');
const owners=[],records=[];
for(const [family,kind]of [['moves',0],['abilities',1],['items',2],['conditions',3],['species',5]]){
 const get=name=>family==='conditions'?dex.conditions.getByID(name):dex[family].get(name);
 for(const name of cat.ids[family].names.slice(1)){
  const effect=get(name);records.push(effect);
  for(const key of Object.keys(effect))if(/^on(?:Any|Ally|Foe|Source)?Type(?:Priority|SubOrder|Order)?$/.test(key)&&effect[key]!==undefined){
   if(key==='onType'){
    const body=expected.get(effect[key]);if(!body)throw Error('Unported Type body '+family+'.'+name);
    if(family==='species'&&effect.baseSpecies.toLowerCase()!==body)throw Error('Unexpected inherited Type body');
    if(family!=='species'&&(family!=='conditions'||name!==body))throw Error('Unexpected Type owner');
    owners.push({family,kind,name,body});
   }else if(key!=='onTypePriority'||effect[key]!== (effect.onType===dex.conditions.getByID('roost').onType?-1:1))
    throw Error('Unexpected Type prefix/order '+family+'.'+name+'.'+key);
  }
 }
}
const format=dex.formats.get('gen9randombattle');records.push(format);
for(const key of Object.keys(format))if(/^on(?:Any|Ally|Foe|Source)?Type/.test(key)&&format[key]!==undefined)
 throw Error('Format Type handlers changed');
const typedItems=dex.items.all().filter(item=>item.onPlate||item.onMemory).map(item=>item.id);
const items=['','leftovers','ironball','abilityshield',...typedItems];
const groups=[],counts={direct:0,single:0,event:0,get:0,has:0,mutation:0,errors:0};
const prop={id:1,target:2,effectOrder:3,counter:4,duration:5,source:6,sourceEffect:7,started:8,
 sourceSlot:14,isSlotCondition:15,ending:16,typeWas:18};
function fixture(owner,variant,held,live=false){
 const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
 const raw={species:'Mew',moves:['tackle'],ability:'No Ability'};
 b.sides[0]=new Side('one',b,0,[structuredClone(raw)]);b.sides[1]=new Side('two',b,1,[structuredClone(raw)]);
 const [s0,s1]=b.sides;s0.foe=s1;s1.foe=s0;const p=s0.pokemon[0],q=s1.pokemon[0];
 const special=new Map([[p,[1,576]],[q,[1,1344]],[s0,[2,0]],[s1,[2,1]],[b.field,[3,0]],[b,[4,0]]]);
 const state=(name,target)=>({id:name,target,effectOrder:7});
 for(const mon of [p,q]){
  mon.isActive=true;mon.side.active[0]=mon;mon.ability='noability';mon.item='';mon.volatiles={};
  mon.abilityState=state(mon.ability,mon);mon.itemState=state('',mon);mon.speciesState=state(mon.species.id,mon);
  mon.speed=77;
 }
 if(owner.family==='species')p.species=p.baseSpecies=dex.species.get(owner.name);
 else if(owner.body==='arceus'||owner.body==='silvally')p.species=p.baseSpecies=dex.species.get(owner.body);
 p.speciesState=state(p.species.id,p);
 p.ability=owner.body==='arceus'?'multitype':owner.body==='silvally'?'rkssystem':'noability';
 if(variant===6)p.ability='noability';p.abilityState=state(p.ability,p);
 p.item=held;p.itemState=state(p.item,p);p.transformed=variant===5;
 const incoming=[['Normal','Flying'],['Flying'],[],['Psychic'],['Ice'],['Flying','Flying']][variant%6].slice();
 // A shared initial array means Normal fallback can mutate both holders.
 p.types=incoming;q.types=incoming;p.addedType=variant%2?'Ghost':'';
 p.terastallized=variant===10?'Fire':variant===11?'Stellar':'';
 if(variant===3)p.volatiles.gastroacid=state('gastroacid',p);
 if(variant===4){p.volatiles.embargo=state('embargo',p);b.field.pseudoWeather.magicroom=state('magicroom',b.field);}
 if(variant===7){q.ability='neutralizinggas';q.abilityState=state(q.ability,q);}
 if(variant===9)p.isActive=false;
 if(live&&owner.body!=='none'&&(variant%3!==0||owner.body==='roost'))p.volatiles.roost=state('roost',p);
 b.effectOrder=17;b.speedOrder=[0,1];
 const refs=new Map(),objects=[];
 function register(obj){if(!refs.has(obj)){refs.set(obj,refs.size+1);objects.push(obj);}return refs.get(obj);}
 const roots=[];function root(at,obj){roots.push([at,register(obj)]);}
 for(const [at,obj]of [[524,b.formatData],[525,b.field.weatherState],[526,b.field.terrainState],[527,b.effectState],[529,b.field.pseudoWeather]])root(at,obj);
 for(const mon of [p,q])for(const [offset,key]of [[89,'speciesState'],[90,'statusState'],[91,'abilityState'],[92,'itemState'],[93,'volatiles'],[96,'types']])root(special.get(mon)[1]+offset,mon[key]);
 for(const side of b.sides){root(4192+side.n*8,side.sideConditions);root(4193+side.n*8,side.slotConditions[0]);}
 const childState={target:q,counter:3};register(childState);
 for(let i=0;i<objects.length;i++)for(const v of Object.values(objects[i]))if(v&&typeof v==='object'&&!special.has(v))register(v);
 function encode(v){
  if(v===undefined)return [0,0,0,0];if(v===null)return [1,0,0,0];if(typeof v==='boolean')return [v?3:2,0,0,0];
  if(typeof v==='string'){if(!Object.hasOwn(cat.ids.strings.map,v))throw Error('Unknown text '+v);return [9,0,id('strings',v),0];}
  if(typeof v==='number')return [v<0?5:4,0,Math.abs(v),1];
  if(typeof v==='object'){
   const ref=special.get(v)||(refs.has(v)?[Array.isArray(v)?8:7,refs.get(v)]:null);
   if(!ref)throw Error('Unobserved allocation');return [10,...ref,0];
  }throw Error('Value');
 }
 function fields(obj){
  if(Array.isArray(obj))return [...Object.keys(obj).map(k=>[Number(k),...encode(obj[k])]),[4294967295,...encode(obj.length)]];
  return Object.entries(obj).map(([key,v])=>{const k=prop[key]||65536+id('conditions',key);
   if(!prop[key]&&!id('conditions',key))throw Error('Property '+key);return [k,...encode(v)];});
 }
 const patches=[...roots,[522,0],[523,0],[4160,576],[4166,1344],[548,576],[535,1344],[549,0],[530,2],[531,0],[532,1]];
 for(const mon of [p,q]){const at=special.get(mon)[1];patches.push([at+1,id('species',mon.species.id)],
  [at+2,id('species',mon.baseSpecies.id)],[at+5,id('abilities',mon.ability)],[at+6,id('items',mon.item)],
  [at+7,mon.hp],[at+8,mon.maxhp],[at+23,+mon.isActive|64|(+mon.transformed*2)|(+!!mon.volatiles.gastroacid*4)|(+!!mon.volatiles.embargo*8)],
  [at+95,mon.speed],[at+97,id('strings',mon.addedType)],[at+98,id('strings',mon.terastallized)]);}
 const initial=objects.map((obj,i)=>[i+1,fields(obj)]),parentState=refs.get(b.effectState);
 const parentObject=objects[parentState-1],effect=owner.family==='species'?dex.species.get(owner.name):dex.conditions.getByID(owner.name);
 // Observe allocation identity only. Original discovery, callback functions,
 // runEvent, getTypes and hasType still perform every game operation.
 function capture(result){
  const before=p.volatiles.roost?.typeWas;if(before&&typeof before==='object'&&!special.has(before))register(before);
  if(result&&typeof result==='object'&&!special.has(result))register(result);return result;
 }
 const originalRun=b.runEvent;b.runEvent=function(name,...args){const result=originalRun.call(this,name,...args);return name==='Type'?capture(result):result;};
 for(const mon of [p,q]){const original=mon.getTypes;mon.getTypes=function(...args){return capture(original.apply(this,args));};}
 const originalInit=b.initEffectState;b.initEffectState=function(obj,order){const out=originalInit.call(this,obj,order);register(out);return out;};
 function parent(depth=0){
  b.effect=dex.conditions.getByID('trickroom');b.effectState=parentObject;b.eventDepth=depth;
  b.event={id:'Parent13',target:p,source:q,effect:undefined,modifier:1.5};b.resetRNG([1,2,3,4]);
  return [3,id('conditions','trickroom'),parentState,13,...encode(p),...encode(q),0,0,0,0,1,6144,depth];
 }
 const steps=[];
 function record(kind,input,run,depth=0){
  const context=parent(depth);let result,error=0;
  try{result=capture(run());}catch(e){if(depth<8||e.message!=='Stack overflow')throw e;error=5;counts.errors++;}
  if(!error&&(b.effectState!==parentObject||b.eventDepth!==depth||b.event.modifier!==1.5))throw Error('Scope');
  steps.push({kind,input,context,error,expected:error?null:encode(result),next:objects.length+1,counter:b.effectOrder,
   snapshots:objects.map((obj,i)=>[i+1,fields(obj)]),rng:b.prng.getSeed()});counts[kind]++;
 }
 const callback=id('callbacks','ontype'),relay=encode(p.types),event=[callback,...encode(p),...encode(null),...encode(null)];
 if(!live){
  record('direct',{op:34,args:[owner.kind,id(owner.family,owner.name),callback,0,0],relay},()=>effect.onType.call(b,p.types,p));
  record('single',{op:35,args:[owner.kind,id(owner.family,owner.name),callback,refs.get(childState),0,0],relay,event},
   ()=>b.singleEvent('Type',effect,childState,p,null,null,p.types));
  record('single',{op:35,args:[owner.kind,id(owner.family,owner.name),callback,0,0,0],relay,event},
   ()=>b.singleEvent('Type',effect,null,p,null,null,p.types));
 }else{
  // Empty membership must still perform the first type query and its writes.
  record('has',{op:50,args:[1,576,0,0,0],ids:[]},()=>p.hasType([]));
  record('event',{op:43,args:[0,0,4,0,0,0],relay,event},()=>b.runEvent('Type',p,null,null,p.types));
  for(const depth of [0,7,8])for(const exclude of [false,true])for(const pretera of [false,true])
   record('get',{op:50,args:[0,576,+exclude,+pretera,0]},()=>p.getTypes(exclude,pretera),depth);
  for(const names of [['Flying'],['Normal'],['Ghost','Ice'],['???','Fire'],['Stellar']])
   record('has',{op:50,args:[1,576,0,0,names.length],ids:names.map(name=>id('strings',name))},()=>p.hasType(names));
  for(const text of ['Flying','Normal','Ghost','Ice','???','Fire','Stellar',''])
   record('has',{op:50,args:[1,576,0,0,1],ids:[id('strings',text)]},()=>p.hasType(text));
  // Alter the shared input after cached Roost references and returned arrays
  // exist. The subsequent query must observe it without rewriting snapshots.
  record('mutation',{op:49,args:[3,refs.get(p.types),0],value:encode('Rock')},()=>p.types.push('Rock'));
  record('get',{op:50,args:[0,1344,0,0,0]},()=>q.getTypes());
  record('get',{op:50,args:[0,576,0,1,0]},()=>p.getTypes(false,true));
 }
 groups.push({owner,variant,held,patches,initial,steps});b.destroy();
}
// Every resolved owner is executable, including inherited species forms.
for(const owner of owners)for(let variant=0;variant<8;variant++)fixture(owner,variant,items[(variant*7)%items.length]);
// All Plate and Memory values and default/wrong item cases, including suppression.
for(const owner of owners.filter(owner=>owner.family==='conditions'))for(const held of items)fixture(owner,4,held);
const liveOwners=[{family:'species',kind:5,name:'mew',body:'none'},
 {family:'species',kind:5,name:'mew',body:'roost'},
 ...['arceus','arceusfire','silvally','silvallyghost'].map(name=>({family:'species',kind:5,name,body:name.startsWith('arceus')?'arceus':'silvally'}))];
for(const owner of liveOwners)for(let variant=0;variant<12;variant++)
 fixture(owner,variant,items[(variant*7)%items.length],true);
console.log(JSON.stringify({owners:owners.length,records:records.length,counts,groups}));
"""


def main():
    source = subprocess.run(["node", "-e", SCRIPT], cwd=ROOT, stdout=subprocess.PIPE)
    if source.returncode:
        raise RuntimeError(f"Original Type fixture failed ({source.returncode})")
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
        for reference, fields in group["initial"]:
            heap.import_raw(reference, fields)
        for at, value in group["patches"]:
            words[at] = value
        for step in group["steps"]:
            inp = step["input"]
            words[8:12] = [1, 2, 3, 4]
            words[0], words[32:32 + len(inp["args"])] = inp["op"], inp["args"]
            words[64:83] = step["context"]
            words[88:92] = inp.get("relay", inp.get("value", [0, 0, 0, 0]))
            if "event" in inp:
                words[104:117] = inp["event"]
            if "ids" in inp:
                words[160:160 + len(inp["ids"])] = inp["ids"]
            words[2176:2688] = actions
            lib.pg9_execute(words)
            assert words[1] == step["error"], (group["owner"], group["variant"], group["held"], step, words[1])
            if not step["error"]:
                start = 96 if inp["op"] in (34, 35, 43) else 64
                assert list(words[start:start + 4]) == step["expected"], (group["owner"], step, list(words[start:start + 4]))
                if inp["op"] in (34, 35, 43):
                    assert words[17] == 1  # Empty arrays are truthy, too.
                if inp["op"] != 49:
                    assert list(words[10216:10235]) == step["context"], (group["owner"], step, list(words[10216:10235]))
            assert list(words[2176:2688]) == actions
            assert list(words[8:12]) == [int(x) for x in step["rng"].split(",")]
            assert heap.inspect(0)[0] == [0, step["next"], step["counter"]], (group["owner"], step, heap.inspect(0)[0])
            for reference, expected_fields in step["snapshots"]:
                assert heap.inspect(reference)[1] == expected_fields, (group["owner"], step, reference, heap.inspect(reference)[1])
            total += 1
    # Invalid diagnostic arguments fail before changing the persistent graph.
    last = fixture["groups"][-1]["steps"][-1]
    before = heap.inspect(0)[0]
    for args in ([2, 576, 0, 0, 0], [0, 577, 0, 0, 0], [0, 576, 2, 0, 0],
                 [0, 576, 0, 2, 0], [1, 576, 0, 0, 65], [1, 576, 0, 0, 1]):
        words[0], words[32:37], words[64:83] = 50, args, last["context"]
        words[160] = len(cat["ids"]["strings"]["names"])
        lib.pg9_execute(words)
        assert words[1] == 3 and heap.inspect(0)[0] == before
    print(f"PASS: {total} original Type callback/event/Pokemon helper transitions; "
          f"{fixture['owners']} resolved owners across {fixture['records']} effects, three original bodies; "
          f"counts {fixture['counts']}; raw Plate/Memory defaults/suppression, transformed aliases, "
          "shared inputs, Roost typeWas, Normal fallback, added types, Tera/Stellar, fresh allocations, "
          "retained graphs, property order, scope and exact RNG")


if __name__ == "__main__":
    main()
