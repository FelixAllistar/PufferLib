import json, sys
for path in sys.argv[1:]:
    R = json.load(open(path)); st = {}
    dec, thrown = {}, {}
    for moves, result in R:
        for ph, e, e2 in moves:
            s = st.setdefault(ph, [0, 0, 0]); s[0] += 1
            if e > -500: s[1] += max(0, min(e - e2, 1000))
            if e > -300 and e - e2 >= 200: s[2] += 1
        if result == 0:  # decisive mistake: first move from >= -150 to <= -300
            for ph, e, e2 in moves:
                if e >= -150 and e2 <= -300: dec[ph] = dec.get(ph, 0) + 1; break
        if result < 1 and any(e >= 300 for _, e, _ in moves):  # had a +3 game and did not win it
            peak = max(range(len(moves)), key=lambda i: moves[i][1])
            ph = moves[peak][0]; thrown[ph] = thrown.get(ph, 0) + 1
    print(path.split('/')[-1], f"{len(R)} games, score {sum(r for _, r in R)/len(R):.2f}")
    for ph in ('opening', 'middlegame', 'endgame'):
        n, cpl, bl = st.get(ph, [0, 0, 0])
        if n: print(f"  {ph:10s} moves {n:5d}  avg cp loss {cpl/n:5.0f}  blunders/100 moves {100*bl/n:4.1f}")
    print('  lost games, phase of the decisive mistake:', dec)
    print('  won-position (>= +3) games not won, phase at peak:', thrown)
