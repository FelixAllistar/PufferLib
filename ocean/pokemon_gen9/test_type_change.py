#!/usr/bin/env python3
"""Original setType/addType with retained graphs and apparent string snapshots.

Explicit fixture species/Tera/known-state edits prepare method inputs; they are
not evidence of ported form/Tera lifecycles. Source methods are never replaced.
"""
import ctypes
import json
from pathlib import Path
import subprocess
from native_test_helpers import EffectHeap

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = r"""
const {oracle,Dex}=require('./ocean/pokemon_gen9/reference.cjs');
const {Battle}=require(oracle+'/dist/sim/battle'),{Side}=require(oracle+'/dist/sim/side');
const cat=require('./build/pokemon_gen9/catalog/catalog.json'),dex=Dex.mod('gen9');
const strings=new Map(cat.ids.strings.names.map((v,i)=>[v,i]));
const pretexts=['type\u0000name','\ud800','\ud83d\ude00','é','e\u0301','Aa','BB'].filter(v=>!strings.has(v));
pretexts.forEach((v,i)=>strings.set(v,cat.ids.strings.names.length+i));
const id=(family,name)=>cat.ids[family].map[name]||0;
const units=v=>Array.from({length:v.length},(_,i)=>v.charCodeAt(i));
const groups=[];
function sequence(speciesNames,rich=false){
 const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
 const raw={species:'Mew',moves:['tackle'],ability:'No Ability'};
 const side=new Side('one',b,0,[structuredClone(raw),structuredClone(raw)]);b.sides[0]=side;
 const mons=side.pokemon,objects=[],refs=new WeakMap();
 function register(arr){if(!refs.has(arr)){refs.set(arr,objects.length+1);objects.push(arr);}return refs.get(arr);}
 function encode(v){if(v===undefined)return [0,0,0,0];if(v===null)return [1,0,0,0];
  if(typeof v==='string'){if(!strings.has(v))throw Error('Unencoded type '+v);return [9,0,strings.get(v),0];}
  if(typeof v==='number')return [4,0,v,1];throw Error('Type cell '+v);}
 const arrays=[['Normal','Flying'],[],['Stellar'],['Fire','Water'],['Normal','Normal'],[''],
  Object.freeze(['Rock','Ice']),['Bird','???'],['type\u0000name','\ud800'],['\ud83d\ude00','é','e\u0301']];
 const sparse=new Array(4);sparse[1]=undefined;sparse[3]=null;arrays.push(sparse);
 for(const arr of arrays)register(arr);
 for(const p of mons){p.types=arrays[0];p.addedType='Bird';p.terastallized='';p.knownType=false;p.apparentType='Rock';register(p.baseTypes);}
 function fields(arr){return [...Object.keys(arr).map(k=>[Number(k),...encode(arr[k])]),[4294967295,4,0,arr.length,1]];}
 const initial=objects.map((arr,i)=>[i+1,fields(arr)]),actions=[];
 function snapshot(){return {roots:mons.map((p,i)=>({at:576+i*128,types:register(p.types),base:register(p.baseTypes),added:strings.get(p.addedType),
  tera:strings.get(p.terastallized),known:+p.knownType,apparent:units(p.apparentType)})),
  objects:objects.map((arr,i)=>[i+1,fields(arr)]),next:objects.length+1};}
 function patch(index,key,value){const p=mons[index],at=576+index*128;
  if(key==='species')p.species=dex.species.get(value);else p[key]=value;
  const offset={species:1,terastallized:98,knownType:99,apparentType:100}[key];
  const word=key==='species'?id('species',p.species.id):key==='knownType'?+value:strings.get(value);
  if(word===undefined)throw Error('Patch text');actions.push({kind:'patch',at:at+offset,value:word,...snapshot()});}
 function call(index,mode,value,enforce=false){const p=mons[index],at=576+index*128;let result,error=false;
  const argument=Array.isArray(value)?register(value):strings.get(value);
  try{result=mode===2?p.addType(value):p.setType(value,enforce);}catch(e){
   if(e.message!=='Must pass type to setType')throw e;error=true;
  }
  register(p.types);actions.push({kind:'call',mode,at,enforce:+enforce,argument,result:!!result,error,...snapshot()});}
 function write(arr,index,value){arr[index]=value;actions.push({kind:'write',ref:register(arr),index,value:encode(value),...snapshot()});}
 function push(arr,value){const length=arr.push(value);actions.push({kind:'push',ref:register(arr),value:encode(value),length,...snapshot()});}
 for(const name of speciesNames){
  patch(0,'species',name);
  call(0,0,'Normal');call(0,0,'');call(0,1,arrays[1]);call(0,0,'Stellar');call(0,1,arrays[2],true);
 }
 if(rich){
  patch(0,'species','mew');
  for(const name of ['mew','arceus','arceusfire','silvally','silvallyflying']){
   patch(0,'species',name);
   for(const tera of ['','Flying','Stellar']){
    patch(0,'terastallized',tera);patch(0,'knownType',false);patch(0,'apparentType','Rock');
    for(const enforce of [false,true]){
     for(const value of ['','Normal','Stellar','type\u0000name','\ud800','\ud83d\ude00'])call(0,0,value,enforce);
     for(const arr of arrays)call(0,1,arr,enforce);
    }
    for(const text of ['','Bird','Stellar','\ud800'])call(0,2,text);
   }
  }
  patch(0,'species','mew');patch(0,'terastallized','');
  // Retained aliases: setType keeps arrays, but apparentType is a snapshot.
  call(0,1,arrays[0],true);call(1,1,arrays[0],true);
  write(arrays[0],0,'Water');push(arrays[0],'Ice');
  call(0,2,'Bird');call(0,1,arrays[3],true);write(arrays[3],1,'Flying');
  call(1,1,arrays[1],true);push(arrays[1],'Normal');
  call(0,1,arrays[10],true);write(arrays[10],0,'Ghost');push(arrays[10],'Bird');
  for(const text of ['Normal','Normal','\ud800','\ud800'])call(0,0,text,true);
 }
 if(rich)for(const arr of arrays)for(const text of ['Normal','Flying','type\u0000name','\ud800','\ud83d\ude00'])
  actions.push({kind:'member',ref:register(arr),argument:strings.get(text),result:arr.includes(text),...snapshot()});
 const patches=mons.flatMap((p,i)=>{const at=576+i*128;return [[at+1,id('species','mew')],[at+96,1],
  [at+97,strings.get('Bird')],[at+98,0],[at+99,0],[at+100,strings.get('Rock')],[at+101,refs.get(p.baseTypes)]];});
 groups.push({initial,patches,actions});b.destroy();
}
const names=cat.ids.species.names.slice(1);
for(let at=0;at<names.length;at+=6)sequence(names.slice(at,at+6));
sequence([],true);
const nums=names.map(name=>[id('species',name),dex.species.get(name).num>>>0]);
console.log(JSON.stringify({groups,pretexts:pretexts.map(units),nums}));
"""


