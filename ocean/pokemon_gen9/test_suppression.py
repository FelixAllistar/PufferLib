#!/usr/bin/env python3
"""Resolved Dex/source ability, item, gas, weather and attacker suppression."""
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
const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
const raw={species:'Mew',moves:['tackle'],ability:'No Ability',level:100};
const s0=new Side('one',b,0,[structuredClone(raw)]),s1=new Side('two',b,1,[structuredClone(raw)]);
b.sides[0]=s0;b.sides[1]=s1;
const p=s0.pokemon[0],q=s1.pokemon[0],result=[];
const masks=[65,64,0,67,69,73,81,97,101,105,109,121,125,127,1,5];
const items=['','abilityshield','leftovers','redorb','blueorb','laggingtail'];
const abilities=['noability','klutz','disguise','neutralizinggas','cloudnine','airlock','levitate'];
const set=(mon,side,ability,item,flags,fainted)=>{
 mon.ability=ability;mon.item=item;mon.isActive=!!(flags&1);mon.transformed=!!(flags&2);
 mon.fainted=!!fainted;mon.volatiles={};mon.abilityState={ending:!!(flags&32)};
 for(const [key,bit] of [['gastroacid',4],['embargo',8],['commanding',16]])if(flags&bit)mon.volatiles[key]={};
 side.active[0]=(flags&64)?mon:null;
};
const test=(ability,item,flags,gasAbility,gasFlags,fainted,magic,actor,ignore)=>{
 set(p,s0,ability,item,flags,0);set(q,s1,gasAbility,'',gasFlags,fainted);
 b.field.pseudoWeather=magic?{magicroom:{}}:{};
 b.activePokemon=actor===1?p:actor===7?q:null;
 b.activeMove=ignore?{ignoreAbility:true}:null;
 result.push({p:[cat.ids.abilities.map[ability],cat.ids.items.map[item]||0,flags,0],
 q:[cat.ids.abilities.map[gasAbility],0,gasFlags,fainted],magic,actor,ignore,
 expected:[+p.ignoringAbility(),+p.ignoringItem(),+p.ignoringItem(true),
  +b.field.suppressingWeather(),+!!b.suppressingAbility(p)]});
};
// Assert the closed weather list against every pinned ability before using it.
const weather=Dex.mod('gen9').abilities.all().filter(a=>a.suppressWeather).map(a=>a.id).sort();
if(JSON.stringify(weather)!==JSON.stringify(['airlock','cloudnine']))throw Error('Weather inventory changed');
for(const ability of cat.ids.abilities.names.slice(1))for(let i=0;i<masks.length;i++)
 test(ability,items[i%items.length],masks[i],abilities[i%abilities.length],masks[(i*7)%masks.length],
  +(i%7===0),+(i%5===0),i%3===0?0:i%3===1?1:7,+(i%4!==0));
for(const item of cat.ids.items.names.slice(1))for(let i=0;i<8;i++)
 test(abilities[i%abilities.length],item,masks[i],i%2?'neutralizinggas':'cloudnine',65,
  +(i%7===0),+(i%5===0),i%3===0?0:i%3===1?1:7,+(i%4!==0));
// Exercise Shield/Klutz and the gas source's inactive, ending, transformed and fainted states.
for(const ability of abilities)for(const item of items)for(const flags of masks)
 for(const gasFlags of [65,64,67,69,97])for(const fainted of [0,1])
  test(ability,item,flags,'neutralizinggas',gasFlags,fainted,+(flags===105),7,1);
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
    words[2] = 1
    words[8:12] = [1, 2, 3, 4]
    for case in cases:
        for index in range(12):
            at = 576 + index * 128
            facts = case["p"] if index == 0 else case["q"] if index == 6 else [0, 0, 0, 0]
            words[at + 5], words[at + 6], words[at + 23], words[at + 81] = facts
        words[0], words[32:36], words[544] = 28, [0, 0, case["actor"], case["ignore"]], case["magic"]
        lib.pg9_execute(words)
        assert words[1] == 0
        assert list(words[16:21]) == case["expected"], (case, list(words[16:21]))
        assert list(words[8:12]) == [1, 2, 3, 4]
    before = list(words[512:cat["mutable_words"]])
    for side, pos, actor, ignore in ((2, 0, 0, 0), (0, 6, 0, 0), (0, 0, 13, 0), (0, 0, 0, 2)):
        words[0], words[32:36] = 28, [side, pos, actor, ignore]
        lib.pg9_execute(words)
        assert words[1] == 3 and list(words[512:cat["mutable_words"]]) == before
    print(f"PASS: {len(cases)} ability/item/Fling/weather/attacker suppression combinations, "
          "all 321 abilities and 583 items; full gas/Shield lifecycle flags and invalid input rejection")


if __name__ == "__main__":
    main()
