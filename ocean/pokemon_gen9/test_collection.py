#!/usr/bin/env python3
"""Original find*EventHandlers, without substituting source discovery."""
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
const families=['moves','abilities','items','conditions',null,'species'];
const prop={id:1,target:2,effectOrder:3,counter:4,duration:5,source:6,sourceEffect:7,started:8,
 sourceSlot:14,isSlotCondition:15};
const groups=[];let visits=0;
for(let scenario=0;scenario<16;scenario++){
 const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
 const sets=Array.from({length:6},(_,i)=>({species:i===2?'Mimikyu':'Mew',moves:['tackle'],ability:'No Ability'}));
 b.sides[0]=new Side('one',b,0,structuredClone(sets));
 b.sides[1]=new Side('two',b,1,structuredClone(sets));
 b.sides[0].foe=b.sides[1];b.sides[1].foe=b.sides[0];
 const all=b.sides.flatMap(side=>side.pokemon),p=all[0],q=all[6],bench=all[1];
 const special=new Map(all.map((mon,i)=>[mon,[1,576+i*128]]));
 for(const side of b.sides)special.set(side,[2,side.n]);
 special.set(b.field,[3,0]);special.set(b,[4,0]);
 for(const mon of all){mon.hp=150;mon.maxhp=300;mon.speed=100+mon.side.n*17;
  mon.storedStats.spe=123+mon.side.n;mon.ability='noability';mon.item='';}
 for(const mon of [p,q]){mon.isActive=true;mon.side.active[0]=mon;}
 const state=(name,target,duration,slot=false)=>({id:name,target,effectOrder:scenario+7,
  ...(duration===undefined?{}:{duration}),...(slot?{isSlotCondition:true}:{})});
 p.status=scenario%2||scenario===8?'par':'brn';p.statusState=state(p.status,p,scenario%3===0?2:0);
 q.status='slp';q.statusState=state('slp',q,undefined);
 const abi=['hugepower','magicbounce','purepower','slowstart','furcoat','hustle','guts','marvelscale',
  'quickfeet','defeatist','damp','pressure','stall','poisontouch','noability','intimidate'];
 p.ability=abi[scenario];p.abilityState=state(p.ability,p,scenario===5?3:undefined);
 q.ability=abi[(scenario+3)%abi.length];q.abilityState=state(q.ability,q,undefined);
 p.abilityState.counter=scenario%3;q.abilityState.counter=(scenario+1)%3;
 p.item=['choiceband','choicescarf','leftovers','assaultvest','eviolite','lightball','ironball','choicespecs'][scenario%8];
 p.itemState=state(p.item,p,scenario===6?1:undefined);
 q.item='laggingtail';q.itemState=state(q.item,q,undefined);
 const volatiles=['protect','confusion','substitute','mustrecharge','ability:slowstart','ability:magicbounce','item:leftovers'];
 for(const [i,name]of volatiles.entries())p.volatiles[name]=state(name,p,i%3===0?2:i%3===1?0:false);
 p.volatiles['ability:slowstart'].counter=scenario%3;
 q.volatiles.disable=state('disable',q,4);
 // Deletion/reinsertion must follow original object enumeration order.
 if(scenario%2){const saved=p.volatiles.confusion;delete p.volatiles.confusion;p.volatiles.confusion=saved;}
 for(const side of b.sides){
  for(const name of ['stealthrock','tailwind','reflect','safeguard'])
   side.sideConditions[name]=state(name,side,name==='stealthrock'?undefined:4);
  for(const mon of side.pokemon){
   side.slotConditions[mon.position]={wish:state('wish',side,2,scenario!==7),
    healingwish:state('healingwish',side,0,true)};
  }
 }
 for(const name of ['trickroom',...(scenario%2?['magicroom']:[]),'gravity'])b.field.pseudoWeather[name]=state(name,b.field,5);
 b.field.weather='raindance';b.field.weatherState=state('raindance',b.field,5);
 b.field.terrain='psychicterrain';b.field.terrainState=state('psychicterrain',b.field,5);
 if(scenario===2)p.hp=0;
 if(scenario===3){p.isActive=false;bench.isActive=true;p.side.active[0]=bench;}
 if(scenario===4){p.isActive=false;q.isActive=false;}
 if(scenario===5){p.baseSpecies=dex.species.get('mimikyu');p.species=dex.species.get('mew');}
 if(scenario===6){delete p.abilityState.effectOrder;delete q.abilityState.effectOrder;p.speed=0;q.speed=0;}
 if(scenario===12)p.baseSpecies=dex.species.get('eevee');
 if(scenario===13)p.baseSpecies=dex.species.get('pikachu');
 if(scenario===7){
  b.formatData.duration=3;
  const side=p.side;[side.pokemon[0],side.pokemon[1]]=[side.pokemon[1],side.pokemon[0]];
  side.pokemon.forEach((mon,i)=>mon.position=i);p.speed=0;
 }
 b.speedOrder=scenario%3===0?[]:scenario%3===1?[p.getFieldPositionValue(),q.getFieldPositionValue()]:
  [q.getFieldPositionValue(),p.getFieldPositionValue()];
 const refs=new Map(),objects=[];
 function register(obj){if(!refs.has(obj)){refs.set(obj,refs.size+1);objects.push(obj);}return refs.get(obj);}
 const roots=[];
 function root(at,obj){roots.push([at,register(obj)]);}
 root(524,b.formatData);root(525,b.field.weatherState);root(526,b.field.terrainState);
 root(527,b.effectState);root(529,b.field.pseudoWeather);
 for(const mon of all){const at=special.get(mon)[1];
  for(const [offset,key]of [[89,'speciesState'],[90,'statusState'],[91,'abilityState'],[92,'itemState'],[93,'volatiles']])root(at+offset,mon[key]);}
 for(const side of b.sides){root(4192+side.n*8,side.sideConditions);
  side.slotConditions.forEach((obj,i)=>root(4193+side.n*8+i,obj));
  side.pokemon.forEach((mon,i)=>roots.push([4160+side.n*6+i,special.get(mon)[1]]));}
 for(let i=0;i<objects.length;i++)for(const value of Object.values(objects[i]))
  if(value&&typeof value==='object'&&!special.has(value))register(value);
 function encode(v){
  if(v===undefined)return [0,0,0,0];if(v===null)return [1,0,0,0];
  if(typeof v==='boolean')return [v?3:2,0,0,0];if(typeof v==='string')return [9,0,id('strings',v),0];
  if(typeof v==='object')return [10,...(special.get(v)||[7,refs.get(v)]),0];
  if(typeof v==='number')return [v<0?5:4,0,Math.abs(v),1];throw Error('unsupported fixture value');
 }
 function fields(obj){return Object.entries(obj).map(([key,value])=>{
  const keyID=prop[key]||65536+id('conditions',key);
  if(!prop[key]&&!id('conditions',key))throw Error('Unknown state key '+key);
  return [keyID,...encode(value)];});}
 const patches=[...roots,[530,b.speedOrder.length],[533,id('conditions',b.field.weather)],[534,id('conditions',b.field.terrain)],
  [544,scenario%2?0:1]]; // deliberately stale legacy flag: the dictionary is authoritative
 b.speedOrder.forEach((value,i)=>patches.push([531+i,value]));
 for(const side of b.sides)patches.push([522+side.n,side.active[0]?side.active[0].position:4294967295]);
 for(const mon of all){const at=special.get(mon)[1];patches.push([at+2,id('species',mon.baseSpecies.id)],
  [at+5,id('abilities',mon.ability)],[at+6,id('items',mon.item)],[at+7,mon.hp],[at+8,mon.maxhp],[at+13,id('conditions',mon.status)],
  [at+21,mon.storedStats.spe],[at+23,+mon.isActive],[at+31,mon.position],[at+95,mon.speed]);}
 const initial=objects.map((obj,i)=>[i+1,fields(obj)]),cases=[];
 const ends=new Map([[p.clearStatus,1],[p.removeVolatile,2],[p.clearAbility,3],[p.clearItem,4],
  [p.side.removeSlotCondition,5],[p.side.removeSideCondition,6],[b.field.removePseudoWeather,7],
  [b.field.clearWeather,8],[b.field.clearTerrain,9]]);
 function listener(h,key,index){
  const family=h.effect.id.startsWith('ability:')||h.effect.id.startsWith('item:')?3:
   h.effect.effectType==='Pokemon'?5:h.effect.effectType==='Ability'?1:h.effect.effectType==='Item'?2:
   h.effect.effectType==='Format'?6:3;
  const effectID=family===6?0:id(families[family],h.effect.id),found=h.callback!==undefined;
  const actual=found&&h.effect[key]===undefined?'onStart':key;
  let tag=4,value=0;
  if(typeof h.callback==='function')tag=0;else if(typeof h.callback==='boolean'){tag=1;value=+h.callback;}
  else if(typeof h.callback==='number'){tag=Number.isInteger(h.callback)&&h.callback>=0?2:5;value=tag===2?h.callback:h.callback*10+32768;}
  else if(typeof h.callback==='string'){tag=3;value=id('strings',h.callback);}
  const end=ends.get(h.end)||0,holder=special.get(h.effectHolder),target=special.get(h.target)||[0,0];
  let endSide=0,endPoke=0,endCondition=0;
  if([1,2,3,4].includes(end))endPoke=holder[1];
  if(end===2||end===6||end===7)endCondition=effectID;
  if(end===6)endSide=holder[1];
  if(end===5){endSide=h.endCallArgs[0].n;endPoke=special.get(h.endCallArgs[1])[1];endCondition=effectID;}
  const stateTarget=special.get(h.state?.target),scope=stateTarget?.[0]===2?(h.state.isSlotCondition?2:1):stateTarget?.[0]===3?3:0;
  const abilityOrder=h.effectHolder.abilityState?.effectOrder;
  const redirect=typeof abilityOrder==='number'?abilityOrder+1:0;
  const keys=[index,Number(h.order||0),(h.priority||0)*10+32768,(h.speed||0)*2+32768,
   (h.subOrder||0)+32768,h.effectOrder||0,redirect,h.index||0];
  visits++;
  return [family,effectID,+found,id('callbacks',key.toLowerCase()),id('callbacks',actual.toLowerCase()),tag,value,
   refs.get(h.state),...holder,end,endSide,endPoke,endCondition,...target,scope,0,0,0,0,0,0,0,...keys];
 }
 // Capture actual requested names at the source priority-resolution boundary.
 const originalResolve=b.resolvePriority;
 b.resolvePriority=function(h,key){const out=originalResolve.call(this,h,key);out.nativeRequested=key;return out;};
 function record(mode,target,key,duration=false,custom=null,source=null,targets=[]){
  let handlers;
  if(mode===0)handlers=b.findPokemonEventHandlers(target,key,duration?'duration':undefined);
  if(mode===1)handlers=b.findSideEventHandlers(target,key,duration?'duration':undefined,custom||undefined);
  if(mode===2)handlers=b.findFieldEventHandlers(target,key,duration?'duration':undefined,custom||undefined);
  if(mode===3)handlers=b.findBattleEventHandlers(key,duration?'duration':undefined,custom||undefined);
  if(mode===4)handlers=b.findEventHandlers(target,key.slice(2),source);
  if(mode===5)handlers=b.findEventHandlers(targets,key.slice(2),source);
  const [family,address]=special.get(target)||[4,0];
  cases.push({input:[mode,family,address,id('callbacks',key.toLowerCase()),+duration,custom?special.get(custom)[1]:0,
   source?special.get(source)[1]:0,targets.length],targets:targets.map(mon=>special.get(mon)[1]),
   expected:handlers.map((h,i)=>listener(h,h.nativeRequested,i))});
 }
 for(const key of cat.ids.callbacks.original.slice(1)){
  if(!key.startsWith('on'))continue;
  for(const duration of [false,true]){
   record(0,p,key,duration);record(0,bench,key,duration);
   record(1,p.side,key,duration);record(2,b.field,key,duration);record(3,b,key,duration);
  }
  for(const target of [p,bench,p.side,b])record(4,target,key,false,null,scenario%2?q:null);
  record(5,b,key,false,null,q,[p,q,bench,p]);
  if(['onResidual','onSwitchIn','onModifySpe','onTryHitSide'].includes(key)){
   record(1,p.side,key,true,q);record(2,b.field,key,true,q);record(3,b,key,true,q);
  }
 }
 // Now run original complete runEvent: discovery is never replaced. Keep
 // allocations and mutations across calls, with independently recorded state.
 b.resolvePriority=originalResolve;
 const executions=[];
 const originalInit=b.initEffectState;
 b.initEffectState=function(obj,order){const state=originalInit.call(this,obj,order);register(state);return state;};
 const parentState=refs.get(b.effectState);
 const parent=[3,id('conditions','trickroom'),parentState,13,10,1,576,0,10,1,1344,0,0,0,0,0,1,6144,0];
 function execute(key,target,relay,fast=false,onEffect=false,effect=null,depth=0){
  b.resetRNG([1,2,3,4]);b.effect=dex.conditions.getByID('trickroom');b.effectState=objects[parentState-1];
  b.event={id:'Parent13',target:p,source:q,effect:undefined,modifier:1.5};b.eventDepth=depth;
  const array=Array.isArray(target),kind=effect?.effectType==='Ability'?1:effect?.effectType==='Item'?2:4;
  const effectID=kind===4?0:id(families[kind],effect.id);
  const effectValue=effect?[10,6,kind*4096+effectID,0]:[0,0,0,0];
  // runEvent aliases and mutates a supplied relay array on fast/falsy returns.
  // Freeze native inputs before advancing the independent source engine.
  const initialRelay=array?relay.map(encode):encode(relay);
  const discovered=b.findEventHandlers(target,key.slice(2),q).map(h=>
   [h.effect.id,refs.get(h.state),h.index??0,special.get(h.effectHolder)]);
  let result,error=0;
  try{result=b.runEvent(key.slice(2),target,q,effect,relay,onEffect,fast);}
  catch(err){if(depth>=8)error=5;else if(onEffect&&(!effect||array))error=7;else throw err;}
  if(b.effectState!==objects[parentState-1]||b.eventDepth!==depth)throw Error('Parent scope changed');
  const current=parent.slice();current[18]=depth;
  const expected=array&&!error?result.map(encode):error?[]:encode(result);
  executions.push({op:array?44:43,input:[+fast,+onEffect,kind,effectID,array?target.length:0,+array],
   parent:current,event:[id('callbacks',key.toLowerCase()),...(array?[0,0,0,0]:encode(target)),...encode(q),...effectValue],
   relay:array?[]:initialRelay,targets:array?target.map(mon=>special.get(mon)[1]):[],
   values:array?initialRelay:[],expected,error,after:b.prng.getSeed(),
   discovered,next:objects.length+1,counter:b.effectOrder,snapshots:objects.map((obj,i)=>[i+1,fields(obj)])});
 }
 for(const key of ['onModifyAtk','onModifyDef','onModifySpA','onModifySpD','onModifySpe']){
  for(const target of [p,q,bench])for(const fast of [false,true])execute(key,target,101,fast);
  execute(key,[p,q,bench],[101,99,37]);
  execute(key,[p,q],[101,99],true);
 }
 execute('onModifyAtk',p,101,false,true,dex.abilities.get('hugepower'));
 execute('onModifySpe',p,101,false,true,dex.abilities.get('slowstart'));
 execute('onPlate',p,101,false,true,dex.abilities.get('hugepower')); // missing raw callback: no allocation
 execute('onCriticalHit',p,true,false,true,dex.abilities.get('battlearmor')); // literal allocates, never binds
 execute('onModifyAtk',p,101,false,true,null); // original invalid onEffect fault
 execute('onModifyAtk',[p,q],[101,99],false,true,dex.abilities.get('hugepower'));
 execute('onPlate',[p,q],[101,99],false,true,dex.abilities.get('hugepower')); // absent callback permits arrays
 execute('onModifyAtk',p,101,false,true,dex.abilities.get('hugepower'),8);
 groups.push({initial,patches,cases,executions});
 b.destroy();
}
console.log(JSON.stringify({groups,visits}));
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
    checked = executed = 0
    for group in fixture["groups"]:
        heap.reset()
        for reference, fields in group["initial"]:
            heap.import_raw(reference, fields)
        # Initial independent diagnostic state; collection never advances it.
        words[512:8192] = [0] * (8192 - 512)
        for address, value in group["patches"]:
            words[address] = value
        words[8:12] = [1, 2, 3, 4]
        before = list(words[512:8192])
        for case in group["cases"]:
            words[0], words[32:40] = 42, case["input"]
            words[160:160 + len(case["targets"])] = case["targets"]
            lib.pg9_execute(words)
            assert words[1] == 0, (checked, case["input"], words[1])
            actual = [list(words[8192 + i * 32:8224 + i * 32]) for i in range(words[16])]
            assert actual == case["expected"], (checked, case["input"], actual, case["expected"])
            assert list(words[8:12]) == [1, 2, 3, 4]
            assert list(words[512:8192]) == before, (checked, "collection mutated private state")
            checked += 1
        # Inspect retained objects only after collection; snapshots are read-only.
        for reference, fields in group["initial"]:
            _, actual = heap.inspect(reference)
            assert actual == fields, (reference, actual, fields)
        for case in group["executions"]:
            words[0], words[32:38] = case["op"], case["input"]
            words[64:83], words[104:117] = case["parent"], case["event"]
            words[8:12] = [1, 2, 3, 4]
            if case["op"] == 43:
                words[88:92] = case["relay"]
            else:
                words[144:144 + len(case["targets"])] = case["targets"]
                for i, value in enumerate(case["values"]):
                    words[160 + i * 4:164 + i * 4] = value
            lib.pg9_execute(words)
            assert words[1] == case["error"], (executed, case, words[1])
            if not case["error"]:
                actual = (list(words[96:100]) if case["op"] == 43 else
                          [list(words[96 + i * 4:100 + i * 4]) for i in range(len(case["targets"]))])
                assert actual == case["expected"], (executed, case["op"], case["event"], actual, case["expected"],
                    case["discovered"], list(words[13824:13824 + words[16]]))
                assert list(words[10216:10235]) == case["parent"], (executed, "parent scope")
            assert list(words[8:12]) == [int(x) for x in case["after"].split(",")], (executed, "RNG")
            for reference, fields in case["snapshots"]:
                meta, actual = heap.inspect(reference)
                assert meta[1:] == [case["next"], case["counter"]], (executed, meta, case["next"])
                assert actual == fields, (executed, reference, actual, fields)
            executed += 1
    print(f"PASS: {checked} original handler-discovery cases, {fixture['visits']} listeners; "
          "exact holder/state/end identities, duration-only effects, insertion order, "
          "scalar bubbling, prefixes, inactive/source cases and indexed array targets; "
          f"{executed} complete collected runEvent calls, onEffect allocation/faults, "
          "persistent mutations, RNG and parent restoration")


if __name__ == "__main__":
    main()
