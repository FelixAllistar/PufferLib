'use strict';

// Regression fixtures. Run through benchmarks/run.sh test under the existing
// serialized guard with the pinned Playwright module and Chromium executable.
// WEBNAV_PLAYWRIGHT_MODULE=/absolute/path/to/playwright-core
// WEBNAV_BROWSER_EXECUTABLE=/absolute/path/to/chromium
const assert = require('node:assert/strict');
const { test, before, after } = require('node:test');
const { collectView, collectPage, executeAction, WF, MAX_NODES, TEXT_BYTES, MAX_CAPABILITIES } = require('./browser_view.cjs');
let browser;

before(async () => {
  const { chromium } = require(process.env.WEBNAV_PLAYWRIGHT_MODULE || 'playwright-core');
  browser = await chromium.launch({ headless: true,
    ...(process.env.WEBNAV_BROWSER_EXECUTABLE ? { executablePath: process.env.WEBNAV_BROWSER_EXECUTABLE } : {}) });
});
after(async () => { if (browser) await browser.close(); });

async function fixture(t, html) {
  const context = await browser.newContext({ viewport: { width: 1280, height: 720 } });
  t.after(() => context.close());
  const page = await context.newPage();
  await page.setContent(html);
  return page;
}
function wrapEvaluate(t, page, intercept) {
  const original = page.evaluate;
  page.evaluate = (fn, arg) => intercept(original.bind(page), fn, arg);
  t.after(() => { page.evaluate = original; });
}
function nodeNamed(view, name, role) {
  const node = view.nodes.find(node => node.name === name && (role === undefined || node.role === role));
  assert.ok(node, `Missing public node ${name}`);
  return node;
}
function capFor(view, kind, ref) {
  const cap = view.capabilities.find(cap => cap.kind === kind && cap.ref === ref);
  assert.ok(cap, `Missing capability (${kind}, ${ref})`);
  return cap;
}
async function act(page, view, kind, ref, args = {}) {
  const cap = capFor(view, kind, ref);
  return executeAction(page, { kind, target: cap.wire_target,
    arg0: cap.min0, arg1: cap.min1, ...args });
}
function assertWire(view) {
  assert.equal(view.version, 2);
  assert.ok(view.nodes.length <= MAX_NODES);
  assert.ok(view.capabilities.length <= MAX_CAPABILITIES);
  const strings = [view.instruction, ...view.nodes.flatMap(node => [node.name, node.value])];
  assert.ok(strings.reduce((n, s) => n + Buffer.byteLength(s, 'utf8') + 1, 0) <= TEXT_BYTES);
  for (const s of strings) { assert.ok(!s.includes('\0')); assert.ok(!/[\uD800-\uDBFF]$/.test(s)); }
  const refs = new Set(view.nodes.map(node => node.ref));
  assert.equal(refs.size, view.nodes.length);
  const pairs = new Set();
  for (const node of view.nodes) {
    assert.ok(node.ref > 0 && node.ref <= 0xffffffff);
    assert.ok(!node.parent || refs.has(node.parent));
    for (const key of ['x', 'y', 'width', 'height', 'scroll_x', 'scroll_y', 'scroll_max_x', 'scroll_max_y'])
      assert.ok(Number.isFinite(node[key]), key);
  }
  for (const cap of view.capabilities) {
    assert.ok(!cap.ref || refs.has(cap.ref));
    assert.ok(!cap.wire_target || refs.has(cap.wire_target));
    const key = `${cap.kind}:${cap.ref}`;
    assert.ok(!pairs.has(key), `Duplicate capability ${key}`); pairs.add(key);
    assert.ok(cap.min0 <= cap.max0 && cap.min1 <= cap.max1);
    for (const n of Object.values(cap)) assert.ok(Number.isInteger(n) && n >= 0 && n <= 0xffffffff);
  }
}