def main():
    fixture = json.loads(subprocess.check_output(["node", "-e", SCRIPT], cwd=ROOT))
    cat = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * cat["word_count"])()
    binary = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, binary, len(binary))
    words[2] = 1
    heap = EffectHeap(lib, words)
    region = cat["regions"]["species"]
    for sid, num in fixture["nums"]:
        assert words[region["start"] + sid * region["stride"] + 51] == num
    total = calls = faults = mutations = memberships = 0
    for group in fixture["groups"]:
        words[512:8192] = [0] * (8192 - 512)
        words[8:12] = [1, 2, 3, 4]
        queue = [0xA55A0000 + i for i in range(512)]
        words[2176:2688] = queue
        heap.reset(37)
        for offset, units in enumerate(fixture["pretexts"]):
            words[0], words[32:34] = 53, [1, len(units)]
            words[160:160 + len(units)] = units
            lib.pg9_execute(words)
            assert words[1] == 0 and words[16] == len(cat["ids"]["strings"]["names"]) + offset
        for ref, fields in group["initial"]:
            heap.import_raw(ref, fields)
        for at, value in group["patches"]:
            words[at] = value
        for action in group["actions"]:
            if action["kind"] == "patch":
                words[action["at"]] = action["value"]
            else:
                if action["kind"] == "call":
                    words[0], words[32:36] = 54, [action["mode"], action["at"], action["enforce"], action["argument"]]
                elif action["kind"] == "write":
                    words[0], words[32:35], words[88:92] = 49, [8, action["ref"], action["index"]], action["value"]
                    mutations += 1
                elif action["kind"] == "member":
                    words[0], words[32:35] = 49, [6, action["ref"], action["argument"]]
                    memberships += 1
                else:
                    words[0], words[32:35], words[88:92] = 49, [3, action["ref"], 0], action["value"]
                    mutations += 1
                private_before = list(words[512:8192])
                lib.pg9_execute(words)
                if action["kind"] == "call":
                    calls += 1
                    faults += action["error"]
                    assert words[1] == (3 if action["error"] else 0), (action, words[1])
                    if not action["error"]:
                        assert list(words[64:68]) == [3 if action["result"] else 2, 0, 0, 0], action
                    allowed = {action["at"] + off for off in [96, 97, 99, 100]}
                    assert all(words[at] == private_before[at - 512] for at in range(512, 8192) if at not in allowed)
                else:
                    assert words[1] == 0 and list(words[512:8192]) == private_before
                    if action["kind"] == "member":
                        assert list(words[64:68]) == [3 if action["result"] else 2, 0, 0, 0]
            for p in action["roots"]:
                at = p["at"]
                assert list(words[at + 96:at + 100]) == [p["types"], p["added"], p["tera"], p["known"]], action
                assert words[at + 101] == p["base"], action
                text = words[at + 100]
                words[0], words[32:34] = 53, [0, text]
                lib.pg9_execute(words)
                assert words[1] == 0 and list(words[8192:8192 + words[19]]) == p["apparent"], action
            assert heap.inspect(0)[0] == [0, action["next"], 37], action
            for ref, fields in action["objects"]:
                assert heap.inspect(ref)[1] == fields, (action, ref)
            assert list(words[8:12]) == [1, 2, 3, 4] and list(words[2176:2688]) == queue
            total += 1
    for mode, at, enforce in [(0, 575, 0), (0, 577, 0), (0, 576, 2), (99, 576, 0)]:
        private = list(words[512:8192])
        before = heap.inspect(0)[0]
        words[0], words[32:36] = 54, [mode, at, enforce, 0]
        lib.pg9_execute(words)
        assert words[1] == 3 and list(words[512:8192]) == private
        assert heap.inspect(0)[0] == before
    print(f"PASS: {total} original type-change/fixture transitions in {len(fixture['groups'])} retained sequences; "
          f"{calls} setType/addType calls, {faults} original empty-string faults and {mutations} independent array writes/pushes; "
          f"{memberships} dynamic/static memberships, all {len(fixture['nums'])} resolved species numbers, Stellar/species/Tera/enforce guards, "
          "array aliases/fresh string arrays, sparse/null/undefined cells, known/added/apparent snapshots, "
          "exact retained objects, private rows, queues, counters and RNG")


if __name__ == "__main__":
    main()
