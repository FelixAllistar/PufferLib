#!/usr/bin/env python3
"""Original boost mutations and retained ordinary-object identity.

Original transformInto/clearVolatile supply only the expected boost stages.
Their remaining lifecycle stages are not claimed by this fixture.
"""
import ctypes
import json
from pathlib import Path
import subprocess
from native_test_helpers import EffectHeap
from test_init import SCRIPT as CONSTRUCTOR_SCRIPT

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = CONSTRUCTOR_SCRIPT.split("const g=Teams.getGenerator(", 1)[0] + r"""
const names=['atk','def','spa','spd','spe','accuracy','evasion'],groups=[];
function value(v){return [v<0?5:4,0,Math.abs(v),1];}
function fields(obj){return Object.entries(obj).map(([key,v])=>[32+names.indexOf(key),...value(v)]);}
for(let scenario=0;scenario<52;scenario++){
 const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
 const raw={species:'Mew',moves:['tackle'],ability:'No Ability'};
 const sides=[new Side('one',b,0,[structuredClone(raw)]),new Side('two',b,1,[structuredClone(raw)])];
 b.sides=sides;sides[0].foe=sides[1];sides[1].foe=sides[0];
 const p=sides[0].pokemon[0],q=sides[1].pokemon[0],graph=effectGraph(b,sides,true);
 const objects=graph.objects,refs=graph.references,boostObjects=[p.boosts,q.boosts];
 // Prepare legal full tables in varied own-property order. Constructor parity
 // is checked separately; these are pre-action inputs to mutation comparisons.
 for(const [i,mon]of [p,q].entries()){
  for(const key of names)delete mon.boosts[key];
  for(let j=0;j<7;j++)mon.boosts[names[(j+scenario+i)%7]]=(scenario+j+i)%13-6;
 }
 const initial=graph.snapshots.map(([ref,data])=>[ref,boostObjects.some(obj=>refs.get(obj)===ref)?fields(objects[ref-1].obj):data]);
 const patches=graph.roots.slice();
 for(const [i,mon]of [p,q].entries())names.forEach((key,j)=>patches.push([576+i*768+24+j,12-(mon.boosts[key]+6)]));
 const actions=[];b.resetRNG([1,2,3,4]);
 function register(obj){if(!refs.has(obj)){refs.set(obj,objects.length+1);objects.push({obj});boostObjects.push(obj);}return refs.get(obj);}
 function snapshot(){return boostObjects.map(obj=>[refs.get(obj),fields(obj)]);}
 function record(op,args,result,extra={}){
  if(b.effectOrder!==graph.counter||b.prng.getSeed()!=='1,2,3,4')throw Error('Original inactive boost fixture changed counter/RNG');
  actions.push({op,args,result,...extra,next:objects.length+1,
  roots:[refs.get(p.boosts),refs.get(q.boosts)],snapshots:snapshot()});}
 function apply(mon,mode,changes){
  const result=mode===0?mon.boostBy(changes):(mode===1?mon.clearBoosts():mon.setBoost(changes),0);
  const pairs=Object.entries(changes).flatMap(([key,v])=>[names.indexOf(key),v+(mode===2?6:32768)]);
  record(20,[mon===q?1:0,0,mode,pairs.length/2],result+32768,{pairs,positive:mon.positiveBoosts()});
 }
 function read(mon){for(let j=0;j<7;j++)record(58,[1,mon===q?1344:576,j],mon.boosts[names[j]]+6);}
 function fresh(mon){mon.clearVolatile();register(mon.boosts);record(58,[2,mon===q?1344:576,0],refs.get(mon.boosts));}
 function share(mon,target){mon.boosts=target.boosts;record(58,[3,mon===q?1344:576,target===q?1344:576],refs.get(mon.boosts));}
 function transform(mon,target){
  if(!mon.transformInto(target))throw Error('Unexpected original Transform rejection');
  record(58,[4,mon===q?1344:576,target===q?1344:576],refs.get(mon.boosts));
 }
 read(p);read(q);
 for(let j=0;j<14;j++){
  const changes=Object.fromEntries([names[(scenario+j)%7],names[(scenario+j+2)%7],names[(scenario+j+5)%7]]
   .map((key,k)=>[key,[-32768,-12,-1,0,1,12,32767][(scenario+j+k)%7]]));
  apply(p,0,changes);read(p);
 }
 apply(p,2,{evasion:-6,atk:6,accuracy:scenario%13-6});apply(p,1,{});read(p);
 apply(q,2,{spa:4,spe:-3,evasion:6});share(p,q);
 apply(p,0,{spe:2,def:-3,evasion:1});read(q);
 apply(q,1,{});read(p);
 apply(p,2,{atk:6,accuracy:-4});fresh(q);read(p);read(q);
 // Original Transform assigns the target's fields into the destination's
 // existing object. It does not replace its identity or update retained aliases.
 apply(q,2,{def:6,spa:-6,spe:3});transform(p,q);read(p);
 apply(p,0,{atk:1,spa:2});read(q);
 fresh(p);apply(p,2,{accuracy:6,evasion:-6});transform(p,p);read(p);
 groups.push({scenario,initial,patches,counter:graph.counter,actions,final:snapshot()});b.destroy();
}
console.log(JSON.stringify(groups));
"""


