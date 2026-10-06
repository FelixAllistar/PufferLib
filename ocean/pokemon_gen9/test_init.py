#!/usr/bin/env python3
"""Independent pre-start constructor and stat tests against pinned Showdown."""
import ctypes
import json
from pathlib import Path
import subprocess

from test_sets import NORMALIZE_JS
from native_test_helpers import EffectHeap, read_text

ROOT = Path(__file__).resolve().parents[2]

SCRIPT = """
const {Dex,Teams,oracle}=require('./ocean/pokemon_gen9/reference.cjs');
const {Battle}=require(oracle+'/dist/sim/battle');
const {Side}=require(oracle+'/dist/sim/side');
const cat=require('./build/pokemon_gen9/catalog/catalog.json');
""" + NORMALIZE_JS + """
function projection(p) {
 const id=(kind,name)=>name?cat.ids[kind].map[norm(name)]:0;
 const order=['hp','atk','def','spa','spd','spe'];
 const out={sid:id('species',p.species.id),base:id('species',p.baseSpecies.id),
  level:p.level,gender:({M:1,F:2,'':3})[p.gender],ability:id('abilities',p.ability),
  item:id('items',p.item),hp:p.hp,maxhp:p.maxhp,baseMaxhp:p.baseMaxhp,
  tera:id('types',p.teraType),first:id('types',p.types[0]),second:id('types',p.types[1]),
  status:id('conditions',p.status),moves:p.moveSlots.map(s=>[id('moves',s.id),s.pp,s.maxpp,+s.disabled,+s.used,id('targets',s.target)]),
  stats:order.map(k=>p.baseStoredStats[k]),weight:p.weighthg,
  newlySwitched:+p.newlySwitched,speed:p.speed,known:+p.knownType,
  apparent:Array.from({length:p.apparentType.length},(_,i)=>p.apparentType.charCodeAt(i)),
  boosts:['atk','def','spa','spd','spe','accuracy','evasion'].map(k=>p.boosts[k]+6)};
 return out;
}
function effectGraph(b,sides,retain=false) {
 const objects=[],references=new WeakMap(),special=new WeakMap([[b,[4,0]],[b.field,[3,0]]]),roots=[];
 function add(obj,kind='fields'){
  if(references.has(obj))throw Error('Unexpected constructor object alias');
  const reference=objects.length+1;references.set(obj,reference);objects.push({reference,obj,kind});return reference;
 }
 roots.push([524,add(b.formatData)],[525,add(b.field.weatherState)],[526,add(b.field.terrainState)],
  [529,add(b.field.pseudoWeather,'map')],[527,add(b.effectState)]);
 if(JSON.stringify(Object.keys(b.field.pseudoWeather))!==JSON.stringify(cat.constructor_rules))
  throw Error('Unexpected constructor rules');
 for(const state of Object.values(b.field.pseudoWeather))add(state);
 for(const side of sides){
  special.set(side,[2,side.n]);
  for(const p of side.pokemon){
   const at=576+(side.n*6+p.position)*128;special.set(p,[1,at]);
   roots.push([4160+side.n*6+p.position,at]);
   for(const [offset,key]of [[89,'speciesState'],[90,'statusState'],[91,'abilityState'],[92,'itemState'],[93,'volatiles']])
    roots.push([at+offset,add(p[key],key==='volatiles'?'map':'fields')]);
  }
  roots.push([4192+side.n*8,add(side.sideConditions,'map')],[4193+side.n*8,add(side.slotConditions[0],'map')]);
  if(side.slotConditions.length!==1)throw Error('Unexpected singles slots');
 }
 // Shared raw type arrays are registered after effect states/maps. This is an
 // identity projection, not the source's physical JS allocation sequence.
 for(const side of sides)for(const p of side.pokemon){
  if(p.types!==p.baseSpecies.types||!Object.isFrozen(p.types))throw Error('Constructor type ownership changed');
  const ref=references.has(p.types)?references.get(p.types):add(p.types,'array');
  const at=576+(side.n*6+p.position)*128;
  roots.push([at+96,ref]);
  const display=cat.ids.species.map[norm(p.set.species)],group=cat.species[display].type_array;
  roots.push([cat.species_type_cache.start+group,ref]);
 }
 for(const side of sides)for(const p of side.pokemon){
  if(p.storedStats===p.baseStoredStats)throw Error('Constructor stat tables alias');
  const at=576+(side.n*6+p.position)*128;
  roots.push([at+102,add(p.storedStats)],[at+103,add(p.baseStoredStats)]);
 }
 for(const side of sides)for(const p of side.pokemon){
  if(p.moveSlots===p.baseMoveSlots||p.moveSlots.some((slot,i)=>slot!==p.baseMoveSlots[i]))
   throw Error('Constructor move-slot ownership changed');
  for(const slot of p.baseMoveSlots)add(slot);
  const at=576+(side.n*6+p.position)*128;
 roots.push([at+108,add(p.baseMoveSlots,'array')],[at+107,add(p.moveSlots,'array')]);
 }
 for(const side of sides)for(const p of side.pokemon){
  const at=576+(side.n*6+p.position)*128;
  roots.push([at+109,add(p.boosts)]);
 }
 const keys={id:1,target:2,effectOrder:3,counter:4,duration:5,source:6,sourceEffect:7,started:8,sourceSlot:14,
  hp:39,atk:32,def:33,spa:34,spd:35,spe:36,accuracy:37,evasion:38,
  move:40,pp:41,maxpp:42,disabled:43,disabledSource:44,used:45,virtual:46};
 function encode(value){
  if(value===undefined)return [0,0,0,0];if(value===null)return [1,0,0,0];
  if(typeof value==='string'){
   if(cat.ids.strings.map[value]===undefined)throw Error('Missing text '+value);
   return [9,0,cat.ids.strings.map[value],0];
  }
  if(typeof value==='object'){
   const identity=special.get(value)||(references.has(value)?[Array.isArray(value)?8:7,references.get(value)]:null);
   if(!identity)throw Error('Unmapped constructor alias');return [10,...identity,0];
  }
  if(typeof value==='boolean')return [value?3:2,0,0,0];
  if(!Number.isInteger(value)||value<0)throw Error('Unexpected constructor number');
  return [4,Math.floor(value/2**32),value>>>0,1];
 }
 const snapshots=objects.map(({reference,obj,kind})=>[reference,kind==='array'?
  [...Object.keys(obj).map(key=>[Number(key),...encode(obj[key])]),[4294967295,...encode(obj.length)]]:
  Object.entries(obj).map(([key,value])=>{
  const property=kind==='map'?65536+cat.ids.conditions.map[key]:keys[key];
  if(property===undefined||Number.isNaN(property))throw Error('Unmapped constructor field '+key);
  return [property,...encode(value)];
 })]);
 const result={roots,snapshots,next:objects.length+1,counter:b.effectOrder};
 if(retain)Object.assign(result,{objects,references,special,encode});
 return result;
}
function constructed(raw) {
 const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
 const side=new Side('test',b,0,[structuredClone(raw)]);
 const out={state:projection(side.pokemon[0]),effects:effectGraph(b,[side])};
 b.destroy();return out;
}
const g=Teams.getGenerator('gen9randombattle','1,2,3,4'),sets=[];
for(const sid of Object.keys(g.randomSets)) {
 g.setSeed('1,2,3,4');
 const s=g.randomSet(sid,{},false,false);
 sets.push({set:packed(s),...constructed(s)});
}
const teams=[];
for(let n=0;n<16;n++) {
 const seed1=[n,2,3,4],seed2=[65535-n,7,11,23];
 g.setSeed(seed1.join(','));const first=g.randomTeam();
 g.setSeed(seed2.join(','));const second=g.randomTeam();
 const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
 const sides=[new Side('one',b,0,structuredClone(first)),new Side('two',b,1,structuredClone(second))];
 b.sides=sides;
 teams.push({seed1,seed2,after:g.prng.getSeed().split(',').map(Number),
  sets:[...first,...second].map(packed),state:sides.flatMap(side=>side.pokemon.map(projection)),
  effects:effectGraph(b,sides)});
 b.destroy();
}
const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
const numeric=[];
for(const [n,nature] of Dex.mod('gen9').natures.all().entries())for(let stage=-6;stage<=6;stage++) {
 const raw={species:'Mew',moves:['psychic'],ability:'Synchronize',level:1+n*3,
  nature:nature.name,evs:{hp:85,atk:85,def:85,spa:85,spd:85,spe:85},
  ivs:{hp:31,atk:31,def:31,spa:31,spd:31,spe:31}};
 const side=new Side('test',b,0,[raw]),p=side.pokemon[0];
 const kind=nature.plus==='atk'?1:nature.minus==='atk'?2:0;
 const move=Dex.mod('gen9').moves.get(n%2?'trumpcard':'psychic');
 const value=p.storedStats.atk;
 numeric.push({base:100,iv:31,ev:85,level:raw.level,kind,stage:stage+6,
  move:cat.ids.moves.map[move.id],expected:[value,p.maxhp,p.calculateStat('atk',stage),
    b.calculatePP(move,move.noPPBoosts||move.id==='trumpcard'?0:3)]});
}
b.destroy();
console.log(JSON.stringify({sets,teams,numeric}));
"""