function assertPage(snapshot) {
  const { view, page } = snapshot; assertWire(view);
  assert.equal(page.version, 1); assert.ok(page.nodes.length <= MAX_NODES);
  assert.ok(page.relations.length <= 512);
  const refs = new Set(view.nodes.map(n => n.ref));
  assert.equal(new Set(page.nodes.map(n => n.ref)).size, page.nodes.length);
  for (const node of page.nodes) assert.ok(refs.has(node.ref));
  for (const relation of page.relations) {
    assert.ok(refs.has(relation.source) && refs.has(relation.target));
    assert.notEqual(relation.source, relation.target); assert.ok([1, 2, 3].includes(relation.kind));
  }
  const texts = [page.url, page.title, ...page.nodes.map(n => n.href)];
  assert.ok(texts.reduce((n, s) => n + Buffer.byteLength(s) + 1, 1) <= 16384);
}

test('page companion retains native table slots, spans, headers, headings and link destinations', async t => {
  const page = await fixture(t, `<title>Stock records</title><base href="https://records.example.test/app/">
    <h1>Inventory</h1><table><caption>Stock</caption><thead><tr>
    <th id="product" scope="col">Product</th><th id="quantity" scope="col">Quantity</th><th id="price" scope="col">Price</th>
    </tr></thead><tbody><tr><th id="alpha" scope="row" rowspan="2"><a href="alpha">Alpha</a></th>
    <td headers="alpha quantity">3</td><td headers="alpha price">12</td></tr>
    <tr><td colspan="2" headers="quantity">Unavailable</td></tr>
    <tr><th scope="row">Beta</th><td>4</td><td>99</td></tr></tbody></table>`);
  const legacy = await collectView(page, 'Inspect stock');
  const snapshot = await collectPage(page, 'Inspect stock');assertPage(snapshot);
  const v = snapshot.view, p = snapshot.page, meta = ref => p.nodes.find(n => n.ref === ref);
  assert.equal(p.title, 'Stock records');assert.equal(p.omitted, 0);
  assert.equal(meta(nodeNamed(v, 'Inventory', WF.TEXT).ref).heading_level, 1);
  assert.equal(meta(nodeNamed(v, 'Stock', WF.PANEL).ref).kind, 1);
  const quantity = nodeNamed(v, 'Quantity', WF.CELL), alpha = nodeNamed(v, 'Alpha', WF.CELL);
  const three = nodeNamed(v, '3', WF.CELL), unavailable = nodeNamed(v, 'Unavailable', WF.CELL);
  assert.equal(meta(alpha.ref).row_span, 2);
  assert.deepEqual([meta(three.ref).row, meta(three.ref).column], [2, 2]);
  assert.deepEqual([meta(unavailable.ref).row, meta(unavailable.ref).column, meta(unavailable.ref).column_span], [3, 2, 2]);
  assert.ok(p.relations.some(r => r.source === three.ref && r.target === alpha.ref && r.kind === 3));
  assert.ok(p.relations.some(r => r.source === three.ref && r.target === quantity.ref && r.kind === 3));
  const four = nodeNamed(v, '4', WF.CELL);
  assert.ok(p.relations.some(r => r.source === four.ref && r.target === quantity.ref && r.kind === 3));
  const link = nodeNamed(v, 'Alpha', WF.LINK);assert.equal(meta(link.ref).href, 'https://records.example.test/app/alpha');
  assert.ok(meta(v.nodes.find(n => n.ref === alpha.parent).ref).kind === 2);
  assert.deepEqual(await collectView(page, 'Inspect stock'), legacy, 'opt-in metadata changed legacy observations');
});

test('page companion resolves public labels and descriptions, including hidden ARIA text', async t => {
  const page = await fixture(t, `<span id="label">Record name</span><span id="help" hidden>Use the published name.</span>
    <input aria-labelledby="label" aria-describedby="help"><label for="amount">Amount</label><input id="amount">`);
  const snapshot = await collectPage(page, 'Edit a record');assertPage(snapshot);
  const { view, page: p } = snapshot;
  const input = nodeNamed(view, 'Record name', WF.INPUT), amount = nodeNamed(view, 'Amount', WF.INPUT);
  for (const [source, kind, name] of [[input.ref, 1, 'Record name'], [input.ref, 2, 'Use the published name.'], [amount.ref, 1, 'Amount']]) {
    const relation = p.relations.find(r => r.source === source && r.kind === kind);assert.ok(relation);
    assert.equal(view.nodes.find(n => n.ref === relation.target).name, name);
  }
  assert.equal(p.omitted, 0);
});

