'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const crypto = require('node:crypto');
const http = require('node:http');
const { test, before, after } = require('node:test');
const { lineRpc } = require('./rpc_client.cjs');
let browser, server, origin, passed = 0, comparisons = 0;
const command = { WAIT: 0, NAVIGATE: 1, BACK: 2, FORWARD: 3, RETRY: 4, RELOAD: 5, OPEN: 6, SWITCH: 7, CLOSE: 8 };
before(async () => {
  server = http.createServer((req, res) => {
    res.setHeader('Content-Type', 'text/html; charset=utf-8');
    res.end(`<title>Document ${req.url}</title><h1>Document ${req.url}</h1><label>Draft <input aria-label="Draft"></label>`);
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  origin = `http://127.0.0.1:${server.address().port}`;
  const { chromium } = require(process.env.WEBNAV_PLAYWRIGHT_MODULE || 'playwright-core');
  browser = await chromium.launch({ headless: true,
    ...(process.env.WEBNAV_BROWSER_EXECUTABLE ? { executablePath: process.env.WEBNAV_BROWSER_EXECUTABLE } : {}) });
});
after(async () => {
  if (browser) await browser.close();
  if (server) await new Promise(resolve => server.close(resolve));
  if (passed !== 3) return;
  const digest = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
  const files = ['ocean/webnav/benchmarks/browser_contexts_test.cjs', 'ocean/webnav/benchmarks/browser_contexts_driver.py',
    'ocean/webnav/benchmarks/browser_contexts_types.py', 'ocean/webnav/benchmarks/public_types.py',
    'ocean/webnav/benchmarks/rpc_client.cjs', 'build/webnav/primitives/browser_contexts/libbrowser_contexts.so'];
  fs.writeFileSync('build/webnav/benchmarks/browser-contexts-validation.json', JSON.stringify({
    completed_at: new Date().toISOString(), browser_fixtures: passed, comparisons,
    source_sha256: Object.fromEntries(files.map(file => [file, digest(file)])),
    scope: 'Settled route, tab identity/selection, bounded back/forward history and reload on independent local Chromium documents.',
    compared_async_timing: false, compared_pending_navigation_commit: false,
    original_webarena_workflow_compared: false, learned_policy_evaluated: false,
  }, null, 2) + '\n');
});
async function fixture(t) {
  const context = await browser.newContext();t.after(() => context.close());
  const rpc = lineRpc('python3', ['ocean/webnav/benchmarks/browser_contexts_driver.py']);t.after(() => rpc.close());
  const urls = ['about:blank', `${origin}/one`, `${origin}/two`, `${origin}/three`];
  let native = await rpc.request({ reset: [42, urls.length, 16] }), active = 1, next = 2;
  const tabs = [{ id: 1, page: await context.newPage() }];
  const current = () => tabs.find(tab => tab.id === active).page;
  async function compare(label) {
    assert.deepEqual(native.tabs.map(tab => tab.identity), tabs.map(tab => tab.id), `${label}: context identities`);
    assert.equal(native.active, active, `${label}: active identity`);
    assert.equal(context.pages().length, tabs.length);
    for (let i = 0; i < tabs.length; i++) {
      const page = tabs[i].page, expected = native.tabs[i];
      const session = await context.newCDPSession(page);
      const history = await session.send('Page.getNavigationHistory');await session.detach();
      assert.equal(urls[expected.route], page.url(), `${label}: tab ${tabs[i].id} route`);
      assert.equal(!!expected.can_back, history.currentIndex > 0, `${label}: tab ${tabs[i].id} back`);
      assert.equal(!!expected.can_forward, history.currentIndex + 1 < history.entries.length, `${label}: tab ${tabs[i].id} forward`);
    }
    comparisons++;
  }
  async function act(kind, argument = 0, foreground = 0) {
    native = await rpc.request({ step: [kind, argument, foreground, 500] });
    if (kind === command.NAVIGATE) await current().goto(urls[argument]);
    else if (kind === command.BACK) await current().goBack();
    else if (kind === command.FORWARD) await current().goForward();
    else if (kind === command.RELOAD) await current().reload();
    else if (kind === command.OPEN) {
      const tab = { id: next++, page: await context.newPage() };tabs.push(tab);
      if (argument) await tab.page.goto(urls[argument]);
      if (foreground) { active = tab.id;await tab.page.bringToFront(); }
    } else if (kind === command.SWITCH) {active = argument;await current().bringToFront();}
    else if (kind === command.CLOSE) {
      const index = tabs.findIndex(tab => tab.id === argument);await tabs[index].page.close();tabs.splice(index, 1);
      if (!tabs.length) {const tab = { id: next++, page: await context.newPage() };tabs.push(tab);active = tab.id;}
      else if (active === argument) active = tabs.at(-1).id;
    }
    await compare(`${kind}/${argument}`);
    return native;
  }
  await compare('initial blank');
  return { act, current, rpc, tabs, compare };
}

test('fresh blank, repeated destination, back/forward and reload match Chromium history', async t => {
  const f = await fixture(t);
  await f.act(command.NAVIGATE, 1);
  await f.act(command.NAVIGATE, 1);
  await f.act(command.BACK);
  await f.act(command.BACK);
  await f.act(command.FORWARD);
  await f.act(command.FORWARD);
  await f.act(command.NAVIGATE, 2);
  await f.act(command.BACK);
  await f.act(command.NAVIGATE, 1);
  await f.act(command.RELOAD);
  await f.act(command.FORWARD);
  await f.act(command.NAVIGATE, 0);
  await f.act(command.BACK);
  await f.act(command.FORWARD);
  passed++;
});
test('tabs retain separate documents and independent navigation histories', async t => {
  const f = await fixture(t);
  await f.act(command.OPEN, 1, 1);await f.current().locator('input').fill('kept in first document');
  await f.act(command.OPEN, 2, 0);
  assert.equal(await f.current().locator('input').inputValue(), 'kept in first document');
  await f.act(command.SWITCH, 3);await f.act(command.NAVIGATE, 3);
  await f.act(command.SWITCH, 2);
  assert.equal(await f.current().locator('input').inputValue(), 'kept in first document');
  await f.act(command.NAVIGATE, 2);await f.act(command.BACK);
  await f.act(command.SWITCH, 3);await f.act(command.BACK);await f.act(command.FORWARD);
  passed++;
});
test('closing selects the final surviving tab and replacing the last tab uses a fresh blank identity', async t => {
  const f = await fixture(t);
  await f.act(command.OPEN, 1, 1);await f.act(command.OPEN, 2, 1);
  await f.act(command.SWITCH, 1);await f.act(command.CLOSE, 1);
  await assert.rejects(f.rpc.request({ step: [command.SWITCH, 1, 0, 500] }), /rejected atomically/);
  await f.act(command.CLOSE, 2);await f.act(command.CLOSE, 3);
  await f.act(command.NAVIGATE, 3);
  assert.equal(f.tabs[0].id, 4);assert.equal(f.tabs.length, 1);
  passed++;
});
