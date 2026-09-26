import ctypes as C
import importlib
import json
from pathlib import Path
import resource
import shlex
import subprocess
import sys
import zipfile

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[3]
ENV = ROOT / 'ocean/kaggriculture'
sys.path.insert(0, str(ENV))
fit = importlib.import_module('fit_potential')


@pytest.fixture(scope='module')
def library(tmp_path_factory):
    path = tmp_path_factory.mktemp('potential') / 'potential.so'
    subprocess.run(['cc', '-x', 'c', '-O2', '-shared', '-fPIC',
        str(ENV/'potential.h'), '-lm', '-o', str(path)], check=True, capture_output=True)
    return fit.load_library(path)


def test_features_prediction_and_reset_adjustment(library):
    config = fit.native.CConfig()
    library.kg_config_default(C.byref(config))
    state = library.kg_create(C.byref(config))
    model = fit.Model(magic=b'KGRIDGE1', version=1, features=fit.FEATURE_COUNT,
        state_version=1, gamma=float(np.float32(.99)), intercept=.7)
    rng = np.random.default_rng(73)
    model.weights[:] = rng.normal(0, .01, fit.FEATURE_COUNT)
    model.mean[:] = rng.normal(0, .1, fit.FEATURE_COUNT)
    model.inverse_scale[:] = rng.uniform(.001, 1, fit.FEATURE_COUNT)
    try:
        for step in range(720):
            if step in [0, 200, 512, 718]:
                for player in range(2):
                    x = np.zeros(fit.FEATURE_COUNT, np.float32)
                    library.kag_potential_features(state, player, x.ctypes.data)
                    assert np.isfinite(x).all()
                    assert x[5] == 1 and x[6] == 1
                    assert x[95] == pytest.approx(x[3]*x[0])
                    assert sum(x[i] for i in [187,189,191,193]) == 1
                    for start in [3000., 13000.]:
                        native = library.kag_potential_predict(C.byref(model), state, player, start)
                        expected = model.intercept + np.dot(np.asarray(model.weights),
                            (x-np.asarray(model.mean))*np.asarray(model.inverse_scale))
                        expected -= model.gamma**(718-step) * (start/3000-1)
                        assert native == pytest.approx(expected, abs=3e-4, rel=1e-5)
            if step < 719:
                library.kg_step(state, (fit.native.CAction*2)())
        assert library.kag_potential_predict(C.byref(model), state, 0, 3000) == 0
    finally:
        library.kg_destroy(state)


def test_game_seed_splits():
    a = [fit.group_split(seed) for seed in range(100)]
    assert set(a) == {'train', 'validation', 'test'}
    assert a == [fit.group_split(seed) for seed in range(100)]


def test_exact_comparison_and_readonly_frame():
    native = fit.native
    value = {'x': [1, 2.0, True, None, {'a': 'hello', 'b': []}]}
    assert native.same_value(value, json.loads(json.dumps(value)))
    assert not native.first_difference(value, json.loads(json.dumps(value)))
    for left, right, path in [
            ({'x': [0]}, {'x': [False]}, '$.x[0]'),
            ({'x': [1]}, {'x': [1.0]}, '$.x[0]'),
            ({'x': []}, {'x': [None]}, '$.x.length'),
            ({'x': None}, {}, '$.x'),
            ({'x': 2}, {'x': 3}, '$.x')]:
        assert not native.same_value(left, right)
        assert native.first_difference(left, right)[0][0] == path
    assert native.first_difference({'x': float('nan')}, {'x': float('nan')})
    assert native.first_difference(list(range(20)), [100]*20, limit=3) == [
        ('$[0]', 0, 100), ('$[1]', 1, 100), ('$[2]', 2, 100)]
    obs = dict(step=0, day=0, hour=0, farms=[], private={}, market={}, town={})
    frame = [dict(observation=obs, status='ACTIVE') for _ in range(2)]
    snapshot = native.canonical_replay_frame(frame)
    view = native.canonical_replay_frame(frame, copy=False)
    assert native.same_value(snapshot, view)
    assert view['farms'] is obs['farms']
    assert snapshot['farms'] is not obs['farms']


