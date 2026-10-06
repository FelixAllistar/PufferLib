#!/usr/bin/env python3
"""Original singleEvent/runEvent gates; marker callbacks isolate execution."""
import ctypes
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = """
const {oracle,Dex}=require('./ocean/pokemon_gen9/reference.cjs');
const {Battle}=require(oracle+'/dist/sim/battle');
const {Side}=require(oracle+'/dist/sim/side');
const cat=require('./build/pokemon_gen9/catalog/catalog.json');
const dex=Dex.mod('gen9'),b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
const raw={species:'Mew',moves:['tackle'],ability:'No Ability'};
const s0=new Side('one',b,0,[structuredClone(raw)]),s1=new Side('two',b,1,[structuredClone(raw)]);
b.sides[0]=s0;b.sides[1]=s1;const p=s0.pokemon[0],q=s1.pokemon[0];
const id=(family,name)=>cat.ids[family].map[name]||0;
const events=['Start','End','SwitchIn','TakeItem','SetAbility','Weather','Residual',
 'FieldStart','FieldResidual','FieldEnd','BeforeMove'];
const result=[];
const set=(mon,side,ability,item,flags)=>{
 mon.ability=ability;mon.item=item;mon.isActive=!!(flags&1);mon.transformed=!!(flags&2);
 mon.fainted=false;mon.volatiles={};mon.abilityState={ending:!!(flags&32)};
 for(const [key,bit] of [['gastroacid',4],['embargo',8],['commanding',16]])if(flags&bit)mon.volatiles[key]={};
 side.active[0]=(flags&64)?mon:null;
};
function test(family,kind,eid,event,mode,index,depth=0,lines=0){
 const name=cat.ids[family].names[eid];
 const effect=family==='moves'?dex.moves.get(name):family==='abilities'?dex.abilities.get(name):
  family==='items'?dex.items.get(name):dex.conditions.getByID(name);
 const flags=[65,64,67,69,73,81,97,105][index%8];
 const ability=family==='abilities'?name:index%3===0?'klutz':'levitate';
 const item=family==='items'?name:index%3===1?'abilityshield':'leftovers';
 const other=['neutralizinggas','cloudnine','airlock','noability'][index%4];
 set(p,s0,ability,item,flags);set(q,s1,other,'',65);
 p.status=index%3===0&&effect.effectType==='Status'?effect.id:'tox';
 b.field.pseudoWeather=index%5===0?{magicroom:{}}:{};
 b.activePokemon=index%3===0?p:index%3===1?q:null;
 b.activeMove=index%2?{ignoreAbility:true}:null;
 b.event={id:'FixtureParent',modifier:1};b.effect={id:'parent',effectType:'Condition'};
 b.eventDepth=depth;b.log=Array(lines).fill('fixture');b.sentLogPos=0;
 let entered=0,error=0;
 const callback=()=>{entered++;return 17;};
 // Replace only callback collection/body; original entry guards and runEvent
 // handler suppression still execute. No native post-state is overwritten.
 b.findEventHandlers=()=>[{effect,callback,state:{},effectHolder:p}];
 try{
  if(mode)b.runEvent(event,p,null,null,23);
  else b.singleEvent(event,effect,{},p,null,null,23,callback);
 }catch(e){if(depth>=8)error=5;else if(!mode&&lines>1000)error=6;else throw e;}
 if(b.prng.getSeed()!=='1,2,3,4')throw Error('Gate used RNG');
 result.push({kind,eid,event:id('callbacks',('on'+event).toLowerCase()),mode,depth,lines,error,entered,
  status:id('conditions',p.status),magic:+!!b.field.pseudoWeather.magicroom,
  p:[id('abilities',ability),id('items',item),flags],q:[id('abilities',other),0,65],
  actor:index%3===0?576:index%3===1?1344:0,ignore:+!!b.activeMove});
}
for(const [kind,family] of ['moves','abilities','items','conditions'].entries())
 for(let eid=1;eid<cat.ids[family].names.length;eid++)
  for(let j=0;j<(kind===0?2:events.length);j++)for(let mode=0;mode<2;mode++)
   test(family,kind,eid,events[j],mode,eid+j+mode);
for(const depth of [0,7,8,9])for(const lines of [0,1000,1001])for(let mode=0;mode<2;mode++)
 test('abilities',1,id('abilities','levitate'),'BeforeMove',mode,1,depth,lines);
console.log(JSON.stringify(result));b.destroy();
"""


def main():
    cases = json.loads(subprocess.check_output(["node", "-e", SCRIPT], cwd=ROOT))
    cat = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * cat["word_count"])()
    data = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, data, len(data))
    words[2], words[8:12] = 1, [1, 2, 3, 4]
    faults = 0
    for case in cases:
        for index in range(12):
            at = 576 + index * 128
            facts = case["p"] if index == 0 else case["q"] if index == 6 else [0, 0, 0]
            words[at + 5], words[at + 6], words[at + 23] = facts
            words[at + 81] = 0
        words[589], words[544], words[548], words[549] = case["status"], case["magic"], case["actor"], case["ignore"]
        words[0], words[32:41] = 31, [case["kind"], case["eid"], case["event"], 0, 0,
                                      case["mode"], 1, case["depth"], case["lines"]]
        lib.pg9_execute(words)
        assert words[1] == case["error"], (case, words[1])
        if not case["error"]:
            assert words[16] == case["entered"], (case, words[16])
        else:
            faults += 1
        assert list(words[8:12]) == [1, 2, 3, 4]
    print(f"PASS: {len(cases)} original singleEvent/runEvent pre-callback gates, "
          f"status replacement, item/ability/weather exceptions; {faults} matching depth/line faults")


if __name__ == "__main__":
    main()
