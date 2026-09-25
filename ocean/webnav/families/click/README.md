# Click, focus and checkbox variants

## 5.0 conversion qualification, 2026-09-24

The capped build passes the nine family laws and imported shared proof suite,
then 460,800 independent transitions covering both transfer modes. Fresh
comparisons against pinned MiniWoB revision
`33c3b4ddef8c6eb67c57a29663d844b1eda7e614` pass 400 original-browser episodes
(20 per task per mode). The public-only scripted controller solves 1,000
generated episodes per task (10,000 total). `RESULTS.json` preserves the earlier
audit; the bounded click/geometry limitations below still apply.

The imported proof dependencies include tree, sequence and autocomplete models
and generators. Their presence is not evidence that their complete runtime,
browser or training workflows have been converted yet.

Ten task names use a shared CPU Bend model: `click-test`, `click-test-2`,
`click-test-transfer`, `click-dialog`, `click-dialog-2`, `click-widget`,
`focus-text-2`, `click-checkboxes-transfer`, `click-checkboxes-large`, and
`click-checkboxes-soft`.

`Model.bend` reuses the checked widget core for reversible checkbox toggles,
radio exclusivity, indexed targets and fractional checkbox rewards. Nine
additional laws cover direct/focus decisions, blur, invalid targets, terminal
absorption, waiting and deadline priority. Behavior and generation are Bend;
C transports arrays, exports public observations and runs independent tests.

```sh
make -f ocean/webnav/families/Makefile test FAMILY=click EPISODES=1000
make -f ocean/webnav/families/Makefile browser FAMILY=click EPISODES=20
```

Builds enter the [resource-limited scope](../BUILD_SAFETY.md). Bounded numeric
dispatch uses `Nat` internally: large `U32` literal matches caused excessive
stock compiler memory use. The external wire format remains U32.

Transfer mode is reset argument 0: zero selects training, one selects test.
Button transfer switches the target from ONE to TWO. Checkbox transfer changes
the number of requested boxes from 0..3 to 4..6. Large checkbox tasks request
5..12 boxes and have a 20-second deadline. Soft checkboxes use the original 34
synonym groups with independently sampled requested/displayed words. Focus
uses three inputs and the original 1st/2nd/3rd instruction vocabulary.

The public scripted controller tests generated-instance solvability; it is not
a learned agent. Browser differential tests import original-generated matched
instances and include deliberately wrong actions and timeouts. Private goals
are available to the conformance fixture, never to the public controller.

This is bounded behavior coverage, not full MiniWoB++ parity. The native
generator uses finite ASCII labels and simplified non-overlapping geometry.
The browser comparison uses unobscured control clicks and a logical clock;
occlusion, arbitrary coordinate actions, full DOM/AX output and complete source
generator distributions remain open. Text entry in click-widget is outside
this click-action preset. The family-v2 libraries are not yet integrated into
the existing twelve-task PPO training profile.

Transport: eight lanes of 2048 words; metadata at 32..39; up to 16 controls at
64, with 40 words each; instruction at 768..1023. Goal flags remain private.
See [RESULTS.json](RESULTS.json): 460,800 native transitions, 400 matched-original
browser episodes, and 10,000/10,000 public-scripted generated successes.
