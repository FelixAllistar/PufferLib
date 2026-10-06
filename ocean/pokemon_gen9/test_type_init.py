#!/usr/bin/env python3
"""Resolved Dex ownership and native constructor type-cache finalization.

Full native single/two-team construction is independently checked by test_init.
This fixture imports only constructor effect-state inputs, then asks the shared
native constructor finalizer to produce arrays/cache entries without importing
source arrays. It covers every display name and deliberate ownership pairs.
"""
import ctypes
import json
from pathlib import Path
import subprocess
from native_test_helpers import EffectHeap, read_text

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = r"""
const {oracle,Dex}=require('./ocean/pokemon_gen9/reference.cjs');
const {Battle}=require(oracle+'/dist/sim/battle'),{Side}=require(oracle+'/dist/sim/side');
const cat=require('./build/pokemon_gen9/catalog/catalog.json'),dex=Dex.mod('gen9');
const id=(family,name)=>cat.ids[family].map[name]||0;
dex.species.all();
const identities=new WeakMap(),arrays=[null],namesByGroup=[null];
for(const name of cat.ids.species.names.slice(1)){
 const species=dex.species.get(name),types=species.types;
 if(!Array.isArray(types)||!Object.isFrozen(types)||!types.length)throw Error('Resolved type invariant');
 if(!identities.has(types)){identities.set(types,arrays.length);arrays.push(types);namesByGroup.push([]);}
 const group=identities.get(types);namesByGroup[group].push(name);
 if(group!==cat.species[id('species',name)].type_array)throw Error('Wrong exported source alias group');
}
if(arrays.length!==cat.species_type_arrays.length)throw Error('Group count');
const expectedArrays=arrays.map(types=>types&&types.map(text=>id('types',text.toLowerCase())));
if(JSON.stringify(expectedArrays)!==JSON.stringify(cat.species_type_arrays))throw Error('Group contents/order');
const batches=[],allNames=cat.ids.species.names.slice(1);
for(let at=0;at<allNames.length;at+=12)batches.push({kind:'catalog',names:allNames.slice(at,at+12)});
for(const names of namesByGroup.slice(1).filter(names=>names.length>1))
 for(let at=1;at<names.length;at+=11)batches.push({kind:'shared',names:[names[0],...names.slice(at,at+11)]});
const byContents=new Map();
for(let group=1;group<arrays.length;group++){
 const contents=JSON.stringify(arrays[group]);if(!byContents.has(contents))byContents.set(contents,[]);
 byContents.get(contents).push(namesByGroup[group][0]);
}
for(const names of byContents.values())if(names.length>1)
 for(let at=1;at<names.length;at+=11)batches.push({kind:'separate',names:[names[0],...names.slice(at,at+11)]});
const groups=[];
for(const batch of batches){
 const names=batch.names.slice();while(names.length<12)names.push(names[0]);
 const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
 const raw=name=>({species:dex.species.get(name).name,moves:['tackle'],ability:'No Ability',gender:'N',level:80});
 const sides=[new Side('one',b,0,names.slice(0,6).map(raw)),new Side('two',b,1,names.slice(6).map(raw))];
 b.sides=sides;sides[0].foe=sides[1];sides[1].foe=sides[0];b.speedOrder=[0,1];
 const mons=sides.flatMap(side=>side.pokemon),objects=[],refs=new Map(),special=new Map([[b,[4,0]],[b.field,[3,0]]]),roots=[];
 function register(obj){if(!refs.has(obj)){refs.set(obj,objects.length+1);objects.push(obj);}return refs.get(obj);}
 function root(at,obj){roots.push([at,register(obj)]);}
 for(const [at,obj]of [[524,b.formatData],[525,b.field.weatherState],[526,b.field.terrainState],[529,b.field.pseudoWeather],[527,b.effectState]])root(at,obj);
 for(const obj of Object.values(b.field.pseudoWeather))register(obj);
 for(const side of sides){special.set(side,[2,side.n]);for(const p of side.pokemon){
  const at=576+(side.n*6+p.position)*128;special.set(p,[1,at]);
  for(const [offset,key]of [[89,'speciesState'],[90,'statusState'],[91,'abilityState'],[92,'itemState'],[93,'volatiles']])root(at+offset,p[key]);
 }root(4192+side.n*8,side.sideConditions);root(4193+side.n*8,side.slotConditions[0]);}
 const prop={id:1,target:2,effectOrder:3,counter:4,duration:5,source:6,sourceEffect:7,started:8,sourceSlot:14};
 function encode(v){
  if(v===undefined)return [0,0,0,0];if(v===null)return [1,0,0,0];if(typeof v==='boolean')return [v?3:2,0,0,0];
  if(typeof v==='number')return [4,Math.floor(v/2**32),v>>>0,1];
  if(typeof v==='string'){if(!Object.hasOwn(cat.ids.strings.map,v))throw Error('Text '+v);return [9,0,id('strings',v),0];}
  const ref=special.get(v)||(refs.has(v)?[Array.isArray(v)?8:7,refs.get(v)]:null);
  if(!ref)throw Error('Unregistered reference');return [10,...ref,0];
 }
 function fields(obj){
  if(Array.isArray(obj))return [...Object.keys(obj).map(key=>[Number(key),...encode(obj[key])]),[4294967295,...encode(obj.length)]];
  return Object.entries(obj).map(([key,v])=>{const k=prop[key]||65536+id('conditions',key);
   if(!prop[key]&&!id('conditions',key))throw Error('Field '+key);return [k,...encode(v)];});
 }
 const initial=objects.map((obj,i)=>[i+1,fields(obj)]),patches=[...roots,[522,4294967295],[523,4294967295],[530,2],[531,0],[532,1]];
 for(const [i,p]of mons.entries()){
  const at=special.get(p)[1];patches.push([4160+i,at],[at+1,id('species',p.species.id)],[at+2,id('species',p.baseSpecies.id)],
   [at+5,id('abilities',p.ability)],[at+6,id('items',p.item)],[at+7,p.hp],[at+8,p.maxhp],[at+95,p.speed],
   [at+77,id('species',names[i])]);
  const resolved=dex.species.get(names[i]);
  if(p.types!==resolved.types||p.types!==p.baseSpecies.types||p.addedType||p.terastallized)throw Error('Constructor type state changed');
 }
 const arrayRoots=[],cache=new Map();
 for(const p of mons){const ref=register(p.types),group=identities.get(p.types);
  arrayRoots.push([special.get(p)[1]+96,ref]);cache.set(cat.species_type_cache.start+group,ref);}
 const snapshots=()=>objects.map((obj,i)=>[i+1,fields(obj)]),attached=snapshots();
 const counter=b.effectOrder,queries=[],parentState=refs.get(b.effectState);
 for(const p of mons){
  b.effect=dex.conditions.getByID('trickroom');b.eventDepth=0;
  b.event={id:'Parent13',target:mons[0],source:mons[6],effect:undefined,modifier:1.5};b.resetRNG([1,2,3,4]);
  const context=[3,id('conditions','trickroom'),parentState,13,...encode(mons[0]),...encode(mons[6]),0,0,0,0,1,6144,0];
  const out=p.getTypes();register(out);
  queries.push({at:special.get(p)[1],context,expected:encode(out),snapshots:snapshots(),next:objects.length+1,counter:b.effectOrder,rng:b.prng.getSeed()});
 }
 groups.push({kind:batch.kind,names,patches,initial,arrayRoots,cache:[...cache],attached,
  attachedNext:attached.length+1,counter,queries,
  apparent:mons.map(p=>Array.from({length:p.apparentType.length},(_,i)=>p.apparentType.charCodeAt(i)))});b.destroy();
}
console.log(JSON.stringify({groups,groupCount:arrays.length-1,displayCount:allNames.length,
 sharedGroups:namesByGroup.slice(1).filter(names=>names.length>1).length,
 separateContents:[...byContents.values()].filter(names=>names.length>1).length}));
"""


