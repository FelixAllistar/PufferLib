# Sequence training generator

`SequenceGenerate.bend` is an independent stock CPU Bend generator for the
first five global training task indices:

| task index | row tag | preset |
| ---: | ---: | --- |
| 0 | 0 | click-button |
| 1 | 0 | click-link |
| 2 | 2 | focus-text |
| 3 | 7 | click-button-sequence |
| 4 | 8 | choose-list |

The entry point is:

```text
generate(task: U32, seed: U32, base: U32, a: Array<U32>) -> Array<U32>
```

It clears exactly 256 words beginning at `base` before writing the initial
private row and public text. The integer mixer is deterministic and separate
from JavaScript `seedrandom`; this generator does not claim exact browser RNG
parity. There is no high-bit train/eval lexicon split in this version, so a
seed's high bit does not silently change the label family.

All public strings use little-endian four-ASCII-byte `U32` words with a zero
terminator. Generic tag-0/tag-2 rows put eight 8-word label slots at words
64..127 and the public instruction at words 224..255. The node triples at
16+3*i remain the existing private ABI `(role, selected, goal)`.

The click-button and click-link presets generate 2..6 distinct labels, select
one target, and leave all controls unselected and running. Button nodes use
role 1; link nodes use pointer-target role 4. The bounded training instruction
is `Click "label".`. Focus-text emits one textbox node (role 3, public target)
and the source instruction `Focus into the textbox.`.

Sequence rows use the existing tag-7 fields: independent left/top values at
13..16, version 1 at 17, and fixed public labels `ONE` and `TWO` in the first
two label slots. The public instruction is the source text at word 224. To
make the center-coordinate training preset solvable, the generator places one
button in left range 0..37 and the other in 78..117, with a deterministic
assignment swap; their 40-pixel hit boxes cannot overlap. This is a documented
layout subset of the original task, whose browser instances can overlap.

Choose-list rows use tag 8 with 3..6 options. Option labels occupy 16 packed
words at 32+16*i, their lengths are at 17+i, `selected=0` is at 10, the
private target index is at 11, version 1 is at 12, and count is at 13. The
public instruction at 224 names the selected visible label and Submit. The
lexicon includes long ASCII country names to exercise the 63-byte option
bound. Native `OPTION` nodes remain outside the current DOM-v2 observation
contract; this generator's packed labels are the explicit training preset
metadata and do not alter that extractor.

The source-facing laws and proofs are in
`SequenceGenerateLAWS.bend` and `SequenceGeneratePROOF.bend`. Run the local
checks with:

```sh
/home/felix/.bend/bin/bend ocean/webnav/miniwob/training/SequenceGenerate.bend
/home/felix/.bend/bin/bend ocean/webnav/miniwob/training/SequenceGeneratePROOF.bend
```

The root training evaluator owns the generated C bridge and validates dirty
row clearing, tag routing, private ABI ranges, packed termination, query
contents, target uniqueness, label variation, non-overlap, and 32-lane seed
variation. No shared Wire, bridge, Makefile, or build file is changed here.

## Other generator presets

The shared `Wire.bend` reset request (tag 11) now dispatches all twelve global
training task indices. Tag 11 is a reset transport command, not a thirteenth
task. Existing checkbox/radio generation is reused; `TextGenerate.bend` and
`TreeGenerate.bend` supply the other presets.

| Task | Current generated distribution |
| --- | --- |
| click-checkboxes / click-option | Existing bounded 2..6-control form generator and public instruction export. |
| enter-text | Pinned original fifty-name pool; one initially empty field. |
| login-user | Lowercase username from the fifty-name pool; 2..5-character alphanumeric password; two initially empty fields. |
| read-table | Two rows; eight key strings and eight value strings; public row relationships and all cells available as copy candidates. |
| navigate-tree | Six shape families, 1..8 nodes, initially collapsed folders, generated unique node labels. |
| use-autocomplete-nodelay | Pinned 232-entry country list, 2..4-character prefix and optional suffix constraints, initially empty field. Original predicate permits any matching string, not only country names. |

Native evaluation's high-bit seeds differ from training seeds, but this is
**not an unseen-vocabulary or unseen-template split**. These are useful small
RL experiments and task screeners. Wider source distributions, large trees,
full layouts, long tables and website workflows remain future work.
