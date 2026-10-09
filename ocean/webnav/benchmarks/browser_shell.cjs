'use strict';
const { BrowserResponse, RESPONSE_REF_BASE } = require('./browser_response.cjs');
const { collectPage, actionCapability, WF } = require('./browser_view.cjs');
const { lineRpc } = require('./rpc_client.cjs');

// Explicit WF v2 composition: reserve 24 browser nodes/4 KiB and at least
// 16 response nodes/4 KiB. This preset is independent of machine resources.
const SHELL_DOCUMENT_PRESET = Object.freeze({ node_limit: 88, text_limit: 8192, capability_limit: 984 });
const CONTROL_REF_BASE = 0xffe10000, CONTROL_REF_END = 0xfff00000, MAX_TABS = 16;
const NAVIGATION_TIMEOUT_MS = 2000;
const isControlRef = ref => ref >= CONTROL_REF_BASE && ref < CONTROL_REF_END;

class BrowserShell extends BrowserResponse {
  constructor() {
    super();this.controlsRpc = lineRpc('build/webnav/benchmarks/browser_controls_rpc');
    this.context = null;this.page = null;this.identities = new Map();this.listeners = new Map();
    this.phases = new Map();this.nextIdentity = 1;this.shell = null;
  }
  get dead() { return super.dead || this.controlsRpc.dead; }
  detach() {
    if (this.context && this.onPage) this.context.off('page', this.onPage);
    for (const [page, handlers] of this.listeners) for (const [event, callback] of handlers) page.off(event, callback);
    this.context = null;this.page = null;this.onPage = null;this.view = null;
    this.identities.clear();this.listeners.clear();this.phases.clear();this.nextIdentity = 1;this.shell = null;
  }
  async reset() {
    this.detach();const state = await super.reset();await this.controlsRpc.request({ reset: true });return state;
  }
  register(page) {
    if (this.identities.has(page)) return;
    if (this.nextIdentity >= 0xffffffff) throw Error('Browser tab identity space exhausted');
    this.identities.set(page, this.nextIdentity++);this.phases.set(page, 0);
    const mainNavigation = request => request.isNavigationRequest() && request.frame() === page.mainFrame();
    const handlers = [
      ['request', request => { if (mainNavigation(request)) this.phases.set(page, 1); }],
      ['requestfailed', request => {
        if (mainNavigation(request) && !/ERR_ABORTED/.test(request.failure()?.errorText || '')) this.phases.set(page, 2);
      }],
      ['domcontentloaded', () => { if (this.phases.get(page) !== 2) this.phases.set(page, 0); }],
    ];
    for (const [event, callback] of handlers) page.on(event, callback);
    this.listeners.set(page, handlers);
  }
  attach(page) {
    if (!page || page.isClosed()) throw Error('Attach a live initial browser page');
    this.detach();this.context = page.context();this.page = page;
    this.onPage = opened => this.register(opened);
    this.context.on('page', this.onPage);
    for (const existing of this.context.pages()) this.register(existing);
  }
  async currentPage() {
    if (!this.context) throw Error('Attach a browser context before collecting controls');
    let pages = this.context.pages().filter(page => !page.isClosed());
    if (!pages.length) { const page = await this.context.newPage();this.register(page);pages = [page]; }
    for (const page of pages) this.register(page);
    if (pages.length > MAX_TABS) throw Error('Browser tab count exceeds the declared 16-tab preset');
    if (!this.page || this.page.isClosed()) this.page = pages.at(-1);
    return this.page;
  }
  async navigation() {
    await this.currentPage();
    const pages = this.context.pages().filter(page => !page.isClosed());
    const tabs = [];
    for (const page of pages) {
      const session = await this.context.newCDPSession(page);
      let history;
      try { history = await session.send('Page.getNavigationHistory'); }
      finally { await session.detach(); }
      tabs.push({ identity: this.identities.get(page), title: await page.title() || 'Blank',
        phase: this.phases.get(page), can_back: history.currentIndex > 0,
        can_forward: history.currentIndex + 1 < history.entries.length });
    }
    return { active: this.identities.get(this.page), can_open: pages.length < MAX_TABS && this.nextIdentity < 0xffffffff,
      can_close: pages.length > 1 || this.nextIdentity < 0xffffffff, address: this.page.url(), tabs };
  }
  async collect(_page, instruction, options = {}) {
    this.view = null;this.shell = null;
    const page = await this.currentPage();
    const snapshot = await collectPage(page, instruction, { ...options, ...SHELL_DOCUMENT_PRESET });
    const navigation = await this.navigation();
    const controls = await this.controlsRpc.request({ append: { navigation, view: snapshot.view } });
    // Response controls come last so long answer drafts use only the capacity
    // remaining after browser controls, including their reserved text space.
    const reply = await this.rpc.request({ append: controls.view });
    this.settleMs = Number.isFinite(options.settle_ms) ? Math.max(0, Math.min(1000, options.settle_ms)) : 100;
    this.state = { phase: reply.phase, ref_base: reply.ref_base, response_json: reply.response_json };
    this.shell = { ref_base: controls.ref_base, ...navigation };this.view = reply.view;
    return { view: reply.view, page: snapshot.page, browser: this.shell };
  }
  async execute(_page, action) {
    const page = await this.currentPage();
    if (!this.view || this.state?.phase !== 0) throw Error('Observe an open browser episode before acting');
    actionCapability(this.view, action);
    if (!isControlRef(action.target || 0)) {
      try { return await super.execute(page, action); }
      finally { await this.currentPage(); }
    }
    const { kind, target = 0, arg0 = 0, arg1 = 0, text = '' } = action;
    this.view = null;
    const intent = await this.controlsRpc.request({ action: { kind, target, arg0, arg1, text } });
    this.state = await this.rpc.request({ blur: true });
    const load = { timeout: NAVIGATION_TIMEOUT_MS, waitUntil: 'load' };
    try {
      if (intent.command === 2) await page.goBack(load);
      else if (intent.command === 3) await page.goForward(load);
      else if (intent.command === 4 || intent.command === 5) await page.reload(load);
      else if (intent.command === 6) {
        if (this.context.pages().length >= MAX_TABS) throw Error('Browser tab capacity changed since observation');
        this.page = await this.context.newPage();this.register(this.page);await this.page.bringToFront();
      } else if (intent.command === 7) {
        const chosen = this.context.pages().find(candidate => this.identities.get(candidate) === intent.argument);
        if (!chosen || chosen.isClosed()) throw Error('Selected tab closed since observation');
        this.page = chosen;await chosen.bringToFront();
      } else if (intent.command === 8) {
        if (this.identities.get(page) !== intent.argument) throw Error('Active tab changed since observation');
        await page.close();this.page = null;await this.currentPage();await this.page.bringToFront();
      } else throw Error('Unknown public browser-control command');
      await (await this.currentPage()).waitForTimeout(this.settleMs);
    } finally { await this.currentPage(); }
    return { domain: 'browser-controls', ...this.state };
  }
  close() { this.detach();this.controlsRpc.close();super.close(); }
}
module.exports = { BrowserShell, SHELL_DOCUMENT_PRESET, CONTROL_REF_BASE, CONTROL_REF_END, MAX_TABS,
  NAVIGATION_TIMEOUT_MS, isControlRef, RESPONSE_REF_BASE };
