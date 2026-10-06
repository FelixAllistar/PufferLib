#!/usr/bin/env python3
"""Original Pokemon stat helpers, original event discovery and aliased boosts."""
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
const stats=['atk','def','spa','spd','spe'],boostNames=[...stats,'accuracy','evasion'];
const prop={id:1,target:2,effectOrder:3,counter:4,duration:5,source:6,sourceEffect:7,started:8,
 sourceSlot:14,isSlotCondition:15,...Object.fromEntries(boostNames.map((key,i)=>[key,32+i]))};
const abilities=['noability','hugepower','purepower','furcoat','hustle','defeatist','guts',
 'marvelscale','quickfeet','slowstart','unaware','klutz'];
const items=['','choiceband','choicescarf','choicespecs','assaultvest','eviolite','lightball','ironball'];
const groups=[];
for(let scenario=0;scenario<48;scenario++){
 const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
 const raw={species:'Mew',moves:['tackle'],ability:'No Ability'};
 b.sides[0]=new Side('one',b,0,[structuredClone(raw)]);
 b.sides[1]=new Side('two',b,1,[structuredClone(raw)]);
 const [s0,s1]=b.sides;s0.foe=s1;s1.foe=s0;
 const p=s0.pokemon[0],q=s1.pokemon[0];
 const special=new Map([[p,[1,576]],[q,[1,1344]],[s0,[2,0]],[s1,[2,1]],[b.field,[3,0]],[b,[4,0]]]);
 const state=(name,target)=>({id:name,target,effectOrder:scenario+3});
 for(const [i,mon]of [p,q].entries()){
  mon.isActive=true;mon.side.active[0]=mon;mon.hp=scenario%4===0?150:301;mon.maxhp=301;
  mon.ability=i===0?abilities[scenario%abilities.length]:['unaware','neutralizinggas','noability'][scenario%3];
  mon.abilityState=state(mon.ability,mon);mon.abilityState.counter=scenario%3;
  mon.item=i===0?items[scenario%items.length]:scenario%8===0?'abilityshield':'';
  mon.itemState=state(mon.item,mon);mon.status=['','par','brn'][scenario%3];
  mon.statusState=state(mon.status,mon);mon.volatiles={};
  mon.baseSpecies=dex.species.get(['mew','pikachu','eevee'][scenario%3]);
  for(const [j,key]of boostNames.entries())mon.boosts[key]=[-6,-4,-1,0,1,3,6][(scenario+i+j)%7];
  mon.storedStats={atk:101+scenario,def:199,spa:337,spd:73,
   spe:[0,1,101,4096,8191,8192,9999,10000,10001,12000][(scenario+i)%10]};
  mon.speed=77+i;
  mon.weighthg=[0,1,2,3,9,101,9999,65535,1048576,4294967295][(scenario+i)%10];
 }
 if(scenario%4===0)p.volatiles.foresight=state('foresight',p);
 if(scenario%4===1)p.volatiles.miracleeye=state('miracleeye',p);
 if(scenario%7===0)p.volatiles.gastroacid=state('gastroacid',p);
 if(scenario%11===0)p.volatiles.embargo=state('embargo',p);
 if(scenario%6===0)p.item='abilityshield';
 if(scenario>=36&&scenario%3===0){p.ability='unaware';p.abilityState=state('unaware',p);}
 if(scenario>=24&&scenario%2===0){p.ability=scenario%4===0?'heavymetal':'lightmetal';
  p.abilityState=state(p.ability,p);p.item='floatstone';}
 if(scenario>=46)for(const mon of [p,q])for(const key of stats)mon.storedStats[key]=scenario===46?101:0;
 if(scenario%13===0)p.isActive=false;
 if(scenario%4===2)s0.sideConditions.tailwind=state('tailwind',s0);
 if(scenario%2)b.field.pseudoWeather.wonderroom=state('wonderroom',b.field);
 if(scenario%3===0)b.field.pseudoWeather.trickroom=state('trickroom',b.field);
 if(scenario%5===0)b.field.pseudoWeather.magicroom=state('magicroom',b.field);
 b.activePokemon=[p,q,null][scenario%3];b.activeTarget=b.activePokemon===p?q:b.activePokemon===q?p:null;
 b.activeMove={ignoreAbility:scenario%6===0};b.effectOrder=17;
 const refs=new Map(),objects=[];
 function register(obj){if(!refs.has(obj)){refs.set(obj,refs.size+1);objects.push(obj);}return refs.get(obj);}
 const roots=[];
 function root(at,obj){roots.push([at,register(obj)]);}
 root(524,b.formatData);root(525,b.field.weatherState);root(526,b.field.terrainState);
 root(527,b.effectState);root(529,b.field.pseudoWeather);
 for(const mon of [p,q]){const at=special.get(mon)[1];
  for(const [offset,key]of [[89,'speciesState'],[90,'statusState'],[91,'abilityState'],[92,'itemState'],[93,'volatiles']])root(at+offset,mon[key]);}
 for(const side of b.sides){root(4192+side.n*8,side.sideConditions);root(4193+side.n*8,side.slotConditions[0]);}
 for(const mon of [p,q])root(special.get(mon)[1]+102,mon.storedStats);
 for(const mon of [p,q])root(special.get(mon)[1]+109,mon.boosts);
 for(let i=0;i<objects.length;i++)for(const v of Object.values(objects[i]))
  if(v&&typeof v==='object'&&!special.has(v))register(v);
 function encode(v){
  if(v===undefined)return [0,0,0,0];if(v===null)return [1,0,0,0];
  if(typeof v==='boolean')return [v?3:2,0,0,0];if(typeof v==='string')return [9,0,id('strings',v),0];
  if(typeof v==='object')return [10,...(special.get(v)||[7,refs.get(v)]),0];
  if(!Number.isInteger(v))throw Error('Non-integer fixture value');
  return [v<0?5:4,0,Math.abs(v),1];
 }
 function fields(obj){return Object.entries(obj).map(([key,value])=>{
  const keyID=prop[key]||65536+id('conditions',key);
  if(!prop[key]&&!id('conditions',key))throw Error('Unknown field '+key);
  return [keyID,...encode(value)];});}
 const patches=[...roots,[522,0],[523,0],[4160,576],[4166,1344],
  [548,b.activePokemon?special.get(b.activePokemon)[1]:0],[535,b.activeTarget?special.get(b.activeTarget)[1]:0],
  [549,+b.activeMove.ignoreAbility],[530,2],[531,0],[532,1]];
 b.speedOrder=[0,1];
 for(const mon of [p,q]){const at=special.get(mon)[1];patches.push(
  [at+2,id('species',mon.baseSpecies.id)],[at+5,id('abilities',mon.ability)],[at+6,id('items',mon.item)],
  [at+7,mon.hp],[at+8,mon.maxhp],[at+13,id('conditions',mon.status)],[at+22,mon.weighthg],[at+23,(+mon.isActive)|64|
   (mon.volatiles.gastroacid?4:0)|(mon.volatiles.embargo?8:0)],[at+95,mon.speed]);
  // Deliberately stale flat mirrors prove every live read uses the persistent
  // storedStats object, including collection's unboosted speed tie-breaker.
  stats.forEach((key,i)=>patches.push([at+17+i,mon.storedStats[key]+173]));
  // Stale stage mirrors must not replace the persistent boost object in either
  // the unmodified read or the fresh ModifyBoost relay clone.
  boostNames.forEach((key,i)=>patches.push([at+24+i,12-(mon.boosts[key]+6)]));}
 const persistentCount=objects.length,initial=objects.map((obj,i)=>[i+1,fields(obj)]);
 const parentState=refs.get(b.effectState);
 const parent=[3,id('conditions','trickroom'),parentState,13,10,1,576,0,10,1,1344,0,0,0,0,0,1,6144,0];
 const originalRun=b.runEvent;
 // Observe ordinary object allocation at the original call boundary. Do not
 // replace discovery, event execution or any original callback.
 b.runEvent=function(event,...args){
  if(event==='ModifyBoost')register(args[3]);
  return originalRun.call(this,event,...args);
 };
 const originalInit=b.initEffectState;
 b.initEffectState=function(obj,order){const out=originalInit.call(this,obj,order);register(out);return out;};
 const cases=[];
 function restoreParent(){b.effect=dex.conditions.getByID('trickroom');b.effectState=objects[parentState-1];
  b.event={id:'Parent13',target:p,source:q,effect:undefined,modifier:1.5};b.eventDepth=0;b.resetRNG([1,2,3,4]);}
 function record(mode,mon,stat=0,unboosted=false,unmodified=false,boost=0,modifier=4096,user=null){
  restoreParent();const firstNew=objects.length,oldBoosts=JSON.stringify([p.boosts,q.boosts]);
  const result=mode===0?mon.getStat(stats[stat],unboosted,unmodified):
   mode===1?mon.calculateStat(stats[stat],boost,modifier/4096,user):
   mode===2?mon.getActionSpeed():mode===3?(mon.updateSpeed(),mon.speed):
   mode===4?stats.indexOf(mon.getBestStat(unboosted,unmodified)):mon.getWeight();
  if(JSON.stringify([p.boosts,q.boosts])!==oldBoosts)throw Error('Original boost table mutated');
  if(b.eventDepth!==0||b.effectState!==objects[parentState-1]||b.event.modifier!==1.5)throw Error('Parent scope');
  const snapshots=objects.map((obj,i)=>[i+1,fields(obj)]).filter(([ref])=>ref<=persistentCount||ref>firstNew);
  cases.push({op:46,input:[mode,special.get(mon)[1],stat,+unboosted,+unmodified,modifier,user?special.get(user)[1]:0],
   boost:encode(boost),expected:result,parent,next:objects.length+1,counter:b.effectOrder,
   rng:b.prng.getSeed(),snapshots,speeds:[p.speed,q.speed]});
 }
 function recordObject(mode,mon,stat=0,boost=0){
  restoreParent();const firstNew=objects.length;
  const object=mode===0?{...mon.boosts}:mode===1?{[stats[stat]]:boost}:null;
  const expected=object?register(object):Math.max(-6,Math.min(6,boost))+6;
  cases.push({op:45,input:[mode,special.get(mon)[1],stat],boost:encode(boost),expected,parent,
   next:objects.length+1,counter:b.effectOrder,rng:b.prng.getSeed(),speeds:[p.speed,q.speed],
   snapshots:objects.map((obj,i)=>[i+1,fields(obj)]).filter(([ref])=>ref<=persistentCount||ref>firstNew)});
 }
 for(const mon of [p,q])recordObject(0,mon);
 for(let stat=0;stat<5;stat++)recordObject(1,p,stat,[-99,-6,0,6,99][stat]);
 for(const boost of [-99,-7,-6,-1,0,1,6,7,99])recordObject(2,p,0,boost);
 for(const mon of [p,q])for(let stat=0;stat<5;stat++)for(const unboosted of [false,true])
  for(const unmodified of [false,true])record(0,mon,stat,unboosted,unmodified);
 for(let stat=0;stat<5;stat++)for(const [i,boost]of [-99,-7,-6,-1,0,1,6,7,99].entries())
  record(1,p,stat,false,false,boost,[0,2048,4096,6144,8192,4095,4097][(i+stat)%7],i%2?q:null);
 for(const mon of [p,q]){record(2,mon);record(3,mon);record(0,mon,4);record(2,mon);}
 for(const mon of [p,q])for(const unboosted of [false,true])for(const unmodified of [false,true])
  record(4,mon,0,unboosted,unmodified);
 for(const mon of [p,q])record(5,mon);
 // Direct and singleEvent paths additionally preserve sparse property order
 // and explicit aliases, including negative/absent evasion and self-Unaware.
 for(const [family,name,key]of [['abilities','unaware','onAnyModifyBoost'],
  ['conditions','ability:unaware','onAnyModifyBoost'],['conditions','foresight','onModifyBoost'],
  ['conditions','miracleeye','onModifyBoost']])for(const single of [false,true])for(const evasion of [-3,0,3,null]){
  restoreParent();const boostObject={spd:2,atk:-2,...(evasion===null?{}:{evasion})};
  const reference=register(boostObject),pre=[[reference,fields(boostObject)]];
  const holder=single?q:p,target=scenario%2?p:q,childState=refs.get(holder.abilityState);
  const effect=family==='abilities'?dex.abilities.get(name):dex.conditions.getByID(name);
  const kind=family==='abilities'?1:3;
  let expected,context=parent.slice();
  if(single){expected=b.singleEvent(key.slice(2),effect,holder.abilityState,target,null,null,boostObject);}
  else {b.effect=effect;b.effectState=holder.abilityState;
   context=[kind,id(family,name),childState,...parent.slice(3)];
   expected=effect[key].call(b,boostObject,target);}
  cases.push({op:single?35:34,input:single?[kind,id(family,name),id('callbacks',key.toLowerCase()),childState,0,0]:
   [kind,id(family,name),id('callbacks',key.toLowerCase()),+(target===q),0],pre,
   boost:encode(boostObject),expected:encode(expected),parent:context,
   event:[id('callbacks',key.toLowerCase()),...encode(target),...encode(null),...encode(null)],
   next:objects.length+1,counter:b.effectOrder,rng:b.prng.getSeed(),
   snapshots:objects.map((obj,i)=>[i+1,fields(obj)]).filter(([ref])=>ref<=persistentCount||ref===reference),
   speeds:[p.speed,q.speed]});
 }
 groups.push({scenario,patches,initial,cases,final:objects.map((obj,i)=>[i+1,fields(obj)])});b.destroy();
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
    checked = callbacks = helpers = 0
    for group in groups:
        heap.reset(17)
        words[512:8192] = [0] * (8192 - 512)
        for reference, fields in group["initial"]:
            heap.import_raw(reference, fields)
        for address, value in group["patches"]:
            words[address] = value
        for case in group["cases"]:
            for reference, fields in case.get("pre", []):
                heap.import_raw(reference, fields)
            words[0], words[8:12] = case["op"], [1, 2, 3, 4]
            words[32:32 + len(case["input"])] = case["input"]
            words[64:83], words[88:92] = case["parent"], case["boost"]
            if case["op"] == 35:
                words[104:117] = case["event"]
            before = list(words[512:8192])
            lib.pg9_execute(words)
            assert words[1] == 0, (group["scenario"], checked, case["input"], words[1])
            actual = words[16] if case["op"] in (45, 46) else list(words[96:100])
            assert actual == case["expected"], (group["scenario"], checked, case["input"], actual, case["expected"])
            if case["op"] != 45:
                assert list(words[10216:10235]) == case["parent"], (checked, "scope")
            assert list(words[8:12]) == [int(x) for x in case["rng"].split(",")], (checked, "rng")
            assert [words[671], words[1439]] == case["speeds"], (checked, "cached speed")
            if case["op"] == 46 and case["input"][0] == 3:
                before[case["input"][1] + 95 - 512] = words[case["input"][1] + 95]
            assert list(words[512:8192]) == before, (checked, "unexpected private mutation")
            for reference, fields in case["snapshots"]:
                meta, actual = heap.inspect(reference)
                assert meta[1:] == [case["next"], case["counter"]], (checked, "allocation", meta, case["next"])
                assert actual == fields, (group["scenario"], checked, reference, actual, fields)
            checked += case["op"] == 46
            callbacks += case["op"] in (34, 35)
            helpers += case["op"] == 45
        for reference, fields in group["final"]:
            assert heap.inspect(reference)[1] == fields, (group["scenario"], reference, "retained alias")
    # Transport validation does not silently accept HP or an invalid holder.
    for at, stat in [(576, 5), (575, 0), (576, 7)]:
        words[0], words[32:39] = 46, [0, at, stat, 0, 0, 4096, 0]
        lib.pg9_execute(words)
        assert words[1] == 3
    # Weather lifecycle bodies remain unsupported; type-based stat bodies are
    # independently compared with original Type events in test_weather_defense.
    words[0], words[32:37] = 34, [3, cat["ids"]["conditions"]["map"]["sandstorm"],
                                cat["ids"]["callbacks"]["map"]["onweather"], 0, 0]
    words[64:83] = groups[-1]["cases"][0]["parent"]
    lib.pg9_execute(words)
    assert words[1] == 4
    print(f"PASS: {checked} original live stat/action-speed/updateSpeed/getBestStat/getWeight calls, {callbacks} original "
          "direct/singleEvent boost bodies; Wonder/Trick/Magic Room, Unaware and suppression, "
          "boost-copy isolation, sparse property order, retained aliases, exact RNG/scopes, cached speed, "
          f"best-stat ties and repeated reads; {helpers} boost-object/clamp fixtures; explicit unsupported weather lifecycle body")


if __name__ == "__main__":
    main()
