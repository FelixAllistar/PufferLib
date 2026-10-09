'use strict';
const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const { lineRpc } = require('./rpc_client.cjs');
const { RESPONSE_REF_BASE: BASE } = require('./browser_response.cjs');
const cap = (kind, ref, extra = {}) => ({ kind, ref, wire_target: ref, flags: 0, text_capacity: 0,
  min0: 0, max0: 0, step0: 1, unit0: 0, min1: 0, max1: 0, step1: 1, unit1: 0, ...extra });
const node = (ref, name) => ({ ref, parent: 0, role: 3, flags: 23, name, value: 'Browser draft',
  x: 20, y: 30, width: 120, height: 20, selection_start: 13, selection_end: 13, capacity: 256,
  scroll_x: 0, scroll_y: 0, scroll_max_x: 0, scroll_max_y: 0 });
const host = { version: 2, instruction: 'Use only public fields', elapsed_ms: 100, deadline_ms: 1000,
  omitted: 0, text_truncated: 0, incomplete: 0, nodes: [node(41, 'Browser field')],
  capabilities: [cap(0, 0), cap(1, 41), cap(2, 41, { wire_target: 0, flags: 1, text_capacity: 255 }), cap(11, 0)] };

async function main() {
  const rpc = lineRpc('build/webnav/benchmarks/response_rpc');
  try {
    await assert.rejects(rpc.request({ result: true }), /reset required/);
    const initial = await rpc.request({ reset: true });assert.equal(initial.phase, 0);
    let snapshot = await rpc.request({ append: host });
    assert.deepEqual(snapshot.view.nodes[0], host.nodes[0]);
    assert.equal(snapshot.view.instruction, host.instruction);
    async function act(kind, label, arg0 = 0, text = '') {
      snapshot = await rpc.request({ append: host });
      const n = snapshot.view.nodes.find(n => n.ref >= BASE && n.name === label);
      assert.ok(n, label);
      return rpc.request({ action: { kind, target: n.ref, arg0, arg1: 0, text } });
    }
    await act(1, 'Retrieved data (JSON)');
    snapshot = await rpc.request({ append: host });
    assert.equal(snapshot.view.nodes[0].flags & 16, 0);
    assert.ok(!snapshot.view.capabilities.some(c => c.kind === 2 && c.wire_target === 0));
    assert.ok(!snapshot.view.capabilities.some(c => c.kind === 11));
    assert.ok(snapshot.view.capabilities.some(c => c.kind === 1 && c.ref === 41));
    await act(9, 'Retrieved data (JSON)');await act(2, 'Retrieved data (JSON)', 0, 'invalid');
    await act(1, 'Finish');snapshot = await rpc.request({ append: host });
    assert.equal(snapshot.phase, 0);assert.ok(snapshot.view.nodes.some(n => n.name.includes('must be valid JSON')));
    const stale = snapshot.view.nodes.find(n => n.name === 'Retrieved data (JSON)').ref;
    await rpc.request({ blur: true });snapshot = await rpc.request({ append: host });
    assert.ok(snapshot.view.nodes[0].flags & 16);
    assert.equal(snapshot.view.nodes.find(n => n.name === 'Retrieved data (JSON)').value, 'invalid');
    await assert.rejects(rpc.request({ action: { kind: 2, target: stale, arg0: 0, arg1: 0, text: 'x' } }), /stale/);
    await act(21, 'Task type', 2);await act(1, 'Retrieved data (JSON)');await act(9, 'Retrieved data (JSON)');
    const payload = '["☃",9007199254740993,1.2300e+04,"\\u0000"]';
    await act(2, 'Retrieved data (JSON)', 0, payload);
    const before = await rpc.request({ result: true });
    await assert.rejects(rpc.request({ append: { ...host, private_goal: 'forbidden' } }), /public view fields/);
    await assert.rejects(rpc.request({ append: { ...host, nodes: [node(BASE, 'Collision')], capabilities: [cap(0, 0)] } }), /overlaps/);
    await assert.rejects(rpc.request({ append: { ...host, nodes: Array.from({ length: 128 }, (_, i) => node(i + 1, 'Full')) } }), /capacity/);
    assert.deepEqual(await rpc.request({ result: true }), before);
    const finished = await act(1, 'Finish');assert.equal(finished.phase, 1);
    assert.equal(finished.response_json, `{"task_type":"RETRIEVE","status":"SUCCESS","retrieved_data":${payload},"error_details":null}`);
    assert.deepEqual(await rpc.request({ expire: true }), finished);
    assert.deepEqual(await rpc.request({ blur: true }), finished);
    const reset = await rpc.request({ reset: true });assert.ok(reset.ref_base > finished.ref_base);
    const expired = await rpc.request({ expire: true });assert.equal(expired.phase, 2);assert.equal(expired.response_json, null);
    assert.deepEqual(await rpc.request({ expire: true }), expired);
  } finally { rpc.close(); }
  const invalid = ['{"reset":true,"reset":true}', '{"reset":1}', '{"reset":true,"private":1}',
    '{"action":{"text":"\\u0000"}}', '{"action":{"text":"\\ud800"}}',
    'x'.repeat(1024 * 1024 + 17), '{"reset":true}\0'];
  const p = spawnSync('build/webnav/benchmarks/response_rpc', [], {
    input: ['{"reset":true}', ...invalid, '{"result":true}'].join('\n') + '\n', encoding: 'utf8', timeout: 30000, maxBuffer: 2 * 1024 * 1024 });
  assert.equal(p.status, 0, p.stderr || String(p.error));
  const rows = p.stdout.trim().split('\n').map(JSON.parse);assert.equal(rows.length, invalid.length + 2);
  for (const row of rows.slice(1, -1)) assert.equal(typeof row.error, 'string');
  assert.deepEqual(rows[0], rows.at(-1));
  console.log('PASS: native response RPC, exact JSON, focus handoff, correction, atomic malformed/stale rejection, fresh resets and expiry');
}
main().catch(e => { console.error(e);process.exitCode = 1; });
