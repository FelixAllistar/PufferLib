# MiniWoB email inbox family

Root validated this family on **2026-09-29**: 12 Bend laws, 641 native cases
and 200 matched original-page episodes passed, with zero instances rejected
for content bounds. See [RESULTS.json](RESULTS.json) for the public scripted
results and exact scope. Full DOM, geometry, generator distribution and
learned-policy coverage remain incomplete.

This family occupies task IDs 0–9 in the shared registry's order. It uses ABI v2,
four CPU lanes of 8192 words each, a 30,000 ms episode clock and a public
`WFView` of at most 128 nodes and 16 KiB of text. All instance generation,
screen transitions, toggles, edits, reward and deadline decisions are in Bend.
The C descriptor validates and packs actions and projects the public view.

| ID | Page | Source goal set | Generated inbox size | Family semantics |
|---:|---|---|---:|---|
| 0 | `email-inbox-delete` | delete | 3 | Sender match, trash |
| 1 | `email-inbox-forward-nl-turk` | forward | 4–11 | Shuffled target; recipient is another inbox sender |
| 2 | `email-inbox-forward-nl` | forward | 3 | Shuffled target; recipient is another inbox sender |
| 3 | `email-inbox-forward` | forward | 3 | Recipient sampled independently of inbox |
| 4 | `email-inbox-important` | important | 3 | Sender match, star |
| 5 | `email-inbox-nl-turk` | reply, forward, delete, important | 4–11 | Shuffled target; forward recipient is another inbox sender |
| 6 | `email-inbox-noscroll` | reply, forward, delete, important | 3 | Same page handlers as inbox, three messages |
| 7 | `email-inbox-reply` | reply | 3 | Sender and exact reply text |
| 8 | `email-inbox-star-reply` | reply, important | 3 | One of the two source goals |
| 9 | `email-inbox` | reply, forward, delete, important | 4–11 | Base mixed inbox |

The `3` count is an original quirk: `generateEmails()` calls `core.randi(4,
MAX_EMAILS)` with `MAX_EMAILS=3`, and `core.randi` is `floor(random*(max-min)+min)`;
thus it returns 3 for a nonzero random draw (an exactly zero draw gives 4,
outside this preset). `MAX_EMAILS=12` produces 4–11. There is no separate
scroll behavior in the source's `email-inbox-noscroll.html`; its only difference
from `email-inbox.html` is the maximum count.

The modeled UI has separate inbox, search, read, reply and forward screens.
Opening search focuses its input. Searching is case-sensitive substring matching
of full sender, subject or body; an all-whitespace query has no results. Clicking
a search-result star or trash bubbles to the thread click and opens the message.
Inbox and read-view star/trash handlers stop bubbling. Inbox stars retain their
toggle; each read view gets a fresh star icon. A read view always closes to the
inbox, including when entered from search. Search text persists until search
cancel clears it but leaves the prior result rows until the next keyup. Reply opens empty. Forward opens with the chosen message body
already filled and a blank recipient. Closing either composer returns to read.

Correct delete/important clicks compare the displayed sender name to the target
sender and finish with raw `+1` and time-scaled reward. Wrong clicks on the
relevant icon finish at raw/timed `-1`; clicking an irrelevant trash icon does
nothing, and an irrelevant star only toggles. Reply Send succeeds only when
sender and exact reply text match. Forward Send checks the recipient and exact
*target body*; the original handler does not check selected sender identity.
Any incorrect Send ends at `-1` without time scaling. The 30-second deadline
ends at `-1`; terminal rows absorb subsequent actions. These rules intentionally
preserve the source's scoring quirks.

Generated sender/order/content/goal use independent salted hashes. Sender names
are an 11-of-16 no-repeat subset of names from `ui_utils.PEOPLE_NAMES`, and
subjects/bodies use a bounded subset of `ui_utils.lorem_words`. That prevents a
fixed row or distractor shortcut, but does **not** reproduce the original
random distribution: original senders may repeat, subjects have 1–2 words,
bodies 5–14 words with occasional internal punctuation, and reply text has
1–4 words. The Bend generator currently emits two-word subjects/replies and
six-word bodies. It emits one canonical instruction per action for the NL and
Turk variants; the originals sample much larger train/test template sets.
The original's duplicate sender names can also make a forward target ambiguous
from the instruction alone; generated names are unique. Those content and template distributions are task-specific parity gaps, not
completed coverage claims. The transition model accepts matched original
instances imported by the browser oracle within its stated bounds.

Transport accepts visible click refs, wait, scroll, printable ASCII insertion,
backspace, delete and select-all. Scroll advances the clock without changing
pixel position. Edits are append-only or replace after select-all; cursor
motion, arbitrary selection, clipboard, highlight markup and pixel scroll
position are outside this bounded model. Left/right/home/end, Enter/Tab,
clipboard copy/paste, option selection and pointer press/release are not
implemented as page actions. The browser oracle scrolls a clicked
DOM target into its container before dispatching a CDP mouse click, and types
through CDP `Input.insertText`; it compares the
resulting source event and UI state, not viewport coordinates. The row supports at most 11 messages,
31-character senders, 39-character subjects, 159-character bodies/replies,
63-character recipients, 127-character search and 1023-character instructions.
Validation rejects larger fields and non-printable or non-ASCII input. Projection returns an
error rather than truncating when `WFView` capacity is exhausted. The browser
oracle counts out-of-bound original instances and exits nonzero; it never treats
them as a passing comparison. The public controller handles the generated
canonical instructions and has no private row access.

`test_email.c` is an independent native oracle based on public query and nodes.
It covers every task's success/failure paths, search, icon bubbling, deadline,
terminal absorption and lane isolation. It checks the 30-second reward formula
against native arithmetic with a float tolerance, while Bend laws cover integer
and branch semantics without relying on F32 proof normalization. `browser.js` extracts original page
instances and DOM state; `browser_oracle.c` imports those instances and compares
visible controls, editable fields, star state, screen, selection and rewards
after success, failure, search and timeout events. The browser oracle preloads
the original CSS icon assets and waits for layout before the initial snapshot;
otherwise icon widths can still be zero. It plans actions from its own initial
page snapshot, advances the
original page and Bend model independently, and never imports page state after
an action. The shared scripted runner supports the family. Reproduce with
`node ocean/webnav/families/build.cjs email --test`,
`make -f ocean/webnav/families/Makefile browser FAMILY=email EPISODES=20`, and
`make -f ocean/webnav/families/Makefile public-check FAMILY=email EPISODES=1000`.
Only root may compile or run validation in this session; family workers remain
source-only.

Source audit: pinned
`build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/`;
all ten `miniwob/email-inbox*.html` files, especially their
`generateEmails`, `showEmail`, `clickEmail`, `displayQuery`, `bindClickEvents`
and `genProblem` functions (roughly lines 203–550); `core/core.js` lines 10–12
and 106–126 for random integer and reward timing; `common/ui_utils.js` lines
13–27 and 86–104 for data and words; and
`common/special/email-inbox-nl/templates.js` for Turk train/test utterances.
