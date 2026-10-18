# Puffer Poker web core

Play the [hosted private table](https://puffer-poker.bouncy-plum-9922.chatgpt.site).

A playable, animated Hold’em table using the same native simulator and bots as
the evaluator. This is single-player practice with independent 200 BB hands,
rotating blinds, no rake, and cumulative session results. Six seats open by
default. Settings select two, six or nine seats and opponent policy.

The browser executes stock **Bend 2.0.6** for action permissions, raise clamping,
turn phases and session accounting (`Controller.bend`). `PROOF.bend` checks eight
laws. The stock Bend HTML bundler compiles these definitions into the actual
browser bundle; they are not a second specification beside handwritten logic.
JavaScript handles DOM, sound, animation and worker transport. The existing C
engine and bots run as WebAssembly in a separate worker. Porting engine state
transitions and policy inference to Bend remains future work.

`bridge.c` independently rejects illegal actions and projects public state.
Only the human's cards and live showdown hands leave the engine. Unrevealed
boards and folded opponents' holdings remain hidden. Browser-hosted code can be
inspected by its owner; future multiplayer needs authoritative server state.

Rules and Range support all table sizes. Fullhouse supports two or six seats;
nine-seat house mixes use Rules/Range. The optional heads-up River candidate
vendors four source files from [PR #5](https://github.com/FelixAllistar/PufferLib/pull/5),
pinned to `59e06c0f92aa6f62909522d5d8b24eaba5ad8cfb`. Its provenance and hashes are in
`experimental/provenance.json`. Only the standard sampled action-value mode is
included; exact/calibrated/CFR/clone modes remain disabled. The PR stays unmerged.
Later candidates can replace this isolated variant without changing the UI or
standard libraries. No live Slumbot requests occur during play.

## Build and play

From the repository root, install Emscripten and the stock Bend CPU toolchain.
Export the pinned Fullhouse weights following `../README.md`, then:

```sh
EMCC="$HOME/emsdk/upstream/emscripten/emcc" bash ocean/poker/web/build.sh
python3 -m http.server 8767 --directory build/poker/web/dist
```

Open `http://localhost:8767`. Serve over HTTP; opening `index.html` with `file://`
does not load module workers. No application server is needed for this version.
The build is about 2 MB, mostly model weights across three WASM modules.
`build-manifest.json` binds source files, model weights and emitted artifacts.
Fullhouse's MIT notice is shipped alongside the game. Generated bundles remain
in the ignored build tree; source stays here.

The build serializes Bend compilation in the existing cgroup/lock guard (2 GiB,
no swap) and refuses an unconfined fallback. It does not rebuild trainer targets.

## Validation

```sh
node ocean/poker/web/test_wasm.mjs
# Playwright is needed only for browser acceptance checks:
PLAYWRIGHT_MODULE=/absolute/path/to/playwright/index.mjs \
  node ocean/poker/web/test_browser.mjs
```

Run the Bend controller test under the same cgroup guard, preloading the stock
`bend2/main.ts` loader: `bun --preload "$BEND_HOME/current/bend2/main.ts"
ocean/poker/web/test_controller.mjs`. The build's proof step uses `bend_cpu.sh`.

The initial checks covered exact native/WASM actions and state on 800 hands,
chip conservation, private-state projection, invalid action rejection, 5,204
Bend control boundaries, and 30 completed browser hands across six configurations.
Browser checks cover raise/call/fold controls, session accounting, chip totals,
mobile seat-label overlap, dialogs and console errors. Desktop and 390-pixel
phone screenshots are emitted under `build/poker/web/`.

Cards and chips use CSS animations, with reduced-motion support. Sound starts
only after an explicit user click. Session state lasts for the current table;
reloading or opening a new table starts a new session. This initial core has no
accounts, multiplayer, persistent balances or real-money features.
