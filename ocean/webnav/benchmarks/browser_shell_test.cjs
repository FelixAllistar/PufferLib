'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const crypto = require('node:crypto');
const http = require('node:http');
const { test, before, after } = require('node:test');
const { BrowserShell, SHELL_DOCUMENT_PRESET, CONTROL_REF_BASE, CONTROL_REF_END, RESPONSE_REF_BASE, isControlRef } = require('./browser_shell.cjs');
const { collectView, WF } = require('./browser_view.cjs');
const { responseArtifact } = require('./browser_response.cjs');
const { lineRpc } = require('./rpc_client.cjs');
let browser, server, origin, flaky = true, passed = 0, policyChoices = 0;
const domain = ref => ref >= RESPONSE_REF_BASE ? 'response' : isControlRef(ref) ? 'controls' : 'document';
before(async () => {
  server = http.createServer((req, res) => {
    if (req.url === '/flaky' && flaky) { req.socket.destroy();return; }
    const title = req.url === '/one' ? 'Records' : req.url === '/two' ? 'Orders' : req.url === '/flaky' ? 'Recovered' : 'Reports';
    res.setHeader('Content-Type', 'text/html; charset=utf-8');
    res.end(`<title>${title}</title><h1>${title}</h1><label>Draft <input aria-label="Draft"></label>
      <a href="/two">Next document</a><a href="/one">First document</a><a href="/flaky">Unavailable document</a>`);
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));origin = `http://127.0.0.1:${server.address().port}`;
  const { chromium } = require(process.env.WEBNAV_PLAYWRIGHT_MODULE || 'playwright-core');
  browser = await chromium.launch({ headless: true,
    ...(process.env.WEBNAV_BROWSER_EXECUTABLE ? { executablePath: process.env.WEBNAV_BROWSER_EXECUTABLE } : {}) });
});
after(async () => {
  if (browser) await browser.close();
  if (server) await new Promise(resolve => server.close(resolve));
  if (passed !== 5) return;
  const digest = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
  const sources = ['browser_shell.cjs', 'browser_shell_test.cjs', 'browser_controls_rpc.c', 'browser_response.cjs',
    'browser_view.cjs', 'rpc_client.cjs', 'response_rpc.c', '../primitives/browser_contexts/public.h',
    '../primitives/browser_contexts/api.c', '../primitives/transport/public_json.h', '../primitives/transport/line.h'];
  const binaries = ['benchmarks/browser_controls_rpc', 'benchmarks/response_rpc', 'benchmarks/policy_rpc',
    'primitives/browser_contexts/libbrowser_contexts.so', 'primitives/response_form/libresponse_form.so'];
  fs.writeFileSync('build/webnav/benchmarks/browser-shell-validation.json', JSON.stringify({
    completed_at: new Date().toISOString(), browser_fixtures: passed, policy_choices: policyChoices,
    document_preset: SHELL_DOCUMENT_PRESET, controls_max_nodes: 24, response_max_nodes: 16,
    source_sha256: Object.fromEntries(sources.map(file => [file, digest(`${__dirname}/${file}`)])),
    binaries: Object.fromEntries(binaries.map(file => [file, digest(`build/webnav/${file}`)])),
    scope: 'Shared native controls bound to actual Chromium pages, including form focus/capacity and unchanged policy execution.',
    original_webarena_site_compared: false, native_timing_parity_claimed: false, learned_success_evaluated: false,
  }, null, 2) + '\n');
});
async function fixture(t) {
  const context = await browser.newContext({ viewport: { width: 1280, height: 720 } });t.after(() => context.close());
  const initial = await context.newPage();await initial.goto(`${origin}/one`);
  const shell = new BrowserShell();t.after(() => shell.close());await shell.reset();shell.attach(initial);
  let snapshot;
  const observe = async () => snapshot = await shell.collect(shell.page, 'Inspect the public documents and respond', { settle_ms: 0 });
  async function act(part, kind, name, extra = {}) {
    await observe();
    const node = snapshot.view.nodes.find(n => domain(n.ref) === part && n.name === name &&
      snapshot.view.capabilities.some(c => c.kind === kind && c.ref === n.ref));
    assert.ok(node, `Missing ${part} ${kind} ${name}`);
    const cap = snapshot.view.capabilities.find(c => c.kind === kind && c.ref === node.ref);
    return shell.execute(shell.page, { kind, target: cap.wire_target, arg0: cap.min0, arg1: cap.min1, text: '', ...extra });
  }
  return { context, initial, shell, observe, act };
}
test('shared controls operate real history, independent tabs and fresh last-tab replacement', async t => {
  const f = await fixture(t), { shell, act, observe, initial } = f;
  await act('document', WF.CLICK, 'Next document');assert.equal(shell.page.url(), `${origin}/two`);
  await act('controls', WF.CLICK, 'Back');assert.equal(shell.page.url(), `${origin}/one`);
  await act('controls', WF.CLICK, 'Reload');
  await act('controls', WF.CLICK, 'Forward');assert.equal(shell.page.url(), `${origin}/two`);
  await act('document', WF.CLICK, 'Draft');await act('document', WF.INSERT, 'Draft', { text: 'kept in original tab' });
  const oldSnapshot = await observe(), oldNew = oldSnapshot.view.nodes.find(n => domain(n.ref) === 'controls' && n.name === 'New tab');
  await shell.execute(shell.page, { kind: WF.CLICK, target: oldNew.ref });
  assert.equal(shell.page.url(), 'about:blank');assert.equal(f.context.pages().length, 2);
  await observe();await assert.rejects(shell.execute(shell.page, { kind: WF.CLICK, target: oldNew.ref }), /catalog/);
  await act('controls', WF.CLICK, 'Orders');assert.equal(shell.page, initial);
  assert.equal(await initial.locator('input').inputValue(), 'kept in original tab');
  await act('controls', WF.CLICK, 'Close tab');assert.ok(initial.isClosed());assert.equal(shell.page.url(), 'about:blank');
  const blankId = (await observe()).browser.active;
  await act('controls', WF.CLICK, 'Close tab');assert.equal(f.context.pages().length, 1);
  assert.ok((await observe()).browser.active > blankId);assert.equal(shell.page.url(), 'about:blank');
  passed++;
});
test('browser controls hand focus back while preserving exact response drafts and completion', async t => {
  const { shell, act, observe, initial } = await fixture(t);
  await act('document', WF.CLICK, 'Draft');await act('document', WF.INSERT, 'Draft', { text: 'browser text' });
  await act('response', WF.SELECT_OPTION, 'Task type', { arg0: 2 });
  await act('response', WF.CLICK, 'Retrieved data (JSON)');await act('response', WF.SELECT_ALL, 'Retrieved data (JSON)');
  const data = '["café",9007199254740993,1.2300e+04]';
  await act('response', WF.INSERT, 'Retrieved data (JSON)', { text: data });
  let view = (await observe()).view;
  assert.equal(view.nodes.filter(n => n.flags & WF.FOCUSED).length, 1);
  await act('controls', WF.CLICK, 'New tab');view = (await observe()).view;
  assert.equal(view.nodes.find(n => n.name === 'Retrieved data (JSON)').value, data);
  assert.equal(view.nodes.find(n => n.name === 'Retrieved data (JSON)').flags & WF.FOCUSED, 0);
  await act('controls', WF.CLICK, 'Records');assert.equal(shell.page, initial);
  assert.equal(await initial.locator('input').inputValue(), 'browser text');
  await act('response', WF.CLICK, 'Finish');assert.equal(shell.state.phase, 1);
  assert.equal(responseArtifact(shell.state), `{"task_type":"RETRIEVE","status":"SUCCESS","retrieved_data":${data},"error_details":null}\n`);
  await assert.rejects(shell.execute(shell.page, { kind: WF.WAIT }), /open browser/);
  passed++;
});
test('full tab strip, clipped DOM and long response drafts fit the declared shared ABI', async t => {
  const { context, shell, initial, observe, act } = await fixture(t);
  await initial.setContent('<title>Records</title>' + Array.from({ length: 180 }, (_, i) => `<button>${i} ${'x'.repeat(1000)}</button>`).join(''));
  for (let i = 1; i < 16; i++) await context.newPage();
  const legacy = await collectView(initial, 'Inspect documents');assert.equal(legacy.nodes.length, 128);
  // Fill via public native form commands to isolate the observation-budget
  // boundary; no private draft state is read or written by the fixture.
  const empty = { version: 2, instruction: '', elapsed_ms: 0, deadline_ms: 25600,
    omitted: 0, text_truncated: 0, incomplete: 0, nodes: [], capabilities: [] };
  async function form(kind, name, text = '', arg0 = 0) {
    const reply = await shell.rpc.request({ append: empty });
    const target = reply.view.nodes.find(n => n.name === name).ref;
    return shell.rpc.request({ action: { kind, target, arg0, arg1: 0, text } });
  }
  await form(WF.SELECT_OPTION, 'Task type', '', 2);
  await form(WF.CLICK, 'Retrieved data (JSON)');await form(WF.SELECT_ALL, 'Retrieved data (JSON)');
  const answer = JSON.stringify('a'.repeat(9000));
  for (let i = 0; i < answer.length; i += 255) await form(WF.INSERT, 'Retrieved data (JSON)', answer.slice(i, i + 255));
  const { view, browser: state } = await observe();assert.equal(state.tabs.length, 16);
  assert.equal(view.nodes.filter(n => domain(n.ref) === 'document').length, 88);
  assert.ok(view.nodes.length <= 128 && view.capabilities.length <= 1024 && view.omitted && view.text_truncated && view.incomplete);
  assert.ok(view.nodes.filter(n => domain(n.ref) === 'controls').length <= 24);
  const strings = [view.instruction, ...view.nodes.flatMap(n => [n.name, n.value])];
  assert.ok(strings.reduce((sum, text) => sum + Buffer.byteLength(text) + 1, 0) <= 16384);
  const newTab = view.nodes.find(n => domain(n.ref) === 'controls' && n.name === 'New tab');
  assert.equal(newTab.flags & WF.ENABLED, 0);
  assert.ok(view.nodes.find(n => n.name === 'Retrieved data (JSON)').value.length < answer.length);
  await act('response', WF.CLICK, 'Finish');assert.equal(shell.state.phase, 1);
  assert.equal(JSON.parse(shell.state.response_json).retrieved_data, 'a'.repeat(9000));
  assert.deepEqual(await collectView(initial, 'Inspect documents'), legacy);
  passed++;
});
test('a failed main-document navigation exposes Retry and recovers through the real browser', async t => {
  flaky = true;const { shell, act, observe } = await fixture(t);
  try { await act('document', WF.CLICK, 'Unavailable document'); }
  catch (error) { assert.match(error.message, /ERR_EMPTY_RESPONSE|Navigation|Timeout/); }
  const { view } = await observe();
  assert.equal(view.nodes.find(n => domain(n.ref) === 'controls' && n.name === 'Browser').value, 'Could not load page');
  assert.ok(view.nodes.find(n => domain(n.ref) === 'controls' && n.name === 'Retry'));
  flaky = false;await act('controls', WF.CLICK, 'Retry');
  assert.equal(await shell.page.title(), 'Recovered');
  assert.equal((await observe()).view.nodes.find(n => domain(n.ref) === 'controls' && n.name === 'Browser').value, 'Ready');
  passed++;
});
test('unchanged random and sampled policies execute complete browser-shell snapshots', async t => {
  for (const checkpoint of ['random', 'checkpoints/webnav_unified/1790999586398/0000000000998400.bin']) {
    const { shell } = await fixture(t);
    const policy = lineRpc('build/webnav/benchmarks/policy_rpc', [checkpoint, '--sample']);t.after(() => policy.close());
    await policy.request({ reset: true, seed: 91037 });let snapshot = null, executed = 0;
    for (let choice = 0; choice < 64 && shell.state.phase === 0; choice++) {
      if (!snapshot) snapshot = await shell.collect(shell.page, 'Inspect documents and respond', { elapsed_ms: executed * 50, deadline_ms: 25600, settle_ms: 0 });
      const action = await policy.request(snapshot.view);policyChoices++;
      if (!action.local) { await shell.execute(shell.page, action);executed++;snapshot = null; }
    }
    if (shell.state.phase === 0) await shell.expire();assert.ok([1, 2].includes(shell.state.phase));
  }
  passed++;
});
