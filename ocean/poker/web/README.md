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
and precise hand names (`Hand.bend`). `PROOF.bend` checks thirty-two laws. The stock Bend HTML bundler compiles these definitions into the actual
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
pinned to `d5c1a654d2245e24ccc11df83e327eca5e73093a`. Its provenance and hashes are in
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
interaction; the Sound button mutes them. Normal table pace is 900 ms per bot
action (relaxed 1,300 ms; quick 80 ms). Session state is saved in sessionStorage; refresh resumes the same
hand, stacks, net result, buy-ins, settings and recent history. Opening a new
table starts a new session. There are no accounts, multiplayer or real-money
features.

## Table layout and action feedback

The second interface iteration follows the supplied Ignition references: a
screen-filling red table, white stack badges, large face cards, and a betting
panel directly below the human seat. History/session information lives in a
drawer. Tens display as `10`; SB/BB tags and the human position line show posted
blind amounts. The current presentation consolidates names into stack badges. Folded cards
tuck away and the seat darkens with a crossed marker. Checks double-tap;
wagers move to each seat’s bet, then collect into the pot. Teal borders,
a small size change and a distinct two-note cue identify the acting seat. No artificial action
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
hashes prevent replay under an incompatible strategy build. The rank-cache
update explicitly permits the previous experimental module hash after paired
action/RNG/state checks; restoration still verifies every bot action and the
complete public state before saving under the new hash. A malformed storage
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

The exact made-hand name appears above the controls. Best-five focus is off
on opening/refresh; clicking the hand name lifts the five cards and shades
unused cards, without outlines. A board-only best hand says Board plays. Starting hands have names
such as Pocket fours and ace–king suited. Opponents show card backs and folded
hands remain hidden; the learning calculation does not use exposed opponent
cards even at a later showdown.

`test_session.mjs` covers exact mid-hand and terminal reload, one-time scoring,
carried winnings, explicit rebuy, separate tabs, corrupt storage and learning
outputs. `test_cash.c` covers deep-stack public events, exact hand/highlights,
royal flush certainty, nine flush outs among 46 turn runouts, and hidden-card/
future-card independence under ASan/UBSan. `test_wasm.mjs` retains 800 exact
native/WASM fixture hands across all four variants.

## Game feedback and spatial layout

`table.js` owns the presentation of public state, without changing game rules.
Seat badges use stable stack-centered anchors; face cards and chip amounts live
in separate layers. Bet placement checks visible card, board, pot, badge, action
cue and other-bet rectangles, selecting the nearest clear space. Seat/card DOM
nodes persist across updates so input changes do not restart action animations.
Names sit inside badges, with transient check/call/raise cues beside the actor;
long-lived action bars and the duplicate visual action feed are removed.
Screen readers retain the actor-qualified action feed and seat states.

The acting seat grows slightly with a teal edge; the human hand also lifts.
Checks double-tap, folds tuck cards into the badge and darken/cross its marker,
wagers fly to the seat’s chips, and chips collect when a street finishes. Actual
payout recipients turn gold and receive the pot animation. Payouts use ending
stack minus starting stack plus contributions, excluding unmatched showdown
refunds. A side-pot recipient is shown even when their net result is negative. The result panel separates pot awards
from the human net result. Quick pace intentionally allows simultaneous cues;
normal/relaxed pace lets each action breathe. Card swishes, double knocks,
chip clicks, turn chimes and win sounds pan gently toward the actor. Reduced
motion suppresses flights and size/tap animation while retaining state/color.

`test_feedback.mjs` checks chip/card/badge/board intersections at 360, 390, 1366
and 1920 px across 2/6/9 seats, exact five-card focus without outlines, stable
DOM, payout markers and reduced motion. `test_browser.mjs` retains cash play,
legal sizing, mobile badges and audio acceptance; `test_session.mjs` checks
restoration/rebuy. This presentation revision keeps all four engine WASM hashes
identical to the prior cash-session release, preserving existing tab saves.

## Latest PR candidate trial

