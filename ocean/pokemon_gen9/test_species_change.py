#!/usr/bin/env python3
"""Unmodified setSpecies: retained stat/type objects and original discovery.

Wrappers observe fresh arrays/spreads at original call boundaries. They never
replace discovery, callbacks or post-action native state. Explicit fixture edits
prepare HP, nature, activity, Tera and source contexts, not their lifecycles.
"""
import ctypes
import json
from pathlib import Path
import subprocess
from native_test_helpers import EffectHeap, read_text
from test_init import SCRIPT as CONSTRUCTOR_SCRIPT

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = CONSTRUCTOR_SCRIPT.split("const g=Teams.getGenerator(", 1)[0] + r"""
const dex=Dex.mod('gen9'),order=['hp','atk','def','spa','spd','spe'];
const id=(family,name)=>cat.ids[family].map[norm(name)]||0;
const groups=[],inventory=[];
for(const [family,names]of [['moves',cat.ids.moves.names],['abilities',cat.ids.abilities.names],
 ['items',cat.ids.items.names],['species',cat.ids.species.names],['conditions',cat.ids.conditions.names]])
 for(const name of names.slice(1)){
  const effect=family==='conditions'?dex.conditions.getByID(name):dex[family].get(name);
  for(const key of Object.keys(effect))if(/ModifySpecies$/.test(key))inventory.push([family,name,key]);
 }
if(inventory.some(([family])=>family!=='conditions'))throw Error('Unexpected default Dex ModifySpecies owner');
const names=cat.ids.species.names.slice(1),natures=dex.natures.all();
for(let scenario=0;scenario<25;scenario++){
 const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]}),nature=natures[scenario];
 const raw={species:'Mew',moves:['tackle'],ability:'No Ability',nature:nature.name,
  level:[1,50,100,9999,17][scenario%5],evs:{},ivs:{}};
 order.forEach((key,i)=>{raw.evs[key]=[0,1,3,4,85,252,255][(scenario+i)%7];raw.ivs[key]=[0,1,30,31][(scenario+i)%4];});
 const sides=[new Side('one',b,0,[structuredClone(raw)]),new Side('two',b,1,[structuredClone(raw)])];
 b.sides=sides;sides[0].foe=sides[1];sides[1].foe=sides[0];
 const p=sides[0].pokemon[0],q=sides[1].pokemon[0];
 if(scenario%2)p.storedStats=Object.fromEntries(Object.entries(p.storedStats).reverse());
 const graph=effectGraph(b,sides,true);
 const objects=graph.objects,refs=graph.references;
 function register(obj,kind='fields'){
  if(!refs.has(obj)){const reference=objects.length+1;refs.set(obj,reference);objects.push({reference,obj,kind});}
  return refs.get(obj);
 }
 const keys={id:1,target:2,effectOrder:3,counter:4,duration:5,source:6,sourceEffect:7,started:8,
  sourceSlot:14,hp:39,atk:32,def:33,spa:34,spd:35,spe:36,accuracy:37,evasion:38,
  move:40,pp:41,maxpp:42,disabled:43,disabledSource:44,used:45,virtual:46};
 function fields(entry){const {obj,kind}=entry;
  if(kind==='array')return [...Object.keys(obj).map(key=>[Number(key),...graph.encode(obj[key])]),[4294967295,...graph.encode(obj.length)]];
  return Object.entries(obj).map(([key,value])=>{
   const property=kind==='map'?65536+id('conditions',key):keys[key];
   if(property===undefined)throw Error('Unmapped object property '+key);return [property,...graph.encode(value)];
  });
 }
 function snapshots(first=0){return objects.slice(first).map(entry=>[entry.reference,fields(entry)]);}
 const plus=order.indexOf(nature.plus),minus=order.indexOf(nature.minus);
 const patches=graph.roots.slice();
 for(const [mon,at]of [[p,576],[q,1344]]){
  patches.push([at+1,id('species',mon.species.id)],[at+2,id('species',mon.baseSpecies.id)],
   [at+3,mon.level],[at+5,id('abilities',mon.ability)],[at+7,mon.hp],[at+8,mon.maxhp],[at+9,mon.baseMaxhp],
   [at+22,mon.weighthg],[at+31,0],[at+77,id('species',mon.set.species)],
   [at+95,mon.speed],[at+99,+mon.knownType],[at+101,refs.get(mon.baseTypes)],
   [at+104,Math.max(0,plus)],[at+105,Math.max(0,minus)],[at+106,mon.set.level]);
  order.forEach((key,i)=>patches.push([at+16+i,mon.baseStoredStats[key]],[at+64+i,mon.set.evs[key]],[at+70+i,mon.set.ivs[key]]));
 }
 const initial=graph.snapshots,next=graph.next,counter=graph.counter;
 const sourceSetType=p.setType,sourceSpread=b.spreadModify;
 p.setType=function(types,enforce){if(Array.isArray(types))register(types,'array');return sourceSetType.call(this,types,enforce);};
 b.spreadModify=function(base,set){const out=sourceSpread.call(this,base,set);register(out);return out;};
 let discovered=0;const originalDiscovery=b.findEventHandlers;
 b.findEventHandlers=function(target,event,source){const out=originalDiscovery.call(this,target,event,source);
  if(event==='ModifySpecies'){
   if(out.length)throw Error('Unexpected applicable ModifySpecies listener');discovered++;
  }return out;
 };
 function state(){return {sid:id('species',p.species.id),display:id('species',p.species.name),
  hp:p.hp,maxhp:p.maxhp,baseMaxhp:p.baseMaxhp,weight:p.weighthg,speed:p.speed,
  stored:refs.get(p.storedStats),base:refs.get(p.baseStoredStats),types:refs.get(p.types),
  baseTypes:refs.get(p.baseTypes),known:+p.knownType,added:cat.ids.strings.map[p.addedType],
  apparent:Array.from({length:p.apparentType.length},(_,i)=>p.apparentType.charCodeAt(i)),
  stats:order.slice(1).map(k=>p.storedStats[k])};}
 const actions=[];
 function patch(offset,value,edit){edit();actions.push({kind:'patch',offset,value});}
 // Difference between the set's level and live level is deliberate.
 patch(3,9999-p.level,()=>p.level=9999-p.level);
 patch(23,scenario%2?65:64,()=>{p.isActive=!!(scenario%2);sides[0].active[0]=p;});
 patch(98,scenario%3===0?cat.ids.strings.map.Fire:0,()=>p.terastallized=scenario%3===0?'Fire':'');
 function call(name,transform,depth=0,useDefault=false){
  const species=dex.species.get(name),first=objects.length;
  const parentEffect=useDefault?dex.moves.get('transform'):dex.conditions.getByID('trickroom');
  const parent=[useDefault?0:3,id(useDefault?'moves':'conditions',useDefault?'transform':'trickroom'),
   refs.get(b.effectState),13,10,1,576,0,10,1,1344,0,0,0,0,0,1,6144,depth];
  b.effect=parentEffect;b.event={id:'Parent13',target:p,source:q,effect:undefined,modifier:1.5};b.eventDepth=depth;
  let result,error=false;
  try{result=p.setSpecies(species,useDefault?undefined:null,transform);}catch(e){
   if(!String(e.message).includes('Stack overflow'))throw e;error=true;
  }
  if(!error&&(result!==species||b.effect!==parentEffect||b.eventDepth!==depth||b.event.modifier!==1.5))throw Error('Original result/scope changed');
  const changed=snapshots(first);
  changed.push([refs.get(p.storedStats),fields(objects[refs.get(p.storedStats)-1])]);
  const live=state();
  actions.push({kind:'call',display:id('species',name),transform:+transform,depth,useDefault:+useDefault,parent,
   result:error?null:[10,5,id('species',name),0],error,live,changed,next:objects.length+1,counter:b.effectOrder,
   spreadHP:error?null:objects.filter(entry=>entry.kind==='fields'&&Object.hasOwn(entry.obj,'hp')).at(-1).obj.hp});
 }
 const selected=names.filter((_,index)=>index%25===scenario);
 for(const [i,name]of [...selected,'shedinja','eternatuseternamax','mew','mew'].entries()){
  if(i%17===0){patch(8,0,()=>p.maxhp=0);patch(7,0,()=>p.hp=0);}
  else if(i%13===0)patch(7,0,()=>p.hp=0);
  call(name,i%3===0,i%9===0?7:0,i%4===0);
 }
 call('charizard',false,8);call('mew',true,9);
 // Retain all old spread tables and species arrays and compare after the full
 // sequence, so alias mistakes cannot hide behind matching scalar stats.
 groups.push({scenario,patches,initial,next,counter,actions,final:snapshots(),discovered});b.destroy();
}
console.log(JSON.stringify({groups,inventory,names:names.length}));
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
    words[2] = 1
    heap = EffectHeap(lib, words)
    total = faults = transformations = 0
    for group in fixture["groups"]:
        words[512:cat["mutable_words"]] = [0] * (cat["mutable_words"] - 512)
        words[8:12] = [1, 2, 3, 4]
        queue = list(range(512))
        words[2176:2688] = queue
        heap.reset(group["counter"])
        for reference, fields in group["initial"]:
            heap.import_raw(reference, fields)
        for at, value in group["patches"]:
            words[at] = value
        for action in group["actions"]:
            if action["kind"] == "patch":
                words[576 + action["offset"]] = action["value"]
                continue
            words[0], words[32:36] = 56, [576, action["display"], action["transform"], action["useDefault"]]
            words[64:83], words[88:92] = action["parent"], [1, 0, 0, 0]
            before = list(words[512:8192])
            lib.pg9_execute(words)
            assert words[1] == (5 if action["error"] else 0), (group["scenario"], action["display"], words[1])
            if action["error"]:
                assert list(words[512:8192]) == before
                faults += 1
            else:
                assert list(words[96:100]) == action["result"]
                assert list(words[10216:10235]) == action["parent"]
                allowed = {576 + i for i in [1, 77, 96, 97, 99, 100, 22, 7, 8, 9, 16, 17, 18, 19, 20, 21, 103, 95]}
                allowed.update(range(cat["species_type_cache"]["start"], cat["species_type_cache"]["end"]))
                assert all(words[i] == before[i - 512] for i in range(512, 8192) if i not in allowed)
                assert words[592] == action["spreadHP"]
                transformations += action["transform"]
            p = action["live"]
            for offset, key in [(1,"sid"),(77,"display"),(7,"hp"),(8,"maxhp"),(9,"baseMaxhp"),
                                (22,"weight"),(95,"speed"),(102,"stored"),(103,"base"),(96,"types"),
                                (101,"baseTypes"),(99,"known"),(97,"added")]:
                assert words[576 + offset] == p[key], (group["scenario"], action["display"], key, words[576 + offset], p[key])
            assert list(words[593:598]) == p["stats"]
            for index, expected in enumerate(p["stats"]):
                words[0], words[32:35] = 55, [1, 576, index]
                lib.pg9_execute(words)
                assert words[1] == 0 and words[16] == expected
            # The initial apparent value is absent from imported numeric rows;
            # the first call succeeds in every sequence before any depth fault.
            assert read_text(lib, words, words[676]) == p["apparent"]
            assert heap.inspect(0)[0] == [0, action["next"], action["counter"]]
            for reference, fields in action["changed"]:
                assert heap.inspect(reference)[1] == fields, (group["scenario"], action["display"], reference)
            assert list(words[8:12]) == [1, 2, 3, 4] and list(words[2176:2688]) == queue
            total += 1
        for reference, fields in group["final"]:
            assert heap.inspect(reference)[1] == fields, (group["scenario"], reference, "retained object")
    for inputs in [(575, 1, 0, 0), (576, 0, 0, 0), (576, cat["regions"]["species"]["count"], 0, 0),
                   (576, 1, 2, 0), (576, 1, 0, 2)]:
        before = list(words[512:8192])
        meta = heap.inspect(0)[0]
        words[0], words[32:36] = 56, inputs
        lib.pg9_execute(words)
        assert words[1] == 3 and list(words[512:8192]) == before
        assert heap.inspect(0)[0] == meta
    print(f"PASS: {total} original setSpecies calls over all {fixture['names']} resolved species/display records, "
          f"25 natures; {transformations} Transform-mode stat updates and {faults} original depth faults; "
          "original empty ModifySpecies discovery, level/EV/IV bounds, maxHP and HP-init order, "
          "retained stored/base stat identities and arrays, exact scopes/heap/private/queue/RNG")


if __name__ == "__main__":
    main()
