#!/usr/bin/env python3
"""Generator component parity against the pinned executable TS implementation."""
import ctypes
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    catalog = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    native = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    native.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    native.pg9_execute.restype = None
    words = (ctypes.c_uint32 * catalog["word_count"])()
    data = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, data, len(data))
    words[2] = 1
    script = """
const {Dex,Teams}=require('./ocean/pokemon_gen9/reference.cjs');
const cat=require('./build/pokemon_gen9/catalog/catalog.json');
const g=Teams.getGenerator('gen9randombattle','1,2,3,4');
const keys=['Physical','Special','Status','damage','technician','skilllink','recoil','drain','stab','stabtera',
 'strongjaw','ironfist','sound','priority','sheerforce','inaccurate','recovery','contrary','physicalsetup',
 'specialsetup','mixedsetup','speedsetup','speedcontrol','setup','hazards'];
const result=[];
for(let tid=1;tid<cat.templates.length;tid++) {
 const t=cat.templates[tid],s=Dex.mod('gen9').species.get(t.species);
 const selected=t.movepool.slice(0,4).map(m=>Dex.mod('gen9').moves.get(m).id);
 const counter=g.queryMoves(new Set(selected),s,t.teraTypes[0],t.abilities);
 g.setSeed('1,2,3,4');
 const weather=tid%2 ? {} : {rain:1,sun:1,sand:1,snow:1};
 const ability=g.getAbility(new Set(s.types),new Set(selected),t.abilities,counter,weather,s,false,false,t.teraTypes[0],t.role);
 const abilitySeed=g.prng.getSeed().split(',').map(Number);
 const lead=tid%3===0;
 let item=g.getPriorityItem(ability,new Set(s.types),new Set(selected),counter,weather,s,lead,t.teraTypes[0],t.role,false);
 if(item===undefined)item=g.getItem(ability,new Set(s.types),new Set(selected),counter,weather,s,lead,t.teraTypes[0],t.role);
 const counts=[0,...cat.ids.types.names.slice(1).map(id=>counter.get(Dex.types.get(id).name)),
  ...keys.map(key=>counter.get(key))];
 const norm=s=>s.toLowerCase().replace(/[^a-z0-9]/g,'');
 result.push({tid,sid:cat.ids.species.map[t.species],tera:cat.ids.types.map[norm(t.teraTypes[0])],
  moves:selected.map(id=>cat.ids.moves.map[id]),counts,
  damaging:[...counter.damagingMoves].map(m=>cat.ids.moves.map[m.id]),
  base_power:[...counter.basePowerMoves].map(m=>cat.ids.moves.map[m.id]),
  ability:cat.ids.abilities.map[norm(ability)],seed:abilitySeed,lead,
  item:item?cat.ids.items.map[norm(item)]:0,itemSeed:g.prng.getSeed().split(',').map(Number),
  weather:[weather.rain||0,weather.sun||0,weather.sand||0,weather.snow||0]});
}
console.log(JSON.stringify(result));
"""
    cases = json.loads(subprocess.check_output(["node", "-e", script], cwd=ROOT))
    for case in cases:
        words[0] = 4
        words[12], words[13], words[14] = case["sid"], case["tid"], case["tera"]
        words[32] = len(case["moves"])
        words[33:33 + len(case["moves"])] = case["moves"]
        native.pg9_execute(words)
        assert words[1] == 0
        assert list(words[64:109]) == case["counts"], (case, list(words[64:109]))
        assert words[112] == len(case["damaging"])
        assert list(words[128:128 + words[112]]) == case["damaging"], case
        assert words[113] == len(case["base_power"])
        assert list(words[192:192 + words[113]]) == case["base_power"], case
        words[0] = 5
        words[8:12] = [1, 2, 3, 4]
        words[48:61] = case["weather"] + [0] * 9
        native.pg9_execute(words)
        assert words[1] == 0
        assert words[16] == case["ability"], (case, words[16])
        assert list(words[8:12]) == case["seed"], (case, list(words[8:12]))
        words[0] = 7
        words[15] = int(case["lead"])
        words[30] = case["ability"]
        native.pg9_execute(words)
        assert words[1] == 0
        assert words[16] == case["item"], (case, words[16])
        assert list(words[8:12]) == case["itemSeed"], (case, list(words[8:12]))
    words[0] = 6
    words[8:12] = [1, 2, 3, 4]
    native.pg9_execute(words)
    assert words[1] == 2 and list(words[8:12]) == [1, 2, 3, 4]
    print(f"PASS: MoveCounter, move typing, ability/item selection and RNG state for all {len(cases)} set templates; empty-sample errors")


if __name__ == "__main__":
    main()
