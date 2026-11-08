# Poker Kirby loop v1 (2026-11-08, poker box, tmux poker_kirby)
The chess lessons applied to poker: no reward shaping (BB won), PFSP over a snapshot pool (the env already has it:
env.league_pfsp, league_newest 0.5), exploiters that start from main (chess exC found holes fastest), small exploiter
share in main's games (PFSP weights them by how much they win), policy and magnet scored on a fixed outside judge.

- main (main_run.sh): gen13 recipe from gen13 trial 0's 600M final (judge 0.2408 BB/hand, the sweep's score), 3B in one
  run, ~63K SPS (table lives + frozen-model seats; gen13 trials ran ~50K). Opponents: main.league (gen12 field: gen11
  t9/t7/t13, gen10 t9, plus each cycle's exploiter), its own live snapshot pool and range/Fullhouse bots (15%).
- loop.sh, every 500M main steps: PAUSE -> main saves state/ (commit 1fc5c526c) -> judge main policy and magnet ->
  exploiter exN (ex_run.sh: init = frozen main, 200M at tables of five copies of it, lr 0.001) -> exN vs main head to head
  (mirror main vs main = rake baseline, -0.065 BB/hand for the start model) and on the judge -> exN joins main.league ->
  main resumes (--base.resume=1).
- Judge = [score_eval] in config/poker.ini: leagues/poker/gen12/score.txt (6 frozen 1024x2 models) + 30% range/Fullhouse,
  fixed 100 BB stacks, deterministic (the start model reproduces 0.240794 over 172k hands).
- Results: results.txt on the box (reports/poker_kirby1). Next by result: exploiters that win big and a main that
  keeps rising on the judge = keep cycling; main flat on the judge = more cycles or a plain self-play control
  (main_run.sh without the exploiter banks) to compare.
