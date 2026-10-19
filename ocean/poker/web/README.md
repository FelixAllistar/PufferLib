# Puffer Poker web core

Play the [hosted private table](https://puffer-poker.bouncy-plum-9922.chatgpt.site).

A playable, animated Hold’em table using the same native simulator and bots as
the evaluator. This is a single-player cash-game NLHE simulation: a 200 BB
initial buy-in, rotating blinds, no rake, and actual stacks carried between
hands. Six seats open by default. Settings select two, six or nine seats and
opponent policy. Busted bots rebuy for 200 BB; the human chooses an explicit
200 BB rebuy after busting. Net profit and total buy-ins are separate.

The browser executes stock **Bend 2.0.6** for action permissions, raise clamping,
turn phases and session accounting (`Controller.bend`), cash rules (`Cash.bend`),
and precise hand names (`Hand.bend`). `PROOF.bend` checks twenty-five laws. The stock Bend HTML bundler compiles these definitions into the actual
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

Cards and chips use CSS animations, with reduced-motion support. Distinct fold, check, call, raise, turn and win sounds start after the first
interaction; the Sound button mutes them. Normal table pace is 600 ms per bot
action. Session state is saved in sessionStorage; refresh resumes the same
hand, stacks, net result, buy-ins, settings and recent history. Opening a new
table starts a new session. There are no accounts, multiplayer or real-money
features.

## Table layout and action feedback

The second interface iteration follows the supplied Ignition references: a
screen-filling red table, white stack badges, large face cards, and a betting
panel directly below the human seat. History/session information lives in a
drawer. Tens display as `10`; SB/BB tags and the human position line show posted
blind amounts. Folded players keep an explicit FOLDED status while their cards
slide away. Checks show a tick, wagers animate chips toward the pot, and the
human turn uses a crisp amber border and a distinct two-note cue. No artificial action
timer or countdown is displayed.

Bend calculates each preset as a legal street total. Preflop: 3 BB / 5 BB for
opening, then 3× / 5× the current bet when facing a raise. Postflop: half pot,
three-quarter pot and pot, including the call before sizing. Every button shows
the resulting amount; a click selects the size and Bet/Raise commits it. Input,
slider, minimum, all-in and one-BB adjustment buttons share the same clamp.

Additional verification: 2,880 independent preset calculations, six new sizing
laws (fourteen total), and another 30 browser hands. Browser checks verified
530 displayed preset values across all four streets, 33 visible-ten cases,
audio node starts after interaction, and the history drawer. The native WASM
artifacts remain byte-for-byte identical to the first release. No database,
chat or multiplayer implementation was added.

## Cash-session restoration

The browser uses sessionStorage, scoped to the tab. It survives refresh; closing
the tab normally ends the session, although browser session restoration may
recover it. JavaScript is the storage bridge; Bend controls carried stacks,
empty-seat buy-ins, terminal score receipts and hand names. Writes contain the
current hand's seed, initial stacks, a public command journal, public view,
accounting and recent UI history. They do not contain a raw hidden-card snapshot.
The worker replays the current hand and checks its exact public state. Engine
hashes prevent replay under an incompatible strategy build. A malformed storage
record starts a new session with a notice; an engine mismatch pauses restoration
until the user opens a new table. Unavailable storage leaves the game playable
and shows its status.

Only the current hand is journaled; new hands use an independent seed, bounding
storage growth. The score receipt prevents a restored terminal hand from scoring
twice. The engine is local and inspectable, so storage is not an anti-cheat or
real-money boundary. Online play would require authoritative server state.

Web builds set POKER_WIDE_CHIPS for uint32 chip amounts and uint64 packed public
events. Compact native/trainer builds retain the historical layout and event
format. The supported starting-stack limit for the web build is 100,000 BB per
seat, with 0.01 BB precision; amounts above the limit are rejected rather than
truncated. Current bots are retained baselines and are not calibrated cash-game
experts at arbitrary depths. The optional PR candidate's minraise lookup remains
scoped to its original 200 BB setting.

## Learning panel

The Learn & odds panel shows estimated outright win probability, tie probability,
and equity (expected showdown share). It samples 4,096 complete unknown deals,
without reading opponents' actual cards or future board cards. Opponents are
uniform random unknown hands, not a calibrated prediction of the bots. Values
refer to the labeled last decision; future betting is excluded.

Final hand-category probabilities enumerate all 1,081 board runouts on the flop
and all 46 on the turn. Preflop uses 4,096 sampled runouts. Categories are exclusive
best-five outcomes; a full house does not also count as one pair. Rare sampled
zeroes say Not sampled, while exact zeroes say 0%. Call price uses the eligible
pot, excluding contributions the short stack cannot win. It is a price comparison,
not a complete action-value recommendation.

The exact made-hand name appears above the controls, with the best five cards
outlined. A board-only best hand says Board plays. Starting hands have names
such as Pocket fours and ace–king suited. Opponents show card backs and folded
hands remain hidden; the learning calculation does not use exposed opponent
cards even at a later showdown.

`test_session.mjs` covers exact mid-hand and terminal reload, one-time scoring,
carried winnings, explicit rebuy, separate tabs, corrupt storage and learning
outputs. `test_cash.c` covers deep-stack public events, exact hand/highlights,
royal flush certainty, nine flush outs among 46 turn runouts, and hidden-card/
future-card independence under ASan/UBSan. `test_wasm.mjs` retains 800 exact
native/WASM fixture hands across all four variants.
