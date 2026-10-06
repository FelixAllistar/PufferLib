#!/usr/bin/env python3
"""Public move-choice projection, including hidden restrictions and locks."""
import ctypes
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = """
const {oracle,Dex,Teams}=require('./ocean/pokemon_gen9/reference.cjs');
const {Battle}=require(oracle+'/dist/sim/battle');
const {Side}=require(oracle+'/dist/sim/side');
const cat=require('./build/pokemon_gen9/catalog/catalog.json');
const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
const id=(family,name)=>name?cat.ids[family].map[Dex.toID(name)]:0;
const result=[];
function cases(p,variant){
 const input=p.moveSlots.map(s=>[id('moves',s.id),s.pp,s.maxpp,
   s.disabled===true?1:s.disabled==='hidden'?2:0,id('targets',s.target)]);
 const locks=[null,'recharge',p.moveSlots[0].id,'ember'];
 for(const restrict of [false,true])for(const lock of locks){
  const rows=p.getMoves(lock,restrict).map(r=>[id('moves',r.id)||0,
   r.id==='recharge'?0:input.findIndex(s=>s[0]===id('moves',r.id))+1,
   r.pp||0,r.maxpp||0,id('targets',r.target)||0,+!!r.disabled,
   ('pp'in r?1:0)|('maxpp'in r?2:0)|('target'in r?4:0)|('disabled'in r?8:0),+(r.id==='recharge')]);
  result.push({input,first:id('types',p.types[0]),second:id('types',p.types[1]),
   third:id('types',p.addedType),species:id('species',p.species.id),
   flags:p.volatiles.healblock?128:0,locked:lock==='recharge'?2:lock?1:0,
   move:lock==='recharge'?0:id('moves',lock),restrict:+restrict,rows,variant});
 }
}
const g=Teams.getGenerator('gen9randombattle','1,2,3,4');
for(const sid of Object.keys(g.randomSets)){
 g.setSeed('1,2,3,4');
 const raw=g.randomSet(Dex.species.get(sid),{},false,false);
 const s=new Side('fixture',b,0,[raw]);b.sides[0]=s;const p=s.pokemon[0];
 for(let variant=0;variant<4;variant++){
  p.moveSlots.forEach((slot,i)=>{slot.pp=variant===3?0:variant===2&&i%2?0:slot.maxpp;
   slot.disabled=variant===0?false:variant===1?'hidden':i%3===0?true:i%3===1?'hidden':false;});
  cases(p,variant);
 }
}
const s=new Side('special',b,0,[{species:'Mew',moves:['curse','pollenpuff','terastarstorm','tackle']}]);
b.sides[0]=s;const p=s.pokemon[0];
for(const ghost of [false,true])for(const added of [false,true])for(const healblock of [false,true])
 for(const stellar of [false,true]){
  p.types=[ghost?'Ghost':'Normal'];p.addedType=added?'Ghost':'';
  p.volatiles=healblock?{healblock:{}}:{};
  p.species=Dex.species.get(stellar?'Terapagos-Stellar':'Mew');
  cases(p,'target overrides');
 }
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
        words[577], words[587], words[588], words[659], words[599] = (
            case["species"], case["first"], case["second"], case["third"], case["flags"])
        words[590] = len(case["input"])
        for i, row in enumerate(case["input"]):
            mid, pp, maximum, hidden, target = row
            words[608 + i * 8:616 + i * 8] = [mid, pp, maximum, hidden, 0, 0, 0, target]
        before = list(words[512:cat["mutable_words"]])
        words[0], words[32:37] = 29, [0, 0, case["locked"], case["move"], case["restrict"]]
        lib.pg9_execute(words)
        assert words[1] == 0
        assert words[16] == len(case["rows"]), (case, words[16])
        actual = [list(words[64 + i * 8:72 + i * 8]) for i in range(words[16])]
        assert actual == case["rows"], (case, actual)
        assert list(words[512:cat["mutable_words"]]) == before
        assert list(words[8:12]) == [1, 2, 3, 4]
    before = list(words[512:cat["mutable_words"]])
    for side, pos, locked, move, restrict in ((2, 0, 0, 0, 0), (0, 6, 0, 0, 0),
            (0, 0, 3, 0, 0), (0, 0, 1, 0, 0), (0, 0, 1, 99999, 0), (0, 0, 0, 0, 2)):
        words[0], words[32:37] = 29, [side, pos, locked, move, restrict]
        lib.pg9_execute(words)
        assert words[1] == 3 and list(words[512:cat["mutable_words"]]) == before
    print(f"PASS: {len(cases)} source move-choice projections across all 509 set species, "
          "PP exhaustion, hidden disables, locks/recharge, target overrides and immutable state")


if __name__ == "__main__":
    main()
