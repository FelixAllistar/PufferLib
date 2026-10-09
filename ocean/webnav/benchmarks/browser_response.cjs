'use strict';
const { collectPage, executeAction, actionCapability, WF } = require('./browser_view.cjs');
const { lineRpc } = require('./rpc_client.cjs');

// A declared interface preset within WF v2, independent of machine resources.
// The remaining 16 nodes, 4 KiB of text and 16 caps hold native response controls.
const RESPONSE_PRESET = Object.freeze({ node_limit: 112, text_limit: 12288, capability_limit: 1008 });
const RESPONSE_REF_BASE = 0xfff00000;
class BrowserResponse {
  constructor() {
    this.rpc = lineRpc('build/webnav/benchmarks/response_rpc');
    this.state = null;this.view = null;
  }
  get dead() { return this.rpc.dead; }
  async reset() { this.view = null;return this.state = await this.rpc.request({ reset: true }); }
  async collect(page, instruction, options = {}) {
    this.view = null;
    const snapshot = await collectPage(page, instruction, { ...options, ...RESPONSE_PRESET });
    const reply = await this.rpc.request({ append: snapshot.view });
    this.settleMs = Number.isFinite(options.settle_ms) ? Math.max(0, Math.min(1000, options.settle_ms)) : 100;
    this.state = { phase: reply.phase, ref_base: reply.ref_base, response_json: reply.response_json };
    this.view = reply.view;
    return { view: reply.view, page: snapshot.page };
  }
  async execute(page, command) {
    if (!this.view || this.state?.phase !== 0) throw Error('Observe an open response episode before acting');
    actionCapability(this.view, command);
    const { kind, target = 0, arg0 = 0, arg1 = 0, text = '' } = command;
    const domain = target >= RESPONSE_REF_BASE ? 'response' : 'browser';
    this.view = null;
    if (domain === 'response') {
      this.state = await this.rpc.request({ action: { kind, target, arg0, arg1, text } });
      await page.waitForTimeout(this.settleMs);
    } else {
      if (kind !== WF.WAIT) this.state = await this.rpc.request({ blur: true });
      await executeAction(page, command);
    }
    return { domain, ...this.state };
  }
  async expire() { this.view = null;return this.state = await this.rpc.request({ expire: true }); }
  close() { this.view = null;this.rpc.close(); }
}
// Null is an absence marker accepted as failing input by the pinned scorer.
// It is not a FinalAgentResponse or a policy claim. Keep exact submitted bytes.
function responseArtifact(state) {
  if (state?.phase === 1 && typeof state.response_json === 'string') return state.response_json + '\n';
  return 'null\n';
}
module.exports = { BrowserResponse, RESPONSE_PRESET, RESPONSE_REF_BASE, responseArtifact };
