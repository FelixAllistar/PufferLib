# Site data collection and replay

The current tools collect public initial-page DOM projections and record original-MiniWoB action traces. They do not yet reconstruct arbitrary website state machines or train a policy on the captured sites.

```sh
make -f ocean/webnav/Makefile capture-tools
mkdir -p build/webnav/captures
build/webnav/capture_dom https://books.toscrape.com/ > build/webnav/captures/books.json
make -f ocean/webnav/Makefile bench-text
```

`capture_dom` opens a disposable Chromium profile and loads the requested page read-only. It records requested/final URL, title, timestamp, DOM-v2 projection and explicit rights/coverage status. It does not submit forms, log in, buy anything, or infer a license from public access. Keep raw captures local until their reuse terms are established. The initial corpus and all raw data are under ignored build/webnav/.

The first collection produced nonempty snapshots for Books to Scrape (catalog sandbox), Adafruit (electronics commerce), Allbirds (retail), and Project Gutenberg (document catalog). Amazon returned an empty snapshot and was excluded from the text corpus, while retained as a collection failure. This is a small development sample, not representative platform coverage or a test of shopping workflows. Inspect captures for consent/interstitial/error pages before treating them as successful site data. The current aggregator only filters empty/error-URL snapshots; it is not an automatic interstitial classifier.

The initial 268-string text benchmark used the first extraction revision before visible-control prioritization. The newer extractor reserves room for visible controls and emits omission counts, so recaptures can produce a different corpus. Record snapshot/corpus hashes and source version when comparing results. No new semantic accuracy or simulator transfer score has been measured on these pages.

For original-MiniWoB traces:

```sh
make -f ocean/webnav/Makefile miniwob
WEBNAV_RECORD=build/webnav/traces.jsonl build/webnav/miniwob_oracle click-checkboxes 100
```

Each JSONL record includes upstream revision, task, seed, schedule, controller provenance, initial public observation, and transitions with observed target ref, logical elapsed milliseconds, next public observation, termination and raw/timed rewards. These are scripted development trajectories with deliberate mistakes and timeouts. They are not human demonstrations, learned-policy rollouts, or an untouched test set. A consumer reconstructs each state from `initial`, then successive `after.obs` entries. Private Bend goal fields are not serialized into the public observations.

The next collection layer needs read-only navigation/action traces, page graphs, actual event targets and before/after state; a model of search, filters, forms, carts and menus; and replay against counterfactual actions. Mix real-browser experience or recorded demonstrations with simulated training using explicitly appropriate objectives and provenance. A recorded action cannot be relabeled as a fresh on-policy sample simply because it uses the same observation schema.

Remaining collector work: full AX/name algorithms where needed, shadow DOM and frames, popup ownership, hit-testing and occlusion, page readiness beyond readyState, trustworthy interstitial/error classification, larger independent-site/platform coverage, and stable held-out site splits. All are tracked in TODO.md.