def test_parallel_preparation_matches_serial_and_reuses_cache(library, tmp_path):
    # Native-generated games exercise the pipeline, not independent rule parity.
    source = tmp_path/'replays.zip'
    with zipfile.ZipFile(source, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for index in range(4):
            config = fit.native.CConfig()
            library.kg_config_default(C.byref(config))
            config.seed = index // 2 + 73
            state = library.kg_create(C.byref(config))
            frames = []
            try:
                for turn in range(720):
                    snapshot = fit.native.c_snapshot(library, state)
                    frame = []
                    for player in range(2):
                        obs = {key:snapshot[key] for key in
                            ['step', 'day', 'hour', 'farms', 'market', 'town']}
                        obs['private'] = snapshot['privates'][player]
                        frame.append(dict(observation=obs, action={'farmer':['PASS']},
                            status='DONE' if snapshot['done'] else 'ACTIVE'))
                    frames.append(frame)
                    if turn < 719:
                        library.kg_step(state, (fit.native.CAction*2)())
                if index == 3:
                    frames[500][0]['observation']['private']['shed']['WHEAT'] += 1
                episode = dict(name='kaggriculture', module_version='1.32.7',
                    configuration={'episodeSteps':720, 'seed':config.seed},
                    info={'EpisodeId':index, 'TeamNames':['a','b']},
                    rewards=[3000,3000], statuses=['DONE','DONE'], steps=frames)
                archive.writestr(f'{index}.json', json.dumps(episode))
            finally:
                library.kg_destroy(state)
    manifests = []
    for workers in [1, 2]:
        output = tmp_path/f'data_{workers}'
        command = [sys.executable, str(ENV/'fit_potential.py'), 'build', str(source),
            '--lib', library._name, '--output', str(output), '--workers', str(workers)]
        subprocess.run(command, check=True, capture_output=True, text=True, timeout=120)
        manifests.append(json.loads((output/'dataset.json').read_text()))
        assert len(manifests[-1]['records']) == 3 and len(manifests[-1]['rejected']) == 1
        assert manifests[-1]['complete']
        again = subprocess.run(command, check=True, capture_output=True, text=True, timeout=120)
        assert json.loads(again.stdout.splitlines()[-1])['cached'] == 3
    assert manifests[0]['records'] == manifests[1]['records']
    assert manifests[0]['rejected'] == manifests[1]['rejected']
    for record in manifests[0]['records']:
        with np.load(tmp_path/'data_1'/record['cache']) as a, \
                np.load(tmp_path/'data_2'/record['cache']) as b:
            assert a.files == b.files
            for key in a.files:
                assert np.array_equal(a[key], b[key]), key


def test_ridge_sweep_is_beta_only():
    result = subprocess.run([sys.executable, str(ENV/'run.py'), 'sweep',
        '--profile', 'ridge', '--dry-run', '--train.learning_rate=0.0003',
        '--train.horizon=512', '--train.minibatch_size=512', '--vec.total_agents=256'],
        cwd=ROOT, check=True, capture_output=True, text=True)
    options = dict(word[2:].split('=',1) for word in shlex.split(result.stdout.strip())[2:])
    assert options['base.load_model_path'].endswith('initial_bc.bin')
    assert options['train.reward_clip'] == '0'
    assert options['env.reward_alive_daily'] == '0'
    assert options['sweep.train.learning_rate.min'] == '0.0003'
    assert options['sweep.train.learning_rate.max'] == '0.0003'
    for key, value in options.items():
        if key.startswith('sweep.') and key.endswith('.min'):
            maximum = options[key[:-3]+'max']
            assert (value != maximum) == (key == 'sweep.env.potential_beta.min')


def test_ridge_and_return_targets(tmp_path):
    records = []
    root = tmp_path/'dataset'
    root.mkdir()
    rng = np.random.default_rng(4)
    for index in range(30):
        x = np.zeros((6, fit.FEATURE_COUNT), np.float32)
        x[:, 3] = rng.normal(size=6)
        x[:, 16] = rng.uniform(0, 1, 6)
        # Known outcome per seat, with samples spanning the whole episode.
        x[:3,16] = x[0,16]
        x[3:,16] = x[3,16]
        path = root/f'{index}.npz'
        np.savez(path, x=x, turn=[0,359,718,0,359,718], player=[0,0,0,1,1,1],
            final_cash=3000*(1+2+4*x[[0,3],16]), starting_money=3000, frames=720)
        records.append(dict(cache=path.name, split=['train','validation','test'][index%3]))
    manifest = dict(complete=True, version=1, library_sha256='synthetic',
        feature_names=fit.feature_names(), records=records)
    (root/'dataset.json').write_text(json.dumps(manifest))
    args = type('Args', (), dict(dataset=root, output=tmp_path/'fit', gamma=1,
        alphas=[.0001,.1,10]))()
    fit.fit(args)
    report = json.loads((args.output/'potential.json').read_text())
    assert report['exported_test']['all']['rmse'] < .001
    assert report['models']['cash_time']['scores']['test']['all']['rmse'] > .5
    model = fit.Model.from_buffer_copy((args.output/'potential.bin').read_bytes())
    assert model.magic == b'KGRIDGE1'
    # At turn t the terminal reward exponent is T-2-t (not T-1-t).
    stats = fit.statistics(root, manifest, .5)
    full = fit.statistics(root, manifest, 1)
    assert stats['train','all']['xy'][0] == pytest.approx(full['train','all']['xy'][0]/3)


@pytest.mark.parametrize('sanitize', [False, True])
def test_native_reward_accounting(tmp_path, sanitize):
    binary = tmp_path/'accounting'
    flags = (['-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer']
        if sanitize else ['-O2'])
    subprocess.run(['cc', '-std=c11', *flags, '-Isrc', '-Iocean/kaggriculture',
        '-Iraylib-5.5_linux_amd64/include', str(Path(__file__).with_suffix('.c')),
        'raylib-5.5_linux_amd64/lib/libraylib.a', '-lGL','-lpthread','-ldl','-lm',
        '-o',str(binary)], cwd=ROOT, check=True, capture_output=True)
    result = subprocess.run([str(binary)], cwd=ROOT, check=True, capture_output=True, timeout=60)
    assert b'cancellation PASS' in result.stdout
    if sanitize:
        return
    model = fit.Model(magic=b'KGRIDGE1', version=1, features=fit.FEATURE_COUNT,
        state_version=1, gamma=.99)
    model.inverse_scale[:] = np.ones(fit.FEATURE_COUNT)
    path = tmp_path/'potential.bin'
    payload = bytes(model)
    for label, data, clip, succeeds in [
            ('valid', payload, '0', True),
            ('truncated', payload[:-1], '0', False),
            ('oversized', payload+b'x', '0', False),
            ('wrong_version', payload[:8]+b'\xff'*4+payload[12:], '0', False),
            ('clipped', payload, '1', False)]:
        path.write_bytes(data)
        result = subprocess.run([str(binary),str(path),clip], cwd=ROOT,
            capture_output=True, timeout=60,
            preexec_fn=lambda:resource.setrlimit(resource.RLIMIT_CORE, (0,0)))
        assert (result.returncode == 0) == succeeds, (label, result.stderr)
