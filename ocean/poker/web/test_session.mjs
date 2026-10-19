import assert from 'node:assert/strict';
import fs from 'node:fs';
const {chromium}=await import(process.env.PLAYWRIGHT_MODULE||'playwright');
const browser=await chromium.launch();const context=await browser.newContext({viewport:{width:1600,height:1000}});const page=await context.newPage();
const errors=[];page.on('pageerror',e=>errors.push(e.message));
await page.addInitScript(()=>{crypto.getRandomValues=a=>{a[0]=1;return a;};});
const origin=process.env.POKER_WEB_URL||'http://127.0.0.1:8767';
await page.goto(origin);await page.waitForFunction(()=>window.poker?.snapshot());
await page.locator('#settings-button').click();await page.locator('#seat-count').selectOption('2');await page.locator('#opponent').selectOption('fullhouse');await page.locator('#speed').selectOption('80');await page.locator('#start-table').click();
await page.waitForFunction(()=>window.poker?.snapshot()?.[0]===2&&window.poker.snapshot()[1]===0&&!window.poker.status().busy);
const before=await page.evaluate(()=>({state:window.poker.snapshot(),session:window.poker.session(),status:window.poker.status()}));
await page.reload();await page.waitForFunction(()=>window.poker?.snapshot()?.[0]===2&&window.poker.snapshot()[1]===0&&!window.poker.status().busy);
assert.deepEqual(await page.evaluate(()=>window.poker.snapshot()),before.state,'Exact mid-hand refresh restoration');
assert.deepEqual(await page.evaluate(()=>window.poker.session()),before.session);
await page.waitForFunction(()=>window.poker.learning());
const learning=await page.evaluate(()=>window.poker.learning().data);assert.equal(learning[0],4096);assert.equal(learning.slice(5,14).reduce((a,b)=>a+b,0),learning[4]);
// Seed 1 loses the 200 BB heads-up buy-in against Fullhouse when moving all-in.
for(let i=0;i<20;i++){
 await page.waitForFunction(()=>!window.poker.status().busy&&(window.poker.snapshot()[1]===0||window.poker.snapshot()[1]===255));
 const s=await page.evaluate(()=>window.poker.snapshot());if(s[1]===255)break;
 if(s[9]&~3){await page.locator('[data-size="all"]').click();await page.locator('#raise-button').click();}else await page.locator('#call-button').click();
}
await page.waitForFunction(()=>window.poker.snapshot()[1]===255&&!window.poker.status().busy);
const terminal=await page.evaluate(()=>({state:window.poker.snapshot(),session:window.poker.session(),status:window.poker.status()}));
assert.equal(terminal.state[24],0);assert.equal(terminal.session.lost,20000);assert.equal(terminal.session.hands,1);
assert.match(await page.locator('#rebuy-button').innerText(),/Rebuy 200 BB/);
await page.reload();await page.waitForFunction(()=>window.poker?.snapshot()?.[1]===255&&!window.poker.status().busy);
assert.deepEqual(await page.evaluate(()=>window.poker.session()),terminal.session,'Reload does not score the terminal hand twice');
await page.locator('#rebuy-button').click();await page.waitForFunction(()=>window.poker.snapshot()[15]===2&&window.poker.snapshot()[1]===0&&!window.poker.status().busy);
const next=await page.evaluate(()=>({state:window.poker.snapshot(),status:window.poker.status()}));
assert.equal(next.state[32],20000,'Human explicitly rebuys 200 BB');assert.equal(next.state[41],40000,'Opponent winning stack carries into next hand');
assert.equal(next.status.buyins,40000);assert.equal(next.status.tableBuyins.reduce((a,b)=>a+b,0),60000);
assert.match(await page.locator('#hero-position').textContent(),/Big blind.*1 BB/);
await page.reload();await page.waitForFunction(()=>window.poker?.snapshot()?.[15]===2&&window.poker.snapshot()[1]===0&&!window.poker.status().busy);
assert.deepEqual(await page.evaluate(()=>window.poker.snapshot()),next.state);
assert.equal(await page.evaluate(()=>window.poker.status().buyins),40000);
// A separate tab without an opener gets a new sessionStorage namespace.
const fresh=await context.newPage();await fresh.goto(origin);await fresh.waitForFunction(()=>window.poker?.snapshot());assert.equal(await fresh.evaluate(()=>window.poker.session().hands),0);assert.equal(await fresh.evaluate(()=>window.poker.status().buyins),20000);await fresh.close();
await page.evaluate(()=>sessionStorage.setItem('puffer-poker.cash.v1','{broken'));
await page.reload();await page.waitForFunction(()=>window.poker?.snapshot());assert.equal(await page.evaluate(()=>window.poker.session().hands),0);
assert.deepEqual(errors,[]);
fs.writeFileSync('build/poker/web/session-validation.json',JSON.stringify({mid_hand_exact_replay:true,terminal_scored_once:true,stack_carryover:true,explicit_human_rebuy:true,winning_opponent_carries_400BB:true,tab_isolation:true,corrupt_storage_recovers:true,learning_samples:learning[0],browser_errors:errors},null,2)+'\n');
console.log('PASS: cash stack carryover, rebuy, exact refresh, score receipts, tab isolation, corrupt storage and learning');
await browser.close();