test('page companion reports unresolved relationships and metadata clipping without dangling refs', async t => {
  const page = await fixture(t, `<a href="https://example.test/${'x'.repeat(20000)}">Destination</a>
    <table><tr><td headers="absent">Value</td></tr></table><h2>Section</h2>`);
  const snapshot = await collectPage(page, 'Read public context');assertPage(snapshot);
  assert.ok(snapshot.page.omitted > 0);assert.equal(snapshot.page.text_truncated, 1);
  assert.equal(snapshot.view.incomplete, 1);
});

test('WF roles, flags, public labels, text context, and stable references', async t => {
  const page = await fixture(t, `<label for="q">Search words</label><input id="q" maxlength="12" value="abc">
    <button>Wrong choice</button><button disabled>Disabled choice</button>
    <label><input type="checkbox" checked>Receive updates</label>
    <input type="radio" aria-label="Radio"><a href="#result">Result link</a>
    <textarea aria-label="Notes">first line</textarea><div role="tab" tabindex="0" aria-selected="true">Overview</div>
    <p>Keep this independent context.</p><div style="position:absolute;top:1500px"><button>Later choice</button></div>`);
  const a = await collectView(page, 'Choose a public control', { elapsed_ms: 12, deadline_ms: 5000 });
  assertWire(a);
  assert.equal(a.elapsed_ms, 12); assert.equal(a.deadline_ms, 5000);
  const input = nodeNamed(a, 'Search words', WF.INPUT);
  assert.equal(input.value, 'abc'); assert.ok(input.flags & WF.CLICKABLE);
  assert.ok(input.flags & WF.VISIBLE); assert.ok(input.flags & WF.ENABLED);
  assert.equal(input.capacity, 49);
  capFor(a, WF.CLICK, nodeNamed(a, 'Wrong choice', WF.BUTTON).ref);
  const disabled = nodeNamed(a, 'Disabled choice', WF.BUTTON);
  assert.ok(!(disabled.flags & WF.ENABLED));
  assert.ok(!a.capabilities.some(c => c.kind === WF.CLICK && c.ref === disabled.ref));
  assert.ok(nodeNamed(a, 'Receive updates', WF.CHECKBOX).flags & WF.CHECKED);
  assert.ok(nodeNamed(a, 'Overview', WF.TAB).flags & WF.SELECTED);
  nodeNamed(a, 'Keep this independent context.', WF.TEXT);
  const later = nodeNamed(a, 'Later choice', WF.BUTTON);
  assert.ok(!(later.flags & WF.VISIBLE));
  const b = await collectView(page, 'Choose a public control');
  assert.equal(nodeNamed(b, 'Search words', WF.INPUT).ref, input.ref);
  await page.goto('data:text/html,<button>Different document</button>');
  const c = await collectView(page, 'New document');
  assert.ok(!new Set(a.nodes.map(n => n.ref)).has(nodeNamed(c, 'Different document').ref));
});

test('bounded nodes and UTF-8 budget expose omissions and retain visible controls', async t => {
  const page = await fixture(t, `${'<p>Public background context.</p>'.repeat(150)}
    <button style="position:fixed;top:10px;right:10px">A wrong but legal choice</button>
    <input style="position:fixed;top:50px;right:10px" aria-label="Edit field" value="${'🙂é'.repeat(3000)}">`);
  const view = await collectView(page, '🙂'.repeat(10000));
  assertWire(view); assert.equal(view.nodes.length, MAX_NODES);
  assert.ok(view.omitted > 0); assert.equal(view.incomplete, 1); assert.equal(view.text_truncated, 1);
  capFor(view, WF.CLICK, nodeNamed(view, 'A wrong but legal choice', WF.BUTTON).ref);
  assert.ok(nodeNamed(view, 'Edit field', WF.INPUT).value.length > 0);
});

