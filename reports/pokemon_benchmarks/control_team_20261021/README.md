# Team-strength control (2026-10-21)

Question: is our fixed learner team the ceiling? The learner team is Starmie / Exeggutor / Rhydon /
Chansey / Snorlax / Tauros, Starmie lead.

`ocean/pokemon/benchmarks/control_team.py` has TaurosV0 pilot our team against TaurosV0 on the three
panel teams. It uses the same seeds (20261025), seats and 48 games per team as the real eval.
`private_adapter.c` gained `private_learner_observation` and `private_step_both`; the existing
functions are unchanged. The box copy is in `/workspace/PufferLib/reports/pokemon_benchmarks/control_team_20261021/`.

| Opponent team | TaurosV0 piloting our team | Our best agent (sweep trial 0) |
|---|---|---|
| zam_egg_mie | 62.5% (30/48) | 47.9% (23/48) |
| jynx_mie_don | 50.0% (24/48) | 56.3% (27/48) |
| gar_egg_zap | 70.8% (34/48) | 60.4% (29/48) |
| **Total** | **61.1% (88/144)** | **54.9% (79/144)** |

- **The team is the main limit.** Even TaurosV0, a strong ladder model, wins only 61% with this team
  against these opponents. Our agent is within about 6 points of that. With 144 games each, that gap
  is roughly one standard error.
- **Next step: change the team.** Score candidate teams with the same control (TaurosV0 piloting
  each), then train on the strongest one.

## Team ranking (2026-10-21) — the team is not the ceiling

`rank_teams.py` screens 41 candidates at 16 games per panel team, then reruns the top 6 at 48. The
candidates are Metamon's 28 competitive gen1ou teams and 13 league teams (each member's drafted
team). TaurosV0 pilots every candidate against TaurosV0. Results are in `team_ranking_20261021/` on
the box.

| Team | Full 144 games (pilot TaurosV0) |
|---|---|
| **Ours** (Starmie/Exeggutor/Rhydon/Chansey/Snorlax/Tauros) | **61.1%** |
| Metamon competitive team_1019 | 59.7% |
| League gar_egg_mie_zap | 59.4% |
| Metamon competitive team_1002 | 55.2% |
| League zam_egg_mie | 53.1% |
| Metamon competitive team_1007 | 52.8% |

- **The team is fine.** No candidate beats ours. Screen leaders (64–72% on 48 games) fall back to
  53–60% at 144 games.
- **Correction to the conclusion above:** this test pits TaurosV0 against itself, so about 50–60% is
  expected. Our agent's 54.9% means it plays about as well as TaurosV0. Getting to 80%+ needs a
  stronger agent, or a weaker or different opponent; the team is not the lever.
- **Matchups vary a lot.** Per-matchup rates range from 38% to 84%. A single-team, single-opponent
  evaluation could therefore read far higher or lower than our 3-team panel.

Queued on the box (tmux `synth_queue`): our trial-0 agent vs SyntheticRLV2, and SyntheticRLV2
piloting our team vs SyntheticRLV2. Both use 48 games per team.
