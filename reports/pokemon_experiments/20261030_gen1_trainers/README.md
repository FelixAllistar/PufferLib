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