The optional two-seat Pro candidate is pinned to PR #5 at `d5c1a654`.
It caches already-visible river hand ranks; other optional PR modes stay off.
The regular two/six/nine-seat modules are byte-identical to V4. A paired run of
2,000 hands against Rules and 2,000 against Fullhouse matched every per-hand
rolling action/RNG/state trace and payoff of the preceding candidate. The
paired strength difference was zero in these deals. The fixed-sample river
equity primitive used about 23% less CPU at the median (1.30× throughput over
five alternating repetitions of 30,000 calls); this is not whole-bot latency.
The default heads-up river policy often takes its separate action-value path.
No live Slumbot comparison was run for this update.

Reproduce after fetching the PR objects, without switching the working branch:

```sh
git fetch origin refs/pull/5/head
python3 ocean/poker/web/test_candidate.py
```

`test_upgrade.mjs --capture` records an actual mid-hand and terminal Pro save
from a separately served predecessor bundle (`POKER_WEB_URL` selects its URL).
After rebuilding, `test_upgrade.mjs` checks those saves against the new bundle:
exact public state, identical accounting, persisted new hash, a fresh hand,
unknown hashes, changed bot journals, and compatibility scope. Capture fixtures
remain in the ignored build tree. The source-bound trial and ASan/UBSan
results are retained in `reports/poker/20261018/pr_try/`.

## Showdown receipts and automatic rounds

Showdown results name each pot winner’s exact made hand, include the leading
kicker when applicable, and show their best five cards in hand order (groups
first, straights descending, with an ace low in a wheel). The human’s own hand
is explicitly labeled. Fold wins say Won without showdown and never expose
folded or unshown opponent cards. The hand history keeps the winner descriptions
and shown winning cards. Unmatched all-in refunds are excluded from pot-winner
markers; genuine side-pot winners remain marked even with a negative net return.

`Hand.bend` evaluates only public cards for presentation and retains the native
packed rank ordering. An independent C oracle matches 12,000 five-, six- and
seven-card sets across all nine categories. The engine and bots are unchanged;
all four WASM hashes remain the same as the previous release. `Cash.bend` owns
auto-deal eligibility, result delays and the refund-adjusted award calculation.
Seven new laws bring the checked total to 32; these do not constitute a full
formal proof of the hand evaluator.

There is no Next hand button. Funded stacks automatically continue after six
seconds at showdown, or three seconds after a fold win. Pause/Resume holds the
receipt, including across refresh. History, Help, Settings and a hidden tab stop
the result clock; returning resumes the remaining reading time. A busted human
must explicitly rebuy, while funded human and bot stacks carry into the new hand.
The dealer disc is 38 px on desktop and 29 px on phones; placement clears cards,
stack badges, board, bets and action cues. Phone spacing leaves room for check
motion as well as the larger disc.

`test_rounds.mjs` checks native winning-five ranks, a losing showdown, automatic
continuation with a real six-second reading pause, stack carryover, held-result
refresh, history/visibility gates, explicit rebuy and fold-win privacy. Its
visibility test uses a controlled document visibilitychange in headless Chromium.
The browser suite accelerates only inter-hand waits with Playwright’s clock;
funded rounds advance through the actual automatic-deal path.

## Casino material pass

`casino.css` supplies a charcoal card room around the red table: generated
wool-baize felt, a padded dark rail with fine brass trim and stitching, warm
ivory cards/badges, and restrained teal/gold action and winner cues. The wider
desktop surface fills the space between seats. Folded seats use charcoal;
check/call and bet/raise controls have separate green and red treatments. The
right learning panel retains its data and gains the same warm neutral palette.
The faint felt logo was removed so it no longer sits behind the human cards.

Only the felt material is a raster asset. The rail, cards, chips, stack badges,
controls and motion remain responsive DOM/CSS. The built-in image_gen tool
produced an art-direction concept and a separate material; exact prompts and
the concept are retained in `reports/poker/20261019/web_v7/`. The material is
encoded as a 287 KiB WebP and bundled locally without a remote image/font
dependency. `build.sh` records asset hashes alongside source hashes. The engine,
bot policy, card privacy and cash-session format are unchanged.
