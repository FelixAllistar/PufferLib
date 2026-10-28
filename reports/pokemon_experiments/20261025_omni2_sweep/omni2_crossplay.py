#!/usr/bin/env python3
"""Cross-play matrix, queued after the OMNI-2 sweep: every candidate plays every other on the 19-team pool (both
seats draw a pool team each game; ./puffer_match match, env.match_team_pool), 4096 games per ordered pair, both
orders averaged to cancel the seat advantage (~0.04). Ratings: Nash averaging (Balduzzi et al. 2018), robust to
non-transitive cycles: solve the symmetric zero-sum meta-game A = P - 0.5 (fictitious play), then rate each model by
its expected margin against the Nash mixture; a plain mean margin is shown beside it.
Candidates (all 1024x2): OMNI-1024 1B and 2B, FP-3 125M, FP-4 375M, and the sweep's top 3 trials by vs_league_late.
Waits for the sweep process to exit. Writes crossplay.txt (pairs, matrix, ratings). Usage: omni2_crossplay.py SWEEP_DIR
"""
import glob, os, re, subprocess, sys, time

D = sys.argv[1]
N = 'reports/pokemon_experiments/20261023_next'


def sweep_running():
    for pid in os.listdir('/proc'):
        try:
            if pid.isdigit() and open(f'/proc/{pid}/cmdline', 'rb').read().split(b'\0')[:2] == [b'./puffer', b'sweep']:
                return True
        except OSError:
            pass
    return False


# CROSSPLAY_NOW=1 starts at once (with the sweep's current top 3) instead of waiting for the sweep to finish.
while sweep_running() and os.environ.get('CROSSPLAY_NOW') != '1':
    time.sleep(300)

scores = {}
for line in open(f'{D}/sweep.log', errors='ignore'):
    m = re.match(r'sweep run=(\d+) score=(\S+)', line)
    if m: scores[int(m.group(1))] = float(m.group(2))
models = {
    'omni1024_2B': glob.glob(f'{N}/omni_master_1024/checkpoints/pokemon/*/0000001999896576.bin')[0],
    'omni1024_1B': glob.glob(f'{N}/omni_master_1024/checkpoints/pokemon/*/0000001000341504.bin')[0],
    'fp3_125M': glob.glob(f'{N}/freepick_omni1024/checkpoints/pokemon/*/0000000125042688.bin')[0],
    'fp4_375M': glob.glob(f'{N}/freepick_league_v2/checkpoints/pokemon/*/0000000375128064.bin')[0],
}
for run in sorted(scores, key=lambda r: -scores[r])[:3]:
    ck = glob.glob(f'{D}/checkpoints/pokemon/sweep_*_{run:04d}')[0]
    models[f'omni2_t{run}'] = sorted(f for f in glob.glob(f'{ck}/*.bin') if not f.endswith('0000000000000000.bin'))[-1]
names = list(models)
out = open(f'{D}/crossplay.txt', 'a')
out.write(f'# {time.strftime("%Y-%m-%d %H:%M")} candidates: ' + ' '.join(f'{k}={v}' for k, v in models.items()) + '\n')


def match(a, b):
    log = f'/workspace/bt/crossplay_{a}_{b}_{os.getpid()}.log'
    subprocess.run(['./puffer_match', 'match', '--headless', '--base.env_name=pokemon',
                    f'--base.load_model_path={models[a]}', f'--base.load_enemy_model_path={models[b]}',
                    f'--env.match_team_pool={D}/pool.ini', '--env.learner_team_pool=None', '--env.native_league=None',
                    '--base.eval_episodes=4096', '--vec.total_agents=2048', '--vec.num_buffers=1', '--metamon.enabled=0'],
                   stdout=open(log, 'w'), stderr=subprocess.STDOUT, check=True)
    m = re.search(r'CUDA_EVAL env=pokemon score=(\S+) .* games=(\d+)', open(log).read())
    out.write(f'{a} vs {b}: {float(m.group(1)):.4f} ({m.group(2)} games, {a} in seat 0)\n'); out.flush()
    return float(m.group(1))


P = {a: {a: 0.5} for a in names}
for i, a in enumerate(names):
    for b in names[i + 1:]:
        P[a][b] = (match(a, b) + 1 - match(b, a)) / 2
        P[b][a] = 1 - P[a][b]
# Nash of the symmetric zero-sum game A = P - 0.5 by fictitious play (both players share the mixture).
A = [[P[a][b] - 0.5 for b in names] for a in names]
counts = [1.0] * len(names)
for _ in range(200000):
    total = sum(counts)
    best = max(range(len(names)), key=lambda i: sum(A[i][j] * counts[j] for j in range(len(names))) / total)
    counts[best] += 1
nash = [c / sum(counts) for c in counts]
out.write('\nP[row beats column], seats averaged:\n' + ' ' * 14 + ''.join(f'{b[:11]:>12}' for b in names) + '\n')
for a in names: out.write(f'{a[:13]:<14}' + ''.join(f'{P[a][b]:12.3f}' for b in names) + '\n')
out.write('\nNash mixture: ' + ', '.join(f'{n} {w:.2f}' for n, w in zip(names, nash) if w > 0.005) + '\n')
out.write('rating = expected win rate vs the Nash mixture (0.5 = in the mixture); mean = mean win rate vs the others\n')
for i in sorted(range(len(names)), key=lambda i: -sum(A[i][j] * nash[j] for j in range(len(names)))):
    vs_nash = 0.5 + sum(A[i][j] * nash[j] for j in range(len(names)))
    mean = sum(P[names[i]][b] for b in names if b != names[i]) / (len(names) - 1)
    out.write(f'  {names[i]:<14} rating {vs_nash:.3f}  mean {mean:.3f}\n')
out.write('CROSSPLAY_DONE\n')
out.close()
