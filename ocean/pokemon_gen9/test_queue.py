#!/usr/bin/env python3
"""Compare original selection-sort swaps, tie shuffle and resulting RNG state."""
import ctypes
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    script = """
const {oracle}=require('./ocean/pokemon_gen9/reference.cjs');
const {Battle}=require(oracle+'/dist/sim/battle');
const cases=[],b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
let x=0x3456789;
const draw=n=>{x=(Math.imul(x,1664525)+1013904223)>>>0;return x%n;};
const orders=[0,1,2,3,4,5,6,101,103,106,107,200,300];
for(let n=0;n<512;n++) {
 const count=n<65?n:draw(65);
 const rows=Array.from({length:count},(_,i)=>n%4===0?[i,200,32768,32968,32768,0,100+i,200+i]:
   n%4===1?[i,orders[draw(orders.length)],32768+draw(151)-75,32768+draw(403)-2,32768+draw(20)-10,draw(8),100+i,200+i]:
   [i,draw(3)?200:106,32768+draw(3)-1,32768+draw(3)*100,32768,0,100+i,200+i]);
 const seed=[n,2,3,4];
 b.resetRNG(seed);
 const records=rows.map(r=>({row:r,order:r[1],priority:(r[2]-32768)/10,
   speed:(r[3]-32768)/2,subOrder:r[4]-32768,effectOrder:r[5]}));
 b.speedSort(records);
 cases.push({mode:0,seed,input:rows,output:records.map(x=>x.row),after:b.prng.getSeed().split(',').map(Number)});
}
for(const mode of [1,2])for(let n=0;n<256;n++) {
 const count=n<65?n:draw(65);
 const rows=Array.from({length:count},(_,i)=>[i,orders[draw(orders.length)],
   32768+draw(5)-2,32768+draw(5)-1,32768+draw(5),draw(5),
   n%3===0?0:1+draw(5),draw(5)]);
 const seed=[n,7,11,23];b.resetRNG(seed);
 const records=rows.map(r=>({row:r,order:r[1],priority:(r[2]-32768)/10,
   speed:(r[3]-32768)/2,subOrder:r[4]-32768,effectOrder:r[5],index:r[7],
   effectHolder:r[6]?{abilityState:{effectOrder:r[6]-1}}:null}));
 records.sort(mode===1?Battle.compareRedirectOrder:Battle.compareLeftToRightOrder);
 cases.push({mode,seed,input:rows,output:records.map(x=>x.row),after:b.prng.getSeed().split(',').map(Number)});
}
for(let n=0;n<65;n++)for(const mode of [0,1,2]){
 const bytes=Buffer.alloc(16);bytes.writeUInt32LE(n,0);
 b.resetRNG('sodium,'+bytes.toString('hex'));const seed=b.prng.getSeed();
 const rows=Array.from({length:n},(_,i)=>[i,200,32768,32968,32768,0,1,0]);
 const records=rows.map(r=>({row:r,order:r[1],priority:0,speed:100,subOrder:0,effectOrder:0,
   effectHolder:{abilityState:{effectOrder:0}},index:0}));
 if(mode===0)b.speedSort(records);
 else records.sort(mode===1?Battle.compareRedirectOrder:Battle.compareLeftToRightOrder);
 cases.push({mode,seed,input:rows,output:records.map(x=>x.row),after:b.prng.getSeed()});
}
b.destroy();
console.log(JSON.stringify(cases));
"""
    cases = json.loads(subprocess.check_output(["node", "-e", script], cwd=ROOT))
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())["word_count"])()
    def sodium_limbs(value):
        data = bytes.fromhex(value.split(",")[1])
        return [int.from_bytes(data[i:i + 4], "little") for i in range(0, 32, 4)]
    for case in cases:
        for operation in (15, 36):
            if isinstance(case["seed"], str):
                words[0], words[8:16] = 24, sodium_limbs(case["seed"])
                lib.pg9_execute(words)
                assert words[1] == 0
            else:
                words[4], words[8:12] = 0, case["seed"]
            words[0] = operation
            if operation == 15:
                words[12], words[13] = len(case["input"]), case["mode"]
            else:
                words[32], words[33] = len(case["input"]), case["mode"]
            words[8192:8704] = [0xA5A5A5A5] * 512
            words[8192:8192 + len(case["input"]) * 8] = [x for row in case["input"] for x in row]
            action_rows = [(0xA31C7E00 + i) & 0xFFFFFFFF for i in range(512)]
            if operation == 36:
                words[2176:2688] = action_rows
            lib.pg9_execute(words)
            assert words[1] == 0
            assert words[16] == len(case["input"])
            assert list(words[8192:8192 + words[16] * 8]) == [x for row in case["output"] for x in row], (
                case, operation, list(words[8192:8192 + words[16] * 8]))
            if isinstance(case["after"], str):
                assert list(words[560:568]) == sodium_limbs(case["after"]), (case, list(words[560:568]))
            else:
                assert list(words[8:12]) == case["after"], (case, list(words[8:12]))
            if operation == 36:
                assert list(words[2176:2688]) == action_rows
    words[0], words[4], words[12], words[13], words[8:12] = 15, 0, 65, 0, [1, 2, 3, 4]
    lib.pg9_execute(words)
    assert words[1] == 3 and list(words[8:12]) == [1, 2, 3, 4]
    words[12], words[13] = 2, 3
    lib.pg9_execute(words)
    assert words[1] == 3 and list(words[8:12]) == [1, 2, 3, 4]
    words[0], words[32], words[33] = 36, 65, 0
    lib.pg9_execute(words)
    assert words[1] == 3
    words[32], words[33] = 2, 3
    lib.pg9_execute(words)
    assert words[1] == 3
    print(f"PASS: {len(cases)} source sorts in each of two buffers ({len(cases) * 2} transitions), "
          "stable ties, payloads, exact RNG and preserved action rows during listener sorting; invalid input rejection")


if __name__ == "__main__":
    main()
