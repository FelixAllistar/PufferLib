#!/usr/bin/env python3
"""Retained move-slot graphs, original PP/disable/getMoveData/getMoves calls.

Original transformInto/clearVolatile supply expected slot stages. This fixture
projects their move arrays/records only; it does not claim their other state,
ability, volatile or public-message stages are ported. Native post-action slot
objects are never overwritten with oracle snapshots.
"""
import ctypes
import json
from pathlib import Path
import subprocess
from native_test_helpers import EffectHeap
from test_init import SCRIPT as CONSTRUCTOR_SCRIPT

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = CONSTRUCTOR_SCRIPT.split("const g=Teams.getGenerator(", 1)[0] + r"""
const dex=Dex.mod('gen9'),groups=[];
const id=(family,name)=>name?cat.ids[family].map[norm(name)]||0:0;
const stringIDs=new Map(cat.ids.strings.names.map((text,i)=>[text,i])),pretexts=[];
function textID(text){if(!stringIDs.has(text)){stringIDs.set(text,cat.ids.strings.names.length+pretexts.length);pretexts.push(text);}return stringIDs.get(text);}
function encode(v){if(v===undefined)return [0,0,0,0];if(typeof v==='boolean')return [v?3:2,0,0,0];
 if(typeof v==='string')return [9,0,textID(v),0];if(!Number.isInteger(v)||v<0)throw Error('Slot scalar domain');return [4,0,v,1];}
const slotKeys={move:40,id:1,pp:41,maxpp:42,target:2,disabled:43,disabledSource:44,used:45,virtual:46};
const targetSets=[['hiddenpower','splash','sketch','transform'],['trumpcard','protect','tackle','psychic'],
 ['struggle','wish','recover','ember'],['metronome','mimic','sleeptalk','spite']];
const allMoves=cat.ids.moves.names.slice(1),seenMoves=new Set(),seenTargets=new Set();
for(let scenario=0;scenario<24+Math.ceil(allMoves.length/4);scenario++){
 const b=new Battle({formatid:'gen9randombattle',seed:[1,2,3,4]});
 const raw={species:'Mew',moves:['psychic','trumpcard','tackle','protect'],ability:'No Ability',hpType:'Fire'};
 const second={...structuredClone(raw),moves:scenario<24?targetSets[scenario%4]:allMoves.slice((scenario-24)*4,(scenario-23)*4),hpType:'Ice'};
 const sides=[new Side('one',b,0,[structuredClone(raw)]),new Side('two',b,1,[second])];
 b.sides=sides;sides[0].foe=sides[1];sides[1].foe=sides[0];
 const p=sides[0].pokemon[0],q=sides[1].pokemon[0],graph=effectGraph(b,sides,true);
 const objects=graph.objects,refs=graph.references;
 const slotObjects=[];
 for(const mon of [p,q])for(const obj of [...mon.baseMoveSlots,mon.baseMoveSlots,mon.moveSlots])
  if(!slotObjects.includes(obj))slotObjects.push(obj);
 function register(obj){if(!refs.has(obj)){const reference=objects.length+1;refs.set(obj,reference);objects.push({reference,obj});}
  if(!slotObjects.includes(obj))slotObjects.push(obj);return refs.get(obj);}
 function registerCurrent(mon){register(mon.moveSlots);for(const slot of mon.moveSlots)register(slot);}
 function fields(obj){if(Array.isArray(obj))return [...Object.keys(obj).map(k=>[Number(k),10,7,refs.get(obj[k]),0]),[4294967295,...encode(obj.length)]];
  return Object.entries(obj).map(([k,v])=>{if(!slotKeys[k])throw Error('Unknown slot property '+k);return [slotKeys[k],...encode(v)];});}
 function snapshots(){return slotObjects.map(obj=>[refs.get(obj),fields(obj)]);}
 function roots(){return [p,q].map((mon,i)=>({at:i?1344:576,current:refs.get(mon.moveSlots),base:refs.get(mon.baseMoveSlots),
  slots:mon.moveSlots.map(s=>[id('moves',s.id),s.pp,s.maxpp,s.disabled===true?1:s.disabled==='hidden'?2:0,+s.used,id('targets',s.target)])}));}
 const patches=graph.roots.slice();
 for(const [mon,at]of [[p,576],[q,1344]]){
  patches.push([at+1,id('species',mon.species.id)],[at+2,id('species',mon.baseSpecies.id)],[at+14,mon.moveSlots.length]);
  mon.moveSlots.forEach((s,i)=>{const start=at+32+i*8;[id('moves',s.id),s.pp,s.maxpp,0,0,0,0,id('targets',s.target)]
   .forEach((value,j)=>patches.push([start+j,value]));});
 }
 const initial=graph.snapshots,counter=b.effectOrder,actions=[];
 function record(op,args,result,extra={}){actions.push({op,args,result,...extra,roots:roots(),snapshots:snapshots(),next:objects.length+1});}
 function pp(mon,move,amount){const result=mon.deductPP(move,amount);record(19,[mon===q?1:0,0,id('moves',move),amount],result);}
 function disable(mon,move,hidden,kind=0,name=''){
  const effect=name?dex[kind===2?'items':kind===1?'abilities':'moves'].get(name):undefined;
  mon.disableMove(move,hidden,effect);record(21,[mon===q?1:0,0,id('moves',move),+hidden,kind,name?id(kind===2?'items':kind===1?'abilities':'moves',name):0],null);
 }
 function lookup(mon,move){const result=mon.getMoveData(move);record(57,[2,mon===q?1344:576,id('moves',move),0],result?refs.get(result):0);}
 function view(mon,lock=null,restrict=false){
  const rows=mon.getMoves(lock,restrict).map((row,index)=>[id('moves',row.id)||0,row.id==='recharge'?0:lock?mon.moveSlots.findIndex(s=>s.id===row.id)+1:index+1,
   row.pp||0,row.maxpp||0,id('targets',row.target)||0,+!!row.disabled,
   ('pp'in row?1:0)|('maxpp'in row?2:0)|('target'in row?4:0)|('disabled'in row?8:0),+(row.id==='recharge')]);
  record(29,[mon===q?1:0,0,lock==='recharge'?2:lock?1:0,lock&&lock!=='recharge'?id('moves',lock):0,+restrict],null,{rows});
 }
 function restore(mon){mon.clearVolatile();registerCurrent(mon);record(57,[3,mon===q?1344:576,0,0],refs.get(mon.moveSlots));}
 function transform(mon,target){
  if(!mon.transformInto(target))throw Error('Original transform guard rejected slot fixture');
  registerCurrent(mon);record(57,[5,mon===q?1344:576,target===q?1344:576,textID(mon.hpType)],refs.get(mon.moveSlots));
 }
 function install(mon,array){const fresh=!refs.has(array);mon.moveSlots=array;registerCurrent(mon);record(57,[4,mon===q?1344:576,refs.get(array),0],refs.get(array),
  {pre:fresh?[[refs.get(array),fields(array)]]:[]});}
 if(scenario>=24){
  // Every resolved move descriptor, including Z/Max and old-generation moves,
  // exercises exact raw target strings and the reverse move-ID mapping. This
  // intentionally exceeds the random-team pool; it is not a reachability claim.
  for(const slot of q.moveSlots){seenMoves.add(slot.id);seenTargets.add(slot.target);pp(q,slot.id,4294967295);}
  transform(p,q);for(const slot of p.moveSlots){lookup(p,slot.id);pp(p,slot.id,0);}restore(p);
  groups.push({scenario,initial,patches,counter,actions,final:snapshots()});b.destroy();continue;
 }
 // Stale mirrors make a flat-only implementation fail before the first write.
 actions.push({patches:[[576+14,1],[576+32,id('moves','splash')],[576+33,0]]});
 lookup(p,'psychic');view(p);lookup(p,'ember');
 for(let i=0;i<8;i++){pp(p,raw.moves[i%4],[0,1,2,4294967295][i%4]);disable(p,raw.moves[i%4],!!(i%2),i%3===0?2:0,i%3===0?'choicescarf':'');view(p,null,!!(i%2));}
 // Duplicate array indices share a record. PP changes only the first match;
 // disabling sees every match and true remains absorbing across all aliases.
 install(p,[p.baseMoveSlots[0],p.baseMoveSlots[0],p.baseMoveSlots[2],p.baseMoveSlots[3]]);
 pp(p,'psychic',1);disable(p,'psychic',true,2,'choicescarf');disable(p,'psychic',false,0,'disable');disable(p,'psychic',true,1,'pressure');view(p);
 restore(p);view(p,'psychic');view(p,'recharge');
 // Exhaust target PP, change names' disable source, then compare Transform's
 // fresh base-PP slots rather than copying the target's depleted values.
 for(const move of second.moves){pp(q,move,4294967295);disable(q,move,true,0,'disable');}
 transform(p,q);view(p);for(const move of second.moves){lookup(p,move);pp(p,move,1);view(p,null,true);}
 restore(p);view(p);pp(p,'psychic',0);
 // Retain another Pokemon's actual array and slot objects, with mutations
 // visible through both holders. Restore isolates only the array identity.
 install(p,q.moveSlots);pp(p,second.moves[0],0);lookup(q,second.moves[0]);view(q);restore(p);
 transform(p,p);view(p);lookup(p,'psychic');restore(p);view(p);
 const final=snapshots();groups.push({scenario,initial,patches,counter,actions,final});b.destroy();
}
const units=text=>Array.from({length:text.length},(_,i)=>text.charCodeAt(i));
if(seenMoves.size!==allMoves.length)throw Error('Resolved move-slot inventory incomplete');
console.log(JSON.stringify({groups,pretexts:pretexts.map(units),moves:seenMoves.size,targets:seenTargets.size}));
"""


