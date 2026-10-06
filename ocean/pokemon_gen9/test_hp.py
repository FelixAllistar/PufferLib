#!/usr/bin/env python3
"""Advance source Pokemon and native HP/faint queue independently."""
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
const raw={species:'Mew',moves:['tackle'],ability:'No Ability',level:100,
 evs:Object.fromEntries(stats.map(k=>[k,0])),ivs:Object.fromEntries(stats.map(k=>[k,31]))};
const make=(hp,flags)=>{
 const s=new Side('fixture',b,0,Array.from({length:6},()=>structuredClone(raw)));
 b.sides[0]=s;b.faintQueue=[];
 const p=s.pokemon[0];p.hp=hp;p.fainted=!!(flags&1);p.faintQueued=!!(flags&2);
 p.switchFlag=true;return p;
};
const amounts=[[0,0,0,1],[0,0,1,2],[0,0,15,16],[0,0,1,1],[0,0,73,1],
 [0,0,1200,1],[0,0,4294967295,1],[0,1,0,1],[0,1,1,1],
 [0,2,341,1],[1,0,0,1],[1,0,1,2],[1,0,1,1],[1,0,73,1],
 [1,0,4294967295,1],[1,1,0,1],[2,0,0,1],[3,0,0,1],[4,0,0,1]];
const value=a=>a[0]===2?NaN:a[0]===3?Infinity:a[0]===4?-Infinity:
 (a[0]===1?-1:1)*(a[1]*2**32+a[2])/a[3];
const effect=b.dex.moves.get('tackle');
const step=(p,mode,amount,sourceIndex)=>{
 const source=sourceIndex<0?null:p.side.pokemon[sourceIndex];
 const result=mode===0?p.damage(value(amount),source,effect):mode===1?p.heal(value(amount)):
  mode===2?p.sethp(value(amount)):p.faint(source,effect);
 return {mode,amount,source:sourceIndex<0?0:576+sourceIndex*128,
  effect:cat.ids.moves.map.tackle,result:result===false?[1,0,0]:[0,+(result<0),Math.abs(result)],
  state:[p.hp,+p.faintQueued,+p.fainted,+!!p.switchFlag],
  queue:b.faintQueue.map(q=>[576+q.target.position*128,q.source?576+q.source.position*128:0,
   0,cat.ids.moves.map[q.effect.id]])};
};
const traces=[];
for(const hp of [0,1,73,341])for(let flags=0;flags<4;flags++){
 // A live Pokemon cannot already be fainted/queued in valid engine state.
 if(hp&&flags)continue;
 for(let mode=0;mode<4;mode++)for(const amount of amounts){
  const p=make(hp,flags);traces.push({hp,flags,steps:[step(p,mode,amount,1)]});
 }
}
let x=0x43993;const draw=n=>{x=(Math.imul(x,1664525)+1013904223)>>>0;return x%n;};
for(let n=0;n<128;n++){
 const hp=[1,73,341][n%3],p=make(hp,0),steps=[];
 for(let i=0;i<32;i++)steps.push(step(p,i===31?3:draw(3),amounts[draw(amounts.length)],draw(3)-1));
 traces.push({hp,flags:0,steps});
}
const queueP=make(341,0),queueSteps=[];
for(let i=0;i<6;i++){
 const p=queueP.side.pokemon[i];queueSteps.push({index:i,...step(p,3,amounts[0],(i+1)%6)});
 queueSteps.push({index:i,...step(p,3,amounts[0],(i+1)%6)});
}
console.log(JSON.stringify({traces,queueSteps,set:[cat.ids.species.map.mew,cat.ids.species.map.mew,0,
 cat.ids.types.map.psychic,cat.ids.abilities.map.noability,0,100,3,0,1],move:cat.ids.moves.map.tackle}));
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

    def reset(hp, flags):
        words[0], words[4], words[8:12] = 12, 0, [1, 2, 3, 4]
        words[64:74] = fixtures["set"]
        words[74] = fixtures["move"]
        words[80:86], words[86:92] = [0] * 6, [31] * 6
        lib.pg9_execute(words)
        assert words[1] == 0
        words[583], words[656], words[657], words[658] = hp, bool(flags & 2), bool(flags & 1), 1

    def step(case, index=0):
        words[0], words[32:42] = 27, [0, index, case["mode"], *case["amount"],
                                      case["source"], 0, case["effect"]]
        lib.pg9_execute(words)
        assert words[1] == 0, (case, words[1])
        assert list(words[16:19]) == case["result"], (case, list(words[16:19]))
        row = 576 + index * 128
        assert [words[row + i] for i in (7, 80, 81, 82)] == case["state"], case
        assert words[528] == len(case["queue"]), case
        assert [list(words[4096 + i * 4:4100 + i * 4]) for i in range(words[528])] == case["queue"], case
        assert list(words[8:12]) == [1, 2, 3, 4]

    count = 0
    for trace in fixtures["traces"]:
        reset(trace["hp"], trace["flags"])
        for case in trace["steps"]:
            step(case)
            count += 1
    reset(341, 0)
    # Initialize the other holders independently once, before either engine steps.
    for i in range(1, 6):
        words[576 + i * 128 + 7] = 341
        words[576 + i * 128 + 8] = 341
    for case in fixtures["queueSteps"]:
        step(case, case["index"])
        count += 1
    before = list(words[512:cat["mutable_words"]])
    for side, pos, mode, tag, high, denominator in ((2, 0, 0, 0, 0, 1),
            (0, 6, 0, 0, 0, 1), (0, 0, 4, 0, 0, 1), (0, 0, 0, 5, 0, 1),
            (0, 0, 0, 0, 65536, 1), (0, 0, 0, 0, 0, 0)):
        words[0], words[32:39] = 27, [side, pos, mode, tag, high, 1, denominator]
        lib.pg9_execute(words)
        assert words[1] == 3 and list(words[512:cat["mutable_words"]]) == before
    # Reject capacity exhaustion before any HP/flag mutation.
    words[583], words[656], words[657], words[658], words[528] = 341, 0, 0, 1, 12
    before = list(words[512:cat["mutable_words"]])
    words[0], words[32:42] = 27, [0, 0, 0, 0, 0, 1200, 1, 0, 0, fixtures["move"]]
    lib.pg9_execute(words)
    assert words[1] == 3 and list(words[512:cat["mutable_words"]]) == before
    print(f"PASS: {count} independent HP/faint transitions, rational/non-finite/U32 "
          "inputs, return tags, deferred record order and duplicate prevention; invalid input rejection")


if __name__ == "__main__":
    main()
