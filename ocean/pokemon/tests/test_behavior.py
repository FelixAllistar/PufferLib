"""Capped event math and wrapper bookkeeping, with mocked base adapter."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import json
import sys
from types import SimpleNamespace
from test_qd_integration import STUB, HERE
sys.path.insert(0, str(HERE))
from behavior_experiment import train_command, event_summary, summarize


class TestBehavior(unittest.TestCase):
    def test_manual_command_and_paired_summary(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p/'config.ini').write_text('[policy]\nhidden_size=128\nnum_layers=2\n')
            args = SimpleNamespace(trainer='trainer', train_steps=100000000)
            cmd = train_command(args, p/'model.bin', p/'job', 'test', 101, [-.5, 0])
            flags = dict(arg[2:].split('=', 1) for arg in cmd[3:])
            self.assertEqual(flags['env.behavior_sleep'], '-0.5')
            self.assertEqual(flags['env.native_league'], 'None')
            self.assertEqual(flags['selfplay.enabled'], '1')
            self.assertEqual(flags['selfplay.seed'], flags['base.seed'])
            self.assertEqual(flags['train.reward_clip'], '0')
            self.assertNotIn('train.seed', flags)
            control = dict(condition='control', seed=101, quality=.5,
                           behavior_events=[.5, 1], opponent_behavior_events=[.25, 2])
            other = dict(control, condition='sleep_minus', quality=.6, behavior_events=[.25, 1])
            report = summarize([control, other])['conditions']['sleep_minus']
            self.assertEqual(report['behavior_events_matched_control_delta_mean'], [-.25, 0])
            self.assertAlmostEqual(report['quality_matched_control_delta_mean'], .1)

    def test_event_report_rejects_stale_or_invalid_metrics(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            row = dict(behavior_version=1, behavior_events=[1, 6], opponent_behavior_events=[0, 0])
            (p/'panel_000.log').write_text('PK_PROFILE '+json.dumps(row)+'\n')
            self.assertEqual(event_summary(p)['behavior_events_capped'], [1, 1])
            row['behavior_version'] = 0
            (p/'panel_000.log').write_text('PK_PROFILE '+json.dumps(row)+'\n')
            with self.assertRaises(ValueError):
                event_summary(p)
            row['behavior_version'] = 1
            row['behavior_events'] = [1, 7]
            (p/'panel_000.log').write_text('PK_PROFILE '+json.dumps(row)+'\n')
            with self.assertRaises(ValueError):
                event_summary(p)

    def test_non_cancelling_terminal_reward(self):
        source = r'''
#include "pokemon.h"
int main(void) {
    assert(pk_behavior_progress(0,1,0)==1);
    assert(pk_behavior_progress(1,2,0)==0);
    assert(pk_behavior_progress(3,4,1)==0);
    assert(pk_behavior_progress(1,0,0)==0);
    assert(fabs(pk_behavior_progress(0,1,1)-1.0/3)<1e-6);
    Env e={0};float r[2]={0},t[2]={0};
    for(int p=0;p<2;p++){e.agents[p].rewards=&r[p];e.agents[p].terminals=&t[p];}
    pk_behavior_enabled=1;pk_behavior_weights[0]=.5;
    e.behavior_delta[0][0]=1;puf_step(&e);
    assert(r[0]==.75 && r[1]==-.75 && e.episode_return==.75);
    e.behavior_delta[0][0]=0;e.end=1;puf_step(&e);
    assert(r[0]==.25 && e.log.episode_return==1 && e.episode_return==0);
    e.end=0;puf_step(&e);assert(r[0]==.25 && e.episode_return==.25);
    e.end=1;e.behavior_delta[0][0]=1;puf_step(&e);
    assert(r[0]==.75 && e.log.episode_return==2 && e.episode_return==0);
    e.behavior_delta[0][0]=0;e.behavior_delta[1][0]=1;puf_step(&e);
    assert(r[0]==-.25 && r[1]==.25);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            for name in ('pokemon.h', 'personality.h', 'behavior.h'):
                shutil.copyfile(HERE/name, p/name)
            (p/'pokemon_base.h').write_text(STUB)
            (p/'test.c').write_text(source)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(p/'test.c'), '-lm', '-o', str(p/'test')], check=True)
            subprocess.run([p/'test'], check=True)
