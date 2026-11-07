# Gen 1 random battles: quick check of the OU ladder model (2026-11-05)

Question (user): can our Gen 1 OU model play Gen 1 random battles as-is? No training; a local check only.

Setup: local Pokémon Showdown 0.11.11 (`build/pokemon/showdown_server`, `--no-security`), format `gen1randombattle`,
model `checkpoints/pokemon_final/20261028_ladder_2B.bin` through the Showdown bridge (argmax). Opponent: `maxdmg.mjs`,
a max-damage bot (base power x STAB x type effectiveness vs the foe's active; forced switches to the healthiest).
`run.sh N_SELF N_MAXDMG` runs it all.

Bridge fix needed: random formats reject a custom team, so `client.ts` now sends `/utm null` when the format name
contains "random".

| Test | Result |
|---|---|
| Self-play, 3 battles | completed, no errors |
| vs max-damage, 200 battles | **168-32 (84.0%, ±5)**, no protocol errors or watchdog answers |

Caveats:
- The model never saw levels other than 100 (the observation has a level byte, constant in training), and the tracker
  still writes 100 for our own Pokémon (`tracker.ts` line 89); foe levels come from Showdown.
- Randbats includes weak and not-fully-evolved Pokémon and movesets outside the 4,150 training teams.
- Max-damage is a weak opponent; 84% says the policy is not broken, not that it is good at randbats. Team luck matters
  more in randbats than in OU.

Next (not done): a randbat fine-tune in our engine with Showdown's Gen 1 random team generator (levels included), then
test against the same bot and against our own OU model piloting random teams.
