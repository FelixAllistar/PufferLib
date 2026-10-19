import assert from 'node:assert/strict';
import fs from 'node:fs';
const {chromium}=await import(process.env.PLAYWRIGHT_MODULE||'playwright');
const origin=process.env.POKER_WEB_URL||'http://127.0.0.1:8767';
const out='build/poker/web';
const browser=await chromium.launch();const page=await browser.newPage({viewport:{width:1920,height:1080}});
const errors=[];page.on('pageerror',e=>errors.push(e.message));
await page.addInitScript(()=>{crypto.getRandomValues=a=>{a[0]=31;return a;};});
await page.goto(origin);await page.waitForFunction(()=>window.poker?.snapshot()?.[1]===0&&!window.poker.status().busy);
let checks=0,maxBets=0;const widths=[];
async function geometry(){
  const result=await page.evaluate(()=>{
    const intersects=(a,b,g=1)=>a.x<b.x+b.width+g&&a.x+a.width+g>b.x&&a.y<b.y+b.height+g&&a.y+a.height+g>b.y;
    const bets=[...document.querySelectorAll('.seat-bet')],failures=[];
    const obstacles=[...document.querySelectorAll('.seat-info,.seat-cards,.seat-cue.visible,.dealer:not([hidden]),.position-tag:not([hidden]),#board,.pot-wrap')].filter(el=>!el.closest('.folded')||!el.classList.contains('seat-cards'));
    bets.forEach((a,i)=>{
      if(a.hidden)failures.push(`Hidden bet ${a.dataset.betSeat}`);
      const r=a.getBoundingClientRect();
      [...obstacles,...bets.slice(i+1)].forEach(b=>{if(intersects(r,b.getBoundingClientRect()))failures.push(`Bet ${a.dataset.betSeat} hits ${b.className} ${b.closest('[data-seat]')?.dataset.seat??''}`);});
    });
    const board=document.querySelector('#board').getBoundingClientRect(),hero=document.querySelector('.hero .seat-cards').getBoundingClientRect();
    if(intersects(board,hero))failures.push('Board touches own cards');
    document.querySelectorAll('.seat:not(.folded) .seat-cards').forEach(n=>{if(intersects(board,n.getBoundingClientRect()))failures.push(`Board touches seat ${n.closest('[data-seat]').dataset.seat} cards`);});
    document.querySelectorAll('.seat:not(.folded) .seat-cue.visible').forEach(cue=>{
      document.querySelectorAll('.seat-info,.seat:not(.folded) .seat-cards,#board').forEach(n=>{if(intersects(cue.getBoundingClientRect(),n.getBoundingClientRect()))failures.push(`Action cue touches ${n.className}`);});
    });
    const badges=[...document.querySelectorAll('.seat-info')];badges.forEach((a,i)=>badges.slice(i+1).forEach(b=>{if(intersects(a.getBoundingClientRect(),b.getBoundingClientRect()))failures.push('Stack badges touch');}));
    if(document.documentElement.scrollWidth>innerWidth)failures.push('Horizontal page overflow');
    return {failures,bets:bets.length};
  });
  if(result.failures.length)await page.screenshot({path:`${out}/feedback-failure.png`});
  assert.deepEqual(result.failures,[]);checks++;maxBets=Math.max(maxBets,result.bets);
}
async function configure(players,pace='80'){
  await page.locator('#settings-button').click();await page.locator('#seat-count').selectOption(String(players));await page.locator('#opponent').selectOption('mixed');await page.locator('#speed').selectOption(pace);await page.locator('#start-table').click();
  await page.waitForFunction(n=>window.poker.snapshot()?.[0]===n&&!window.poker.status().busy,players);
}
for(const [w,h] of [[1920,1080],[1366,768],[390,844],[360,780]]){
 await page.setViewportSize({width:w,height:h});widths.push(w);
 if(w<1200&&await page.locator('#learning-panel').isVisible())await page.locator('#close-learning').click();
 for(const n of [2,6,9]){
  await configure(n);
  let guard=0;
  while(guard++<160){
   await page.waitForFunction(()=>!window.poker.status().busy&&(window.poker.snapshot()[1]===0||window.poker.snapshot()[1]===255));
   // Let color/scale transitions settle before measuring their final geometry.
   await page.waitForTimeout(300);await geometry();
   const s=await page.evaluate(()=>window.poker.snapshot());
   if(s[1]===255){
    const winners=await page.locator('.seat.winner').count();assert.ok(winners>0,'Payout recipient is highlighted');
    assert.equal(await page.locator('.cue-win').count(),winners);
    if(w===1920&&n===6)await page.screenshot({path:`${out}/feedback-winner.png`});
    break;
   }
   if(n===6&&w===1920){
    if(s[2]===0)await page.screenshot({path:`${out}/feedback-preflop.png`});
    if(s[2]===1){
     await page.screenshot({path:`${out}/feedback-flop.png`});
     await page.evaluate(()=>{window.cardNode=document.querySelector('.hero .card');window.seatNode=document.querySelector('.hero .seat-info');});
     await page.locator('#hand-rank').click();
     assert.equal(await page.locator('.card.best').count(),5);
     assert.equal(await page.locator('.card.best').first().evaluate(n=>getComputedStyle(n).outlineStyle),'none');
     assert.equal(await page.evaluate(()=>window.cardNode===document.querySelector('.hero .card')&&window.seatNode===document.querySelector('.hero .seat-info')),true,'Card focus preserves DOM nodes');
     await page.screenshot({path:`${out}/feedback-focus.png`});await page.locator('#hand-rank').click();
    }
   }
   if(w===390&&n===6&&s[2]===1)await page.screenshot({path:`${out}/feedback-mobile.png`});
   if(s[1]===0)await page.locator('#call-button').click();
  }
  assert.ok(guard<160,'Game progresses to a result');console.log(`Passed feedback ${w}px / ${n} seats`);
 }
}
// Refresh must preserve V3 engine/session compatibility and focus starts calm.
await page.reload();await page.waitForFunction(()=>window.poker?.snapshot());assert.equal(await page.locator('.card.best').count(),0);
// Reduced motion suppresses decorative flights while preserving action cues.
await page.emulateMedia({reducedMotion:'reduce'});await configure(2);
await page.waitForFunction(()=>window.poker.snapshot()[1]===0&&!window.poker.status().busy);await page.locator('#call-button').click();
assert.equal(await page.locator('.flying-chip').count(),0);
assert.deepEqual(errors,[]);
fs.writeFileSync(`${out}/feedback-validation.json`,JSON.stringify({layout_checks:checks,max_visible_bets:maxBets,widths,seats:[2,6,9],chip_obstacle_intersections:0,stable_card_and_seat_DOM:true,optional_best_five_without_outlines:true,winners_marked_from_actual_payout:true,reduced_motion:true,browser_errors:errors},null,2)+'\n');
console.log(`PASS: ${checks} geometry checks, up to ${maxBets} bets, action focus/winners/reduced motion`);await browser.close();
