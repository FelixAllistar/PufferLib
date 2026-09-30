# Autocomplete family

Task ID 0 is `use-autocomplete-nodelay`; ID 1 is `use-autocomplete`, with
the original 300 ms search debounce. Both use the ABI-v2 stock CPU Bend loader.
This library reuses the preserved autocomplete/text models and their 14+12 laws;
`Body.bend` adapts the old transport's command decoding to Nat. The old combined
dispatcher is no longer required for this task.

The pinned original page uses the 249-entry country source list, case
insensitive prefix filtering, source ordering, and jQuery menu navigation.
Submission checks the requested case-sensitive prefix and, when requested,
suffix. It does not require that the submitted string be a country; this
source quirk is retained and tested. The deadline is ten seconds. Menu
filtering, selection, text editing, generation and rewards all execute in Bend.
C projects the menu labels already serialized by Bend, rather than calculating
the suggestion list itself. `Delayed.bend` preserves the old menu until the
debounce fires, replaces pending searches on edits, and implements synchronous
arrow-key search. A pending callback can reopen a menu after selection, matching
jQuery UI 1.12.1. Submit cancels the callback; episode timeout preempts new edits.
Four additional laws cover debounce scheduling, boundaries and absorption.

Rows are 2048 U32 words, four lanes: common header, old body at 32, public
instruction at 320, and up to 64 packed menu labels at 512. Input is bounded
ASCII up to 64 characters, with at most 32 characters per insertion. More than
64 suggestions reports omitted nodes. The generator uses the original country
pool, prefix/suffix lengths 2–4 and a 75% suffix constraint, with an independent
deterministic RNG. Exact source RNG parity is not claimed.
Delayed state uses words 288–290 for due time, modifiers and last-search length,
and 1536–1599 for the last search value. These fields are private implementation
state, excluded from the public view.

The fresh guarded build passes the preserved independent editing/menu oracle
and 384 generated public-only menu solves with target noninterference, plus
1,024 dirty-row comparisons. Original-page comparisons pass 200 seeded episodes
and 990 actions across both tasks, including successful, wrong, keyboard,
debounce-boundary, stale-menu, queued-reopen and timeout traces. The fixture runs
the original delayed search callbacks on a controlled clock; zero-delay calls
settle normally. Public scripted controllers solve 2,000/2,000 generated tasks.
Real scheduler timing, unrestricted pointer/focus behavior and full DOM/AX
geometry remain outside this preset. These are conformance checks, not trained
RL scores. See [RESULTS.json](RESULTS.json) for current evidence.

```sh
make -f ocean/webnav/families/Makefile browser FAMILY=autocomplete EPISODES=100
make -f ocean/webnav/families/Makefile public-check FAMILY=autocomplete EPISODES=1000
```
