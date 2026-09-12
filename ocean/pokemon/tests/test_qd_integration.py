"""Mocked adapter/process integration contracts, NOT engine or GPU tests."""
import json
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE=Path(__file__).resolve().parents[1]
STUB=r'''
#pragma once
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t obs_t;
typedef struct { double value; } DictItem;
typedef struct { double weights[5],clip; } Ini;
typedef Ini Dict;
static void pk_configure(Ini* i,const char* m) { (void)i;(void)m; }
static Dict* puf_ini_section(Ini* i,const char* s,int n) { (void)s;(void)n;return i; }
static DictItem* dict_find(Dict* i,const char* key) {
    static DictItem item;
    const char* names[]={"paralysis","sleep","offense","defense","reserve"};
    for(int k=0;k<5;k++) if(!strcmp(key+12,names[k])) { item.value=i->weights[k];return &item; }
    return NULL;
}
static double puf_ini_get(Ini* i,const char* s,const char* k) { (void)s;(void)k;return i->clip; }
typedef struct { float* rewards; float* terminals; } Agent;
typedef struct { struct { uint8_t obs[2][640]; } game; Agent agents[2];
    float reward_gamma,episode_return; struct {float episode_return;} log;
    int end,calls;
} Env;
static void pk_audit(Env* e,int inputs) { (void)inputs; assert(e->agents[0].rewards[0]==-e->agents[1].rewards[0]); }
void puf_step(Env* e) {
    e->calls++;
    e->agents[0].rewards[0]=.25f;e->agents[1].rewards[0]=-.25f;
    e->episode_return+=.25f;
    e->agents[0].terminals[0]=e->agents[1].terminals[0]=(float)e->end;
    if(e->end) {
        e->log.episode_return+=e->episode_return;e->episode_return=0;
        memset(e->game.obs,0,sizeof(e->game.obs));
        /* Deliberately nonzero reset potential: terminal must ignore this. */
        e->game.obs[0][0]=1;e->game.obs[0][17]=255;
    } else e->game.obs[0][17]=64;
}
#define PUF_CONFIGURE(i,m) pk_configure(i,m)
'''
HARNESS=r'''
#include "pokemon.h"
int main(int argc,char** argv) {
    Ini ini={0};ini.weights[4]=.5;
    if(argc>1) {
        if(!strcmp(argv[1],"clip")) ini.clip=1;
        else if(!strcmp(argv[1],"nan")) ini.weights[0]=NAN;
        else if(!strcmp(argv[1],"norm")) ini.weights[0]=1;
        PUF_CONFIGURE(&ini,"train");return 0;
    }
    Env env={0};float r[2]={0},t[2]={0};
    for(int p=0;p<2;p++){env.agents[p].rewards=&r[p];env.agents[p].terminals=&t[p];}
    env.reward_gamma=.99f;
    env.game.obs[0][0]=env.game.obs[1][0]=1;env.game.obs[0][17]=128;
    puf_step(&env);assert(env.calls==1 && r[0]==.25f && env.episode_return==.25f);
    PUF_CONFIGURE(&ini,"train");env.game.obs[0][17]=128;
    double before=pk_personality_potential(env.game.obs[0],env.game.obs[1],pk_personality_weights);
    puf_step(&env);
    double after=pk_personality_potential(env.game.obs[0],env.game.obs[1],pk_personality_weights);
    double extra=env.reward_gamma*after-before;
    assert(fabs(r[0]-(.25+extra))<1e-6);
    assert(fabs(env.episode_return-(.5+extra))<1e-6);
    float prior=env.episode_return;env.end=1;
    puf_step(&env);
    assert(t[0]==1 && r[0]==-r[1]);
    assert(fabs(r[0]-(.25-after))<1e-6);
    assert(fabs(env.log.episode_return-(prior+.25-after))<1e-6);
    assert(env.episode_return==0 && env.game.obs[0][17]==255);
    return 0;
}
'''
MOCK=r'''
import configparser,json,pathlib,sys
args=sys.argv[1:]
if args[0]=='train':
    c=configparser.ConfigParser(interpolation=None);c.read('config/pokemon.ini')
    for arg in args[2:]:
        key,value=arg.lstrip('-').split('=',1);section,key=key.replace('-','_').split('.',1)
        if not c.has_section(section):c.add_section(section)
        c[section][key]=value
    assert c['env']['max_updates']=='17'
    assert c['env']['reward_hp_scale']==c['env']['reward_ko_scale']=='0'
    assert c['selfplay']['enabled']=='0'
    print('PK_PERSONALITY_CONFIG v=1 weights='+','.join(c['env']['personality_'+n] for n in ('paralysis','sleep','offense','defense','reserve')))
    path=pathlib.Path(c['base']['checkpoint_dir'])/'pokemon'/c['base']['run_id']
    path.mkdir(parents=True)
    (path/'0000000000000008.bin').write_text('FAKE CHECKPOINT: test fixture only')
    with open(path/'config.ini','w') as f:c.write(f)
else:
    assert args[0]=='eval' and 'env.max_updates=17' in args
    games=int(next(x.split('=')[1] for x in args if x.startswith('--games=')))
    for _ in range(games):
        print('PK_PROFILE '+json.dumps(dict(qd_version=1,score=.5,species_ids=[1,2,3,4,5,6],set_ids=[0,1,2,3,4,5],personality=[0,0,0,0,1])))
'''


