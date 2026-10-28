# Metamon gen1ou "competitive" team set (pinned)

Copied unchanged from Metamon's `get_metamon_teams("gen1ou", "competitive")`,
Metamon revision `0a00a759c9a4382a2877088d828302ec294a05a5`. These are the 28 human-made
sample teams that upstream Metamon uses for its own evaluations. Use them with
`evaluate_private.py --team-dir reports/pokemon_benchmarks/metamon_competitive_gen1ou`.
Metamon then plays each team, and our learner keeps its own fixed team.

Our native engine reads species and moves only. Gen1 has no items or abilities. EV
lines are already at maximum, so they change nothing. `IVs: 2 Atk` (a low Attack DV that
reduces confusion self-hit damage) is ignored, so those Pokémon get maximum DVs.
