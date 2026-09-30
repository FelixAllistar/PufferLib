# Catalog source audit

Pinned MiniWoB-plusplus revision:
`33c3b4ddef8c6eb67c57a29663d844b1eda7e614`.
The three assigned pages are `phone-book.html`, `order-food.html`, and
`search-engine.html`. The common `core.randi(min,max)` upper bound is exclusive.

`phone-book` has five paginated contacts and a 15-second deadline. Clicking a
phone, email, or address link ends the episode. Correct contact and property
earn +1. Correct contact with wrong property earns +0.4; wrong contact with
correct property earns -0.4; both wrong earn -1. Positive outcomes receive
time scaling. The score compares the selected contact's original index, so
duplicate displayed names can be ambiguous. The original generator samples
five contacts from a shuffled name list with replacement, then chooses an
index and a property. The first page is displayed when the pagination widget
binds. Only the current page number and adjacent Previous/Next arrows are
visible; the public action adapter requires adjacent navigation. The bounded
Bend generator rotates five distinct public names and
varies phone, email, address, target page, and property by independently salted
seed streams. It does not claim full original content distribution.

`order-food` has a fixed twelve-item menu and a 20-second deadline. Each item
has independent quantity, clamped at zero on removal; the source has no upper
quantity cap. The instruction chooses either two distinct named items, each
with quantity one and no extras, or a food type with a total quantity from two
through four across only items with that type. `itemsMatchByName` checks
selected item count, set membership by name, and quantity one. It does not
enforce order. `itemsMatchByType` checks total quantity and every selected
item's type; unlike the name mode, any mix of eligible quantities can pass.
Submit ends the episode with +1 or -1. Public node values expose the original
DOM `data-quantity`, including zero; the rendered quantity text is blank at zero.

`search-engine` has nine results over three pages and a 20-second deadline.
Its instruction gives an exact search string and an ordinal result position.
The search click rebuilds pagination and shows page one. Wrong entered text
creates three fake results on each page, with `data-result=-1`; clicking one
fails. Correct text is case insensitive and shows the real results. Clicking
any result ends the episode based on the original result index, not title
text. Original randomly generated titles can duplicate the target title, so
the ordinal position matters. Input editing, repeated search, page changes,
wrong query, wrong result, and timeout all need comparison.
Changing to a different page reevaluates the current input. Editing the input
alone leaves the previously loaded results and their click-handler query in
place. The active page number is noninteractive in this action preset.

The stock CPU Bend modules now own generation, task state transitions, reward,
and clock for all three tasks. The ABI v2 C adapter projects public contact,
menu, and search nodes and transports actions. It initializes an empty public
text reference before adding any text. The public controller reads only
`WFView`; changing the private target leaves the view byte-for-byte unchanged.
The native oracles cover phone-book's four grades; food's named/type scoring,
overfill, wrong item, zero-clamped removal, and timeout; search's text editing,
wrong query, fake result, wrong position, repeat query, case folding, and
timeout. Dirty-row reset is checked for all three tasks. The browser oracle
imports an original page instance only at reset, then advances original DOM
handlers and Bend independently. Its schedules include partial phone grades,
food failure submissions, fake and wrong search results, repeat searches, and
timeouts; it compares post-action state without writing original state back
into the model.

The bounded phone-book generator uses five distinct names, whereas the
original samples names with replacement. Search result and contact content
come from bounded varied Bend generators, not the original random distribution.
The browser oracle imports the original generated content at episode start for
matched-instance comparison. Fake search results use a fixed bounded table;
the source regenerates fake titles, URLs and descriptions on wrong-query
Search/page changes. Differential checks cover fake result count, indices and
outcomes, but not that regenerated content. Original food quantities have no upper limit;
the ABI retains `U32` quantities and rejects invalid or overflowing transport
inputs rather than silently accepting them. The public projection contains
semantic nodes with normalized geometry. Root's guarded proof/native checks
and 60 original-page episodes (209 independent actions) pass. Browser clicks
use hittable client rectangles of the original controls after scrolling;
wrapped links do not rely on their union bounding-box midpoint. The public
scripted gate and exact source hashes are recorded in `RESULTS.json`.
