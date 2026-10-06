#!/usr/bin/env python3
"""Source singles move request after independently resolved lock events."""
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
const raw={species:'Mew',moves:['psychic','tackle','protect','splash'],ability:'No Ability',level:100};
const s=new Side('fixture',b,0,Array.from({length:6},()=>structuredClone(raw)));b.sides[0]=s;
const p=s.pokemon[0],result=[];
const id=(kind,name)=>name?cat.ids[kind].map[Dex.toID(name)]||0:0;
const locks=[null,'psychic','ember','recharge'];
const packLock=name=>[name==='recharge'?2:name?1:0,name==='recharge'?0:id('moves',name)];
for(const active of [false,true])for(let bits=0;bits<96;bits++)for(let hard=0;hard<4;hard++)for(let semi=0;semi<4;semi++)
 for(let variant=0;variant<4;variant++){
  const mask=Math.floor(bits/3),trapped=[false,true,'hidden'][bits%3],disabled=!!(mask&1),maybeLocked=!!(mask&2),
   maybeTrapped=!!(mask&4),canSwitch=!!(mask&8),tera=mask&16?'Ghost':false;
  p.trapped=trapped;p.maybeDisabled=disabled;p.maybeLocked=maybeLocked;p.maybeTrapped=maybeTrapped;
  p.isActive=active;s.active[0]=active?p:null;
  p.canTerastallize=tera;
  for(const bench of s.pokemon.slice(1)){bench.hp=canSwitch?bench.maxhp:0;bench.fainted=!canSwitch;}
  p.getLockedMove=()=>locks[hard];p.getSemiLockedMove=()=>locks[semi];
  p.moveSlots.forEach((slot,i)=>{slot.pp=variant===3?0:slot.maxpp;
    slot.disabled=variant===0?false:variant===1?'hidden':i%2?true:false;});
  const input=p.moveSlots.map(slot=>[id('moves',slot.id),slot.pp,slot.maxpp,
    slot.disabled===true?1:slot.disabled==='hidden'?2:0,id('targets',slot.target)]);
  const initial=[trapped===true?1:trapped==='hidden'?2:0,+disabled,+maybeLocked,+maybeTrapped,id('types',tera)];
  const data=p.getMoveRequestData();
  const rows=data.moves.map(r=>[id('moves',r.id),r.id==='recharge'||r.id==='struggle'?0:
    input.findIndex(s=>s[0]===id('moves',r.id))+1,r.pp||0,r.maxpp||0,id('targets',r.target),+!!r.disabled,
    ('pp'in r?1:0)|('maxpp'in r?2:0)|('target'in r?4:0)|('disabled'in r?8:0),
    r.id==='recharge'?1:r.id==='struggle'?2:0]);
  const flags=+!!data.trapped+2*+!!data.maybeTrapped+4*+!!data.maybeDisabled+8*+!!data.maybeLocked;
  if(Object.keys(data).some(k=>!['moves','trapped','maybeTrapped','maybeDisabled','maybeLocked','canTerastallize'].includes(k)))
    throw Error('Unexpected Gen9 request field');
  result.push({input,initial,active:+active,hard:packLock(locks[hard]),semi:packLock(locks[semi]),canSwitch:+canSwitch,
    rows,flags,tera:id('types',data.canTerastallize),
    after:[p.trapped===true?1:p.trapped==='hidden'?2:0,+p.maybeDisabled,+p.maybeLocked,+p.maybeTrapped,id('types',p.canTerastallize)]});
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
    words[2], words[8:12], words[590] = 1, [1, 2, 3, 4], 4
    for case in cases:
        words[599] = 65 if case["active"] else 0
        words[660:665] = case["initial"]
        for i, row in enumerate(case["input"]):
            move, pp, maximum, hidden, target = row
            words[608 + i * 8:616 + i * 8] = [move, pp, maximum, hidden, 0, 0, 0, target]
        words[0], words[32:39] = 30, [0, 0, *case["hard"], *case["semi"], case["canSwitch"]]
        before = list(words[512:cat["mutable_words"]])
        lib.pg9_execute(words)
        assert words[1] == 0
        assert list(words[16:19]) == [len(case["rows"]), case["flags"], case["tera"]], case
        rows = [list(words[64 + i * 8:72 + i * 8]) for i in range(words[16])]
        assert rows == case["rows"], (case, rows)
        assert list(words[660:665]) == case["after"], (case, list(words[660:665]))
        expected = before[:]
        expected[148:153] = case["after"]
        assert list(words[512:cat["mutable_words"]]) == expected
        assert list(words[8:12]) == [1, 2, 3, 4]
    before = list(words[512:cat["mutable_words"]])
    for side, pos, hard, mid, semi, sid, switch in ((2, 0, 0, 0, 0, 0, 0),
            (0, 6, 0, 0, 0, 0, 0), (0, 0, 3, 0, 0, 0, 0), (0, 0, 1, 0, 0, 0, 0),
            (0, 0, 0, 0, 3, 0, 0), (0, 0, 0, 0, 1, 99999, 0), (0, 0, 0, 0, 0, 0, 2)):
        words[0], words[32:39] = 30, [side, pos, hard, mid, semi, sid, switch]
        lib.pg9_execute(words)
        assert words[1] == 3 and list(words[512:cat["mutable_words"]]) == before
    print(f"PASS: {len(cases)} original singles move requests with resolved locks, "
          "hidden restrictions, trapping visibility, Struggle/recharge, Tera and persistent flag updates")


if __name__ == "__main__":
    main()
