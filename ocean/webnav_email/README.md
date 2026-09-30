# WebNav email PPO adapter

This is a native `puf` adapter for the existing ten-task, stock CPU Bend
`email` family. The DSO at `build/webnav/families/email/libemail.so` owns task
generation, transitions, deadline and raw reward. The adapter selects tasks by
`task_mask` and projects only `WFView` plus its own resettable interaction
history into policy observations. The task ID, seed, private target row and
reward fields do not enter those observations or action candidates.

The 170 discrete actions are wait, clicks on the first 40 visible public nodes,
select all, backspace, 32 public text candidates and 95 printable ASCII
characters. The email family has at most 34 nodes in its inbox or search view.
Candidates include quoted instruction spans, bounded natural-language clause
spans, sender names seen in the inbox, and visible body or node text. The
instruction is also preserved as 1024 byte features. Character actions permit
edits when no suitable copy candidate exists. The policy must choose both UI
operations and text; this adapter does not parse a private target or choose a
sender/recipient for it. Recipient names from other inbox threads remain in
public history after opening an email. Select-all history tracks the Bend
field-selection action, which the family's public field node does not expose.

The feature encoder uses ordinal nodes and omits synthetic geometry. It exposes
node names, values, roles, flags, parent relationships, click history, and
candidate bytes. It cannot fully encode body text directly in a node's 40 value
bytes, though the candidate encoder carries up to 159 bytes. It proposes a
bounded set of clause spans; unusual original NL/Turk phrasings can omit the
intended recipient from that set, leaving the character actions. Non-ASCII
typing, cursor movement, clipboard shortcuts and original-page pixel scrolling
are outside the existing Bend transport. Search result bubbling and icon state
follow that family's existing model. The separate original-browser evaluator
rebuilds `WFView` from live DOM text and visible controls; it never reads the
original page's private target or sampled email array. Original inbox subject
previews can be truncated where the Bend public view contains the full subject,
so policy transfer is an empirical question rather than a parity claim.

Root integrated and validated this adapter on 2026-09-29; see [RESULTS.md](RESULTS.md). From the repository root, the integrating agent
can run the serialized guarded family build, then build and run this adapter:

```sh
node ocean/webnav/families/build.cjs email --test
./build.sh webnav_email build/webnav_email_train --float
make -f ocean/webnav_email/Makefile test
make -f ocean/webnav_email/Makefile build/webnav_email/native_eval
./build/webnav_email/native_eval random 100
./build/webnav_email/native_eval checkpoints/webnav_email/CHECKPOINT.bin 100
make -f ocean/webnav_email/Makefile browser-build
./build/webnav_email/browser_eval checkpoints/webnav_email/CHECKPOINT.bin 20
```

The shared `build.sh` now recognizes `webnav_email`. Train with
`build/webnav_email_train train`; run its build under the same 6 GiB/no-swap
serialization guard documented for the other adapters.
The checkpoint evaluator expects the configured H64/L1 policy. Its output is
one JSON line per task with episode count, full-credit successes and steps.
Task-specific evaluation accepts a final original task name argument.

Current root validation and measured learned results: [RESULTS.md](RESULTS.md).
