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
