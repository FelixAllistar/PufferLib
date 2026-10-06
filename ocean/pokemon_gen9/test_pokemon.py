#!/usr/bin/env python3
"""Persistent PP/disable/boost traces from the original Pokemon methods."""
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
const stats=['hp','atk','def','spa','spd','spe'];
const names=['atk','def','spa','spd','spe','accuracy','evasion'];
const moves=['psychic','trumpcard','tackle','protect'];
const raw={species:'Mew',moves,ability:'No Ability',level:100,
 evs:Object.fromEntries(stats.map(k=>[k,0])),ivs:Object.fromEntries(stats.map(k=>[k,31]))};
const id=(kind,name)=>cat.ids[kind].map[name];
const make=()=>{const s=new Side('fixture',b,0,[structuredClone(raw)]);b.sides[0]=s;return s.pokemon[0];};
let x=0x74321;
const draw=n=>{x=(Math.imul(x,1664525)+1013904223)>>>0;return x%n;};
const pp=[];
for(let n=0;n<128;n++) {
 const p=make(),trace=[];
 for(let i=0;i<32;i++) {
  const move=[...moves,'ember'][draw(5)],amount=i===31?4294967295:[0,1,1,2,4][draw(5)];
  const value=p.deductPP(move,amount);
  trace.push({move:id('moves',move),amount,value,slots:p.moveSlots.map(s=>[s.pp,+s.used])});
 }
 pp.push(trace);
}
const boosts=[],p=make();
for(let n=0;n<1024;n++) {
 const mode=n%17===0?1:n%13===0?2:0;
 const order=[0,1,2,3,4,5,6];
 for(let i=6;i>0;i--){const j=draw(i+1);[order[i],order[j]]=[order[j],order[i]];}
 const count=mode===1?0:draw(8),entries=order.slice(0,count).map(index=>[index,
   mode===2?draw(13)-6:draw(21)-10]);
 const values=Object.fromEntries(entries.map(([index,delta])=>[names[index],delta]));
 const value=mode===1?(p.clearBoosts(),0):mode===2?(p.setBoost(values),0):p.boostBy(values);
 boosts.push({mode,input:entries.map(([index,delta])=>[index,delta+(mode===2?6:32768)]),
  value:value+32768,positive:p.positiveBoosts(),stages:names.map(k=>p.boosts[k]+6)});
}
const disabled=[];
for(let n=0;n<64;n++) {
 const p=make(),trace=[];
 for(let i=0;i<8;i++) {
  const move=[...moves,'ember'][draw(5)],hidden=!!draw(2);
  const effect=i%3===0?null:{name:i%3===1?'Choice Scarf':'Disable'};
  p.disableMove(move,hidden,effect);
  trace.push({move:id('moves',move),hidden,
    effectKind:effect?(i%3===1?2:0):0,
    effect:effect?id(i%3===1?'items':'moves',i%3===1?'choicescarf':'disable'):0,
    slots:p.moveSlots.map(s=>[s.disabled===true?1:s.disabled==='hidden'?2:0,s.disabledSource||''])});
 }
 disabled.push(trace);
}
const initial=make();
console.log(JSON.stringify({pp,boosts,disabled,moves:moves.map(x=>id('moves',x)),
 set:[id('species','mew'),id('species','mew'),0,id('types','psychic'),id('abilities','noability'),
 0,100,3,0,4],initialPP:initial.moveSlots.map(s=>s.pp)}));
b.destroy();
"""


def main():
    fixtures = json.loads(subprocess.check_output(["node", "-e", SCRIPT], cwd=ROOT))
    cat = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * cat["word_count"])()
    binary = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, binary, len(binary))
    words[2] = 1

    def reset():
        words[0], words[8:12] = 12, [1, 2, 3, 4]
        words[64:74] = fixtures["set"]
        words[74:78] = fixtures["moves"]
        words[80:86], words[86:92] = [0] * 6, [31] * 6
        lib.pg9_execute(words)
        assert words[1] == 0
        assert [words[609 + i * 8] for i in range(4)] == fixtures["initialPP"]

    for trace in fixtures["pp"]:
        reset()
        for case in trace:
            words[0], words[32:36] = 19, [0, 0, case["move"], case["amount"]]
            lib.pg9_execute(words)
            assert words[1] == 0 and words[16] == case["value"], (case, words[16])
            assert [[words[609 + i * 8], words[612 + i * 8]] for i in range(4)] == case["slots"]
            assert list(words[8:12]) == [1, 2, 3, 4]
    reset()
    for case in fixtures["boosts"]:
        words[0], words[32:36] = 20, [0, 0, case["mode"], len(case["input"])]
        words[64:64 + len(case["input"]) * 2] = [x for row in case["input"] for x in row]
        lib.pg9_execute(words)
        assert words[1] == 0
        assert list(words[16:18]) == [case["value"], case["positive"]], (case, list(words[16:18]))
        assert list(words[600:607]) == case["stages"]
        assert list(words[8:12]) == [1, 2, 3, 4]
    for trace in fixtures["disabled"]:
        reset()
        for case in trace:
            words[0], words[32:38] = 21, [0, 0, case["move"], case["hidden"], case["effectKind"], case["effect"]]
            lib.pg9_execute(words)
            assert words[1] == 0
            for i, (disabled, source) in enumerate(case["slots"]):
                slot = 608 + i * 8
                assert words[slot + 3] == disabled, (case, i)
                if source:
                    source_name = "".join(c.lower() for c in source if c.isascii() and c.isalnum())
                    kind = "items" if source_name == "choicescarf" else "moves"
                    assert words[slot + 5] == (2 if kind == "items" else 0), (case, i)
                    assert words[slot + 6] == cat["ids"][kind]["map"][source_name], (case, i)
            assert list(words[8:12]) == [1, 2, 3, 4]
    # Reject duplicated/invalid boost keys before any mutation.
    before = list(words[576:704])
    for entries in ([0, 32769, 0, 32767], [7, 32769], [0, 65536]):
        words[0], words[32:36] = 20, [0, 0, 0, len(entries) // 2]
        words[64:64 + len(entries)] = entries
        lib.pg9_execute(words)
        assert words[1] == 3 and list(words[576:704]) == before
    for op in (19, 20, 21):
        words[0], words[32], words[33], words[35] = op, 2, 0, 0
        lib.pg9_execute(words)
        assert words[1] == 3 and list(words[576:704]) == before
    print("PASS: 4096 PP deductions, 1024 ordered boost transitions, 512 disable "
          "transitions; persistent state, RNG preservation and invalid-input rejection")


if __name__ == "__main__":
    main()