def write_set(words, s):
    words[64:74] = [s[k] for k in ("mon", "display", "role", "tera", "ability", "item", "level", "gender", "shiny")] + [len(s["moves"])]
    words[74:74 + len(s["moves"])] = s["moves"]
    words[80:86], words[86:92] = s["evs"], s["ivs"]


def compare(words, at, case, position=0):
    s, p = case["set"], case["state"]
    expected = [s["mon"], p["sid"], p["base"], p["level"], p["gender"], p["ability"],
                p["item"], p["hp"], p["maxhp"], p["baseMaxhp"], p["tera"], p["first"],
                p["second"], p["status"], len(p["moves"]), s["shiny"]]
    assert list(words[at:at + 16]) == expected, (case, list(words[at:at + 16]), expected)
    assert list(words[at + 16:at + 22]) == p["stats"], (case, list(words[at + 16:at + 22]))
    assert words[at + 22] == p["weight"], (case, words[at + 22])
    assert list(words[at + 24:at + 31]) == p["boosts"]
    assert words[at + 31] == position
    for i, (mid, pp, maximum, disabled, used, target) in enumerate(p["moves"]):
        assert list(words[at + 32 + i * 8:at + 40 + i * 8]) == [mid, pp, maximum, disabled, used, 0, 0, target], case
    assert list(words[at + 64:at + 70]) == s["evs"]
    assert list(words[at + 70:at + 76]) == s["ivs"]
    assert list(words[at + 94:at + 96]) == [p["newlySwitched"], p["speed"]]
    assert list(words[at + 97:at + 99]) == [0, 0]
    assert words[at + 99] == p["known"] and words[at + 101] == words[at + 96]
    assert list(words[at + 104:at + 107]) == [0, 0, s["level"]]


