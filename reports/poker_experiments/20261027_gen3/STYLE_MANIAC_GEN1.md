# STYLE POLICY, NOT A STRONG BASELINE: "maniac" (gen1, archived 2026-10-27)

`maniac_gen1.bin` is a byte copy of leagues/poker/gen1/gen1.bin (128x3 PufferNet, 175 observations). Keep it only as a
**style opponent** (league member, web-table personality, the "aggressive" corner of a style set), never as a reference
for good play or a warm start for a strong policy.

Why: in play against a human (27 Oct) it re-raises and calls down with any mildly good hand, and two copies at one table
re-raise each other to the cap; a human cannot get a normal hand in. The panel hid this (it beats rules/range/maniac/
station bots, loses only to tight: -1.8 BB/hand), because no panel bot re-raises back. It is a real, entertaining
aggressive style, but an exploitable one.

Its original copy stays at leagues/poker/gen1/gen1.bin because the web tables and old reports point there; the web game
should label its seats as this style.
