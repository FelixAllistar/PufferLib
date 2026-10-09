'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { test, before, after } = require('node:test');
const { BrowserResponse, RESPONSE_REF_BASE, RESPONSE_PRESET, responseArtifact } = require('./browser_response.cjs');
const { collectView, WF } = require('./browser_view.cjs');
const { lineRpc } = require('./rpc_client.cjs');
let browser, passed = 0;
before(async () => {
  const { chromium } = require(process.env.WEBNAV_PLAYWRIGHT_MODULE || 'playwright-core');
  browser = await chromium.launch({ headless: true,
    ...(process.env.WEBNAV_BROWSER_EXECUTABLE ? { executablePath: process.env.WEBNAV_BROWSER_EXECUTABLE } : {}) });
});
after(async () => {
  if (browser) await browser.close();
  if (passed !== 4) return;
  const sources = ['browser_response.cjs', 'browser_view.cjs', 'rpc_client.cjs', 'response_rpc.c', 'browser_response_test.cjs'];
  const digest = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
  fs.writeFileSync('build/webnav/benchmarks/browser-response-validation.json', JSON.stringify({
    completed_at: new Date().toISOString(), browser_fixtures: passed, preset: RESPONSE_PRESET,
    source_sha256: Object.fromEntries(sources.map(file => [file, digest(path.join(__dirname, file))])),
    binaries: Object.fromEntries(['benchmarks/response_rpc', 'benchmarks/policy_rpc', 'primitives/response_form/libresponse_form.so']
      .map(file => [file, digest(path.join('build/webnav', file))])),
    original_webarena_site_compared: false, learned_success_evaluated: false,
  }, null, 2) + '\n');
});
async function fixture(t, html) {
  const context = await browser.newContext({ viewport: { width: 1280, height: 720 } });
  t.after(() => context.close());
  const page = await context.newPage();await page.setContent(html);
  const response = new BrowserResponse();t.after(() => response.close());await response.reset();
  let snapshot;
  const observe = async () => snapshot = await response.collect(page, 'Read the published records and respond', { settle_ms: 0 });
  const act = async (domain, kind, name, extra = {}) => {
    await observe();
    const n = snapshot.view.nodes.find(n => n.name === name && (n.ref >= RESPONSE_REF_BASE) === (domain === 'response') &&
      snapshot.view.capabilities.some(c => c.kind === kind && c.ref === n.ref));
    assert.ok(n, `Missing ${domain} node ${name}`);
    const cap = snapshot.view.capabilities.find(c => c.kind === kind && c.ref === n.ref);
    assert.ok(cap, `Missing ${kind} for ${name}`);
    return response.execute(page, { kind, target: cap.wire_target, arg0: cap.min0, arg1: cap.min1, ...extra });
  };
  return { page, response, observe, act };
}

test('trusted browser editing and native response editing exchange focus and preserve exact answers', async t => {
  const f = await fixture(t, '<label>Name <input aria-label="Name" value="Ada"></label><h1>Records</h1><a href="#next">Next</a>');
  const { page, response, act, observe } = f;
  await act('browser', WF.CLICK, 'Name');await act('browser', WF.INSERT, 'Name', { text: ' Lovelace' });
  const browserValue = await page.locator('input').inputValue();assert.ok(browserValue.includes('Lovelace'));
  await act('response', WF.CLICK, 'Retrieved data (JSON)');
  let { view } = await observe();
  assert.equal(view.nodes.filter(n => n.flags & WF.FOCUSED).length, 1);
  assert.ok(view.nodes.find(n => n.name === 'Retrieved data (JSON)').flags & WF.FOCUSED);
  await assert.rejects(response.execute(page, { kind: WF.INSERT, target: 0, text: 'wrong field' }), /catalog/);
  await act('response', WF.SELECT_ALL, 'Retrieved data (JSON)');
  const data = '["café",9007199254740993,1.2300e+04]';
  await act('response', WF.INSERT, 'Retrieved data (JSON)', { text: data });
  assert.equal(await page.locator('input').inputValue(), browserValue);
  await act('response', WF.CLICK, 'Finish');assert.equal(response.state.phase, 0);
  view = (await observe()).view;assert.ok(view.nodes.some(n => n.name.includes('must be valid JSON')));
  await act('browser', WF.CLICK, 'Name');view = (await observe()).view;
  assert.ok(view.nodes.find(n => n.name === 'Name' && n.role === WF.INPUT).flags & WF.FOCUSED);
  assert.equal(view.nodes.find(n => n.name === 'Retrieved data (JSON)').flags & WF.FOCUSED, 0);
  assert.equal(view.nodes.find(n => n.name === 'Retrieved data (JSON)').value, data);
  await act('response', WF.SELECT_OPTION, 'Task type', { arg0: 2 });
  const result = await act('response', WF.CLICK, 'Finish');assert.equal(result.domain, 'response');assert.equal(result.phase, 1);
  assert.equal(responseArtifact(response.state), `{"task_type":"RETRIEVE","status":"SUCCESS","retrieved_data":${data},"error_details":null}\n`);
  assert.equal(await page.locator('input').inputValue(), browserValue);
  assert.deepEqual(await response.expire(), { phase: 1, ref_base: result.ref_base, response_json: result.response_json });
  passed++;
});