test('trusted click, insertion, editing, selection, tab, enter, and asynchronous effects', async t => {
  const page = await fixture(t, `<form><label for="a">Field A</label><input id="a" maxlength="6">
    <label for="b">Field B</label><input id="b"><button>Submit</button></form><p id="status">Waiting</p>
    <script>
    window.events = [];
    for (const type of ['click', 'input', 'keydown']) document.addEventListener(type, event => events.push([type, event.isTrusted]));
    document.querySelector('form').addEventListener('submit', event => {
      event.preventDefault(); setTimeout(() => document.querySelector('#status').textContent = 'Submitted', 10);
    });
    </script>`);
  const observe = () => collectView(page, 'Type and submit', { settle_ms: 40 });
  let v = await observe();
  await act(page, v, WF.CLICK, nodeNamed(v, 'Field A', WF.INPUT).ref);
  v = await observe(); const id = nodeNamed(v, 'Field A', WF.INPUT).ref;
  assert.equal(capFor(v, WF.INSERT, id).text_capacity, 6);
  await act(page, v, WF.INSERT, id, { text: 'abc' });
  v = await observe(); assert.equal(nodeNamed(v, 'Field A', WF.INPUT).value, 'abc');
  await act(page, v, WF.LEFT, id);
  v = await observe(); await act(page, v, WF.BACKSPACE, id);
  assert.equal(await page.locator('#a').inputValue(), 'ac');
  v = await observe(); await act(page, v, WF.DELETE, id);
  assert.equal(await page.locator('#a').inputValue(), 'a');
  v = await observe(); await act(page, v, WF.HOME, id);
  v = await observe(); await act(page, v, WF.RIGHT, id);
  v = await observe(); await act(page, v, WF.END, id);
  v = await observe(); await act(page, v, WF.SELECT_ALL, id);
  v = await observe();
  assert.equal(nodeNamed(v, 'Field A', WF.INPUT).selection_start, 0);
  assert.equal(nodeNamed(v, 'Field A', WF.INPUT).selection_end, 1);
  await act(page, v, WF.INSERT, id, { text: 'done' });
  v = await observe(); await act(page, v, WF.TAB_KEY, 0);
  v = await observe(); assert.ok(nodeNamed(v, 'Field B', WF.INPUT).flags & WF.FOCUSED);
  await act(page, v, WF.ENTER, 0);
  v = await observe(); nodeNamed(v, 'Submitted', WF.TEXT);
  const events = await page.evaluate(() => window.events);
  assert.ok(events.some(([type]) => type === 'click'));
  assert.ok(events.some(([type]) => type === 'input'));
  assert.ok(events.every(([, trusted]) => trusted));
  await assert.rejects(executeAction(page, { kind: 20 }), /absent/);
});

test('native select preserves original option indices including wrong and disabled options', async t => {
  const page = await fixture(t, `<label for="pick">Choose item</label><select id="pick">
    <option value="zero">First wrong choice</option><option value="one" disabled>Disabled choice</option>
    <optgroup label="Alternatives"><option value="two">Second wrong choice</option><option value="three">Final choice</option></optgroup>
    </select><script>window.changedTrusted = false; document.querySelector('select').addEventListener('change', e => window.changedTrusted = e.isTrusted);</script>`);
  let view = await collectView(page, 'Choose an item'); assertWire(view);
  const select = nodeNamed(view, 'Choose item', WF.SELECT);
  const option = nodeNamed(view, 'Second wrong choice', WF.OPTION);
  assert.equal(option.parent, select.ref);
  const cap = capFor(view, WF.SELECT_OPTION, option.ref);
  assert.equal(cap.wire_target, select.ref); assert.equal(cap.min0, 2); assert.equal(cap.max0, 2);
  assert.ok(!view.capabilities.some(c => c.kind === WF.SELECT_OPTION && c.ref === select.ref));
  const disabled = nodeNamed(view, 'Disabled choice', WF.OPTION);
  assert.ok(!view.capabilities.some(c => c.kind === WF.SELECT_OPTION && c.ref === disabled.ref));
  await act(page, view, WF.SELECT_OPTION, option.ref);
  assert.equal(await page.locator('#pick').inputValue(), 'two');
  assert.equal(await page.evaluate(() => window.changedTrusted), true);
  view = await collectView(page, 'Choose an item');
  assert.ok(nodeNamed(view, 'Second wrong choice', WF.OPTION).flags & WF.SELECTED);
});

