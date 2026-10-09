'use strict';
const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const { lineRpc } = require('./rpc_client.cjs');
const BASE = 0xffe10000;
const host = { version: 2, instruction: 'Use the public browser controls', elapsed_ms: 0, deadline_ms: 1000,
  omitted: 0, text_truncated: 0, incomplete: 0, nodes: [],
  capabilities: [{ kind: 0, ref: 0, wire_target: 0, flags: 0, text_capacity: 0,
    min0: 0, max0: 0, step0: 1, unit0: 0, min1: 0, max1: 0, step1: 1, unit1: 0 }] };
const navigation = { active: 7, can_open: true, can_close: true, address: 'https://example.test/records', tabs: [
  { identity: 7, phase: 0, can_back: true, can_forward: true, title: 'Records' },
  { identity: 11, phase: 0, can_back: false, can_forward: false, title: 'Orders' },
] };
async function main() {
  const rpc = lineRpc('build/webnav/benchmarks/browser_controls_rpc');
  try {
    await assert.rejects(rpc.request({ result: true }), /reset required/);
    assert.deepEqual(await rpc.request({ reset: true }), { ref_base: 0, ready: false });
    let snapshot = await rpc.request({ append: { navigation, view: host } });
    assert.equal(snapshot.ref_base, BASE);
    assert.deepEqual(snapshot.view.capabilities[0], host.capabilities[0]);
    assert.equal(snapshot.view.nodes.find(n => n.name === 'Address').value, navigation.address);
    assert.ok(snapshot.view.nodes.find(n => n.name === 'Records').flags & 128);
    let oldRef;
    for (const [label, command, argument, foreground] of [
      ['Back', 2, 0, 0], ['Forward', 3, 0, 0], ['Reload', 5, 0, 0],
      ['New tab', 6, 0, 1], ['Close tab', 8, 7, 0], ['Orders', 7, 11, 0],
    ]) {
      snapshot = await rpc.request({ append: { navigation, view: host } });
      const target = snapshot.view.nodes.find(n => n.name === label).ref;oldRef = target;
      assert.deepEqual(await rpc.request({ action: { kind: 1, target, arg0: 0, arg1: 0, text: '' } }), { command, argument, foreground });
      await assert.rejects(rpc.request({ action: { kind: 1, target, arg0: 0, arg1: 0, text: '' } }), /observe/);
    }
    snapshot = await rpc.request({ append: { navigation, view: host } });
    const before = await rpc.request({ result: true });
    await assert.rejects(rpc.request({ action: { kind: 1, target: oldRef, arg0: 0, arg1: 0, text: '' } }), /stale/);
    for (const changed of [
      { ...navigation, private_goal: 9 }, { ...navigation, active: 999 },
      { ...navigation, tabs: [navigation.tabs[0], navigation.tabs[0]] },
      { ...navigation, tabs: Array.from({ length: 17 }, (_, i) => ({ ...navigation.tabs[0], identity: i + 1 })) },
      { ...navigation, tabs: [{ ...navigation.tabs[0], phase: 3 }] },
    ]) await assert.rejects(rpc.request({ append: { navigation: changed, view: host } }), /public|navigation/);
    await assert.rejects(rpc.request({ append: { navigation, view: { ...host, private_seed: 1 } } }), /public view/);
    const collided = structuredClone(snapshot.view);
    await assert.rejects(rpc.request({ append: { navigation, view: collided } }), /overlaps/);
    const full = { ...host, nodes: Array.from({ length: 128 }, (_, i) => ({ ...snapshot.view.nodes[0], ref: i + 1 })) };
    await assert.rejects(rpc.request({ append: { navigation, view: full } }), /capacity/);
    assert.deepEqual(await rpc.request({ result: true }), before);
    const disabled = { ...navigation, can_open: false, can_close: false,
      tabs: navigation.tabs.map(t => ({ ...t, can_back: false, can_forward: false })) };
    snapshot = await rpc.request({ append: { navigation: disabled, view: host } });
    for (const label of ['Back', 'Forward', 'New tab', 'Close tab']) {
      const n = snapshot.view.nodes.find(n => n.name === label);assert.equal(n.flags & 2, 0);
      assert.ok(!snapshot.view.capabilities.some(c => c.ref === n.ref));
      await assert.rejects(rpc.request({ action: { kind: 1, target: n.ref, arg0: 0, arg1: 0, text: '' } }), /disabled/);
    }
    const previousBase = snapshot.ref_base;await rpc.request({ reset: true });
    snapshot = await rpc.request({ append: { navigation, view: host } });
    assert.ok(snapshot.ref_base > previousBase);
  } finally { rpc.close(); }
  const invalid = ['{"reset":true,"reset":true}', '{"reset":1}', '{"reset":true,"goal":1}',
    '{"append":{"navigation":{"address":"\\u0000"}}}', '{"append":{"navigation":{"address":"\\ud800"}}}',
    'x'.repeat(1024 * 1024 + 1), '{"reset":true}\0'];
  const process = spawnSync('build/webnav/benchmarks/browser_controls_rpc', [], {
    input: ['{"reset":true}', ...invalid, '{"result":true}'].join('\n') + '\n',
    encoding: 'utf8', timeout: 30000, maxBuffer: 2 * 1024 * 1024 });
  assert.equal(process.status, 0, process.stderr || String(process.error));
  const rows = process.stdout.trim().split('\n').map(JSON.parse);assert.equal(rows.length, invalid.length + 2);
  for (const row of rows.slice(1, -1)) assert.equal(typeof row.error, 'string');
  assert.deepEqual(rows[0], rows.at(-1));
  console.log('PASS: public browser-control RPC, exact WF bindings, monotonic refs, once-per-observation actions and atomic malformed/stale/capacity rejection');
}
main().catch(error => { console.error(error);process.exitCode = 1; });