test('composed snapshots reserve controls and report DOM clipping while legacy limits stay unchanged', async t => {
  const html = Array.from({ length: 180 }, (_, i) => `<button style="width:60px;height:20px" aria-label="${i} ${'x'.repeat(1000)}">${i}</button>`).join('');
  const { page, observe, response } = await fixture(t, html);
  const legacy = await collectView(page, 'Inspect records');assert.equal(legacy.nodes.length, 128);
  const snapshot = await observe(), v = snapshot.view;
  assert.equal(v.nodes.filter(n => n.ref < RESPONSE_REF_BASE).length, RESPONSE_PRESET.node_limit);
  assert.ok(v.nodes.length <= 128);assert.ok(v.omitted > 0 && v.text_truncated && v.incomplete);
  const strings = [v.instruction, ...v.nodes.flatMap(n => [n.name, n.value])];
  assert.ok(strings.reduce((n, s) => n + Buffer.byteLength(s) + 1, 0) <= 16384);
  assert.ok(v.nodes.some(n => n.ref >= RESPONSE_REF_BASE && n.name === 'Finish'));
  assert.deepEqual(await collectView(page, 'Inspect records'), legacy);
  const p = lineRpc('build/webnav/benchmarks/policy_rpc', ['random']);t.after(() => p.close());
  await p.request({ reset: true, seed: 7 });const command = await p.request(v);
  assert.equal(typeof command.kind, 'number');assert.equal(response.state.phase, 0);
  passed++;
});

test('expiry produces absence and stale submission controls cannot act after reset', async t => {
  const { response, page, observe } = await fixture(t, '<h1>Record list</h1><button>Open</button>');
  const first = await observe(), finish = first.view.nodes.find(n => n.ref >= RESPONSE_REF_BASE && n.name === 'Finish');
  const expired = await response.expire();assert.equal(expired.phase, 2);assert.equal(responseArtifact(expired), 'null\n');
  await assert.rejects(response.execute(page, { kind: WF.CLICK, target: finish.ref }), /open response/);
  const reset = await response.reset();assert.ok(reset.ref_base > finish.ref);await observe();
  await assert.rejects(response.execute(page, { kind: WF.CLICK, target: finish.ref }), /catalog/);
  assert.equal(response.state.phase, 0);assert.equal(responseArtifact(response.state), 'null\n');passed++;
});

test('unchanged sampled checkpoint and random policy drive composed browser episodes', async t => {
  const drivers = ['random', 'checkpoints/webnav_unified/1790999586398/0000000000998400.bin'];
  for (const checkpoint of drivers) {
    const { page, response } = await fixture(t, '<h1>Public records</h1><label>Filter <input aria-label="Filter"></label><button>Search</button>');
    const policy = lineRpc('build/webnav/benchmarks/policy_rpc', [checkpoint, '--sample']);t.after(() => policy.close());
    await policy.request({ reset: true, seed: 91037 });
    let snapshot = null, executed = 0;
    for (let step = 0; step < 64 && response.state.phase === 0; step++) {
      if (!snapshot) snapshot = await response.collect(page, 'Find a public record', { elapsed_ms: executed * 50, deadline_ms: 25600, settle_ms: 0 });
      const command = await policy.request(snapshot.view);
      if (!command.local) { await response.execute(page, command);executed++;snapshot = null; }
    }
    if (response.state.phase === 0) await response.expire();
    assert.ok([1, 2].includes(response.state.phase));assert.equal(typeof responseArtifact(response.state), 'string');
  }
  passed++;
});