test('select omission, unsupported widgets, frames, and shadow roots mark incomplete', async t => {
  const page = await fixture(t, `<select aria-label="Large list">${Array.from({ length: 180 }, (_, i) => `<option>Choice ${i}</option>`).join('')}</select>
    <select multiple aria-label="Multiple"><option>A</option></select><input type="date" aria-label="Date">
    <div contenteditable="true">Editable rich text</div><iframe srcdoc="Public child frame"></iframe><div id="shadow"></div>
    <script>document.querySelector('#shadow').attachShadow({mode:'open'}).innerHTML='<button>Public shadow control</button>';</script>`);
  const view = await collectView(page, 'Choose a visible item'); assertWire(view);
  assert.equal(view.incomplete, 1); assert.ok(view.omitted > 0);
  const select = nodeNamed(view, 'Large list', WF.SELECT);
  assert.ok(!view.capabilities.some(c => c.kind === WF.SELECT_OPTION && c.ref === select.ref));
  for (const cap of view.capabilities.filter(c => c.kind === WF.SELECT_OPTION)) {
    assert.equal(cap.min0, cap.max0);
    assert.ok(view.nodes.some(n => n.ref === cap.ref && n.role === WF.OPTION));
  }
});

test('trusted wheel scroll uses public container bounds and reobserves offsets', async t => {
  const page = await fixture(t, `<div aria-label="Scroll region" style="height:180px;width:240px;overflow:auto">
    <div style="height:1200px">Visible start<p style="margin-top:900px">Later public context</p></div></div>`);
  let view = await collectView(page, 'Scroll the region', { settle_ms: 200 });
  const region = nodeNamed(view, 'Scroll region', WF.PANEL);
  const cap = capFor(view, WF.SCROLL, region.ref);
  assert.ok(cap.max1 > 0); assert.equal(cap.unit1, 2);
  await act(page, view, WF.SCROLL, region.ref, { arg1: Math.min(500, cap.max1) });
  view = await collectView(page, 'Scroll the region');
  assert.ok(nodeNamed(view, 'Scroll region').scroll_y > 0);
});

test('stale targets, changed focus, and unadvertised actions are rejected', async t => {
  const page = await fixture(t, '<input aria-label="One"><input aria-label="Two"><button>Detach</button>');
  let v = await collectView(page, 'Edit');
  await act(page, v, WF.CLICK, nodeNamed(v, 'One').ref);
  v = await collectView(page, 'Edit');
  await page.locator('input').nth(1).focus();
  await assert.rejects(act(page, v, WF.INSERT, nodeNamed(v, 'One').ref, { text: 'x' }), /Focused field/);
  v = await collectView(page, 'Click');
  const detached = nodeNamed(v, 'Detach');
  await page.locator('button').evaluate(el => el.remove());
  await assert.rejects(act(page, v, WF.CLICK, detached.ref), /stale|detached/);
  await assert.rejects(executeAction(page, { kind: WF.WAIT }), /collectView/);
  v = await collectView(page, 'Navigate');
  await page.goto('data:text/html,<button>Replacement</button>');
  await assert.rejects(act(page, v, WF.WAIT, 0), /previous document/);
});

test('select commits only the requested option, never intermediate onchange submissions', async t => {
  const page=await fixture(t,'<select aria-label="Pick"><option>A</option><option>B</option><option>C</option></select>');
  await page.evaluate(()=>{window.changes=[];document.querySelector('select').addEventListener('change',e=>window.changes.push(e.target.selectedIndex));});
  const v=await collectView(page,'Choose C');
  await act(page,v,WF.SELECT_OPTION,nodeNamed(v,'Pick',WF.SELECT).ref,{arg0:2});
  assert.deepEqual(await page.evaluate(()=>window.changes),[2]);
});

test('ill-formed public UTF-16 is normalized and flagged before JSON transport', async t => {
  const page=await fixture(t,'<button>Label</button>');
  await page.locator('button').evaluate(el=>{el.textContent='Bad\ud800text';});
  const v=await collectView(page,'Instruction\udc00');
  assert.equal(v.incomplete,1);assert.equal(v.text_truncated,1);
  assert.ok(v.instruction.isWellFormed());
  for(const n of v.nodes){assert.ok(n.name.isWellFormed());assert.ok(n.value.isWellFormed());}
  assert.ok(!/\\ud[89ab][0-9a-f]{2}/i.test(JSON.stringify(v)));
});

