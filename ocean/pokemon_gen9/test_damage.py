#!/usr/bin/env python3
"""Compare numeric damage stages to the original getDamage and confusion paths.

Custom Showdown event hooks supply resolved modifiers and effectiveness. This
isolates rounding/RNG; it does not validate native event collection or handlers.
"""
import ctypes
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]

SCRIPT = """
const {oracle}=require('./ocean/pokemon_gen9/reference.cjs');
const {Battle}=require(oracle+'/dist/sim/battle');
const {Side}=require(oracle+'/dist/sim/side');
const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
const raw={species:'Mew',moves:['psychic'],ability:'No Ability',level:100};
b.sides[0]=new Side('p1',b,0,[structuredClone(raw)]);
b.sides[1]=new Side('p2',b,1,[structuredClone(raw)]);
const p=b.sides[0].pokemon[0],q=b.sides[1].pokemon[0];
let config;
q.runImmunity=()=>true;
q.runEffectiveness=()=>config.exponent-6;
b.onEvent('WeatherModifyDamage',b.format,function() {this.chainModify([config.weather,4096]);});
b.onEvent('ModifySTAB',b.format,()=>config.stab/4096);
b.onEvent('ModifyDamage',b.format,function() {this.chainModify([config.final,4096]);});
b.onEvent('CriticalHit',b.format,()=>!!config.allowed);
let x=0x12345678;
const draw=n=>{x=(Math.imul(x,1664525)+1013904223)>>>0;return x%n;};
const seed=()=>[draw(65536),draw(65536),draw(65536),draw(65536)];
const mods=[0,2048,3072,4096,4915,5324,6144,8192,9216];
const cases=[],confusion=[],rolls=new Set();
const originalRandom=b.random.bind(b);
b.random=(...args)=>{const out=originalRandom(...args);if(args[0]===16)rolls.add(out);return out;};
for(let n=0;n<960;n++) {
 const flags=n%16,will=Math.floor(n/16)%3,ratio=Math.floor(n/48)%5,allowed=Math.floor(n/240)%2;
 config={level:1+draw(100),power:1+draw(250),attack:1+draw(4000),defense:1+draw(4000),
   flags,weather:mods[draw(mods.length)],stab:mods[draw(mods.length)],
   exponent:draw(13),final:mods[draw(mods.length)],will,ratio,allowed};
 const start=seed();b.resetRNG(start);
 p.level=config.level;p.storedStats.atk=config.attack;q.storedStats.def=config.defense;
 p.status=flags&4?'brn':'';
 const move=b.dex.getActiveMove('psychic');
 move.type=flags&2?'???':'Psychic';move.category='Physical';move.basePower=config.power;
 move.critRatio=ratio;move.willCrit=will===0?undefined:will===2;
 if(flags&1){move.multihitType='parentalbond';move.hit=2;}
 const hit=q.getMoveHitData(move);hit.bypassProtect=!!(flags&8);
 const amount=b.actions.getDamage(p,q,move,true);
 cases.push({...config,seed:start,amount,crit:+hit.crit,after:b.prng.getSeed().split(',').map(Number)});
}
for(let n=0;n<512;n++) {
 const config={level:1+draw(100),power:40,attack:1+draw(10000),defense:1+draw(10000)};
 // Exercise the pre-roll 16-bit boundary in the actual confusion path.
 if(n===0)Object.assign(config,{level:100,attack:195051,defense:100});
 if(n===1)Object.assign(config,{level:100,attack:195048,defense:100});
 const start=seed();b.resetRNG(start);
 p.status='';p.level=config.level;p.storedStats.atk=config.attack;p.storedStats.def=config.defense;
 const amount=b.actions.getConfusionDamage(p,config.power);
 confusion.push({...config,seed:start,amount,after:b.prng.getSeed().split(',').map(Number)});
}
b.destroy();
if(rolls.size!==16)throw Error('fixtures do not exercise all damage rolls');
console.log(JSON.stringify({cases,confusion}));
"""


def main():
    fixtures = json.loads(subprocess.check_output(["node", "-e", SCRIPT], cwd=ROOT))
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())["word_count"])()
    for case in fixtures["cases"]:
        words[0], words[8:12] = 16, case["seed"]
        words[32:44] = [case[k] for k in ("level", "power", "attack", "defense",
                                       "flags", "weather", "stab", "exponent", "final",
                                       "will", "ratio", "allowed")]
        lib.pg9_execute(words)
        assert words[1] == 0
        assert words[16] == case["amount"], (case, list(words[16:19]))
        assert words[18] == case["crit"], (case, list(words[16:19]))
        assert list(words[8:12]) == case["after"], (case, list(words[8:12]))
    for case in fixtures["confusion"]:
        words[0], words[8:12] = 17, case["seed"]
        words[32:36] = [case[k] for k in ("level", "power", "attack", "defense")]
        lib.pg9_execute(words)
        assert words[1] == 0
        assert words[16] == case["amount"], (case, words[16])
        assert list(words[8:12]) == case["after"], (case, list(words[8:12]))
    for op in (16, 17):
        words[0], words[8:12], words[35] = op, [1, 2, 3, 4], 0
        lib.pg9_execute(words)
        assert words[1] == 3 and list(words[8:12]) == [1, 2, 3, 4]
    print(f"PASS: {len(fixtures['cases'])} resolved damage/crit cases, all 16 rolls; "
          f"{len(fixtures['confusion'])} confusion cases; exact RNG and zero-defense rejection")


if __name__ == "__main__":
    main()
