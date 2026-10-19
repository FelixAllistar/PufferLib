import assert from 'node:assert/strict';
import fs from 'node:fs';
import {spawnSync} from 'node:child_process';
const {chromium}=await import(process.env.PLAYWRIGHT_MODULE||'playwright');
const origin=process.env.POKER_WEB_URL||'http://127.0.0.1:8767',out='build/poker/web';
fs.writeFileSync(`${out}/rank_args.c`,`#include <stdio.h>\n#include "${process.cwd()}/ocean/poker/core.h"\nint main(int n,char **v){uint8_t c[7];for(int i=1;i<n;i++)c[i-1]=atoi(v[i]);printf("%u\\n",poker_rank(c,n-1));}`);
let cc=spawnSync('clang-19',['-O2','-DPOKER_PLAYERS=2','-DPOKER_BATCH=1',`${out}/rank_args.c`,'-lm','-o',`${out}/rank_args`],{encoding:'utf8'});assert.equal(cc.status,0,cc.stderr);
const nativeRank=cards=>Number(spawnSync(`${out}/rank_args`,cards.map(String),{encoding:'utf8'}).stdout.trim());
const browser=await chromium.launch(),errors=[];
async function table(seed,width=1440,opponent='fullhouse'){
 const p=await browser.newPage({viewport:{width,height:width<700?844:1000}});p.on('pageerror',e=>errors.push(e.message));await p.clock.install();
 await p.addInitScript(seed=>{crypto.getRandomValues=a=>{a[0]=seed;return a;};},seed);
 await p.goto(origin);await p.waitForFunction(()=>window.poker?.snapshot());
 await p.locator('#settings-button').click();await p.locator('#seat-count').selectOption('2');await p.locator('#opponent').selectOption(opponent);await p.locator('#speed').selectOption('80');await p.locator('#start-table').click();return p;
}
async function finish(p,allin=false){
 for(let i=0;i<80;i++){
  await p.waitForFunction(()=>window.poker?.snapshot()&&!window.poker.status().busy&&(window.poker.snapshot()[1]===0||window.poker.snapshot()[1]===255));
  const s=await p.evaluate(()=>window.poker.snapshot());if(s[1]===255)return s;
  if(allin&&(s[9]&~3)){await p.locator('[data-size="all"]').click();await p.locator('#raise-button').click();}else await p.locator('#call-button').click();
 }throw Error('Hand failed to finish');
}
async function checkReceipt(p,s){
 assert.equal(await p.locator('#next-button').count(),0);
 const shown=await p.locator('.winner-hand').evaluateAll(ns=>ns.map(n=>({p:Number(n.dataset.winner),text:n.querySelector('.winner-description').textContent,cards:[...n.querySelectorAll('.card')].map(c=>Number(c.dataset.card))})));
 assert.ok(shown.length>0);
 for(const w of shown){
  if(s[14]){
   assert.equal(w.cards.length,5);const seven=[s[27+9*w.p],s[28+9*w.p],...s.slice(16,21)];
   assert.ok(seven.every(c=>c<52));assert.equal(nativeRank(w.cards),nativeRank(seven),'Winning five match the native evaluator');
   assert.match(w.text,/High card|One pair|Two pair|Three of a kind|Straight|Flush|Full house|Four of a kind/);
  }else{assert.deepEqual(w.cards,[]);assert.match(w.text,/Won without showdown/);}
 }
 return shown;
}
const p=await table(31,1440,'rules');const s=await finish(p);assert.ok(s[24]>0);const receipt=await checkReceipt(p,s);
await p.locator('#hold-result').click();await p.clock.fastForward(9000);assert.equal(await p.evaluate(()=>window.poker.snapshot()[15]),s[15]);
const session=await p.evaluate(()=>window.poker.session());await p.reload();await p.waitForFunction(()=>window.poker?.snapshot()&&!window.poker.status().busy);
assert.deepEqual(await p.evaluate(()=>window.poker.snapshot()),s);assert.deepEqual(await p.evaluate(()=>window.poker.session()),session);assert.equal(await p.evaluate(()=>window.poker.status().reviewPaused),true);
assert.ok((await p.locator('.dealer:not([hidden])').boundingBox()).width>=38);
await p.waitForTimeout(650);await p.screenshot({path:`${out}/result-desktop.png`});
await p.locator('#hold-result').click();await p.locator('#history-button').click();await p.clock.fastForward(9000);assert.equal(await p.evaluate(()=>window.poker.snapshot()[15]),s[15]);await p.locator('#close-drawer').click();
// Browser visibility is explicitly controlled because headless Chromium keeps
// multiple test pages visible. Exercise the same native visibilitychange path.
await p.evaluate(()=>{Object.defineProperty(document,'hidden',{configurable:true,get:()=>window.testHidden});window.testHidden=true;document.dispatchEvent(new Event('visibilitychange'));});
await p.clock.fastForward(9000);assert.equal(await p.evaluate(()=>window.poker.snapshot()[15]),s[15]);
const began=Date.now();await p.evaluate(()=>{window.testHidden=false;document.dispatchEvent(new Event('visibilitychange'));});
await p.waitForTimeout(800);assert.equal(await p.evaluate(()=>window.poker.snapshot()[15]),s[15],'Result remains readable before automatic deal');
await p.waitForFunction(h=>window.poker.snapshot()[15]>h,s[15],{timeout:9000});const elapsed=Date.now()-began;
assert.ok(elapsed>=2500&&elapsed<9000,`Automatic deal returned after ${elapsed} ms`);const next=await p.evaluate(()=>window.poker.snapshot());
assert.equal(next[15],s[15]+1);for(let q=0;q<2;q++)assert.equal(next[32+9*q],s[24+9*q]||20000,'Actual stacks carry into automatic deal');
await p.close();
const loss=await table(1,390);const busted=await finish(loss,true);assert.equal(busted[24],0);const losingReceipt=await checkReceipt(loss,busted);
assert.ok(losingReceipt.every(w=>w.p!==0));assert.ok(losingReceipt[0].text.includes('Fullhouse'));
assert.equal(await loss.locator('#rebuy-button').isVisible(),true);await loss.clock.fastForward(20000);assert.equal(await loss.evaluate(()=>window.poker.snapshot()[15]),busted[15]);assert.equal(await loss.evaluate(()=>window.poker.status().buyins),20000);
assert.ok((await loss.locator('.dealer:not([hidden])').boundingBox()).width>=29);await loss.waitForTimeout(650);await loss.screenshot({path:`${out}/result-mobile-loss.png`,fullPage:true});
await loss.locator('#rebuy-button').click();await loss.waitForFunction(h=>window.poker.snapshot()[15]>h,busted[15]);assert.equal(await loss.evaluate(()=>window.poker.status().buyins),40000);await loss.close();
const fold=await table(19,1440,'rules');await fold.waitForFunction(()=>window.poker.snapshot()?.[1]===0&&!window.poker.status().busy);await fold.locator('#fold-button').click();await fold.waitForFunction(()=>window.poker.snapshot()[1]===255&&!window.poker.status().busy);
const folded=await fold.evaluate(()=>window.poker.snapshot());assert.equal(folded[14],0);await checkReceipt(fold,folded);assert.deepEqual(folded.slice(36,38),[52,52]);
await fold.clock.fastForward(3500);await fold.waitForFunction(h=>window.poker.snapshot()[15]>h,folded[15]);await fold.close();
assert.deepEqual(errors,[]);await browser.close();
const report={showdown_receipt:receipt,losing_receipt:losingReceipt,native_winning_five:true,automatic_next_hand:true,real_result_pause_ms:elapsed,paused_result_survives_refresh:true,history_pauses_dealing:true,hidden_tab_gate:'controlled visibilitychange',stack_carryover:true,busted_player_not_auto_bought_in:true,explicit_rebuy:true,fold_win_hides_cards:true,dealer_diameter_desktop:38,dealer_diameter_mobile:29,no_next_hand_button:true,browser_errors:errors};
fs.writeFileSync(`${out}/rounds-validation.json`,JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));
