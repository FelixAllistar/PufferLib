# Menus family

## 5.0 conversion qualification, 2026-09-24

Implementation sources match the preserved `5c` family. The capped build passes
all 13 laws and 833 native checks. Fresh comparisons against MiniWoB revision
`33c3b4ddef8c6eb67c57a29663d844b1eda7e614` pass 24 browser episodes per task
(48 total), including visible submenu and reward comparisons. The public-only
scripted controller solves 1,000 generated episodes per task (2,000 total).
`RESULTS.json` retains the historical audit; these fresh checks do not expand
its timing/distribution claims or establish learned PPO performance.

This family models MiniWoB `click-menu` and `click-menu-2` with stock CPU Bend.
The Bend state owns menu visibility, hover paths, opening and closing the second
task's menu, branch expansion, leaf selection, deadlines, and rewards. `WF_CLICK`
selects a visible item and `WF_POINTER_MOVE` targets a visible reference to open
its submenu. Private target flags remain in the Bend row and never enter `WFView`.

`Generate.bend` supplies deterministic instances for both tasks. The generated
`click-menu` tree is a compact three-level family with a hidden leaf target;
`click-menu-2` uses the original seven target labels and icon names, its Playback
submenu, and disabled Print item. `browser_oracle.c` imports instances generated
by the pinned original pages, then compares public visibility, submenu state,
terminal status, and rewards while sending real CDP mouse moves and clicks.
Geometry in the public projection is ordinal; the browser harness uses original
element bounds when it dispatches physical input.

The browser preset waits 350 ms after each event for jQuery hover timers and
animations to settle while retaining the controlled episode clock. Intermediate
animation and real wall-clock deadline equivalence are not claimed. The path
laws include keeping ancestors expanded and hiding grandchildren until their
direct parent is opened. These caught a reversed ancestor traversal during
original-browser validation.

The native test covers reset and four-lane isolation, private-goal
noninterference, nested hover paths, open and close behavior, correct and wrong
leaf choices, timing, and terminal absorption. `LAWS.bend` states the matching
core transition properties and `PROOF.bend` proves them.

Run the family build and independent native test through the resource-capped
builder from the repository root:

```sh
make -f ocean/webnav/families/Makefile family FAMILY=menus
make -f ocean/webnav/families/Makefile browser FAMILY=menus EPISODES=20
```

Browser comparison needs the pinned MiniWoB checkout and the local
`chrome-headless-shell`; set `WEBNAV_CHROME` to override its default path.
The shared public-check runner drives menu actions from public observations.
See [RESULTS.json](RESULTS.json) for 48 original-browser episodes and 2,000
public-scripted generated episodes. These are not learned PPO results.