test('restored document with an existing namespace cannot accept a newer snapshot', async t => {
  const page=await fixture(t,'<button>First</button>');
  const first=await collectView(page,'Observe first');
  // Model the browser restoring its earlier document globals through BFCache.
  const original=await page.evaluate(()=>{const key=Object.getOwnPropertyNames(window).find(k=>k.startsWith('__wf_public_'));return {key,base:window[key].documentBase};});
  await page.evaluate(({key})=>{window[key].documentBase+=0x100000;},original);
  const newer=await collectView(page,'Observe second');
  await page.evaluate(({key,base})=>{window[key].documentBase=base;},original);
  await assert.rejects(act(page,newer,WF.WAIT,0),/previous document/);
  assert.ok(first.nodes.length);
});

test('collection retries navigation before projection with a fresh nonzero reservation', async t => {
  const page = await fixture(t, '<button>Before navigation</button>');
  const first = await collectView(page, 'Observe before');
  const reservations = [];
  wrapEvaluate(t, page, async (evaluate, fn, arg) => {
    // A separate namespace-presence probe would reintroduce the race.
    assert.ok(arg && Number.isInteger(arg.base) && arg.base > 0);
    reservations.push(arg.base);
    if (reservations.length === 1) {
      await assert.rejects(executeAction(page, { kind: WF.WAIT }), /collectView/);
      await page.goto('data:text/html,<button>After navigation</button>');
      throw new Error('page.evaluate: Execution context was destroyed, most likely because of a navigation');
    }
    return evaluate(fn, arg);
  });
  const view = await collectView(page, 'Observe after');
  assertWire(view);
  assert.equal(reservations.length, 2);
  assert.equal(reservations[1], reservations[0] + 0x100000);
  assert.equal(nodeNamed(view, 'After navigation').ref, reservations[1]);
  assert.ok(!new Set(first.nodes.map(n => n.ref)).has(nodeNamed(view, 'After navigation').ref));
  assert.ok(!Object.hasOwn(view, 'created'));
  assert.ok(!Object.hasOwn(view, 'documentBase'));
});

test('the default deadline is usable and explicit deadline values are preserved', async t => {
  const page = await fixture(t, '<button>Public control</button>');
  const defaultView = await collectView(page, 'Default timing');
  assert.equal(defaultView.deadline_ms, 25600);
  const explicit = await collectView(page, 'Explicit timing', { deadline_ms: 1234 });
  assert.equal(explicit.deadline_ms, 1234);
  const zero = await collectView(page, 'Explicit zero', { deadline_ms: 0 });
  assert.equal(zero.deadline_ms, 0);
});

test('an ambiguous failure after namespace creation never reuses its range', async t => {
  const page = await fixture(t, '<button>Original document</button>');
  await collectView(page, 'Observe original');
  const reservations = [];
  wrapEvaluate(t, page, async (evaluate, fn, arg) => {
    // Allow executeAction's existing document-identity check through unchanged.
    if (!arg || !Number.isInteger(arg.base)) return evaluate(fn, arg);
    reservations.push(arg.base);
    if (reservations.length === 1) {
      await page.goto('data:text/html,<button>Created before failure</button>');
      const lost = await evaluate(fn, arg);
      assert.equal(lost.created, true);
      assert.equal(lost.documentBase, arg.base);
      throw new Error('page.evaluate: Cannot find context with specified id');
    }
    return evaluate(fn, arg);
  });
  const recovered = await collectView(page, 'Recover same document');
  assertWire(recovered);
  assert.equal(reservations.length, 2);
  assert.equal(reservations[1], reservations[0] + 0x100000);
  assert.equal(nodeNamed(recovered, 'Created before failure').ref, reservations[0]);
  // The retry reports the actual installed base, preserving action/BFCache checks.
  await executeAction(page, { kind: WF.WAIT });
  await page.goto('data:text/html,<button>Following document</button>');
  const following = await collectView(page, 'Observe following');
  assert.equal(nodeNamed(following, 'Following document').ref, reservations[1]);
  assert.notEqual(nodeNamed(following, 'Following document').ref, nodeNamed(recovered, 'Created before failure').ref);
});

