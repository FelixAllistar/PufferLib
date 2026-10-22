#!/usr/bin/env node
// Create an explicit fresh league; do not start GPU work or overwrite old state.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const report=path.dirname(fileURLToPath(import.meta.url));
const root=path.resolve(report,'../../..');
const league=path.join(root,'leagues/pokemon/rr50m_256x2_20261019');
if(fs.existsSync(league))throw Error('League already exists; inspect/resume it, do not recreate');
const old=JSON.parse(fs.readFileSync(path.join(root,'reports/pokemon_experiments/20261017_roundrobin_50m/experiment.json')));
const binary=path.join(league,'bin/puffer');
const overrides={...Object.fromEntries(Object.entries(old.ppo).map(([k,v])=>['train.'+k,String(v)])),
    'train.learning_rate':'0.0005','train.total_timesteps':'50331648',
    'policy.hidden_size':'256','policy.num_layers':'2',
    'vec.total_agents':'16384','vec.num_buffers':'1','vec.num_threads':'4',
    'vec.hist_policy_hidden_size':'256','vec.hist_policy_num_layers':'2',
    'base.checkpoint_interval':'0','base.load_model_path':'None','base.async':'0',
    'base.reset_every_horizon':'0',
    'env.native_league':'None','env.force_core_combos':'0','env.force_core_prob':'0',
    'env.reset_state_prob':'0','env.reward_hp_scale':'0','env.reward_ko_scale':'0',
    'env.behavior_sleep':'0','env.behavior_paralysis':'0',
    'env.personality_paralysis':'0','env.personality_sleep':'0','env.personality_offense':'0',
    'env.personality_defense':'0','env.personality_reserve':'0',
    'oak.enabled':'0','metamon.enabled':'0','metamon.interval_fraction':'0.1',
    'metamon.games':'2','metamon.final_games':'24','metamon.min_free_gb':'2'};
const args=['ocean/pokemon/freeleague.mjs','create',league,
    '--master','None','--new-policies','0','--teams',path.join(report,'roster.json'),
    '--seed','73','--selfplay','0.5','--steps','50331648','--defer-init',
    '--binary',binary,'--lr-decay-rounds','50','--lr-final-ratio','0.1','--min-free-gb','2','--',
    ...Object.entries(overrides).map(([k,v])=>k+'='+v)];
const result=spawnSync(process.execPath,args,{cwd:root,stdio:'inherit'});
if(result.status!==0)throw Error('League creation failed');
fs.mkdirSync(path.dirname(binary),{recursive:true});
fs.copyFileSync(path.join(root,'puffer'),binary,fs.constants.COPYFILE_EXCL);
fs.chmodSync(binary,0o755);
const state=JSON.parse(fs.readFileSync(path.join(league,'state.json')));
const manifest={created:new Date().toISOString(),league,binary,
    binary_sha256:crypto.createHash('sha256').update(fs.readFileSync(binary)).digest('hex'),
    architecture:state.architecture,members:state.members.length,target_completed_rounds:50,
    steps_per_member_round:50331648,steps_per_full_round:50331648*14,
    total_steps:50331648*14*50,lr_schedule:state.lr_schedule,
    initialization:'Independent random weights, not resized old checkpoints',overrides,args};
fs.writeFileSync(path.join(report,'experiment.json'),JSON.stringify(manifest,null,2)+'\n',{flag:'wx'});
console.log('Prepared '+league+'; 14 fresh 256x2 members; no GPU work started.');
