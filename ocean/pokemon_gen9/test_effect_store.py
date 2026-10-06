#!/usr/bin/env python3
"""Original effect-state helpers and retained native object aliases across calls."""
import ctypes
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = r"""
const {oracle}=require('./ocean/pokemon_gen9/reference.cjs');
const {Battle}=require(oracle+'/dist/sim/battle'),{Side}=require(oracle+'/dist/sim/side');
const cat=require('./build/pokemon_gen9/catalog/catalog.json');
const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
b.sides[0]=new Side('one',b,0,[{species:'Mew',moves:['tackle'],ability:'No Ability'}]);
const p=b.sides[0].pokemon[0];
const keys=['','id','target','effectOrder','counter','duration','source','sourceEffect','started','other','nested','falseValue','missing'];
let objects=[null],references=new WeakMap(),random=0x19394ec7;
const special=new WeakMap([[p,[1,576]],[p.side,[2,0]],[b.field,[3,0]],[b,[4,0]]]);
const pick=n=>{random=(Math.imul(random,1664525)+1013904223)>>>0;return random%n;};
function encode(v){
 if(v===undefined)return [0,0,0,0];if(v===null)return [1,0,0,0];
 if(typeof v==='boolean')return [v?3:2,0,0,0];
 if(typeof v==='string')return [9,0,cat.ids.strings.map[v]||0,0];
 if(typeof v==='object')return [10,...(special.get(v)||[7,references.get(v)]),0];
 if(Number.isNaN(v))return [6,0,0,0];if(v===Infinity)return [7,0,0,0];if(v===-Infinity)return [8,0,0,0];
 const sign=v<0||Object.is(v,-0)?5:4,n=Math.abs(v);
 return Number.isInteger(v)?[sign,Math.floor(n/2**32),n>>>0,1]:[sign,0,Math.round(n*10),10];
}
const values=[undefined,null,false,true,0,-0,1,7,4294967295,-3,0.5,-0.1,NaN,Infinity,-Infinity,
 ...cat.ids.strings.names,p,p.side,b.field,b];
function value(){return objects.length>1&&pick(7)===0?objects[1+pick(objects.length-1)]:values[pick(values.length)];}
const cases=[];
function command(mode,reference=0,key=0,fields=[],flags=[0,0,0],order=undefined,written=undefined){
 const inputReference=reference;
 const inputFields=fields.map(([key,v])=>[key,...encode(v)]),inputValue=encode(written),inputOrder=encode(order);
 p.isActive=!!flags[1];let out;
 if(mode===0){b.effectOrder=reference;objects=[null];references=new WeakMap();reference=0;}
 else if(mode===1||mode===6){
  const obj=mode===6?{...objects[reference]}:{};
  for(const [key,v]of fields)obj[keys[key]]=v;
  out=b.initEffectState(obj,flags[2]?order:undefined);
  reference=objects.length;objects.push(out);references.set(out,reference);
  out=obj;
 }else if(mode===2)out=objects[reference][keys[key]];
 else if(mode===3)objects[reference][keys[key]]=written;
 else if(mode===4)delete objects[reference][keys[key]];
 else if(mode===5)b.clearEffectState(objects[reference]);
 const obj=objects[reference],snapshot=obj?Object.entries(obj).map(([k,v])=>[keys.indexOf(k),...encode(v)]):[];
 cases.push({mode,inputReference,
  key,fields:inputFields,flags,order:inputOrder,value:inputValue,reference,next:objects.length,counter:b.effectOrder,
  output:encode(out),snapshot});
}
command(0,0);
// Every combination of id/target truthiness and active Pokemon metadata,
// explicit undefined/null/zero/negative/non-finite order and automatic order.
for(const id of ['',undefined,null,false,0,'bide'])for(const target of [undefined,null,false,p,p.side,b.field,b])
 for(const active of [false,true])for(const explicit of [false,true]){
  command(1,0,0,[[4,7],[1,id],[2,target],[5,3]], [+ (target===p),+active,+explicit],
   explicit?[undefined,null,-0,-3,NaN,Infinity][pick(6)]:undefined);
 }
// Shallow cloning retains a nested object alias; clearing a scope/object
// changes neither that nested object nor its reference in the clone.
command(1,0,0,[[1,'bide'],[2,p],[4,7]],[1,1,0]);const inner=objects.length-1;
command(1,0,0,[[1,'rollout'],[2,p],[10,objects[inner]]],[1,1,0]);const outer=objects.length-1;
command(6,outer,0,[[5,9]],[1,1,0]);const clone=objects.length-1;
command(3,inner,4,[],[0,0,0],undefined,23);
command(2,clone,10);command(2,inner,4);command(5,outer);command(2,clone,10);
command(7,clone);command(7,inner);
for(let n=0;n<2400;n++){
 const mode=[1,2,3,4,5,6,7][pick(7)],ref=1+pick(objects.length-1),key=1+pick(keys.length-1);
 if(mode===1||mode===6){
  const target=[p,p.side,b.field,b,undefined,null][pick(6)],active=pick(2),explicit=pick(2);
  const fields=[[1,['','iceball','bide'][pick(3)]],[2,target]];
  for(let i=0,count=pick(7);i<count;i++)fields.push([1+pick(keys.length-1),value()]);
  // Derive instanceof from the final target value, including clone overrides.
  const probe=mode===6?{...objects[ref]}:{};
  for(const [key,v]of fields)probe[keys[key]]=v;
  command(mode,ref,key,fields,[+(probe.target===p),active,explicit],value());
 }else command(mode,ref,key,[],[0,0,0],undefined,value());
}
command(0,4294967294);
command(1,0,0,[[1,'bide'],[2,b]],[0,0,0]);
command(1,0,0,[[1,'bide'],[2,p]],[1,0,0]);
command(1,0,0,[[1,'bide'],[2,b]],[0,0,1],7);
console.log(JSON.stringify(cases));b.destroy();
"""


