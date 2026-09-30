# Travel family source audit

Local tasks are 0 `book-flight-nodelay`, 1 `book-flight`, and 2
`buy-ticket`. The pinned sources are the three matching pages beneath
`build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/`,
plus `html/core/core.js`, `html/common/ui_utils.js`, and
`html/common/special/book-flight/domestic.js`. The family has a stock CPU
Bend model, generator, laws, array ABI, and C ABI-v2 transport. Shared
registry and runner changes belong to the integrating agent.

The flight pages share their episode logic and a 30 s deadline.
`book-flight-nodelay` uses zero autocomplete delay and no datepicker
animation; `book-flight` leaves the UI defaults. The source samples a
full domestic airport name for each end of the route, but may show either
its city or airport code in the instruction. A field is valid only when it
is a full name from `DOMESTIC_FLIGHTS`. Correctness then checks that the
full field contains the shown city/code substring. Generated target dates
are October 1 through December 30, 2016: the source's December 31 end
instant is exclusive. December 31 remains selectable. Blank or invalid
airport fields, or a blank date, leave the form visible with error styling;
each Search clears the previous errors before revalidating. A valid but
wrong route or date displays newly generated fake flights. Buying one
returns −1 because the source leaves its success thresholds at their
sentinels. A correct search displays three or four reset-owned flights.
The lowest price or shortest **exact millisecond** duration wins, including
ties. Success is +1 with time scaling; failures and timeout are −1.

`buy-ticket` has a 10 s deadline and exactly four displayed trips. The
source comment claiming five is stale (`MAX_TRIPS = 4`). Its invisible date
is sampled from January 1 through May 30, 2017. The criterion is cheapest
cost, shortest duration, longest duration, or most expensive cost. Equal
best metrics all receive +1 with time scaling. A middle choice receives
−0.5 and the opposite extreme receives −1, neither time-scaled.

The generated flight preset uses six names from the original domestic
catalog, with city/code variants in the visible instruction. The model
represents these choices as indices 1–6, blank as 0, and invalid text as
7. Dates use 1–92 for October 1–December 31. The Bend generator writes a
separate real and fake flight table and their 3/4 counts at row words 40
and 44. Airport labels are serialized by Bend at row 1024 rather than
hardcoded in the C view. In original-browser instances, a reset-only
fixture imports the sampled full airport names and six-entry choice
catalog. The browser fixture can thus use any pinned-source airport name
that fits the 96-character slot; generated episodes remain the finite
six-name preset.

The compact model compares airport indices exactly. The source's substring
rule can accept a different full airport name sharing the requested city/code.
Generated choices have distinct cities/codes, and the matched-browser preset
uses the sampled correct name and a wrong name excluding that alias. Broader
overlapping airport aliases remain outside this preset.

ABI v2 uses 8192 words per row and four lanes. The flight form fields are
at words 32–34, requested values at 35–37, criterion at 38, phase at 39,
errors at 41, and the fake flag at 42. Flight pairs begin at 64 (real)
and 128 (fake). The public instruction begins at 512. The ticket criterion
and invisible date are at 32–33; its four pairs begin at 64. All task
transitions, terminal guards, ranking, timeout, and reward are in Bend.
C only validates the ABI, packs actions, and projects public observations.
Private targets, fake-result flags, and ticket dates are absent from the
public view. `test_public.c` drives the family using only `WFView`.

The public flight control preset provides atomic `WF_SELECT_OPTION`
choices for the two airport inputs and the date field; source autocomplete
and datepicker pointer/timer gestures are outside this preset. The
instruction, form field values, Search/Back/Book controls, and result
price/duration are compared with the original page. Exact duration comes
from the original DOM's public `data-duration` attribute; the visible
`h/m` text alone can be ambiguous for shortest-flight choices. An AX-only
preset would need to expose that attribute or treat such instances as
ambiguous.

The original-browser differential fixture precommits both result tables
at reset by probing the source page with the same seed, then reseeds and
restores the original initial page before any compared actions. It does
not copy browser state into the model after actions. Its wrong-search
traces use one wrong Search per reset. Repeated wrong Search calls in the
original page regenerate new fake flights; the current bounded family
holds one reset-owned fake table, so repeated wrong-search content is not
claimed as equivalent. The source page also has a reset typo,
`removeClass('.error')`; the fixture starts each matched episode from a
clean error style.

Root's guarded qualification passed 60 original-page episodes (220 independently
advanced actions) and 3,000/3,000 public scripted generated episodes. Native
tests cover four ticket criteria, flight search and ranking, validation recovery,
exact deadlines, terminal absorption, dirty resets and private-goal independence.
These are behavioral checks for the stated control preset; no learned-policy
result or full browser parity is claimed. Exact evidence is in `RESULTS.json`.
