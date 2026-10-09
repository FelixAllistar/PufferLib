'use strict';

// Bounded public DOM projection for WF ABI v2. This is not a full AX tree.
// No benchmark globals, reference answers, task selectors, or rewards are read.
const { randomBytes } = require('node:crypto');
const WF = Object.freeze({ WAIT: 0, CLICK: 1, INSERT: 2, BACKSPACE: 3,
  DELETE: 4, LEFT: 5, RIGHT: 6, HOME: 7, END: 8, SELECT_ALL: 9,
  ENTER: 10, TAB_KEY: 11, SCROLL: 12, SELECT_OPTION: 21,
  OTHER: 0, BUTTON: 1, CHECKBOX: 2, INPUT: 3, LINK: 4, RADIO: 5,
  SELECT: 6, OPTION: 7, FOLDER: 8, FILE: 9, CELL: 10, TAB: 11,
  PANEL: 12, TEXT: 13, SLIDER: 14, CANVAS: 15, TEXTAREA: 16,
  VISIBLE: 1, ENABLED: 2, CLICKABLE: 4, CHECKED: 8, FOCUSED: 16,
  EXPANDED: 32, READONLY: 64, SELECTED: 128 });
const pages = new WeakMap();
let nextDocumentBase = 1;
const DOCUMENT_REF_RANGE = 0x100000, COLLECTION_ATTEMPTS = 3;
const DEFAULT_DEADLINE_MS = 25600;
const MAX_NODES = 128, TEXT_BYTES = 16384, MAX_CAPABILITIES = 1024;

function reserveDocumentBase() {
  if (nextDocumentBase > 0xffe00000) throw new Error('DOM reference space exhausted');
  const base = nextDocumentBase;
  nextDocumentBase += DOCUMENT_REF_RANGE;
  return base;
}

function navigationContextError(page, error) {
  if (page.isClosed()) return false;
  return /Execution context (?:was |is )?destroyed|Cannot find (?:execution )?context with (?:specified )?id/i
    .test(String(error?.message || ''));
}

function stateFor(page) {
  let state = pages.get(page);
  if (!state) {
    state = { namespace: `__wf_public_${randomBytes(16).toString('hex')}`, view: null };
    pages.set(page, state);
  }
  return state;
}

/** Return the flat JSON wire format consumed by the C WFView/WUCapabilities parser.
 * options.elapsed_ms/deadline_ms are host timing, never browser task state.
 * An omitted deadline defaults to 25600 ms; explicit deadlines are preserved.
 * options.settle_ms controls the post-action wait (default 100, maximum 1000).
 * node_limit/text_limit/capability_limit reserve ABI capacity for composed
 * host controls. Defaults retain the legacy 128 nodes/16 KiB/1024 caps.
 * Refs identify DOM objects, not task IDs; navigation invalidates old refs.
 */