def compare_effects(words, heap, effects):
    for at, reference in effects["roots"]:
        assert words[at] == reference, (at, words[at], reference)
    meta, _ = heap.inspect(0)
    assert meta == [0, effects["next"], effects["counter"]], meta
    for reference, fields in effects["snapshots"]:
        assert heap.inspect(reference)[1] == fields, (reference, fields, heap.inspect(reference)[1])


def main():
    cat = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * cat["word_count"])()
    data = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, data, len(data))
    words[2] = 1
    heap = EffectHeap(lib, words)
    end = cat["mutable_words"]
    fixtures = json.loads(subprocess.check_output(["node", "-e", SCRIPT], cwd=ROOT))
    for case in fixtures["sets"]:
        words[0] = 12
        words[8:12] = [1, 2, 3, 4]
        words[512:end] = [0xA5A5A5A5] * (end - 512)
        write_set(words, case["set"])
        lib.pg9_execute(words)
        assert words[1] == 0, (case, words[1])
        assert list(words[8:12]) == [1, 2, 3, 4]
        compare(words, 576, case)
        allowed = {at for at, _ in case["effects"]["roots"]}
        assert not any(words[i] for i in range(512, end) if not 576 <= i < 704 and i not in allowed)
        compare_effects(words, heap, case["effects"])
        assert read_text(lib, words, words[576 + 100]) == case["state"]["apparent"]
    for case in fixtures["teams"]:
        words[0], words[8:12], words[12:16] = 14, case["seed1"], case["seed2"]
        words[512:end] = [0xA5A5A5A5] * (end - 512)
        lib.pg9_execute(words)
        assert words[1] == 0, (case["seed1"], case["seed2"], words[1])
        assert list(words[8:12]) == case["after"]
        assert list(words[512:524]) == [1, 1, 0, 0, 0, 0, 0, 0, 6, 6, 0xFFFFFFFF, 0xFFFFFFFF]
        for i, (s, p) in enumerate(zip(case["sets"], case["state"])):
            compare(words, 576 + i * 128, {"set": s, "state": p}, i % 6)
        allowed = {at for at, _ in case["effects"]["roots"]}
        assert not any(words[i] for i in range(2112, end) if i not in allowed)
        compare_effects(words, heap, case["effects"])
        for i, p in enumerate(case["state"]):
            assert read_text(lib, words, words[576 + i * 128 + 100]) == p["apparent"]
    for case in fixtures["numeric"]:
        words[0] = 13
        words[8:15] = [case[k] for k in ("base", "iv", "ev", "level", "kind", "stage", "move")]
        for hp, amount, maximum in [(0, 100, 300), (1, 100, 300), (290, 100, 300), (300, 0, 300), (300, 0xFFFFFFFF, 300)]:
            words[22:25] = hp, amount, maximum
            lib.pg9_execute(words)
            assert words[1] == 0
            expected = case["expected"] + [max(0, hp - amount), min(amount, maximum - hp) if hp else 0]
            assert list(words[16:22]) == expected, (case, list(words[16:22]), expected)
    print(f"PASS: {len(fixtures['sets'])} Pokémon constructors, {len(fixtures['teams'])} two-team assemblies with dirty state, "
          f"exact persistent constructor graphs/maps/text/creation order, baseTypes/known/apparent snapshots and newlySwitched/speed; "
          f"{len(fixtures['numeric'])} nature/boost cases and bounded HP")


if __name__ == "__main__":
    main()
