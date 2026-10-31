# Gen 1 trainer round robin, cartridge AI on both sides (2026-10-30)

All 391 Pokémon Red/Blue trainers (ocean/pokemon/cartridge/trainers.json), every pair, 10,000 battles each with
sides alternating: 76,245 pairs, 762,450,000 battles, 541 s on 12 threads, 0 engine errors. Engine: our pkmn/engine
fork (github.com/FelixAllistar/engine, branch single-player, commit 9878e78c) in mirror mode. Draws: 1000 turns, or
75 turns without HP change (PimanRules' rules). Ratings: Bradley-Terry (draws half) on the Elo scale
(ocean/pokemon/cartridge/rank.py); rank_10k.json has every trainer, rr_10k.tsv.gz every pair (i j a_wins b_wins draws).

Compared with PimanRules' tournament (87,000 battles, two mirrored emulators, Elo): the same story at the ends. Prof.
Oak's unused team with Blastoise is #1 (his S-tier winner), the champion rival follows, Lance and Agatha lead the Elite
Four, Sabrina is far above the other gym leaders, Lorelei sits low (#45), Brock low (#339), and the level-5 rival with
Squirtle is last.

Top: Prof. Oak #1 (Blastoise) 3621, #2 (Venusaur) 3619, #3 (Charizard) 3544; Champion (Venusaur) 3521, (Blastoise)
3484, (Charizard) 3473; Lance 3247; Agatha 3100. Gym leaders: Sabrina #14, Blaine #20, Giovanni (gym) #21, Koga #34,
Erika #58, Lt. Surge #175, Misty #216, Brock #339. Elite Four: Lance #7, Agatha #8, Bruno #15, Lorelei #45.

## Level 100, Showdown-standard stats (rank_l100.json, rr_l100.tsv.gz)

Same parties rebuilt at level 100 (the level-100 learnset moves, special moves kept), DVs 15 and maximum stat
experience, cartridge AI on both sides, 10,000 battles per pair: 762M battles in 661 s, 0 engine errors. Party size now
dominates: median rank by party size 1: #324, 2: #252, 3: #156, 4: #96, 5: #36, 6: #4. The champion rival is #1-3,
mid-game rivals #4-14 (six Pokémon with Alakazam), Prof. Oak #8-11. Movers: Lorelei #45 -> #15 (Jynx gets Blizzard,
Slowbro Psychic), Brock #339 -> #241, Misty #216 -> #140; Lance #7 -> #19, Bruno #15 -> #56, Blaine #20 -> #109,
Koga #34 -> #93. Biggest climbs are early-route trainers with four or five Pokémon (Jr. Trainer♀ #5: Oddish and
Bellsprout x2, #267 -> #52); biggest falls are strong single-Pokémon trainers (Biker #6 Weezing #121 -> #388).

Link desyncs: in cartridge mode pkmn/engine ends a battle with an Error result in the few cases where two linked Game
Boys would disagree (Counter after a 0-damage move, Mirror Move against a trapping move, ...). The arena counts these
as draws (PimanRules counted emulator freezes and bugs the same way): 0.05% of battles (4,037 of 7.6M in a 100-per-pair
pass). The engine's `mod` option would resolve them but also adds Showdown clauses (Freeze Clause, Endless Battle
Clause, a 1000-turn limit), so it stays off.

## Our model, pick three (rank_model.json, model_pairs.tsv.gz)

The ladder-run 2B checkpoint fine-tuned 1B steps (143K SPS) on level-100 trainer parties, half mirror matches and half
against a trainer within 300 of its level-100 rating; each side sees both parties and brings up to three (Pokémon
Stadium cup). Then its final checkpoint picked and piloted both sides of every pair, 20 games per pair, sides
alternating: 1,524,900 games in about a minute (tournament.sh; the first launch failed because config/pokemon.ini had
no trainer_tournament key, now in make_config.py). Party size stops dominating once everyone brings three; quality of
the best three decides. Top: Juggler #2 (Drowzee, Hypno, Kadabra x2), Rival 2 #9, Sabrina #3, Lorelei #4,
Channeler #24 (Gastly x2, Haunter) #5; champion rival #7, #12, #14; Agatha #8; Prof. Oak #9, #10, #18; Lance #23.
Versus the cartridge AI at level 100: Sabrina #17 -> #3, Lorelei #15 -> #4, Agatha #16 -> #8, Erika #54 -> #32,
Blaine #109 -> #63, Lt. Surge #155 -> #78, Misty #140 -> #98; Bruno #56 -> #135, Koga #93 -> #147, Giovanni (gym)
#36 -> #69, Brock #241 -> #266. Biggest climbs are water trainers (Surf/Blizzard users: Fisherman #7 Seaking x3
#225 -> #43, Beauty #15 Seadra x2 #251 -> #49); biggest falls are big parties of weak Pokémon (Rocket #40 Rattata x3
#59 -> #238, Biker #7 Grimer x4 #79 -> #236).