def main():
    fixture = json.loads(subprocess.check_output(["node", "-e", SCRIPT], cwd=ROOT))
    cat = json.loads((ROOT / "build/pokemon_gen9/catalog/catalog.json").read_text())
    lib = ctypes.CDLL(str(ROOT / "build/pokemon_gen9/native/libpokemon_gen9.so"))
    lib.pg9_execute.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    lib.pg9_execute.restype = None
    words = (ctypes.c_uint32 * cat["word_count"])()
    data = (ROOT / "build/pokemon_gen9/catalog/catalog.bin").read_bytes()
    ctypes.memmove(words, data, len(data))
    words[2] = 1
    heap = EffectHeap(lib, words)
    checked = copies = restores = 0
    for group in fixture["groups"]:
        words[512:cat["mutable_words"]] = [0] * (cat["mutable_words"] - 512)
        words[8:12] = [1, 2, 3, 4]
        queue = list(range(512)); words[2176:2688] = queue
        heap.reset(group["counter"])
        for index, units in enumerate(fixture["pretexts"]):
            words[0], words[32:34] = 53, [1, len(units)]
            words[160:160 + len(units)] = units
            lib.pg9_execute(words)
            assert words[1] == 0 and words[16] == len(cat["ids"]["strings"]["names"]) + index
        for reference, fields in group["initial"]:
            heap.import_raw(reference, fields)
        for at, value in group["patches"]:
            words[at] = value
        for action in group["actions"]:
            if "patches" in action:
                for at, value in action["patches"]:
                    words[at] = value
                continue
            for reference, fields in action.get("pre", []):
                heap.import_raw(reference, fields)
            words[0], words[32:32 + len(action["args"])] = action["op"], action["args"]
            before = list(words[512:8192])
            lib.pg9_execute(words)
            assert words[1] == 0, (group["scenario"], action["args"], words[1])
            if action["result"] is not None:
                assert words[16] == action["result"], (group["scenario"], action["args"], words[16], action["result"])
            if "rows" in action:
                assert words[16] == len(action["rows"])
                assert [list(words[64+i*8:72+i*8]) for i in range(words[16])] == action["rows"]
            allowed = set()
            if action["op"] in (19, 21) or action["op"] == 57 and action["args"][0] >= 3:
                at = 576 + action["args"][0] * 768 if action["op"] in (19,21) else action["args"][1]
                allowed = {at+14,at+107} | set(range(at+32,at+64))
            assert all(words[i] == before[i-512] for i in range(512,8192) if i not in allowed)
            for mon in action["roots"]:
                assert list(words[mon["at"]+107:mon["at"]+109]) == [mon["current"],mon["base"]]
            assert heap.inspect(0)[0] == [0, action["next"], group["counter"]]
            for reference, fields in action["snapshots"]:
                assert heap.inspect(reference)[1] == fields, (group["scenario"], action["args"], reference)
            assert list(words[8:12]) == [1,2,3,4] and list(words[2176:2688]) == queue
            copies += action["op"] == 57 and action["args"][0] == 5
            restores += action["op"] == 57 and action["args"][0] == 3
            checked += 1
        for reference, fields in group["final"]:
            assert heap.inspect(reference)[1] == fields
    # Diagnostic rejection contract, distinct from the original Pokemon's
    # lifecycle guards. These inputs must fail before changing persistent state.
    rejected = 0
    bad = [[6,576,0,0],[1,575,0,0],[1,577,0,0],[1,2112,0,0],
           [2,576,0,0],[2,576,len(cat["ids"]["moves"]["names"]),0],
           [5,576,575,0],[5,576,577,0],[5,576,2112,0]]
    large = heap.inspect(0)[0][1]
    heap.import_raw(large, [[4294967295,4,0,5,1]])
    bad.append([4,576,large,0])
    for args in bad:
        metadata = heap.inspect(0)[0]
        before = list(words[512:8192])
        words[0], words[32:36] = 57, args
        lib.pg9_execute(words)
        assert words[1] == 3, args
        assert list(words[512:8192]) == before, args
        assert heap.inspect(0)[0] == metadata, args
        rejected += 1
    previous = words[576+14]
    words[576+14] = 5
    metadata = heap.inspect(0)[0]
    before = list(words[512:8192])
    words[0], words[32:36] = 57, [0,576,0,0]
    lib.pg9_execute(words)
    assert words[1] == 3 and list(words[512:8192]) == before
    assert heap.inspect(0)[0] == metadata
    words[576+14] = previous
    rejected += 1
    print(f"PASS: {checked} original move-slot method/projection transitions in {len(fixture['groups'])} retained sequences; "
          f"{copies} transformInto slot stages and {restores} clearVolatile array stages; "
          f"all {fixture['moves']} resolved move IDs and {fixture['targets']} source target strings; "
          "base/current shared records, fresh virtual copied slots, base PP cap and Hidden Power name, "
          "PP exhaustion/used/duplicate matches, true/hidden disabling, cross-holder aliases, self-target ordering, "
          f"stale-mirror getters, all retained graphs and exact counters/private/queue/RNG; {rejected} rejected diagnostic inputs")


if __name__ == "__main__":
    main()