test('stable observations reclaim unused reservations without changing document refs', async t => {
  const page = await fixture(t, '<button>Stable document</button>');
  const initial = await collectView(page, 'Observe stable');
  const initialRef = nodeNamed(initial, 'Stable document').ref;
  const reservations = [], created = [];
  wrapEvaluate(t, page, async (evaluate, fn, arg) => {
    reservations.push(arg.base);
    const result = await evaluate(fn, arg);
    created.push(result.created);
    return result;
  });
  for (let i = 0; i < 12; i++) {
    const view = await collectView(page, 'Observe stable');
    assert.equal(nodeNamed(view, 'Stable document').ref, initialRef);
  }
  assert.ok(reservations.every(base => base === initialRef + 0x100000));
  assert.ok(created.every(value => value === false));
  await page.goto('data:text/html,<button>Next document</button>');
  const next = await collectView(page, 'Observe next');
  assert.equal(nodeNamed(next, 'Next document').ref, initialRef + 0x100000);
  assert.equal(created.at(-1), true);
});

test('simultaneous pages reserve disjoint document ranges before evaluating', async t => {
  const left = await fixture(t, '<button>Left page</button>');
  const right = await fixture(t, '<button>Right page</button>');
  const reservations = [];
  let release;
  const gate = new Promise(resolve => { release = resolve; });
  for (const page of [left, right]) wrapEvaluate(t, page, async (evaluate, fn, arg) => {
    reservations.push(arg.base);
    if (reservations.length === 2) release();
    await gate;
    return evaluate(fn, arg);
  });
  const [a, b] = await Promise.all([collectView(left, 'Observe left'), collectView(right, 'Observe right')]);
  assertWire(a); assertWire(b);
  assert.equal(reservations.length, 2);
  assert.notEqual(reservations[0], reservations[1]);
  assert.ok(reservations.every(base => base > 0));
  const refs = new Set(a.nodes.map(n => n.ref));
  assert.ok(b.nodes.every(n => !refs.has(n.ref)));
});

test('an unused concurrent reservation cannot rewind past another page allocation', async t => {
  const existing = await fixture(t, '<button>Existing page</button>');
  const other = await fixture(t, '<button>Concurrent page</button>');
  const following = await fixture(t, '<button>Following page</button>');
  const initial = await collectView(existing, 'Observe existing');
  let release, entered;
  const gate = new Promise(resolve => { release = resolve; });
  const arrived = new Promise(resolve => { entered = resolve; });
  let unusedBase;
  wrapEvaluate(t, existing, async (evaluate, fn, arg) => {
    unusedBase = arg.base;
    entered();
    await gate;
    return evaluate(fn, arg);
  });
  const pending = collectView(existing, 'Reobserve existing');
  await arrived;
  let concurrent;
  try { concurrent = await collectView(other, 'Observe concurrent'); }
  finally { release(); }
  const stable = await pending;
  assert.equal(nodeNamed(stable, 'Existing page').ref, nodeNamed(initial, 'Existing page').ref);
  const otherRef = nodeNamed(concurrent, 'Concurrent page').ref;
  assert.equal(otherRef, unusedBase + 0x100000);
  const next = await collectView(following, 'Observe following');
  assert.equal(nodeNamed(next, 'Following page').ref, otherRef + 0x100000);
});

test('context retries are bounded and leave no older cached view on exhaustion', async t => {
  const page = await fixture(t, '<button>Old catalog</button>');
  await collectView(page, 'Observe old');
  const failure = new Error('page.evaluate: Execution context was destroyed, most likely because of a navigation');
  const reservations = [];
  wrapEvaluate(t, page, async (_evaluate, _fn, arg) => {
    reservations.push(arg.base);
    throw failure;
  });
  await assert.rejects(collectView(page, 'Retry then fail'), error => error === failure);
  assert.equal(reservations.length, 3);
  assert.equal(new Set(reservations).size, 3);
  await assert.rejects(executeAction(page, { kind: WF.WAIT }), /collectView/);
});

test('schema and closed-page errors propagate without navigation retries', async t => {
  for (const message of ['Unexpected public schema', 'Target page, context or browser has been closed']) {
    const page = await fixture(t, '<button>Old catalog</button>');
    await collectView(page, 'Observe old');
    const failure = new Error(message);
    let calls = 0;
    wrapEvaluate(t, page, async () => { calls++; throw failure; });
    await assert.rejects(collectView(page, 'Fail immediately'), error => error === failure);
    assert.equal(calls, 1);
    await assert.rejects(executeAction(page, { kind: WF.WAIT }), /collectView/);
  }
});