## Pick bug, then every possible set (2026-10-31)

The first run's picks were uniform random. The pick phase (3) had no case in semantic_contract.h, so every pick
action scored 0. Fixed: a pick now scores like switching that Pokémon in. Instead of training picks, every pair played
every set each side could bring against every set of the other: min(party, 3) Pokémon and the lead, 2.75M set pairs
× 20 games = 55M games in ~75 min (env.trainer_tournament_sets, tournament_sets.sh). Each pair is then a
simultaneous zero-sum pick game, solved by regret matching+ (nash_sets.py, max gap 0.012). rank.py ranks the
equilibrium scores (rank_sets.json). Best sets per trainer are in sets_picks.json, and pair scores in
sets_pairs.tsv.gz.

Top: the champion rival (Charizard) #1, mid-game rivals #2-5, Lorelei #8, Sabrina #9, Agatha #10, Juggler #2 #11,
Prof. Oak #12, #13, #19, Lance #21. Versus the cartridge AI at level 100 (whole parties):
- Lorelei 15 -> 8, Sabrina 17 -> 9, Agatha 16 -> 10, Erika 54 -> 37, Blaine 109 -> 48 (Arcanine lead), Lt. Surge
  155 -> 81, Misty 140 -> 115.
- Bruno 56 -> 68, Koga 93 -> 101, Brock 241 -> 256.
- Biggest climbs are water trainers (Beauty #15 251 -> 56, Fisherman #7 225 -> 33). Biggest falls are big weak
  parties (Biker #7 79 -> 240).
Equilibrium picks spread over leads, so shares per set are small for six-Pokémon parties.

## Weedle vs the game (2026-10-31, ai_challenge.sh)

Gym Leader Matt's challenge: a level 100 Weedle (perfect DVs, maximum stat experience, PP Ups, all badges) against
the gym leaders, Giovanni, the Elite Four and the Champion at their original levels. They play under the cartridge
trainer AI and single-player rules (pk_start_vs_ai / pk_ai_action, from our engine fork), with 20,000 games each.
- Poison Sting only (no decisions; checks the setup): Bruno 1 in 6 (Matt 1 in 5.5), Lance 1 in 29 (1 in 31),
  Lorelei 1 in 1,818 (1 in 2,633), Champion ~1 in 3,400 over the three teams (1 in 4,770), Agatha 0 (below 1 in 500M).
  Giovanni is 0 here vs Matt's 1 in 66; his team resists Poison Sting about 4x, so his figure may be wrong.
- The trainer fine-tune with String Shot as well does worse everywhere (it was never trained to play a lone Weedle).
- Weedle specialist (run_weedle.sh: 300M steps against the hard trainers, side 2 frozen and replaced by the AI, only
  the Weedle learns): Bruno 1 in 3 (better than Poison Sting spam), String Shot now helps against Koga and Sabrina.
  It is worse than spam against Lance (1 in 369) and 0 against Lorelei, the Champion and Agatha: those wins are too
  rare for 300M steps.
Earlier bugs: the env built upstream pkmn/engine (no single-player rules; now our fork for PK_CARTRIDGE=1), and the
player lost its PP Ups (35 instead of 56 Poison Sting PP).
- Weedle specialist 2 (run_weedle2 + run_weedle2b: 1B steps against Lorelei, Bruno, Agatha, Lance and the Champion
  only; the first try stopped at 288M when the box's disk filled): Bruno 1 in 3 (twice Poison Sting spam's 1 in 6),
  Lance 1 in 31 (spam 1 in 29, up from 1 in 369), Lorelei 1 in 4,000, the Champion ~1 in 25,000, Agatha 0 in 20,000.
  Agatha stays out of reach: her Gengars resist Poison Sting, and Night Shade and Hypnosis grind a lone Weedle down.