def main():
    groups = json.loads(subprocess.check_output(["node", "-e", SCRIPT], cwd=ROOT))
    cat = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * cat["word_count"])()
    data = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, data, len(data))
    words[2] = 1
    heap = EffectHeap(lib, words)
    checked = mutations = copies = replacements = shared = 0
    for group in groups:
        words[512:cat["mutable_words"]] = [0] * (cat["mutable_words"] - 512)
        words[8:12] = [1,2,3,4]
        heap.reset(group["counter"])
        for reference, fields in group["initial"]:
            heap.import_raw(reference, fields)
        for address, v in group["patches"]:
            words[address] = v
        for action in group["actions"]:
            words[0], words[32:32+len(action["args"])] = action["op"], action["args"]
            if "pairs" in action:
                words[64:64+len(action["pairs"])] = action["pairs"]
            before = list(words[512:8192])
            lib.pg9_execute(words)
            assert words[1] == 0, (group["scenario"], action["args"], words[1])
            assert words[16] == action["result"], (group["scenario"], action["args"], words[16], action["result"])
            if "positive" in action:
                assert words[17] == action["positive"]
            allowed = set()
            if action["op"] == 20 or action["args"][0] >= 2:
                at = 576+action["args"][0]*768 if action["op"] == 20 else action["args"][1]
                allowed = {at+109} | set(range(at+24,at+31))
            assert all(words[i] == before[i-512] for i in range(512,8192) if i not in allowed)
            assert [words[576+109],words[1344+109]] == action["roots"]
            assert heap.inspect(0)[0] == [0,action["next"],group["counter"]]
            for reference, fields in action["snapshots"]:
                assert heap.inspect(reference)[1] == fields, (group["scenario"], action["args"], reference)
            assert list(words[8:12]) == [1,2,3,4]
            mutations += action["op"] == 20
            if action["op"] == 58:
                copies += action["args"][0] == 4
                replacements += action["args"][0] == 2
                shared += action["args"][0] == 3
            checked += 1
        for reference, fields in group["final"]:
            assert heap.inspect(reference)[1] == fields
    rejected = 0
    for args in [[5,576,0],[1,576,7],[1,575,0],[1,577,0],
                 [3,576,575],[4,576,577],[3,576,704],[4,576,704]]:
        before = list(words[512:8192])
        metadata = heap.inspect(0)[0]
        words[0], words[32:35] = 58, args
        lib.pg9_execute(words)
        assert words[1] == 3, args
        assert list(words[512:8192]) == before, args
        assert heap.inspect(0)[0] == metadata, args
        rejected += 1
    print(f"PASS: {checked} retained boost transitions in {len(groups)} sequences; {mutations} original boostBy/clearBoosts/setBoost calls, "
          f"{copies} Transform boost stages, {replacements} clearVolatile boost stages and {shared} sharing assignments; "
          "stale mirrors, all stages, ordered changes, clamping/last delta/positive sums, "
          f"retained cross-holder aliases, distinct replacement/copy behavior, exact graphs/private/queue/RNG; {rejected} rejected diagnostic inputs")


if __name__ == "__main__":
    main()
