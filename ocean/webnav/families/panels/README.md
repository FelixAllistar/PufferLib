# Panel family

## 5.0 conversion qualification, 2026-09-24

The family implementation is preserved from `5c`, without trainer changes.
The memory-capped build passes all 14 laws and 110,592 independent transition
checks. Fresh browser comparisons pass 20 episodes per task (180 total) against
the pinned MiniWoB revision below. The public-only scripted runner succeeds in
1,000 generated episodes per task (9,000 total). These checks retain the
settled-animation and bounded-generator limitations described below; they are
not learned PPO results. `RESULTS.json` preserves the earlier historical audit.

Install the original browser pages with `bash ocean/webnav/setup_miniwob.sh`:
revision `33c3b4ddef8c6eb67c57a29663d844b1eda7e614`, archive SHA-256
`dffc87414e5081b282fc54aefad7c3f4406f3a10dcb5923dfd9babdcf9aa60e9`.
Use `WEBNAV_CHROME` for an existing Chrome for Testing executable, or install
the pinned browser with `bash ocean/webnav/setup_browser.sh`.

Nine MiniWoB tasks share CPU Bend navigation, visibility and reward behavior:
`click-tab`, `click-tab-2`, `click-tab-2-easy`, `click-tab-2-medium`,
`click-tab-2-hard`, `click-collapsible`, `click-collapsible-nodelay`,
`click-collapsible-2`, `click-collapsible-2-nodelay`.

`Model.bend` defines transitions. `Generate.bend` creates small independent
instances. `Wire.bend` packs owned arrays; C only transports actions and public
observations. `LAWS.bend`/`PROOF.bend` check terminal absorption, timeout priority,
hidden-link identity, source goal decisions, panel toggling and submit behavior.
The shared runtime builds independently from the old twelve-task PPO profile.

```sh
make -f ocean/webnav/families/Makefile test FAMILY=panels EPISODES=1000
make -f ocean/webnav/families/Makefile browser FAMILY=panels EPISODES=20
```

See [RESULTS.json](RESULTS.json) for exact results and source fingerprints.
Behavior comparison uses pinned original generators, real CDP clicks and a
logical clock. Animation queues are explicitly settled before observations.
The delayed variants retain their identity and metadata, but animation-in-flight
geometry and real scheduler timing are not modeled. This is not full parity.

Important source distinctions: easy has one tab; hard has 2..6 and a 20-second
deadline; medium removes second-panel links and makes the second tab a success
only when the requested text is absent from the first. The simple accordion's
Submit paragraph becomes a second jQuery header: submit scores the current
visible content, then bubbling activates that extra header. This ordering is
preserved in the model and browser comparison.

Transport v2 uses 4 lanes of 8192 U32s, a 32-word common header, panel metadata
at 32..39, up to 128 nodes (48 words each) at 64, and public instruction text at
6208..6463. Node words are kind, panel, private target flag, reserved, then up
to 39 ASCII characters and terminator. Target flags and fallback answers never
enter WFView. Hidden links are omitted and action packing rejects them.

Generators use two uniquely labeled links per panel, bounded ASCII labels and
ordinal positions. Browser-imported instances include the original labels and
numbers of links within capacity. Original distractor paragraphs, complete
geometry/AX output and source-distribution generation remain open. The public
scripted controller is a solvability diagnostic, not RL training.