def main():
    cases = json.loads(subprocess.check_output(["node", "-e", SCRIPT], cwd=ROOT))
    cat = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * cat["word_count"])()
    binary = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, binary, len(binary))
    words[2], words[8:12] = 1, [1, 2, 3, 4]
    private = [0xEFFE0000 + i for i in range(512)]
    words[2176:2688] = private
    for case in cases:
        words[0] = 40
        words[32:42] = [case["mode"], case["inputReference"],
                        len(case["fields"]) if case["mode"] in (1, 6) else case["key"],
                        *case["flags"], *case["order"]]
        words[64:68] = case["value"]
        for index, field in enumerate(case["fields"]):
            words[8192 + index * 5:8197 + index * 5] = field
        lib.pg9_execute(words)
        assert words[1] == 0, (case, words[1])
        assert list(words[16:20]) == [case["reference"], case["next"], case["counter"], len(case["snapshot"])], (case, list(words[16:20]))
        assert list(words[64:68]) == case["output"], (case, list(words[64:68]))
        actual = [list(words[8192 + i * 5:8197 + i * 5]) for i in range(words[19])]
        assert actual == case["snapshot"], (case, actual)
        assert list(words[8:12]) == [1, 2, 3, 4]
        assert list(words[2176:2688]) == private
    # Counter overflow is an explicit domain error, never a silent wrap.
    words[0], words[32:38] = 40, [1, 0, 2, 0, 0, 0]
    words[8192:8202] = [1, 9, 0, cat["ids"]["strings"]["map"]["bide"], 0, 2, 10, 4, 0, 0]
    lib.pg9_execute(words)
    assert words[1] == 3
    for mode in (2, 3, 4, 5, 6, 7):
        words[0], words[32:38] = 40, [mode, 999999, 1, 0, 0, 0]
        words[64:68] = [3, 0, 0, 0]
        lib.pg9_execute(words)
        assert words[1] == 3
    print(f"PASS: {len(cases)} original effect-state/helper transitions across retained native calls; "
          "creation order, shallow clones/aliases, field ordering/deletion, clear semantics and exact RNG; invalid references/overflow rejected")


if __name__ == "__main__":
    main()
