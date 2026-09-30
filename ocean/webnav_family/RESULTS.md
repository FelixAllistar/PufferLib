# Learned click-family benchmark, 2026-09-27

The final mixed-task checkpoint is
`checkpoints/webnav_family/1790563051426/0000000002998272.bin`
(SHA-256 `e16a158230ea588ef3580bdc0ee3f659f22ee8c06989ef1b9d5553b41893e451`).
It uses the 108,160-parameter H64/L1 PufferLib policy, three million training
steps, both MiniWoB data modes, the optional frozen Potion similarity feature,
and public instruction-class, role and ordinal cues. Task generation and
transitions remain in the checked CPU Bend `click` family.

| Task | Native full credit | Original Chromium, greedy | Original Chromium, sampled |
| --- | ---: | ---: | ---: |
| click-test | 100.0% | 100/100 | 100/100 |
| click-test-2 | 100.0% | 100/100 | 100/100 |
| click-test-transfer | 100.0% | 100/100 | 100/100 |
| click-dialog | 100.0% | 100/100 | 100/100 |
| click-dialog-2 | 100.0% | 100/100 | 100/100 |
| click-widget | 99.99% | 94/100 | 94/100 |
| focus-text-2 | 100.0% | 100/100 | 100/100 |
| click-checkboxes-transfer | 99.45% | 100/100 | 99/100 |
| click-checkboxes-large | 97.10% | 99/100 | 98/100 |
| click-checkboxes-soft | 62.17% | 63/100 | 61/100 |

Native evaluation uses `data_mode=1`, seed offset 1,000,000, and at least 256
episodes requested per task; actual completed episode counts are in
`build/webnav_family/class_native_eval.txt`. Original Chromium evaluation uses
the pinned MiniWoB revision `33c3b4ddef8c6eb67c57a29663d844b1eda7e614`,
test mode, seeds 200000–200099, public query/control observations, and CDP
clicks. The two browser action selectors use the same checkpoint. Training
included both data modes, so mode 1 is a separate-seed comparison, **not** a
held-out transfer split. The ten tasks are a subset of the 125-name MiniWoB++
registry, not a claim of complete benchmark parity.

The first three-million-step mixed checkpoint used structured checkbox labels
and adjacent target locations; despite 95.4% native success on large
checkboxes, it reached only 12/50 on the original page and 20/50 on checkbox
transfer. Replacing the Bend generator with short alphanumeric labels and
independently ranked targets raised a nonsemantic mixed policy to 96/100 and
100/100, respectively. With the frozen Potion feature and public instruction
cues, the final policy reaches 99/100 large and 63/100 soft checkboxes. A
10,000-trial six-option diagnostic using distinct synonyms from the public
source pool gives the frozen Potion vectors 88.85% top choice; production
features do not read that source synonym table.

Focused 1M-step policies confirm that the two basic-task paths are sound:
`checkpoints/webnav_family/1790562828548/0000000000999424.bin` scores
100/100 on original `focus-text-2`, and
`checkpoints/webnav_family/1790562907186/0000000000999424.bin` scores
100/100 on original `click-widget`. The final shared policy nearly matches
them but still misses six widget instances. Soft synonym selection is the
clearest remaining text limitation. Full DOM/AX interaction, unrestricted
pointer actions, and the other task families remain separate work.

Reproduce the final measurements after building the family and trainer:

```sh
bash ocean/webnav_family/eval_tasks.sh checkpoints/webnav_family/1790563051426/0000000002998272.bin 256 1
WEBNAV_SEMANTIC=1 build/webnav_family/browser_eval checkpoints/webnav_family/1790563051426/0000000002998272.bin 100
WEBNAV_SEMANTIC=1 WEBNAV_SAMPLE=1 build/webnav_family/browser_eval checkpoints/webnav_family/1790563051426/0000000002998272.bin 100
```

The source checkout needs the pinned Potion tokenizer and safetensors assets
from `make -C ocean/webnav text-test`; the checkpoint itself is local and is
not part of Git. Browser runs are headless. The family build and differential
tests use the guarded 6 GiB, no-swap workflow in
[`BUILD_SAFETY.md`](../webnav/families/BUILD_SAFETY.md).
