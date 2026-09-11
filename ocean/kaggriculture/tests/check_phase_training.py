"""Short real-trainer phase integration gate; never promotes a model."""
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from expansion_experiment import argv, execute, save
from policy_identity import digest
from profit_ablation import trial_values

def main():
    source, out = map(lambda p: Path(p).resolve(), sys.argv[1:])
    if out.exists():
        raise ValueError('Use a new output directory')
    plan = json.loads((source/'plan.json').read_text())
    plan['steps'] = 4194304
    out.mkdir(parents=True)
    hashes = []
    for i, phase in enumerate((0, 0, 2)):
        run_id = f'phase_gate_{out.name}_{i}'
        values = trial_values(plan, dict(seed=42, deadline=240,
                             expansion=0, win=1, phase=phase), out, run_id)
        values['selfplay.eval_pool_size'] = '0'
        checkpoint_dir = out/'checkpoints/kaggriculture'/run_id
        rc, seconds = execute(argv('train', values), out/f'train_{i}.log')
        if rc:
            raise RuntimeError(f'Trainer failed: {rc}')
        checkpoint = sorted(checkpoint_dir.glob('*.bin'))[-1]
        hashes.append(digest(checkpoint))
        print(f'phase={phase} hash={hashes[-1]} seconds={seconds:.1f}', flush=True)
    if hashes[0] != hashes[1]:
        raise AssertionError('Control repeat differs: cannot attribute divergence to phases')
    if hashes[0] == hashes[2]:
        raise AssertionError('Phases did not change trained weights')
    save(out/'PASS.json', dict(hashes=hashes, control_repeat_identical=True,
                              phase_weights_different=True))
    print('PASS: repeat controls identical; phase-on weights differ', flush=True)

if __name__ == '__main__':
    main()
