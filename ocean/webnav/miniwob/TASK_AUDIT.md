# First-wave source audit

Source: the pinned original task HTML and bundled `core/core.js`, `common/ui_utils.js`, d3, jQuery 1.12.4 and jQuery UI 1.12.1. Source hashes are recorded in ITERATION_RESULTS.json. This is an audit of the HTML task semantics, not the entire Python wrapper's configurable action/reward API.

Common behavior: `core.startEpisodeReal` calls the generator, clears done/reward state, starts a 10-second episode, and hides the start cover. `core.endEpisode` accepts the first completion only, records raw reward, optionally multiplies it by max(0,1-elapsed/10000), marks done, and restores the cover. Timeout gives -1. A positive reward and exact task completion are not always equivalent. The oracle currently uses a controlled clock, not real scheduling.

| Task | Generator and reward | Model status / next requirement |
| --- | --- | --- |
| click-button | Six generated content positions mix text, textboxes and buttons; at least one button. Eight base captions with capitalization variation. Any button with the selected caption succeeds, including duplicates. Correct +1 is timed, wrong -1 is not. | Matched-instance click/wait port tested. Initial geometry and text are imported. |
| click-checkboxes | 2..6 checkboxes, generated labels of length 2..7, independently requested with probability .5. An empty request is allowed. Submit raw reward is (correct states - incorrect states)/count; positive values receive time scaling. | Matched-instance port tested, including all target/state combinations natively. Original task is NOT exact-set binary reward. |
| click-link | Twenty generated words, some wrapped in clickable styled spans; generation repeats until at least one exists. Any matching-caption span succeeds. +1 timed, -1 untimed. | Matched-instance port tested. The controls are not semantic anchors; DOM fallback detects pointer-style leaf elements. |
| focus-text | One textbox with randomized margins. Focusing it immediately blurs it and ends successfully with time scaling. | Matched-instance click-to-focus port tested. Keyboard focus realization not yet tested. |
| click-button-sequence | Two positioned buttons; success only after exactly two clicks matching ONE then TWO. Positions can overlap. +1 timed, -1 untimed. | Implemented bounded coordinate hit-testing, topmost overlap dispatch, history and timed reward. Original-browser checks cover overlap; policy integration remains open. |
| click-option | 2..6 radio buttons with generated labels. A requested radio index is correct; Submit returns +1/-1, positive timed. | Matched-instance port and independent Bend generator tested. Radio exclusivity/reselection implemented. Private indexed goals preserve duplicate-label ambiguity; the controller uses public labels. |
| choose-list | Shuffled people/country list with 3..9 options; browser selects the first by default. Submit checks selected visible text. +1 timed/-1 untimed. | Implemented bounded selection and text-based submission, checked with real native dropdown keys. Native OPTION policy observations remain open. |
| enter-text | Target is one of FIFTY_NAMES; input and submit margins vary. Submit checks exact input string. +1 timed/-1 untimed. | Bounded ASCII text/edit preset tested; see TEXT_TASKS.md. Unicode, arbitrary selection/key actions and independent generation remain open. |
| login-user | Lowercase sampled name plus generated password of length 2..5. Both fields must match exactly; +1 timed/-1 untimed. | Two independent fields and exact submit tested in bounded ASCII preset. Missing for-bindings remain visible; synthetic password values are exposed by this DOM preset. |
| read-table | Five shuffled key/value rows from categories including names, country and birth year. Instruction asks for a row's value; submit checks exact text. +1 timed/-1 untimed. | Bounded text entry tested; TABLE/TR observation nodes now preserve row relationships. Unrestricted input and independent generation remain open. |
| navigate-tree | Generated nested files/folders, repeated names possible, tree plugin initially collapsed. Any clicked node with target name succeeds; wrong file fails; wrong folder may expand without ending. | Implemented bounded preorder structure, expansion, visibility and original bubbling under label-click preset. Full DOM geometry and arbitrary hit regions remain open. |
| use-autocomplete-nodelay | Country-prefix suggestions, delay=0. Prompt gives a prefix and sometimes suffix. Submit checks startsWith and optional endsWith; it does NOT require the string to be an actual country. | Implemented bounded ASCII edits, retained menu term, suggestions, exact submission predicate and external popup scope. Oracle settles zero-delay callbacks under a controlled clock; unrestricted browser scheduling remains open. |

Boundary decisions for subsequent ports:

- Retain control-targeted DOM actions as a separately named preset. Add explicit caret/selection and select/scroll operations; do not claim their parity with arbitrary low-level key sequences until tested.
- Detect whether an intended control receives the actual click. Bounding boxes alone cannot model overlap. Preserve the event target/point in traces.
- Capture popup/menu nodes outside `#area` without feeding the benchmark's reward HUD or private state to the policy.
- Carry label/row/tree relations in observations; missing markup must remain visible as uncertainty or a lower-quality view, not silently repaired from task answers.
- Preserve upstream goal predicates even when they permit surprising solutions. Stricter task variants belong in a separate suite.

Independent generators beyond checkbox/radio forms, full layout, all keyboard actions, wall-clock scheduler parity, pixel observations, original-wrapper scoring presets, and learned-policy evaluation on these ports remain open. The old pilot's checkpoint does not run on these new observations.