async function collectSnapshot(page, instruction, options = {}, includePage = false) {
  if (typeof instruction !== 'string') throw new TypeError('instruction must be a string');
  for (const key of ['elapsed_ms', 'deadline_ms']) {
    if (options[key] !== undefined && (!Number.isInteger(options[key]) ||
        options[key] < 0 || options[key] > 0xffffffff)) throw new RangeError(key);
  }
  const nodeLimit = options.node_limit ?? MAX_NODES;
  const textLimit = options.text_limit ?? TEXT_BYTES;
  const capabilityLimit = options.capability_limit ?? MAX_CAPABILITIES;
  for (const [name, value, min, max] of [
    ['node_limit', nodeLimit, 1, MAX_NODES],
    ['text_limit', textLimit, 1 + 2 * nodeLimit, TEXT_BYTES],
    ['capability_limit', capabilityLimit, 3, MAX_CAPABILITIES],
  ]) if (!Number.isInteger(value) || value < min || value > max) throw new RangeError(name);
  const state = stateFor(page);
  // A failed reobservation must not leave an older action catalog usable.
  state.view = null;
  const viewport = page.viewportSize();
  if (!viewport || viewport.width !== 1280 || viewport.height !== 720)
    await page.setViewportSize({ width: 1280, height: 720 });
  const project = ({ ns, base, instruction, elapsed, deadline, includePage, nodeLimit, textLimit, capabilityLimit }) => {
    const F = { V: 1, E: 2, C: 4, CHECK: 8, FOCUS: 16, EXPAND: 32, RO: 64, SEL: 128 };
    let state = globalThis[ns];
    const created = !state;
    if (created) {
      if (!Number.isInteger(base) || base <= 0) throw new Error('Invalid document reference base');
      state = { refs: new WeakMap(), elements: new Map(), next: base, limit: base + 0x100000, documentBase: base };
      Object.defineProperty(globalThis, ns, { value: state, configurable: true });
    }
    const ref = node => {
      if (!state.refs.has(node)) {
        if (state.next >= state.limit) throw new Error('Document reference space exhausted');
        const id = state.next++;
        state.refs.set(node, id);
        // Only current represented elements are kept strongly, below.
      }
      return state.refs.get(node);
    };
    const norm = s => String(s || '').replace(/\s+/g, ' ').trim();
    // Companion metadata v1, aligned with primitives/page/page.h. Enabling it
    // retains structural elements without changing the legacy WF roles/ABI.
    const structure = el => {
      if (!includePage) return 0;
      const explicit = el.getAttribute('role')?.split(/\s+/)[0];
      if (explicit === 'none' || explicit === 'presentation') return 0;
      const kind = ({ table: 1, grid: 1, treegrid: 1, row: 2, columnheader: 3,
        rowheader: 4, heading: 5, cell: 6, gridcell: 6 })[explicit];
      if (kind) return kind;
      if (el.tagName === 'TABLE') return 1;
      if (el.tagName === 'TR') return 2;
      if (el.tagName === 'TH') return /^(row|rowgroup)$/.test(el.scope) ||
        (!el.scope && el.parentElement?.parentElement?.tagName !== 'THEAD' && el.cellIndex === 0 && el.parentElement?.rowIndex > 0) ? 4 : 3;
      if (/^H[1-6]$/.test(el.tagName)) return 5;
      return el.tagName === 'TD' ? 6 : 0;
    };
    const role = el => {
      const explicit = ({ button: 1, checkbox: 2, textbox: 3, searchbox: 3,
        link: 4, radio: 5, combobox: 6, listbox: 6, option: 7, treeitem: 8,
        cell: 10, gridcell: 10, columnheader: 10, rowheader: 10, tab: 11,
        tabpanel: 12, region: 12, slider: 14 })[el.getAttribute('role')?.split(/\s+/)[0]];
      if (explicit) return explicit;
      if (el.tagName === 'BUTTON' || el.tagName === 'SUMMARY') return 1;
      if (el.tagName === 'A' && el.hasAttribute('href')) return 4;
      if (el.tagName === 'SELECT') return 6;
      if (el.tagName === 'OPTION') return 7;
      if (el.tagName === 'TEXTAREA') return 16;
      if (el.tagName === 'CANVAS') return 15;
      if (/^(TD|TH)$/.test(el.tagName)) return 10;
      if (el.tagName === 'INPUT') return ({ checkbox: 2, radio: 5,
        button: 1, submit: 1, reset: 1, range: 14, hidden: 0 })[el.type] ?? 3;
      if (el.isContentEditable) return 3;
      const kind = structure(el);
      if (kind) return kind === 5 ? 13 : 12;
      return 0;
    };
    const enabled = el => !el.matches(':disabled') &&
      !el.closest('[inert],[aria-disabled="true"]');
    const shown = (el, b) => {
      const s = getComputedStyle(el);
      return b.width > 0 && b.height > 0 && s.display !== 'none' &&
        s.visibility !== 'hidden' && s.visibility !== 'collapse' &&
        b.bottom > 0 && b.right > 0 && b.top < innerHeight && b.left < innerWidth;
    };
    const scrollable = el => {
      const s = getComputedStyle(el);
      return (/(auto|scroll)/.test(s.overflowX) && el.scrollWidth > el.clientWidth) ||
        (/(auto|scroll)/.test(s.overflowY) && el.scrollHeight > el.clientHeight);
    };
    const clickable = (el, r) => r !== 7 && (r === 1 || r === 2 || r === 3 ||
      r === 4 || r === 5 || r === 6 || r === 11 || r === 14 || r === 15 ||
      r === 16 || el.tabIndex >= 0 || el.hasAttribute('onclick') ||
      getComputedStyle(el).cursor === 'pointer');
    const hitPoint = (el, b, scrolling = false) => {
      const left = Math.max(0, b.left), right = Math.min(innerWidth, b.right);
      const top = Math.max(0, b.top), bottom = Math.min(innerHeight, b.bottom);
      if (right <= left || bottom <= top) return null;
      for (const [u, v] of [[.5, .5], [.1, .1], [.9, .1], [.1, .9], [.9, .9]]) {
        const x = left + (right - left) * u, y = top + (bottom - top) * v;
        let hit = document.elementFromPoint(x, y);
        if (!hit || !(el === hit || el.contains(hit))) continue;
        if (scrolling) {
          while (hit && hit !== el && !scrollable(hit)) hit = hit.parentElement;
          if (hit !== el) continue;
        }
        return { x, y };
      }
      return null;
    };
    const name = (el, r) => {
      const labelled = el.getAttribute('aria-labelledby');
      if (labelled) {
        const s = norm(labelled.split(/\s+/).map(id => document.getElementById(id)?.textContent || '').join(' '));
        if (s) return s;
      }
      if (el.hasAttribute('aria-label')) return norm(el.getAttribute('aria-label'));
      if (el.labels?.length) return norm([...el.labels].map(label => label.textContent).join(' '));
      if (structure(el) === 5) return norm(el.innerText || el.textContent);
      if (structure(el) === 1 && el.tagName === 'TABLE' && el.caption) return norm(el.caption.textContent);
      if ([1, 4, 7, 8, 10, 11].includes(r)) return norm(el.innerText || el.textContent || el.value);
      return norm(el.getAttribute('placeholder') || el.getAttribute('title') ||
        (el.tagName === 'IMG' ? el.getAttribute('alt') : ''));
    };
    let incomplete = 0, omitted = 0;
    const semanticTargets = new Set();
    if (includePage) {
      let scannedRelations = 0;
      for (const el of document.querySelectorAll('[aria-labelledby],[aria-describedby],[headers],label')) {
        if (++scannedRelations > 20000) { omitted++; incomplete = 1; break; }
        if (el.tagName === 'LABEL') semanticTargets.add(el);
        for (const attribute of ['aria-labelledby', 'aria-describedby', 'headers'])
          for (const id of (el.getAttribute(attribute) || '').split(/\s+/).filter(Boolean)) {
            const target = document.getElementById(id); if (target) semanticTargets.add(target);
          }
      }
    }
    const candidates = [], root = document.body;
    const walker = root && document.createTreeWalker(root, NodeFilter.SHOW_ELEMENT | NodeFilter.SHOW_TEXT);
    let scanned = 0;
    if (walker) for (let node = walker.currentNode; node; node = walker.nextNode()) {
      if (++scanned > 20000) { omitted++; incomplete = 1; break; }
      if (node.nodeType === Node.TEXT_NODE) {
        const el = node.parentElement;
        if (!el || /^(SCRIPT|STYLE|NOSCRIPT|OPTION|TEXTAREA)$/.test(el.tagName) ||
            el.closest('button,a,select,[role="button"],[role="link"],[role="option"]')) continue;
        const text = norm(node.nodeValue);
        if (!text) continue;
        const range = document.createRange(); range.selectNodeContents(node);
        const b = range.getBoundingClientRect();
        if (b.width && b.height && getComputedStyle(el).visibility !== 'hidden')
          candidates.push({ node, el, role: 13, b, name: text, value: '', visible: shown(el, b), click: false });
        continue;
      }
      const el = node;
      if (/^(SCRIPT|STYLE|NOSCRIPT)$/.test(el.tagName) || (el.tagName === 'INPUT' && el.type === 'hidden')) continue;
      if (el.shadowRoot || /^(IFRAME|FRAME)$/.test(el.tagName)) incomplete = 1;
      const r = role(el), b = el.getBoundingClientRect(), s = getComputedStyle(el);
      const nativeOption = el.tagName === 'OPTION' && el.closest('select');
      const semanticTarget = semanticTargets.has(el);
      if (!semanticTarget && (s.display === 'none' || s.visibility === 'hidden' || s.visibility === 'collapse')) continue;
      const scroll = scrollable(el);
      if (!(r || semanticTarget || clickable(el, r) || scroll || el.hasAttribute('aria-label') || el.tagName === 'IMG')) continue;
      if ((!b.width || !b.height) && !nativeOption && !semanticTarget) continue;
      candidates.push({ node, el, role: r || (scroll ? 12 : 0), b,
        name: semanticTarget ? norm(el.innerText || el.textContent) : name(el, r), value: 'value' in el ? String(el.value) : '',
        visible: shown(el, b), click: clickable(el, r), scroll });
      if (el.isContentEditable || r === 14 || r === 15 ||
          (el.tagName === 'INPUT' && !/^(text|search|tel|url|password|checkbox|radio|button|submit|reset)$/.test(el.type))) incomplete = 1;
    }
    if (!root) incomplete = 1;
    // No goal-dependent filtering. Reserve context while prioritizing visible controls.
    const priority = c => c.visible && (c.click || c.scroll) ? 0 :
      c.role === 7 && c.el.closest('select') && shown(c.el.closest('select'), c.el.closest('select').getBoundingClientRect()) ? 1 :
      c.visible && c.role === 13 ? 2 : 3;
    const ordered = [...candidates].sort((a, b) => priority(a) - priority(b));
    const chosen = new Set(ordered.filter(c => priority(c) === 0).slice(0, Math.min(96, nodeLimit)));
    for (const c of candidates.filter(c => c.visible && c.role === 13).slice(0, 32))
      if (chosen.size < nodeLimit) chosen.add(c);
    for (const c of ordered) if (chosen.size < nodeLimit) chosen.add(c);
    const kept = candidates.filter(c => chosen.has(c));
    omitted += candidates.length - kept.length;
    if (omitted) incomplete = 1;
    const keepNodes = new Set(kept.map(c => c.node));
    state.elements.clear();
    for (const c of kept) if (c.node.nodeType === Node.ELEMENT_NODE) state.elements.set(ref(c.node), c.node);
    // Reserve every trailing NUL before allocating bytes; never split UTF-8.
    let budget = textLimit - (1 + 2 * kept.length), text_truncated = 0;
    const encoder = new TextEncoder();
    const clip = (s, limit) => {
      s = String(s || ''); let result = '', used = 0;
      if (!s.isWellFormed()) { s = s.toWellFormed(); text_truncated = 1; incomplete = 1; }
      for (const cp of s) {
        if (cp === '\0') { text_truncated = 1; incomplete = 1; continue; }
        const n = encoder.encode(cp).length;
        if (used + n > limit || n > budget) { text_truncated = 1; incomplete = 1; break; }
        result += cp; used += n; budget -= n;
      }
      return result;
    };
    const clippedInstruction = clip(instruction, 2048);
    for (const c of [...kept].sort((a, b) => priority(a) - priority(b))) {
      c.publicName = clip(c.name, 1024); c.publicValue = clip(c.value, 2048);
    }
    const capabilities = [];
    const cap = (kind, ref, wire_target = ref, extra = {}) => {
      if (capabilities.length >= capabilityLimit) { incomplete = 1; return; }
      capabilities.push({ kind, ref, wire_target, flags: 0, text_capacity: 0,
        min0: 0, max0: 0, step0: 1, unit0: 0,
        min1: 0, max1: 0, step1: 1, unit1: 0, ...extra });
    };
    cap(0, 0); cap(11, 0); cap(10, 0);
    const active = document.activeElement;
    const nodes = kept.map(c => {
      const { el, node, b, role: r } = c, id = ref(node);
      let p = node.parentNode;
      while (p && !keepNodes.has(p)) p = p.parentNode;
      const e = enabled(el), readonly = !!el.readOnly || el.getAttribute('aria-readonly') === 'true';
      const focused = active === node;
      const textField = node === el && (el.tagName === 'TEXTAREA' ||
        (el.tagName === 'INPUT' && /^(text|search|tel|url|password)$/.test(el.type)));
      const maxLength = textField && el.maxLength >= 0 ? el.maxLength : null;
      const capacity = textField ? (maxLength === null ? 0xffffffff : Math.min(0xffffffff, maxLength * 4 + 1)) : 0;
      const start = textField ? (el.selectionStart ?? 0) : 0, end = textField ? (el.selectionEnd ?? 0) : 0;
      const flags = (c.visible ? F.V : 0) | (e ? F.E : 0) | (c.click ? F.C : 0) |
        (el.checked || el.getAttribute('aria-checked') === 'true' ? F.CHECK : 0) |
        (focused ? F.FOCUS : 0) | (el.open || el.getAttribute('aria-expanded') === 'true' ? F.EXPAND : 0) |
        (readonly ? F.RO : 0) | (el.selected || el.getAttribute('aria-selected') === 'true' ? F.SEL : 0);
      if (node === el && c.visible && e && c.click) {
        if (hitPoint(el, b)) cap(1, id);
        else incomplete = 1;
      }
      if (textField && c.visible && e && !readonly && focused) {
        const room = maxLength === null ? 255 : Math.max(0, maxLength - el.value.length + end - start);
        if (room) cap(2, id, 0, { flags: 1, text_capacity: Math.min(255, room) });
        for (let k = 3; k <= 10; k++) cap(k, id, 0);
      }
      const style = getComputedStyle(el);
      const maxX = Math.max(0, el.scrollWidth - el.clientWidth), maxY = Math.max(0, el.scrollHeight - el.clientHeight);
      if (node === el && c.visible && e && c.scroll) {
        if (hitPoint(el, b, true)) cap(12, id, id, { flags: 4,
          max0: /(auto|scroll)/.test(style.overflowX) ? Math.floor(maxX) : 0,
          max1: /(auto|scroll)/.test(style.overflowY) ? Math.floor(maxY) : 0, unit0: 2, unit1: 2 });
        else incomplete = 1;
      }
      return { ref: id, parent: p ? ref(p) : 0, role: r, flags,
        name: c.publicName, value: c.publicValue, x: b.x, y: b.y,
        width: b.width, height: b.height, selection_start: start, selection_end: end,
        capacity, scroll_x: node === el ? Math.max(0, el.scrollLeft) : 0,
        scroll_y: node === el ? Math.max(0, el.scrollTop) : 0,
        scroll_max_x: node === el ? maxX : 0, scroll_max_y: node === el ? maxY : 0 };
    });
    for (const c of kept) if (c.el.tagName === 'SELECT') {
      const el = c.el, id = ref(el);
      if (el.multiple) { incomplete = 1; continue; }
      if (!c.visible || !enabled(el)) continue;
      const opts = [...el.options];
      if (opts.some(option => !keepNodes.has(option))) incomplete = 1;
      for (let index = 0; index < opts.length; index++) {
        const option = opts[index];
        if (keepNodes.has(option) && enabled(option)) cap(21, ref(option), id,
          { flags: 4, min0: index, max0: index, unit0: 1 });
      }
      if (opts.length && opts.every(option => keepNodes.has(option) && enabled(option)))
        cap(21, id, id, { flags: 4, max0: opts.length - 1, unit0: 1 });
    }
    const scrolling = document.scrollingElement;
    if (scrolling && (scrolling.scrollWidth > innerWidth || scrolling.scrollHeight > innerHeight)) {
      // A wheel over nested overflow would scroll that container instead.
      let point = null;
      for (const [x, y] of [[5, 5], [innerWidth - 5, 5], [5, innerHeight - 5], [innerWidth - 5, innerHeight - 5], [innerWidth / 2, innerHeight / 2]]) {
        let hit = document.elementFromPoint(x, y);
        while (hit && hit !== scrolling && !scrollable(hit)) hit = hit.parentElement;
        if (!hit || hit === scrolling) { point = { x, y }; break; }
      }
      if (point) cap(12, 0, 0, { flags: 4,
        max0: Math.max(0, Math.floor(scrolling.scrollWidth - innerWidth)),
        max1: Math.max(0, Math.floor(scrolling.scrollHeight - innerHeight)), unit0: 2, unit1: 2 });
      else incomplete = 1;
    }
    // Unsupported ARIA widgets remain observable; they do not gain invented protocols.
    for (const c of kept) if ((c.role === 6 && c.el.tagName !== 'SELECT') ||
      (c.role === 7 && c.el.tagName !== 'OPTION')) incomplete = 1;
    let publicPage;
    if (includePage) {
      let metadataOmitted = 0, metadataTruncated = 0, metadataBudget = 16384 - 3 - kept.length;
      const metadataText = text => {
        let result = '';
        for (const cp of String(text || '').toWellFormed()) {
          const bytes = encoder.encode(cp).length;
          if (cp === '\0') { metadataTruncated = 1; continue; }
          if (bytes > metadataBudget) { metadataTruncated = 1; break; }
          result += cp; metadataBudget -= bytes;
        }
        return result;
      };
      const url = metadataText(location.href), title = metadataText(document.title);
      const positions = new Map(), tables = new Set();
      for (const c of kept) if (/^(TD|TH|TR|TABLE)$/.test(c.el.tagName)) {
        const table = c.el.closest('table'); if (table) tables.add(table);
      }
      // Native table slots include spans and omitted/offscreen preceding rows.
      // Intervals avoid allocating a dense matrix for large DOM span values.
      for (const table of tables) {
        let active = [];
        for (const row of table.rows) {
          const rowIndex = row.rowIndex + 1;
          positions.set(row, { row: rowIndex, column: 0, row_span: 0, column_span: 0 });
          active = active.filter(span => span.last >= rowIndex);
          let column = 1;
          for (const cell of row.cells) {
            const width = cell.colSpan;
            let overlap;
            do {
              overlap = active.find(span => column <= span.end && column + width - 1 >= span.start);
              if (overlap) column = overlap.end + 1;
            } while (overlap);
            const groupRemaining = Math.max(1, row.parentElement.rows.length - row.sectionRowIndex);
            const height = Math.min(cell.rowSpan || groupRemaining, groupRemaining);
            positions.set(cell, { row: rowIndex, column, row_span: height, column_span: width });
            if (height > 1) active.push({ start: column, end: column + width - 1, last: rowIndex + height - 1 });
            column += width;
          }
        }
      }
      const positive = (el, name) => {
        const value = Number(el.getAttribute(name));
        return Number.isInteger(value) && value > 0 && value <= 0xffffffff ? value : 0;
      };
      const relations = [], relationKeys = new Set();
      const relation = (source, target, kind) => {
        if (!target || !keepNodes.has(target)) { metadataOmitted++; return; }
        if (source === target) { metadataOmitted++; return; }
        const item = { source: ref(source), target: ref(target), kind };
        const key = `${item.source}:${item.target}:${kind}`;
        if (relationKeys.has(key)) return;
        if (relations.length >= 512) { metadataOmitted++; return; }
        relationKeys.add(key); relations.push(item);
      };
      const ids = (el, attribute, kind) => {
        for (const id of (el.getAttribute(attribute) || '').split(/\s+/).filter(Boolean))
          relation(el, document.getElementById(id), kind);
      };
      const metadataNodes = kept.map(c => {
        const { node, el } = c, element = node === el, kind = element ? structure(el) : 0;
        let position = element && positions.get(el);
        if (!position) position = { row: kind === 2 ? positive(el, 'aria-rowindex') : kind >= 3 && kind !== 5 ?
          positive(el, 'aria-rowindex') || positive(el.closest('[role="row"]') || el, 'aria-rowindex') : 0,
          column: kind >= 3 && kind !== 5 ? positive(el, 'aria-colindex') : 0,
          row_span: 0, column_span: 0 };
        if (kind >= 3 && kind !== 5 && !positions.has(el)) {
          position.row_span = position.row ? positive(el, 'aria-rowspan') || 1 : 0;
          position.column_span = position.column ? positive(el, 'aria-colspan') || 1 : 0;
          if (!position.row || !position.column) metadataOmitted++;
        }
        let heading = kind === 5 ? positive(el, 'aria-level') || Number(el.tagName[1]) || 0 : 0;
        if (kind === 5 && (!heading || heading > 6)) { metadataOmitted++; heading = 0; }
        if (element) {
          ids(el, 'aria-labelledby', 1); ids(el, 'aria-describedby', 2);
          for (const label of el.labels || []) relation(el, label, 1);
          if (el.hasAttribute('headers')) ids(el, 'headers', 3);
          else if (/^(TD|TH)$/.test(el.tagName)) {
            const table = el.closest('table'), cellPosition = positions.get(el);
            for (const header of table?.querySelectorAll('th') || []) {
              if (header === el || header.closest('table') !== table) continue;
              const hp = positions.get(header); if (!hp || !cellPosition) continue;
              const headerKind = structure(header);
              const columnOverlap = hp.column < cellPosition.column + cellPosition.column_span &&
                cellPosition.column < hp.column + hp.column_span;
              if (header.scope === 'colgroup') { metadataOmitted++; continue; }
              if (!header.scope && headerKind === 3 && hp.row > 1 && header.parentElement.parentElement.tagName !== 'THEAD') {
                metadataOmitted++; continue;
              }
              const matches = header.scope === 'rowgroup' ? header.parentElement.parentElement === el.parentElement.parentElement :
                headerKind === 4 ? hp.row === cellPosition.row && hp.column < cellPosition.column :
                headerKind === 3 && hp.row < cellPosition.row && columnOverlap;
              if (matches) relation(el, header, 3);
            }
          }
        }
        return { ref: ref(node), kind: kind === 5 && !heading ? 0 : kind, heading_level: heading,
          ...position, href: metadataText(element && el.tagName === 'A' && el.hasAttribute('href') ? el.href : '') };
      });
      if (metadataOmitted || metadataTruncated) incomplete = 1;
      publicPage = { version: 1, url, title, omitted: metadataOmitted,
        text_truncated: metadataTruncated, nodes: metadataNodes, relations };
    }
    return { created, documentBase: state.documentBase, page: publicPage, view: { version: 2, instruction: clippedInstruction, elapsed_ms: elapsed,
      deadline_ms: deadline, omitted, text_truncated, nodes, capabilities, incomplete } };
  };
  for (let attempt = 0; attempt < COLLECTION_ATTEMPTS; attempt++) {
    state.view = null;
    // Reserve before sending the single evaluation: navigation cannot cause a
    // namespace to be initialized with zero or another document's references.
    const base = reserveDocumentBase();
    let result;
    try {
      result = await page.evaluate(project, { ns: state.namespace, base, instruction,
        includePage, nodeLimit, textLimit, capabilityLimit,
        elapsed: options.elapsed_ms || 0,
        deadline: options.deadline_ms === undefined ? DEFAULT_DEADLINE_MS : options.deadline_ms });
    } catch (error) {
      // The callback may have installed its namespace before its context died.
      // An ambiguous failure always consumes its reservation, even on retry.
      if (attempt + 1 >= COLLECTION_ATTEMPTS || !navigationContextError(page, error)) throw error;
      await page.waitForTimeout(50);
      continue;
    }
    if (!result || typeof result.created !== 'boolean' ||
        !Number.isInteger(result.documentBase) || result.documentBase <= 0 ||
        result.documentBase > 0xffffffff || !result.view || result.view.version !== 2 ||
        (result.created && result.documentBase !== base))
      throw new TypeError('Invalid public projection result');
    // Reclaim only an explicitly unused reservation still at the allocator's
    // tail. Another concurrent call may have reserved or used a later range.
    if (result.created === false && nextDocumentBase === base + DOCUMENT_REF_RANGE)
      nextDocumentBase = base;
    state.view = result.view;
    state.documentBase = result.documentBase;
    state.settle_ms = Number.isFinite(options.settle_ms) ? Math.max(0, Math.min(1000, options.settle_ms)) : 100;
    return { view: result.view, ...(includePage ? { page: result.page } : {}) };
  }
}

