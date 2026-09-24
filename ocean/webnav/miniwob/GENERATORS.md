# CPU Bend form generation

`Generate.bend` owns deterministic sampling, labels, task goals, public instruction construction, and initial widget state. `Widgets.bend` owns all selection/submit/deadline/reward transitions. `Wire.bend` owns packing and reset. All use stock Bend 2.0.6 CPU execution. The C bridge copies buffers; `forms.c` only serializes public fields to JSONL. The browser adapter is test infrastructure and deliberately retains the original HTML event/reward implementation as an independent reference.

Two independent generators are available: `click-checkboxes` and `click-option`. They produce 2..6 controls, 2..7-character alphanumeric labels, and a Submit button. Checkbox targets include the empty and full set. Radio targets contain exactly one index, with no initial selection. These match the source's ranges and goal rules, not JavaScript seedrandom's exact stream, layout distribution, or finite-precision probability distribution. The generator uses a deterministic integer mixer with a separate label stream; it is not cryptographic. Version 1 and seeds identify instances. Seed metadata is provenance, not a policy feature.

```sh
make -f ocean/webnav/Makefile miniwob
# 1000 public forms starting at seed 42; no browser needed.
build/webnav/generate_forms click-checkboxes 1000 42 > build/webnav/checkbox-forms.jsonl
build/webnav/generate_forms click-option 1000 42 > build/webnav/radio-forms.jsonl
# Exercise generated forms through original HTML event/reward handlers.
WEBNAV_GENERATED_FORMS=1 build/webnav/miniwob_oracle click-checkboxes 1000
WEBNAV_GENERATED_FORMS=1 build/webnav/miniwob_oracle click-option 1000
# Record full normalized browser observations and actions for these forms.
WEBNAV_GENERATED_FORMS=1 WEBNAV_RECORD=build/webnav/generated-traces.jsonl \
    build/webnav/miniwob_oracle click-option 100
```

The JSONL schema `webnav-generated-form-v1` contains task/seed provenance, public instruction, and controls with ref/role/name/checked. It contains no private goal array. This is an abstract form record, not DOM-v2: it has no browser geometry, layout or AX tree. Use recorded browser traces for DOM-v2 observations. All output is development data, not a frozen held-out test set.

For differential testing, the harness replaces only the original form-building helper with the Bend-generated instance. The original `genProblem` constructs the instruction and installs its original reward callback; actual Chromium events drive selection and submission. The test compares the instruction, private imported specification, public DOM projection, focus, checked bits, termination and rewards against Bend. It explicitly reports `Bend-generated form in original task HTML`, separate from the upstream-generated test mode. This validates sampled generated instances against those handlers, not equivalence of the two generator distributions.

Duplicate labels remain possible, as upstream allows them. Checkbox answers and radio correctness are indexed privately; identical public labels can make a task ambiguous. The original-generator harness imports those private bits for the simulator specification only. Scripted solving uses the public instruction/labels and does not get the answer index. Do not silently remove ambiguous instances from benchmark results or give the policy private goals.

The reset wire command is 3, task 1 (checkbox) or 3 (radio), seed at word 13. Reset clears the whole 256-word row before writing it and ignores old state. Word 15 records generator version 1. Labels occupy 8-word zero-terminated ASCII slots from word 64; the instruction occupies words 128..255. Generated forms have at most seven controls including Submit. This extension is private simulator transport; no checkpoint should ingest the complete row.

Native validation checks 8,192 forms across both families for deterministic reset independent of old memory, size/alphabet/instruction rules, initial state, and public-label solvability on the sampled seeds. It also exercises 3,448 radio combinations, including malformed multi-selected initial input, repeated clicks, exclusivity repair, and submission. Bend laws cover initial selection/focus/outcome, generated node count, radio assignment, overwrite behavior, preservation of goals, and checkbox frame behavior. Universal generator range bounds, exact probability laws, and complete browser correctness are not proved.

Next: add text/caret/selection and select controls, then standalone generation for the remaining task families. These forms are not yet connected to the PufferLib trainer; the old pilot checkpoint remains a different observation/action contract.

A three-second warm single-thread CPU sample measured 393,494 form resets/sec and 952,677 active widget clicks/sec. This includes the C transport and excludes browser rendering, text encoding, policy features, training, and runtime startup. The click phase repeats active selections with a fixed logical clock, so it is an isolated transition benchmark, not episode throughput. Reproduce with `make -f ocean/webnav/Makefile bench-forms`; results vary with machine load. Raw evidence is in `build/webnav/forms-benchmark.json` and [../ITERATION_RESULTS.json](../ITERATION_RESULTS.json).
