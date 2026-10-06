#!/usr/bin/env python3
"""Independent resolvePriority metadata fixtures; no handler execution claim."""
import ctypes
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]

SCRIPT = """
const {oracle}=require('./ocean/pokemon_gen9/reference.cjs');
const {Battle}=require(oracle+'/dist/sim/battle');
const {Side}=require(oracle+'/dist/sim/side');
const cat=require('./build/pokemon_gen9/catalog/catalog.json');
const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
const raw={species:'Mew',moves:['psychic'],ability:'No Ability'};
b.sides[0]=new Side('p1',b,0,[structuredClone(raw)]);
b.sides[1]=new Side('p2',b,1,[structuredClone(raw)]);
const p=b.sides[0].pokemon[0],q=b.sides[1].pokemon[0];
p.storedStats.spe=123;
const kinds=['Condition','Ability','Item','Format','Weather','Rule','Ruleset','Status'];
const callbacks=['onResidual','onSwitchIn','onFoeRedirectTarget','onAllyTryHitSide'];
const rows=[];
function check(kind,ability,scope,sub,callback,holder,cached,rank) {
 const cb=callbacks[callback],name=ability?b.dex.abilities.get(ability).name:'fixture';
 const effect={effectType:kinds[kind],name,[cb+'SubOrder']:sub};
 const target=scope===1||scope===2?b.sides[0]:scope===3?b.field:scope===0?p:{};
 const state={target,isSlotCondition:scope===2,effectOrder:17};
 p.speed=cached;
 b.speedOrder=rank===0?[]:rank===1?[p.getFieldPositionValue(),q.getFieldPositionValue()]:
   [q.getFieldPositionValue(),p.getFieldPositionValue()];
 const out=b.resolvePriority({effect,callback:()=>{},state,end:null,effectHolder:holder?p:b},cb);
 const flags=(callback===1?1:callback===2?2:callback===3?4:0)|(holder?8:0);
 rows.push({input:[kind,ability?cat.ids.abilities.map[ability]:0,scope,sub+32768,
   flags,17,cached,123,rank],
  output:[out.subOrder+32768,out.effectOrder||0,Math.round((out.speed||0)*2+32768)]});
}
for(const ability of Object.keys(cat.ids.abilities.map))for(let callback=0;callback<4;callback++)
 for(const sub of [0,-3,6])check(1,ability,0,sub,callback,true,callback===1?0:42,2);
for(let kind=0;kind<8;kind++)for(let scope=0;scope<5;scope++)
 for(let callback=0;callback<4;callback++)for(const holder of [false,true])
  for(let rank=0;rank<3;rank++)check(kind,kind===1?'magicbounce':null,scope,0,callback,holder,42,rank);
b.destroy();
console.log(JSON.stringify(rows));
"""


def main():
    fixtures = json.loads(subprocess.check_output(["node", "-e", SCRIPT], cwd=ROOT))
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())["word_count"])()
    for case in fixtures:
        words[0], words[8:12], words[32:41] = 18, [1, 2, 3, 4], case["input"]
        lib.pg9_execute(words)
        assert words[1] == 0
        assert list(words[16:19]) == case["output"], (case, list(words[16:19]))
        assert list(words[8:12]) == [1, 2, 3, 4]
    print(f"PASS: {len(fixtures)} listener metadata cases, ability exceptions, "
          "side/slot/field ordering, SwitchIn fractional speed and Magic Bounce")


if __name__ == "__main__":
    main()