class TestWrapperContract(unittest.TestCase):
    def test_build_entry_type_declaration(self):
        self.assertRegex((HERE/'pokemon.h').read_text(), r'typedef\s+.*obs_t')

    def test_wrapper_reset_bookkeeping(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)
            for f in ('pokemon.h','personality.h'):shutil.copyfile(HERE/f,p/f)
            (p/'pokemon_base.h').write_text(STUB);(p/'test.c').write_text(HARNESS)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
            subprocess.run([p/'test'],check=True)
            for flag in ('clip','norm','nan'):
                self.assertNotEqual(subprocess.run([p/'test',flag],capture_output=True).returncode,0)

    def test_profile_additive_json(self):
        source=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint8_t obs[2][640];int teams[2][6],picks,updates,max_updates,result,battle;} PKGame;
static const char* pk_species_labels[150]={"", "One","Two","Three","Four","Five","Six"};
static int pk_species(int set){return set+1;}
static int pk_turn(const int* battle){return *battle;}
#include "profile.h"
int main(void) {
    PKGame g={0};g.picks=6;g.updates=10;g.max_updates=512;g.battle=9;
    g.obs[0][0]=1;g.obs[0][4]=1;
    for(int k=403;k<=408;k++)g.obs[0][k]=6;
    for(int k=0;k<6;k++){g.teams[0][k]=k;g.obs[0][17+32*k]=255;}
    g.obs[0][18]=5;
    PKProfile p={0};PKProfileSummary summary={0};
    pk_profile_step(&p,&g,0);pk_profile_accumulate(&summary,&p,&g,0);
    pk_profile_summary(&summary);pk_profile_emit(&p,&g,0,.5);return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)
            for f in ('profile.h','personality.h'):shutil.copyfile(HERE/f,p/f)
            (p/'test.c').write_text(source)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
            output=subprocess.check_output([p/'test'],text=True)
            row=json.loads(next(x[11:] for x in output.splitlines() if x.startswith('PK_PROFILE ')))
            self.assertEqual(row['species_ids'],[1,2,3,4,5,6]);self.assertEqual(row['set_ids'],list(range(6)))
            self.assertEqual(len(row['descriptors']['species']),149)
            self.assertEqual(row['qd_version'],1);self.assertAlmostEqual(row['personality'][0],-1/6)
            self.assertEqual(row['personality'][4],1)


class TestRunnerContract(unittest.TestCase):
    def test_mock_run_resume_and_input_rejection(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);folder=root/'ocean/pokemon';folder.mkdir(parents=True)
            for name in ('qd.py','cma_qd.py','personality.h','pokemon.h','profile.h'):
                shutil.copyfile(HERE/name,folder/name)
            (folder/'data').mkdir();(folder/'data/catalog.json').write_text('{}')
            (root/'config').mkdir();(root/'config/default.ini').write_text('[base]\n')
            text='[vec]\ntotal_agents=2\n[train]\nminibatch_size=4\n[env]\nlearner_team=None\n[policy]\nhidden_size=8\nnum_layers=1\n'
            (root/'config/pokemon.ini').write_text(text)
            (root/'bank').mkdir();(root/'bank/model.bin').write_text('FAKE FIXTURE')
            (root/'bank/config.ini').write_text(text.replace('learner_team=None','learner_team=0,1,2,3,4,5'))
            (root/'native.ini').write_text('[native]\nbanks=1\n[bank.0]\npath='+str(root/'bank/model.bin')+'\nteam=0,1,2,3,4,5\n')
            for name in ('puffer','pokemon'):
                (root/name).write_text('#!'+sys.executable+' -S\n'+MOCK);(root/name).chmod(0o755)
            cmd=[sys.executable,str(folder/'qd.py'),'run','--native-league','native.ini','--out','out',
                 '--population','4','--generations','1','--train-steps','8','--games','8','--horizon','2','--max-updates','17']
            first=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
            self.assertEqual(first.returncode,0,first.stdout+first.stderr)
            path=root/'out/state.json';state=json.loads(path.read_text())
            self.assertEqual(state['generation'],1);self.assertEqual(state['requested_train_steps'],48)
            cmd[cmd.index('--generations')+1]='2';cmd+=['--resume']
            resumed=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
            self.assertEqual(resumed.returncode,0,resumed.stdout+resumed.stderr)
            state=json.loads(path.read_text());self.assertEqual(state['generation'],2)
            self.assertEqual(state['requested_train_steps'],88)
            (root/'out/panel/0/model.bin').write_text('CHANGED INPUT')
            bad=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
            self.assertNotEqual(bad.returncode,0);self.assertIn('resume contract mismatch',bad.stderr)


if __name__=='__main__':unittest.main()