def main():
    source = subprocess.run(["node", "-e", SCRIPT], cwd=ROOT, stdout=subprocess.PIPE)
    if source.returncode:
        raise RuntimeError(f"Original constructor type fixture failed ({source.returncode})")
    fixture = json.loads(source.stdout)
    cat = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * cat["word_count"])()
    data = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, data, len(data))
    words[2] = 1
    heap = EffectHeap(lib, words)
    region = cat["regions"]["speciesTypeArrays"]
    for group, ids in enumerate(cat["species_type_arrays"][1:], 1):
        at = region["start"] + group * region["stride"]
        count, ptr = words[at:at + 2]
        assert count == len(ids) and list(words[ptr:ptr + count]) == ids
    total = 0
    for group in fixture["groups"]:
        heap.reset(group["counter"])
        words[512:16384] = [0xDEADBA5E] * (16384 - 512)
        words[512:8192] = [0] * (8192 - 512)
        for reference, fields in group["initial"]:
            heap.import_raw(reference, fields)
        for at, value in group["patches"]:
            words[at] = value
        for repeat in range(2):
            words[0], words[8:12] = 51, [1, 2, 3, 4]
            lib.pg9_execute(words)
            assert words[1] == 0, (group["kind"], group["names"], words[1])
            for index, (at, ref) in enumerate(group["arrayRoots"]):
                assert words[at] == ref
                assert list(words[at + 1:at + 3]) == [0, 0]
                assert words[at + 3] == 1 and words[at + 5] == ref
                assert read_text(lib, words, words[at + 4]) == group["apparent"][index]
            expected_cache = dict(group["cache"])
            for at in range(cat["species_type_cache"]["start"], cat["species_type_cache"]["end"]):
                assert words[at] == expected_cache.get(at, 0)
            assert heap.inspect(0)[0] == [0, group["attachedNext"], group["counter"]]
            for ref, fields in group["attached"]:
                assert heap.inspect(ref)[1] == fields
            assert list(words[8:12]) == [1, 2, 3, 4]
        for query in group["queries"]:
            words[0], words[32:37], words[64:83], words[8:12] = 50, [0, query["at"], 0, 0, 0], query["context"], [1, 2, 3, 4]
            lib.pg9_execute(words)
            assert words[1] == 0 and list(words[64:68]) == query["expected"]
            assert list(words[10216:10235]) == query["context"]
            assert heap.inspect(0)[0] == [0, query["next"], query["counter"]]
            for ref, fields in query["snapshots"]:
                assert heap.inspect(ref)[1] == fields
            assert list(words[8:12]) == [int(x) for x in query["rng"].split(",")]
            total += 1
    print(f"PASS: {fixture['displayCount']} resolved species/display names, {fixture['groupCount']} source type-array identities; "
          f"{fixture['sharedGroups']} shared groups and {fixture['separateContents']} equal-content groups with distinct ownership; "
          f"{len(fixture['groups'])} independent constructor cache sequences with two attachments each, {total} original getTypes calls; "
          "frozen/nonempty source inventory, exact arrays/roots/cache/aliases, no allocation on cache reuse, persistent graphs, scopes and RNG")


if __name__ == "__main__":
    main()
