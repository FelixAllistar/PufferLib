import assert from 'node:assert/strict';
import fs from 'node:fs';
const {chromium}=await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const origin=process.env.POKER_WEB_URL || 'http://127.0.0.1:8767';
const out='build/poker/web';fs.mkdirSync(out,{recursive:true});
const browser=await chromium.launch({headless:true});
const page=await browser.newPage({viewport:{width:1440,height:1100}});
const errors=[],report=[];
page.on('pageerror',e=>errors.push(e.message));
page.on('console',m=>{if(m.type()==='error'&&!m.text().includes('fonts.googleapis'))errors.push(m.text());});
await page.goto(origin);await page.waitForFunction(()=>window.poker?.snapshot());
async function configure(players,opponent){
  await page.locator('#settings-button').click();
  await page.locator('#seat-count').selectOption(String(players));
  await page.locator('#opponent').selectOption(opponent);
  await page.locator('#speed').selectOption('80');
  await page.locator('#start-table').click();
  await page.waitForFunction(({players,opponent})=>window.poker?.snapshot()?.[0]===players&&window.poker.status().settings.opponent===opponent&&!window.poker.status().busy,{players,opponent});
}
for(const [players,opponent] of [[2,'rules'],[2,'range'],[2,'fullhouse'],[2,'river'],[6,'mixed'],[9,'mixed']]){
  await configure(players,opponent);
  let decisions=0,raises=0,folds=0;
  const start=Date.now();
  while(await page.evaluate(()=>window.poker.session().hands)<5){
    assert.ok(Date.now()-start<120000,'Hand progress timed out');
    const status=await page.evaluate(()=>({s:window.poker.snapshot(),status:window.poker.status()}));
    assert.equal(status.status.failed,false);
    if(status.status.busy){await page.waitForTimeout(25);continue;}
    const s=status.s;
    if(s[1]===255){
      const stats=await page.evaluate(()=>({core:window.poker.session(),net:document.querySelector('#session-net').textContent}));
      assert.equal(stats.core.hands,s[15]);
      if(stats.core.hands<5)await page.locator('#next-button').click();
    }else if(s[1]===0){
      assert.ok(await page.locator('#call-button').isEnabled());
      assert.equal(await page.locator('#fold-button').isEnabled(),s[12]>0);
      if(decisions%7===0&&(s[9]&~3)){
        await page.locator('[data-size="min"]').click();await page.locator('#raise-button').click();raises++;
      }else if(decisions%11===0&&s[12]){await page.locator('#fold-button').click();folds++;}
      else await page.locator('#call-button').click();
      decisions++;
    }else await page.waitForTimeout(35);
  }
  const snapshot=await page.evaluate(()=>window.poker.snapshot());
  assert.equal(snapshot[1],255);
  assert.equal(snapshot.slice(24).filter((_,i)=>i%9===0).reduce((a,b)=>a+b,0),20000*players);
  report.push({players,opponent,hands:5,human_decisions:decisions,raises,folds});
}
await configure(6,'mixed');await page.waitForFunction(()=>window.poker.snapshot()[1]===0);
await page.screenshot({path:`${out}/desktop.png`,fullPage:true});
for(const players of [2,6,9]){
  await page.setViewportSize({width:390,height:844});await configure(players,'mixed');
  await page.waitForFunction(()=>window.poker.snapshot()[1]===0||window.poker.snapshot()[1]===255);
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false,'Mobile horizontal overflow');
  const bounds=await page.locator('.seat-info').evaluateAll(nodes=>nodes.map(n=>{const r=n.getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height}}));
  for(let i=0;i<bounds.length;i++)for(let j=i+1;j<bounds.length;j++){
    const a=bounds[i],b=bounds[j];assert.ok(a.x+a.w<=b.x||b.x+b.w<=a.x||a.y+a.h<=b.y||b.y+b.h<=a.y,`Overlapping ${players}-seat mobile labels ${i}/${j}`);
  }
  await page.screenshot({path:`${out}/mobile${players}.png`,fullPage:true});
}
await page.locator('#help-button').click();assert.equal(await page.locator('#help').isVisible(),true);await page.keyboard.press('Escape');
await page.locator('#sound-button').click();assert.equal(await page.locator('#sound-button').getAttribute('aria-pressed'),'true');
await configure(2,'river');await page.locator('#settings-button').click();await page.locator('#seat-count').selectOption('9');
assert.equal(await page.locator('#opponent option[value="river"]').isDisabled(),true);
assert.equal(await page.locator('#opponent option[value="fullhouse"]').isDisabled(),true);
await page.keyboard.press('Escape');
assert.deepEqual(errors,[],'Browser runtime errors');
fs.writeFileSync(`${out}/browser-validation.json`,JSON.stringify({tables:report,completed_hands:30,mobile:[2,6,9],console_errors:errors},null,2)+'\n');
console.log(JSON.stringify(report,null,2));await browser.close();
