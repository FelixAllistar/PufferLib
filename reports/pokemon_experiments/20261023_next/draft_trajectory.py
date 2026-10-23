import sys, json, collections
path, chunks = sys.argv[1], int(sys.argv[2])
rows = []
for l in open(path):
    try: r = json.loads(l)
    except Exception: continue
    if r.get('draft_mode') != 'normal': continue
    for side, b in enumerate(r['banks']):
        if b == 0: rows.append((tuple(sorted(r['teams'][side])), r.get('move_pairs', [0, 0])[side]))
league = {'gar_egg_zap':'Gengar Exeggutor Zapdos Chansey Snorlax Tauros','zam_egg_cloy_don':'Alakazam Exeggutor Rhydon Cloyster Chansey Tauros',
 'gar_egg_mie_zap':'Gengar Exeggutor Starmie Zapdos Snorlax Tauros','zam_egg_zap':'Alakazam Exeggutor Zapdos Chansey Snorlax Tauros',
 'zam_egg_mie':'Alakazam Exeggutor Starmie Chansey Snorlax Tauros','gar_egg_mie':'Gengar Exeggutor Starmie Chansey Snorlax Tauros',
 'mie_egg_zap':'Starmie Exeggutor Zapdos Chansey Snorlax Tauros','jynx_egg_zap':'Jynx Exeggutor Zapdos Chansey Snorlax Tauros',
 'jynx_mie_don':'Jynx Starmie Rhydon Chansey Snorlax Tauros','mie_egg_cloy':'Starmie Exeggutor Cloyster Chansey Snorlax Tauros',
 'jynx_egg_gar_zap':'Jynx Exeggutor Gengar Zapdos Snorlax Tauros','zam_egg_gar':'Alakazam Exeggutor Gengar Chansey Snorlax Tauros',
 'mie_egg_don':'Starmie Exeggutor Rhydon Chansey Snorlax Tauros'}
league = {k: frozenset(v.split()) for k, v in league.items()}
def nearest(team):
    t = frozenset(team); best = max(league, key=lambda k: len(league[k] & t)); return best, len(league[best] & t)
print(f"{len(rows)} learner free drafts in {path}")
n = len(rows); size = max(1, n // chunks)
print("chunk  drafts  distinct  top-team share  exact league team  Lapras  Exeggutor  Snorlax  top team (nearest league team, shared)")
for c in range(chunks):
    part = rows[c*size:(c+1)*size] if c < chunks-1 else rows[c*size:]
    if not part: continue
    teams = collections.Counter(t for t, _ in part)
    top, k = teams.most_common(1)[0]
    exact = sum(v for t, v in teams.items() if frozenset(t) in league.values())
    sp = lambda s: 100 * sum(v for t, v in teams.items() if s in t) / len(part)
    nb, shared = nearest(top)
    print(f"{c:5d}  {len(part):6d}  {len(teams):8d}  {100*k/len(part):13.0f}%  {100*exact/len(part):16.0f}%  {sp('Lapras'):5.0f}%  {sp('Exeggutor'):8.0f}%  {sp('Snorlax'):6.0f}%  {', '.join(top)} ({nb}, {shared}/6)")