async function collectView(page, instruction, options = {}) {
  return (await collectSnapshot(page, instruction, options, false)).view;
}
async function collectPage(page, instruction, options = {}) {
  return collectSnapshot(page, instruction, options, true);
}

/** Execute only a capability from the latest snapshot using trusted input.
 * INSERT and edit keys address the already focused field (wire target zero).
 * SCROLL args are absolute pixel offsets; native select args are original indices.
 * Resolving elements uses evaluate; it never mutates DOM values or dispatches events.
 */
function actionCapability(view, command) {
  const { kind, target = 0, arg0 = 0, arg1 = 0, text = '' } = command || {};
  if (![kind, target, arg0, arg1].every(n => Number.isInteger(n) && n >= 0 && n <= 0xffffffff) ||
      typeof text !== 'string' || text.includes('\0')) throw new TypeError('Invalid WFAction');
  const capability = view.capabilities.find(c => c.kind === kind && c.wire_target === target &&
    arg0 >= c.min0 && arg0 <= c.max0 && arg1 >= c.min1 && arg1 <= c.max1 &&
    (arg0 - c.min0) % (c.step0 || 1) === 0 && (arg1 - c.min1) % (c.step1 || 1) === 0 &&
    (!(c.flags & 1) || (Buffer.byteLength(text, 'utf8') > 0 && Buffer.byteLength(text, 'utf8') <= c.text_capacity)));
  if (!capability) throw new RangeError('Action is absent from the public capability catalog');
  return capability;
}
async function executeAction(page, command) {
  const state = pages.get(page);
  if (!state?.view) throw new Error('collectView must precede executeAction');
  const capability = actionCapability(state.view, command);
  const { kind, target = 0, arg0 = 0, arg1 = 0, text = '' } = command;
  const ns = state.namespace;
  if (!await page.evaluate(({ns,documentBase}) => globalThis[ns]?.documentBase === documentBase,
      {ns,documentBase:state.documentBase})) throw new Error('Snapshot belongs to a previous document');
  const focusedEdit = kind >= WF.INSERT && kind <= WF.SELECT_ALL;
  if (focusedEdit && !await page.evaluate(({ ns, ref }) =>
    globalThis[ns]?.refs.get(document.activeElement) === ref,
  { ns, ref: capability.ref })) throw new Error('Focused field changed since snapshot');
  let handle;
  try {
    if (target) {
      handle = await page.evaluateHandle(({ ns, target }) => {
        const el = globalThis[ns]?.elements.get(target);
        return el?.isConnected ? el : null;
      }, { ns, target });
      if (!handle.asElement()) throw new Error('Target is stale or detached');
    }
    if (kind === WF.CLICK) {
      await handle.asElement().click({ timeout: 2000 });
    } else if (kind === WF.INSERT) {
      await page.keyboard.insertText(text);
    } else if (kind === WF.SELECT_OPTION) {
      const info = await handle.evaluate((el, index) => {
        if (el.tagName !== 'SELECT' || el.multiple || el.disabled) return null;
        const enabled = [...el.options].map((option, i) => ({ option, i }))
          .filter(({ option }) => !option.disabled && !option.closest('optgroup[disabled]'));
        const rank = enabled.findIndex(({ i }) => i === index);
        return rank < 0 ? null : { rank, selected: el.selectedIndex };
      }, arg0);
      if (!info) throw new Error('Select option is no longer enabled');
      await handle.asElement().click({ timeout: 2000 });
      await page.keyboard.press('Home');
      for (let i = 0; i < info.rank; i++) await page.keyboard.press('ArrowDown');
      await page.keyboard.press('Enter');
      if (await handle.evaluate(el=>el.selectedIndex) !== arg0)
        throw new Error('Native select did not commit the requested option');
    } else if (kind === WF.SCROLL) {
      const point = await page.evaluate(({ ns, target, x, y }) => {
        const el = target ? globalThis[ns]?.elements.get(target) : document.scrollingElement;
        if (!el?.isConnected) return null;
        const b = target ? el.getBoundingClientRect() : { left: 0, right: innerWidth, top: 0, bottom: innerHeight };
        const scrollable = e => {
          const s = getComputedStyle(e);
          return (/(auto|scroll)/.test(s.overflowX) && e.scrollWidth > e.clientWidth) ||
            (/(auto|scroll)/.test(s.overflowY) && e.scrollHeight > e.clientHeight);
        };
        const l = Math.max(0, b.left), r = Math.min(innerWidth, b.right), t = Math.max(0, b.top), d = Math.min(innerHeight, b.bottom);
        const points = target ? [[.5, .5], [.1, .1], [.9, .1], [.1, .9], [.9, .9]]
          .map(([u, v]) => [l + (r - l) * u, t + (d - t) * v]) :
          [[5, 5], [innerWidth - 5, 5], [5, innerHeight - 5], [innerWidth - 5, innerHeight - 5], [innerWidth / 2, innerHeight / 2]];
        for (const [px, py] of points) {
          let hit = document.elementFromPoint(px, py);
          if (target && (!hit || !(el === hit || el.contains(hit)))) continue;
          while (hit && hit !== el && !scrollable(hit)) hit = hit.parentElement;
          if (hit !== el && (target || hit)) continue;
          return { x: px, y: py, dx: x - el.scrollLeft, dy: y - el.scrollTop };
        }
        return null;
      }, { ns, target, x: arg0, y: arg1 });
      if (!point) throw new Error('No public wheel target remains visible');
      await page.mouse.move(point.x, point.y);
      await page.mouse.wheel(point.dx, point.dy);
    } else if (kind !== WF.WAIT) {
      const keys = { 3: 'Backspace', 4: 'Delete', 5: 'ArrowLeft', 6: 'ArrowRight',
        7: 'Home', 8: 'End', 9: 'ControlOrMeta+A', 10: 'Enter', 11: 'Tab' };
      if (!keys[kind]) throw new RangeError('Unsupported browser action');
      await page.keyboard.press(keys[kind]);
    }
    await page.waitForTimeout(state.settle_ms);
    return { ok: true };
  } finally {
    if (handle) await handle.dispose();
    // Every action can change focus, navigation, DOM, or bounds. Reobserve first.
    state.view = null;
  }
}

module.exports = { collectView, collectPage, executeAction, actionCapability, WF, MAX_NODES, TEXT_BYTES, MAX_CAPABILITIES };
