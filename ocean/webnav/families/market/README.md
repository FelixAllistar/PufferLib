# Stock market source audit

Local task 0 is `stock-market`. The pinned source is
`build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/stock-market.html`,
with `html/core/core.js`, D3 v3 and `html/common/shapes.js`. The first
two-price compiler increment and expanded 100-price implementation passed
root's guarded checks. Qualification includes six Bend laws, independent
native tests, 20 original-page episodes and 1,000/1,000 public scripted solves.
Exact commands and source fingerprints are in [RESULTS.json](RESULTS.json).
These checks do not include RL training.

The page uses core’s default 10,000 ms deadline. The source generates 100
prices at reset. Its initial price is an integer from $40 through $59;
each later point adds one of −1.5 through +1.5 dollars in $0.10 steps,
rounds to two decimals, then clamps to $0.01. `setInterval` draws index
zero at 100 ms and one new point every 100 ms. Before the first tick a
fresh page has no displayed price. The threshold is price index 75 and is
printed in the instruction. The instruction says “less than,” but the Buy
handler accepts `currentPrice <= thresholdPrice`: an exact tie succeeds.
Buying a blank price fails because its parsed value is `NaN`. Success is
+1 with time scaling; a wrong purchase or timeout is −1 without scaling.

The original `genProblem` does not clear `#stock-price`, so an in-page
reset after a previous episode retains the last display until the first
new tick. This family uses a **clean-page reset preset**: Bend reset starts
with no visible price, and the browser fixture clears the original page’s
display before calling `core.startEpisodeReal()`. It does not claim the
retained-display path. The original stock interval also continues drawing
after a purchase until the next source reset; the family’s terminal state
absorbs subsequent actions, and browser traces end when reward terminates.

The source’s `displayPrice` appends `0` after any noninteger JavaScript
number. A one-decimal number displays conventionally (`$45.10`), but a
two-decimal number such as `$0.01` displays `$0.010`; `parseFloat` still
recovers the same value. Bend stores prices in integer cents and the C
public projection preserves that display spelling. Bend’s seeded preset
generates a varying three-letter symbol and a 100-point price walk. The
reset-owned threshold equals the 76th displayed point, so a public-only
controller can always wait until that point or buy earlier at a qualifying
price. The controller reads only the instruction and currently displayed
price, and sends Buy at the same elapsed timestamp as its observation.

ABI v2 uses 8192 words per row, four lanes. The current drawn tick and
price are at 32–33, threshold at 34, count at 35, all 100 reset-owned
prices at 64–163, instruction at 512, and stock symbol at 1000. Future
prices are absent from `WFView`; the displayed price and threshold wording
are public. Bend owns price generation, clock advancement, purchase
comparison, deadline and reward. C validates/serializes the transport and
projects the public view; it does not simulate market transitions.

For matched original-browser instances, the fixture captures the original
price array and symbol at reset, then uses the page’s own ticker and Buy
handler on a controlled 100 ms clock. The model is not overwritten with
browser data after actions. At the exact 10 s deadline, the fixture runs
ticks through 9,900 ms and the source timeout; the model also advances to
the last predeadline tick before marking timeout. Prices drawn after the
episode has already ended are outside the terminal observation preset.
