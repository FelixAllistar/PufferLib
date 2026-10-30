# Gen 1 single-player battle rules missing from pkmn/engine (spec for our fork, 2026-10-30)

pkmn/engine (github.com/pkmn/engine; our fork github.com/FelixAllistar/engine) built with `showdown = false` is
cartridge-accurate for link battles: its cartridge-mode tests pass (261 pass, 164 skipped Gen 2 / Showdown-only).
In-game trainer battles run the same core, plus the branches below, which the cartridge takes whenever
`wLinkState != LINK_STATE_BATTLING`. References are to pret/pokered at the commit in POKERED_COMMIT.

Side 1 = the player, side 2 = the enemy trainer (AI).

## Enemy action selection

- Move choice: `AIEnemyTrainerChooseMoves` (engine/battle/trainer_ai.asm). Every move starts at score 10; the
  trainer class's modifiers (data/trainers/move_choices.asm, e.g. Agatha: 1) adjust scores; the moves sharing the
  minimum score are picked uniformly. Modifier 1: +5 to non-damaging status moves (sleep, poison, paralysis effects) if
  the player already has a status. Modifier 2: -1 to moves with stat-up / in-between effects, only on the turn after
  the enemy switched in (wAILayer2Encouragement == 1). Modifier 3: -1 to super-effective moves; +1 to not-very-
  effective damaging moves when a damaging move of another type (or Super Fang / special-damage / Fly) exists.
  A disabled move gets score $50. Wild and link battles skip this.
- `SelectEnemyMove` (core.asm 2915): no choice while recharging, raging, charging, thrashing, frozen, asleep, trapping,
  storing energy (Bide) or trapped by the player's Wrap.
- Trainer AI (`TrainerAI`, trainer_ai.asm): runs at the enemy's slot in turn order (core.asm 416 and 454), after both
  moves were chosen and speed was compared. If it acts (switch or item), the enemy's move is skipped that turn, but
  its residual damage still applies. Each class has a routine and a use count per battle (data/trainers/
  ai_pointers.asm). The random number comes from `Random`, not `BattleRandom`.
  - Routines: Juggler 25% switch; Blackbelt 13% X Attack; Giovanni 25% Guard Spec; Cooltrainer M 25% X Attack;
    Cooltrainer F (bug: the 25% check is skipped) Hyper Potion below 1/10 HP, else switch below 1/5; Brock Full Heal
    if statused; Misty 25% X Defend; Lt. Surge 25% X Speed; Erika 50% Super Potion below 1/10; Koga 25% X Attack;
    Blaine 25% Super Potion (no HP check); Sabrina 25% Hyper Potion below 1/10; Rival 2 13% Potion below 1/5;
    Rival 3 13% Full Restore below 1/5; Lorelei 50% Super Potion below 1/5; Bruno 25% X Defend; Agatha 8% switch,
    else 50% Super Potion below 1/4; Lance 50% Hyper Potion below 1/5; everyone else nothing.
  - Item effects and the switch choice: AIUse* and AISwitchIfEnoughMons in trainer_ai.asm (to port line by line).

  - The use count is per Pokemon, not per battle: EnemySendOut (core.asm 1276/1307) sets wAICount to $FF on every
    send-out, and the next TrainerAI call reloads it from the class table. Only items use it up; switching does not.
    So Agatha has two Super Potions for each of her Pokemon.
  - Probabilities: `X percent` is X * 255 / 100, so the checks are r < 20 (8%), r < 32 (13% - 1), r < 64 (25% + 1),
    r < 128 (50% + 1) for one uniform byte r. HP checks are hp < max_hp / n (integer division).
  - Items: Potion 20, Super Potion 50, Hyper Potion 200 HP (capped at max); Full Restore sets HP to max and cures;
    Full Heal cures the status and the badly-poisoned flag but not confusion, and stat penalties from paralysis or
    burn stay; X items run the stat-up move effect for the enemy (no badge glitch on the enemy's turn);
    Guard Spec sets Mist.
- Move pick after scoring (core.asm 2971): one byte r picks slot 1 if r < 63, 2 if r < 127, 3 if r < 190, else 4
  (63/64/63/66 out of 256), rerolled while the slot is empty or disabled. Layer 2 uses wAILayer2Encouragement, which
  is zeroed when a fainted enemy is replaced (core.asm 911) and incremented on every ExecuteEnemyMove (core.asm 5474),
  so it applies on the enemy's second action after a send-out.
- Enemy PP never goes down: DecrementPP (decrement_pp.asm) only decrements the player's PP.

## Rules that only apply in single player

- Enemy stat-down moves miss 25% of the time (effects.asm 553, `cp 25 percent + 1`), before the substitute check.
- Badge boosts: the player's attack, defense, speed, special are multiplied by 1.125 for Boulder, Thunder, Soul and
  Volcano badges (core.asm 6454 ApplyBadgeStatBoosts, capped at 999), on switch-in (core.asm 1659) and level up.
- Badge boost glitch: the boosts are applied again to all four stats whenever the player uses a stat-up move
  (effects.asm 499) and whenever the enemy uses a stat-down move on the player (effects.asm 689).
- Enemy DVs fixed (attack 9, defense 8, speed 8, special 8), no stat experience (core.asm 5993 LoadEnemyMonData).
- When an enemy Pokemon faints, the trainer sends out the next healthy Pokemon in party order (core.asm 1315); the
  "Will you change Pokemon?" prompt (Shift mode) only affects the player's switch.
- BattleRandom is the overworld Random (core.asm 6546); link battles use a shared list. Same distribution.
- Disobedience (core.asm 3833) for traded Pokemon above the badge level cap: out of scope (our player Pokemon are
  not traded) unless a scenario needs it.

## How we check it

1. Unit tests per rule in the fork (the engine's own test style).
2. Differential test against the real cartridge: Pokemon Red (SHA-1 ea9bcae617fdf159b045185467ae58b2e4a48b9a, the
   user's own dump; never committed) in a headless emulator, battles set up in RAM, outcome distributions compared
   with the engine on the same positions.
3. Gym Leader Matt's published odds with Poison Sting spam as a sanity check: Bruno ~1 in 5.5, Lance ~1 in 31,
   Giovanni ~1 in 66 (15 PP), Lorelei ~1 in 2,633, Champion ~1 in 4,770, Agatha below ~1 in 500M.

## Status (2026-10-30)

Implemented on github.com/FelixAllistar/engine branch `single-player` (two commits on our pkmn/engine pin
9b88fd6c): data/trainer.zig (generated by gen_trainer_data.py) and the single-player section of gen1/mechanics.zig,
opt-in through an `options.trainer` field. Cartridge and Showdown test suites pass. First check against Gym Leader
Matt's published odds (level 100 Weedle, Poison Sting only, all badges, 20,000 battles each): Lance 1 in 31.6
(published 1 in 31), Bruno 1 in 5.3 (1 in 5.5), Agatha 0 wins. Still to do: the differential test against the ROM.
