# WebNav goal and resource coordination

The current goal is to refactor WebNav into reusable controls, browser/page
state, persistent records and transactions that compose into application
workflows toward full WebArena coverage. Preserve checked MiniWoB compatibility
and the shared learner, and distinguish native validation, browser fidelity
and learned performance.

User instruction, 2026-10-03: if system resources constrain progress, pause
the goal and alert the user. This is explicit standing authorization to pause
when that condition occurs. Report the limiting resource and the evidence,
preserve the work, and wait for the user's direction before resuming.

Use the existing serialized build guard (up to 6 GiB, no swap, one CPU).
Do not compensate for resource constraints by changing limits or timeouts,
weakening checks, reducing intended coverage, changing rewards, or adding
temporary implementation workarounds. Resource workarounds must not become
lasting changes. Do not stop or reconfigure unrelated workloads.

The additional implementation rules in `families/AGENTS.md` still apply to
family work; this resource coordination rule takes precedence over older
instructions to investigate or work around resource constraints immediately.
