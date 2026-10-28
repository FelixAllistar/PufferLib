# One-off: trial 0's held-out mirror vs Kakuna (its TaurosV0 / SyntheticRLV2 results exist), then one full line in heldout.txt.
import glob, json, sys
sys.argv = ['x', 'reports/pokemon_experiments/20261026_final_sweep']
exec(open('reports/pokemon_experiments/20261026_final_sweep/sweep_heldout.py').read().split("if len(sys.argv) > 2")[0])
ck = sorted(f for f in glob.glob(f'{D}/checkpoints/pokemon/sweep_*_0000/*.bin') if not f.endswith('0000000000000000.bin'))[-1]
res = {opp: mirror(ck, f'{D}/heldout/t0000_{opp}', opp) for opp in ('Kakuna', 'TaurosV0', 'SyntheticRLV2')}
line = (f'trial    0 heldout mirror Kak {res["Kakuna"][0]:.1f} Tau {res["TaurosV0"][0]:.1f} Syn {res["SyntheticRLV2"][0]:.1f} '
        f'(+/- {res["TaurosV0"][1]:.1f}, {res["TaurosV0"][2]} games each)')
open(f'{D}/heldout.txt', 'a').write(line + '\n'); print(line)
