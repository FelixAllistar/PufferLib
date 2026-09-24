# Public DOM contract, version 2

Implementation: dom.h/dom.c and web/dom_snapshot.js. This is a separate contract from the original 128-word/640-feature pilot. No existing pilot checkpoint is reinterpreted as DOM-v2.

An observation holds up to 128 nodes plus a 32-KiB UTF-8 text pool. Each node has an opaque occurrence ref, nearest retained parent ref, role, state flags, name provenance, bounding rectangle and spans for name/value/text. The instruction is a separate span. Counts explicitly report omitted nodes and truncated strings; the C decoder rejects invalid references, malformed types, unsupported roles/flags, nonfinite geometry and text-pool overflow. Parent refs must precede children. Node refs are handles, not semantic numeric features or labels for the correct action.

The extractor prefers visible controls while reserving context capacity, then restores DOM order. It excludes non-rendered hidden elements. Names come from aria-labelledby/aria-label, associated labels, visible control text, then placeholder/title. Custom pointer-style leaf elements can be actionable without a semantic role. The visibility bit means geometric viewport intersection plus basic CSS visibility, not clickability through overlays. Offscreen rendered elements can still be present, with visibility false. The projection does not implement the complete accessible-name algorithm, AX tree, shadow DOM, iframe traversal or all CSS rendering rules.

Text strings are clipped to 512 UTF-8 bytes with an overall exporter budget; truncation is reported. Input values preserve whitespace. The C text pool uses NUL-terminated strings, so embedded NUL text is outside this DOM contract (the independent tokenizer API does support explicit byte lengths). The exporter explicitly rejects NUL text instead of silently normalizing it. Public labels can reference hidden label text through ARIA; this is distinct from exposing private task goals. Initial captured data and raw rewards stay in separate trace fields.

Four extraction profiles are available via `webnavDOM(root, instructionSelector, quality)`:

0. Accessible names and semantic roles where available.
1. Ignore ARIA names/roles; use native controls and labels.
2. Also omit associated-label recovery; keep control text/fallbacks.
3. As 2, with deterministic name dropout on one third of refs.

These alter the observation only, not browser behavior or simulator truth. They are diagnostic degradation profiles, not a calibrated model of real website error rates. Sparse refs are episode/runtime-specific; freeze profiles/seeds when comparing policies.

An optional fourth argument supplies explicit public popup selectors, for example `webnavDOM('#area', '#query', 0, ['ul.ui-autocomplete'])`. It includes rendered popup roots and descendants outside the main task container, deduplicates overlapping scopes, and keeps document order and the existing global bounds. Hidden menus are excluded. The default scope is unchanged. Callers must select only public widget containers, excluding benchmark HUD/reward/private-state elements; this is not automatic accessibility ownership discovery. Popup scope must be recorded with any future training dataset/checkpoint configuration.

The header declares action kinds for wait, click, focus, type, select, key, scroll and back, with an observed target ref and optional text payload. The click-widget lane implements control clicks and explicit timeout waits. A separate bounded ASCII text lane now implements focus-at-end, insertion and editing; see TEXT_TASKS.md below. The generic action vocabulary is not yet connected to training, and generic select/scroll/back policy dispatch remains unimplemented. A separate matched-instance choose-list preset now models native selection; see [task expansion](miniwob/EXPANSION.md).

The text encoder is independent: it consumes public UTF-8 spans and returns token IDs or 256-dimensional frozen vectors. The simulator must not expose private goal fields through embeddings, IDs, masks or candidate payloads. A future RL adapter should store compact text IDs with a versioned shared cache, not duplicate all vectors into every rollout step. Any checkpoint using cache-local IDs must pin the cache file hash in addition to encoder/profile/DOM versions. Trainer integration and enforcement for new checkpoints are not implemented yet.

Tests: `make -f ocean/webnav/Makefile test-dom`. They cover name provenance and degradation, whitespace preservation, hidden-text exclusion, retaining a visible control after 200 context nodes, explicit omission counts, invalid schema versions and duplicate refs.

Text-task follow-up: TABLE/TR nodes are retained as context, preserving TD/TH parent-row relationships. Text insertion/editing is available in a separate bounded ASCII browser preset described in [TEXT_TASKS.md](miniwob/TEXT_TASKS.md). Selection endpoints are currently compared separately by the harness; policy-facing selection fields and training integration remain open.
