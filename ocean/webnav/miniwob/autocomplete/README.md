# `use-autocomplete-nodelay`

This lane models the pinned MiniWoB task at
`build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/use-autocomplete-nodelay.html`.
The source list and jQuery UI behavior were audited from that checkout. The
model keeps the text field and the autocomplete popup as separate state: the
popup is appended below `document.body`, so an observation rooted only at
`#area` does not include it. A browser observation must add
`ul.ui-autocomplete` as an external popup root and deduplicate it with the
ordinary DOM roots.

The model follows the source's two different predicates. Suggestions are
case-insensitive prefix matches in the original `ui_utils.COUNTRIES` order.
Submission uses JavaScript's case-sensitive `startsWith` and `endsWith`; it
does not require the entered value to be a country. With `match_end == false`,
any printable value with the required prefix succeeds, including a value with
no suggestion. The menu retains its original search term while a highlighted
item temporarily replaces the field value. The first Up/Down on a closed
widget opens a menu without highlighting an item; crossing an open menu's
boundary restores the search term, and Enter selects only an active item.
Button submission closes the popup as the input blur handler does.

## Bounded CPU contract

Each lane is 256 `U32` words and has tag `10` at word zero. The transport is a
private simulator row, not the DOM observation ABI.

| Words | Meaning |
| ---: | --- |
| 0..10 | tag, field count, focus, outcome, correct, elapsed ms, saved elapsed ms, command, command argument, raw reward bits, timed reward bits |
| 11..21 | insertion length, `match_end`, prefix/suffix lengths, menu kind, value length, selection start/end, retained term length, visible count, active item (one-based) |
| 32..95 | field value units |
| 96..159 | retained autocomplete search term units |
| 160..191 | prefix goal units |
| 192..223 | suffix goal units |
| 224..255 | insertion payload units |

Commands are `0` Wait, `1` focus/end, `2` insert payload, `3` Backspace,
`4` Delete, `5` Left, `6` Right, `7` Home, `8` End, `9` SelectAll,
`10` Submit, `11` Timeout, `12` menu Down, `13` menu Up, `14` select a
zero-based menu item, and `15` accept the active menu item. Focus `0` ignores
field and menu commands; focus `1` is the input; outcome states are terminal.
Elapsed time is monotonic and the deadline is 10,000 ms. Raw rewards
are `+1` for a correct submission and `-1` for an incorrect submission or
timeout. The timed reward is `max(0, 1 - elapsed / 10000)` for a correct
submission and `-1` otherwise.

The validator accepts printable ASCII only, a field and retained term up to 64
units, insertion payloads up to 32 units, two-to-five-unit prefix and suffix
goals, and the 232-entry bounded country source (the validator's 249 limit is
an ABI capacity bound). Unicode, IME composition,
clipboard replacement, arbitrary mouse coordinates, and unbounded caret
operations require a new wire version. `MenuSelect` is the bounded semantic
equivalent of clicking a visible suggestion.

## Checks

From the repository root:

```sh
BEND_NO_TELEMETRY=1 /home/felix/.bend/bin/bend ocean/webnav/miniwob/autocomplete/PROOF.bend
BEND_NO_TELEMETRY=1 /home/felix/.bend/bin/bend ocean/webnav/miniwob/autocomplete/Main.bend \
  -o build/webnav/autocomplete/autocomplete_generated.c
clang-19 -O3 -std=c11 \
  -I ocean/webnav -I ocean/webnav/miniwob/autocomplete -I build/webnav \
  ocean/webnav/miniwob/autocomplete/test_autocomplete.c \
  ocean/webnav/miniwob/autocomplete/test_bridge.c -lpthread -lm \
  -o build/webnav/autocomplete/test_autocomplete
timeout -s KILL 60s ./build/webnav/autocomplete/test_autocomplete
```

The native test covers source ordering and case folding, non-country prefix
success, exact suffix failure, menu open/highlight/boundary/selection behavior,
focus gating, selection offsets, deadline and terminal absorption, validator
mutations, rewards, and all 32 lanes. The repository's browser oracle adds
real CDP insertion, key, outside-blur, button, and menu-click actions. It waits
for the zero-delay callback to settle under a controlled clock before comparing
field, caret, focus, popup, retained term, and reward state.
