// Exact action/state parity with a separately compiled native bridge.
// Run after build.sh: node ocean/poker/web/test_wasm.mjs
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {pathToFileURL} from 'node:url';
const root=process.cwd(), build=path.join(root,'build/poker/web');
const hands=200, report=[];
for(const variant of ['2','6','9','2pro']) {
  const players=Number(variant.replace('pro','')),src=path.join(build,'source',variant);
  const probe=path.join(src,'probe.c'),binary=path.join(src,'probe');
  fs.writeFileSync(probe,`#include <stdio.h>
#include "bridge.c"
int main(void) {
 web_init(927451); int steps=0;
 for(int h=0;h<${hands};h++) {
  if(h)web_next();
  for(;;){int32_t *s=web_state(); printf("S");for(int i=0;i<24+9*POKER_PLAYERS;i++)printf(" %d",s[i]);puts("");
   if(s[1]==255)break;
   int policy=${players}==9?(steps%2?7:1):(steps%3==0?1:steps%3==1?4:7);
   int a=web_bot(policy); printf("A %d %d\\n",policy,a);if(!web_act(a))return 3;steps++;
  }
 }
 return 0;
}`);
  const model=players===9?[]:[`-DPOKER_FULLHOUSE_MODEL="${root}/build/poker/fullhouse_weights.h"`];
  const cc=spawnSync('clang-19',['-O3','-ffp-contract=off','-std=c11',`-DPOKER_PLAYERS=${players}`,'-DPOKER_BATCH=1','-DPOKER_WIDE_CHIPS=1',...model,probe,'-lm','-o',binary],{encoding:'utf8'});
  assert.equal(cc.status,0,cc.stderr);
  const oracle=spawnSync(binary,[],{encoding:'utf8',maxBuffer:64*1024*1024});assert.equal(oracle.status,0,oracle.stderr);
  const {default:create}=await import(pathToFileURL(path.join(build,'dist',`poker${variant}.mjs`)));
  const engine=await create();engine._web_init(927451);
  let terminal=false,decisions=0,states=0,completed=0;
  for(const line of oracle.stdout.trim().split('\n')) {
    const [kind,...numbers]=line.split(' '),xs=numbers.map(Number);
    if(kind==='S'){
      if(terminal){engine._web_next();terminal=false;}
      const ptr=engine._web_state()>>>2,actual=Array.from(engine.HEAP32.subarray(ptr,ptr+24+players*9));
      assert.deepEqual(actual,xs,`${variant} state ${states}`);
      assert.equal(engine._web_act(-2),0,'Invalid fold encoding rejected');
      assert.equal(engine._web_act(1000000),0,'Oversized raise rejected');
      if(actual[1]!==255&&actual[12]===0)assert.equal(engine._web_act(-1),0,'Free fold rejected');
      let chips=actual[3];
      for(let p=0;p<players;p++){
        const row=actual.slice(24+9*p,33+9*p);chips+=row[0];
        if(p&&(!actual[14]||!row[5]))assert.deepEqual(row.slice(3,5),[52,52],'Private opponent cards stay hidden');
      }
      assert.equal(chips,20000*players,'Chip conservation');
      for(let i=actual[13];i<5;i++)assert.equal(actual[16+i],52,'Future board stays hidden');
      assert.equal(actual[7],(actual[15]-1)%players,'Button rotation');
      states++;
      if(actual[1]===255){terminal=true;completed++;}
    } else {
      const [policy,action]=xs;assert.equal(engine._web_bot(policy),action,`${variant} action ${decisions}`);
      assert.equal(engine._web_act(action),1);decisions++;
    }
  }
  report.push({variant,hands:completed,states,decisions,exact_native_wasm_parity:true});
}
fs.writeFileSync(path.join(build,'wasm-validation.json'),JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify(report,null,2));
