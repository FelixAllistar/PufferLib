// Capture fixtures with the prior live worker before rebuilding, then run this
// against the updated bundle. Fixtures stay in the ignored build tree.
import assert from 'node:assert/strict';
import fs from 'node:fs';
const {chromium}=await import(process.env.PLAYWRIGHT_MODULE||'playwright');
const origin=process.env.POKER_WEB_URL||'http://127.0.0.1:8767';
if(process.argv.includes('--capture')){
 const b=await chromium.launch(),p=await b.newPage();
 await p.addInitScript(()=>{crypto.getRandomValues=a=>{a[0]=31;return a;};});
 await p.goto(origin);await p.waitForFunction(()=>window.poker?.snapshot());
 await p.locator('#settings-button').click();await p.locator('#seat-count').selectOption('2');await p.locator('#opponent').selectOption('river');await p.locator('#speed').selectOption('80');await p.locator('#start-table').click();
 let mid=null,terminal=null;
 for(let i=0;i<80;i++){
  await p.waitForFunction(()=>window.poker?.snapshot()&&!window.poker.status().busy&&(window.poker.snapshot()[1]===0||window.poker.snapshot()[1]===255));
  const saved=await p.evaluate(()=>JSON.parse(sessionStorage.getItem('puffer-poker.cash.v1')));
  if(saved.publicState[1]===255){terminal=saved;if(mid)break;if(saved.publicState[24]===0)await p.locator('#rebuy-button').click();else await p.waitForFunction(h=>window.poker.snapshot()[15]>h,saved.publicState[15]);continue;}
  if(saved.checkpoint.commands.length>=2)mid=saved;
  await p.locator('#call-button').click();
 }
 assert.ok(mid&&terminal,'Capture must include both current and completed hands');
 fs.mkdirSync('build/poker/pr_try',{recursive:true});fs.writeFileSync('build/poker/pr_try/saved-upgrade.json',JSON.stringify({mid,terminal}));await b.close();
 console.log('Captured predecessor mid-hand and terminal saves');process.exit(0);
}
const fixtures=JSON.parse(fs.readFileSync('build/poker/pr_try/saved-upgrade.json','utf8'));
const manifest=JSON.parse(fs.readFileSync('build/poker/web/dist/build-manifest.json','utf8'));
const hash=manifest.artifacts['poker2pro.wasm'];
assert.notEqual(hash,fixtures.mid.checkpoint.hash,'Test must cross an actual engine-hash update');
const browser=await chromium.launch();const errors=[];
async function open(saved,badTarget=false){
 const p=await browser.newPage({viewport:{width:1440,height:1000}});await p.clock.install();p.on('pageerror',e=>errors.push(e.message));
 await p.addInitScript(s=>{if(!sessionStorage.getItem('candidate-test-imported')){sessionStorage.setItem('puffer-poker.cash.v1',JSON.stringify(s));sessionStorage.setItem('candidate-test-imported','1');}},saved);
 if(badTarget)await p.route('**/build-manifest.json',route=>{const m=structuredClone(manifest);m.experimental.replay_compatible_target_wasm_sha256='0'.repeat(64);return route.fulfill({json:m});});
 await p.goto(origin);return p;
}
for(const key of ['mid','terminal']){
 const saved=fixtures[key],p=await open(saved);
 await p.waitForFunction(()=>window.poker?.snapshot()&&!window.poker.status().busy);
 assert.equal(await p.evaluate(()=>window.poker.status().failed),false);
 assert.deepEqual(await p.evaluate(()=>window.poker.snapshot()),saved.publicState);
 assert.deepEqual(await p.evaluate(()=>window.poker.session()),saved.session);
 assert.equal(await p.evaluate(()=>JSON.parse(sessionStorage.getItem('puffer-poker.cash.v1')).checkpoint.hash),hash,'Successful replay adopts current hash');
 await p.reload();await p.waitForFunction(()=>window.poker?.snapshot()&&!window.poker.status().busy);
 assert.deepEqual(await p.evaluate(()=>window.poker.snapshot()),saved.publicState);
 assert.deepEqual(await p.evaluate(()=>window.poker.session()),saved.session,'Refresh does not score terminal hand twice');
 // The upgraded candidate also plays a fresh cash hand and keeps its new hash.
 if(key==='terminal'){
  if(saved.publicState[24]===0)await p.locator('#rebuy-button').click();else await p.clock.fastForward(6500);await p.waitForFunction(h=>window.poker.snapshot()?.[15]>h&&!window.poker.status().busy&&(window.poker.snapshot()[1]===0||window.poker.snapshot()[1]===255),saved.publicState[15]);
  for(let i=0;i<20;i++){
   await p.waitForFunction(()=>!window.poker.status().busy&&(window.poker.snapshot()[1]===0||window.poker.snapshot()[1]===255));
   if(await p.evaluate(()=>window.poker.snapshot()[1]===255))break;await p.locator('#call-button').click();
  }
  assert.equal(await p.evaluate(()=>window.poker.snapshot()[1]),255);
 }
 if(key==='mid'){await p.waitForTimeout(650);await p.screenshot({path:'build/poker/pr_try/desktop.png'});}
 await p.close();
}
for(const kind of ['unknown_hash','wrong_bot_action','plain_table_scope','target_mismatch']){
 const saved=structuredClone(fixtures.mid);
 if(kind==='unknown_hash')saved.checkpoint.hash='0'.repeat(64);
 if(kind==='wrong_bot_action'){
  const command=saved.checkpoint.commands.find(c=>c.kind==='bot');assert.ok(command);command.action=command.action===-1?0:-1;
 }
 if(kind==='plain_table_scope'){saved.settings.opponent='range';saved.checkpoint.experimental=false;}
 const p=await open(saved,kind==='target_mismatch');await p.waitForFunction(()=>window.poker?.status().failed);
 assert.equal(await p.evaluate(()=>window.poker.snapshot()),undefined);
 assert.deepEqual(await p.evaluate(()=>JSON.parse(sessionStorage.getItem('puffer-poker.cash.v1')).checkpoint),saved.checkpoint,'Failed replay preserves the old save');
 await p.close();
}
assert.deepEqual(errors,[]);await browser.close();
fs.writeFileSync('build/poker/pr_try/upgrade-validation.json',JSON.stringify({old_hash:fixtures.mid.checkpoint.hash,new_hash:hash,mid_hand_exact:true,terminal_exact:true,scored_once_after_refresh:true,migrated_hash_saved:true,fresh_hand_plays:true,unknown_hash_rejected:true,altered_bot_journal_rejected:true,compatibility_scoped_to_pro:true,compatibility_bound_to_verified_target:true,failed_restore_preserves_save:true,browser_errors:errors},null,2)+'\n');
console.log('PASS: old experimental saves replay exactly; migration is scoped, checked and persisted');
