#!/usr/bin/env node
// Local, explicit-budget population training. No QD solver or Python CLI.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const here=path.dirname(fileURLToPath(import.meta.url));
export const ROOT=path.resolve(here,'../..');
const RULES=JSON.parse(fs.readFileSync(path.join(here,'data/freepick.json'),'utf8'));
const VERSION=3;
const STAGES=['base','vec','selfplay','policy','env','train'];
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const fail=message=>{throw Error(message);};
const readJSON=file=>JSON.parse(fs.readFileSync(file,'utf8'));
const exists=file=>fs.existsSync(file);
const none=value=>value===undefined || value===null || value==='' || value==='None';
const number=value=>Number(String(value).replaceAll('_',''));
const safeName=name=>typeof name==='string' && /^[a-zA-Z][a-zA-Z0-9_-]{0,63}$/.test(name);
const timestamp=()=>new Date().toISOString();
export function parseIni(text) {
    const result={};let section='';
    for(let line of text.split(/\r?\n/)) {
        line=line.trim();if(!line || /^[#;]/.test(line))continue;
        if(line.startsWith('[') && line.endsWith(']')) {section=line.slice(1,-1);continue;}
        const eq=line.indexOf('=');if(eq<0 || !section)fail('Malformed INI line: '+line);
        let value=line.slice(eq+1).trim();
        // Native configs quote whole values, not embedded shell expressions.
        if((value.startsWith("'") && value.endsWith("'")) || (value.startsWith('"') && value.endsWith('"')))value=value.slice(1,-1);
        result[section+'.'+line.slice(0,eq).trim()]=value;
    }
    return result;
}
const readIni=file=>parseIni(fs.readFileSync(file,'utf8'));
export function formatIni(config) {
    const sections=new Map();
    for(const [key,value] of Object.entries(config)) {
        if(!/^[a-zA-Z0-9_.]+$/.test(key) || /[\r\n\0]/.test(String(value)))fail('Invalid config key/value '+key);
        const dot=key.lastIndexOf('.');if(dot<1)fail('Config keys must be section.key: '+key);
        const section=key.slice(0,dot),field=key.slice(dot+1);
        if(!sections.has(section))sections.set(section,[]);
        sections.get(section).push(field+' = '+String(value));
    }
    return [...sections].map(([section,rows])=>'['+section+']\n'+rows.join('\n')).join('\n\n')+'\n';
}
function atomic(file,text) {
    fs.mkdirSync(path.dirname(file),{recursive:true});
    const tmp=file+'.tmp-'+process.pid+'-'+crypto.randomBytes(5).toString('hex');
    const fd=fs.openSync(tmp,'wx',0o600);
    try {fs.writeFileSync(fd,text);fs.fsyncSync(fd);} finally {fs.closeSync(fd);}
    fs.renameSync(tmp,file);
    const dir=fs.openSync(path.dirname(file),'r');try {fs.fsyncSync(dir);}finally{fs.closeSync(dir);}
}
const jsonWrite=(file,value)=>atomic(file,JSON.stringify(value,null,2)+'\n');
function defaults() {
    const all={...readIni(path.join(ROOT,'config/default.ini')),...readIni(path.join(ROOT,'config/pokemon.ini'))};
    return Object.fromEntries(Object.entries(all).filter(([key])=>STAGES.includes(key.split('.')[0])));
}
function overrides(args) {
    const result={};
    for(const arg of args) {
        const eq=arg.indexOf('=');if(eq<1)fail('Expected section.key=value: '+arg);
        const key=arg.slice(0,eq);
        if(!STAGES.includes(key.split('.')[0]) || !/^[a-zA-Z0-9_]+\.[a-zA-Z0-9_]+$/.test(key))fail('Unsupported override '+key);
        result[key]=arg.slice(eq+1);
    }
    return result;
}
export function speciesID(value) {
    if(typeof value==='number' || /^\d+$/.test(String(value))) {
        const n=number(value);if(Number.isInteger(n) && n>=1 && n<=149)return n;
    }
    const normalized=String(value).toLowerCase().replace(/[^a-z0-9]/g,'');
    const found=RULES.species.find(s=>s.id && s.name.toLowerCase().replace(/[^a-z0-9]/g,'')===normalized);
    if(!found)fail('Unknown or banned Gen1 OU species: '+value);return found.id;
}
export function constraint(team,lead='None') {
    if(none(team))team=[];
    else if(typeof team==='string')team=team.replace(/^species:/,'').split(',');
    if(!Array.isArray(team) || team.length>6)fail('Team must contain 1..6 distinct species or be None');
    const ids=team.map(speciesID);
    if(new Set(ids).size!==ids.length)fail('Duplicate species in team');
    const leadID=none(lead) || lead===0 || lead==='0'?0:speciesID(lead);
    if(ids.length===6 && leadID && !ids.includes(leadID))fail('Lead is not in the required team');
    return {team:ids.length?'species:'+ids.join(','):'None',lead:leadID?String(leadID):'None'};
}
export function parameterCount(hidden,layers) {
    const aug=n=>(n+8)&~7;
    const inputs=[318,454,800,326,318,hidden],mids=[64,64,hidden,64,64,hidden],outs=[32,32,hidden,32,32,260];
    return inputs.reduce((n,input,i)=>n+mids[i]*aug(input)+outs[i]*aug(mids[i]),layers*3*hidden*hidden);
}
export function checkpoint(file,expected) {
    file=path.resolve(ROOT,file);
    if(!fs.statSync(file).isFile())fail('Not a checkpoint file: '+file);
    const config=readIni(path.join(path.dirname(file),'config.ini'));
    if(number(config['env.abi_version'])!==3 || number(config['env.policy_version'])!==3 || config['env.rules_sha']!==RULES.sha256)
        fail('Incompatible checkpoint (requires semantic free-pick ABI/policy 3): '+file);
    const hidden=number(config['policy.hidden_size']),layers=number(config['policy.num_layers']);
    if(!Number.isInteger(hidden) || hidden<8 || hidden%8 || hidden>4096 || !Number.isInteger(layers) || layers<1 || layers>32)fail('Invalid checkpoint architecture');
    if(expected && (hidden!==expected.hidden || layers!==expected.layers))fail('League checkpoint architecture mismatch: '+file);
    const data=fs.readFileSync(file);
    if(data.length!==4*parameterCount(hidden,layers))fail('Checkpoint byte count does not match its semantic architecture: '+file);
    for(let i=0;i<data.length;i+=4)if(!Number.isFinite(data.readFloatLE(i)))fail('Non-finite checkpoint weights: '+file);
    const binding=constraint(config['env.learner_team'],config['env.learner_lead']);
    return {path:file,sha256:sha(data),config_sha256:sha(fs.readFileSync(path.join(path.dirname(file),'config.ini'))),hidden,layers,config,...binding};
}
export function bindingConfig(base,member) {
    const config={...base,...member.overrides,
        'env.learner_team':member.team,'env.learner_lead':member.lead,
        'env.opponent_team':member.team,'env.opponent_lead':member.lead,
        'env.abi_version':'3','env.policy_version':'3','env.rules_sha':RULES.sha256,
        'base.env_name':'pokemon','base.async':'0','base.world_size':'1','base.rank':'0',
        'vec.num_buffers':'1','vec.gpu_env':'0','vec.action_mask_size':'168',
        'env.team_selection':'1','selfplay.enabled':'0','base.load_enemy_model_path':'None',
        'selfplay.opponent_pool':'None','selfplay.opponent_league':'None','selfplay.eval_pool_size':'0',
        'base.eval_epoch_mult':'0','base.wandb':'False'};
    if(member.team!=='None' || member.lead!=='None') {
        config['env.force_core_combos']='0';config['env.force_core_prob']='0';config['env.reset_state_prob']='0';
    }
    return config;
}
function validateConfig(config,architecture) {
    if(number(config['policy.hidden_size'])!==architecture.hidden || number(config['policy.num_layers'])!==architecture.layers)
        fail('Per-member architecture overrides are not supported in one resident league');
    for(const key of ['vec.total_agents','vec.num_threads','train.horizon','train.minibatch_size']) {
        const n=number(config[key]);if(!Number.isInteger(n) || n<1)fail('Expected positive integer '+key);
    }
    const agents=number(config['vec.total_agents']),horizon=number(config['train.horizon']),batch=number(config['train.minibatch_size']);
    if(agents%2 || batch%horizon || batch>agents*horizon)fail('Require even total_agents and horizon-divisible minibatch_size <= total_agents*horizon');
    for(const key of ['env.force_core_prob','env.reset_state_prob']) {
        const n=number(config[key]??0);if(!Number.isFinite(n) || n<0 || n>1)fail(key+' must be in [0,1]');
    }
    if(number(config['env.force_core_combos']) && number(config['env.reset_state_prob']))fail('Core forcing and snapshot resets are mutually exclusive');
}
function storedCheckpoint(info) {
    return {path:info.path,sha256:info.sha256,config_sha256:info.config_sha256,team:info.team,lead:info.lead};
}
function verifyStored(stored,architecture) {
    const current=checkpoint(stored.path,architecture);
    if(current.sha256!==stored.sha256 || current.config_sha256!==stored.config_sha256)fail('A frozen checkpoint/config changed on disk: '+stored.path);
    return current;
}
function saveState(directory,state) {state.updated=timestamp();jsonWrite(path.join(directory,'state.json'),state);}
function loadState(directory) {
    const state=readJSON(path.join(directory,'state.json'));
    if(state.version!==VERSION || state.rules_sha!==RULES.sha256)fail('Incompatible league version or legality rules; create a fresh league');
    return state;
}
function lock(directory) {
    const file=path.join(directory,'.lock');
    if(exists(file)) {
        const old=readJSON(file);let alive=true;
        try {process.kill(old.pid,0);}catch(e){if(e.code==='ESRCH')alive=false;}
        if(old.host!==os.hostname() || alive)fail('League is locked by '+old.host+' PID '+old.pid);
        fs.unlinkSync(file); // Only a verified dead owner's lock, never artifacts.
    }
    const fd=fs.openSync(file,'wx',0o600);fs.writeFileSync(fd,JSON.stringify({pid:process.pid,host:os.hostname(),created:timestamp()}));fs.closeSync(fd);
    return ()=>{if(exists(file) && readJSON(file).pid===process.pid)fs.unlinkSync(file);};
}
let child=null,interrupted=false,liveOutput=false;
async function run(args,logFile) {
    if(interrupted)fail('Interrupted; resume this league to continue');
    fs.mkdirSync(path.dirname(logFile),{recursive:true});
    const fd=fs.openSync(logFile,'a');
    fs.writeSync(fd,'\nCOMMAND '+JSON.stringify(args)+'\n');
    try {
        await new Promise((resolve,reject)=>{
            // A pipe/tee makes isatty false and disables the native dashboard.
            // util-linux script supplies a PTY and records its output, including
            // ANSI redraws, without logging keyboard input.
            const quote=value=>"'"+String(value).replaceAll("'","'\\''")+"'";
            child=liveOutput?
                spawn('script',['-q','-e','-f','-a','-E','never','-c','exec '+args.map(quote).join(' '),logFile],
                    {cwd:ROOT,stdio:'inherit'}):
                spawn(args[0],args.slice(1),{cwd:ROOT,stdio:['ignore',fd,fd]});
            child.once('error',reject);
            child.once('close',(code,signal)=>code===0?resolve():reject(Error('Command failed ('+(signal||code)+'); see '+logFile)));
        });
    } finally {child=null;fs.closeSync(fd);}
    if(interrupted)fail('Interrupted; resume this league to continue');
}
function trainCommand(directory,state,member,config,runID,load,steps,league='None',selfplay=1) {
    const work=path.join(directory,'work');
    const actual={...config,'base.run_id':runID,'base.seed':String(member.seed),
        'base.checkpoint_dir':path.join(work,'checkpoints'),'base.log_dir':path.join(work,'logs'),
        'base.load_model_path':load,'train.total_timesteps':String(steps),
        'env.native_league':league,'env.expert_fraction':String(1-selfplay),
        'vec.num_frozen_banks':'0','vec.frozen_bank_pct':'0'};
    const args=[state.binary,'train','pokemon',...Object.entries(actual).map(([key,value])=>key+'='+value)];
    return {args,config:actual,checkpoint:path.join(work,'checkpoints','pokemon',runID,String(steps).padStart(16,'0')+'.bin'),
        log:path.join(directory,'logs',runID+'.log')};
}
function publish(directory,member,label,source,config) {
    const dir=path.join(directory,'members',member.name,label);
    fs.mkdirSync(dir,{recursive:true});
    const file=path.join(dir,'weights.bin');
    if(exists(file))fail('Refusing to overwrite an existing member version '+dir);
    atomic(file,fs.readFileSync(source));
    if(exists(source+'.emag'))atomic(file+'.emag',fs.readFileSync(source+'.emag'));
    atomic(path.join(dir,'config.ini'),formatIni({...config,'env.learner_team':member.team,'env.learner_lead':member.lead}));
    return storedCheckpoint(checkpoint(file));
}

export function nativeManifest(members,architecture) {
    if(!members.length || members.length>32)fail('Native export needs 1..32 members');
    const config={'native.version':'3','native.banks':String(members.length),'native.hidden_size':String(architecture.hidden),
        'native.num_layers':String(architecture.layers),'native.rules_sha':RULES.sha256};
    members.forEach((member,i)=>{
        const c=member.current||member;
        config['bank.'+i+'.name']=member.name||String(i);
        config['bank.'+i+'.path']=c.path;config['bank.'+i+'.team']=member.team??c.team;config['bank.'+i+'.lead']=member.lead??c.lead;
    });
    return formatIni(config);
}

export function planCreate(options={},overrideArgs=[]) {
    const common={...defaults(),...overrides(overrideArgs)};
    const seed=number(options.seed??73);
    if(!Number.isInteger(seed) || seed<0 || seed>0x7fffffff)fail('seed must be a nonnegative 31-bit integer');
    const master=none(options.master)?null:checkpoint(options.master);
    if(master) {
        if(!overrideArgs.some(x=>x.startsWith('policy.hidden_size=')))common['policy.hidden_size']=String(master.hidden);
        if(!overrideArgs.some(x=>x.startsWith('policy.num_layers=')))common['policy.num_layers']=String(master.layers);
    }
    const architecture={hidden:number(common['policy.hidden_size']),layers:number(common['policy.num_layers'])};
    if(!Number.isInteger(architecture.hidden) || architecture.hidden<8 || architecture.hidden%8 || architecture.hidden>4096 ||
       !Number.isInteger(architecture.layers) || architecture.layers<1 || architecture.layers>32)fail('Invalid policy architecture');
    if(master && (master.hidden!==architecture.hidden || master.layers!==architecture.layers))fail('Master architecture differs from requested policy');
    let specs=[];
    if(options.teams) {
        const presets={smogon:'sample_compositions.json','experts-three':'experts_three_compositions.json'};
        const source=readJSON(presets[options.teams]?path.join(here,presets[options.teams]):path.resolve(ROOT,options.teams));
        specs=Array.isArray(source)?source:source.members;
        if(!Array.isArray(specs))fail('Team file needs a members array');
    }
    const fresh=number(options['new-policies']??(options.teams?1:2));
    if(!Number.isInteger(fresh) || fresh<0 || fresh>32)fail('new-policies must be an integer in [0,32]');
    specs=[...Array.from({length:fresh},(_,i)=>({name:i?'free_'+String(i+1).padStart(2,'0'):'master',team:'None'})),...specs];
    for(const [i,source] of (options.anchor||[]).entries()) {
        const cp=checkpoint(source,architecture);
        specs.push({name:'anchor_'+String(i+1).padStart(2,'0'),team:cp.team,lead:cp.lead,checkpoint:cp.path,trainable:false});
    }
    if(!specs.length || specs.length>32)fail('A league needs 1..32 total members');
    const members=specs.map((spec,i)=>{
        if(!safeName(spec.name))fail('Invalid member name: '+spec.name);
        if(spec.moves || spec.sets)fail('This league learns moves freely; specify species in team, not moves/sets');
        const binding=constraint(spec.team,spec.lead);
        const init=spec.checkpoint===undefined?master:none(spec.checkpoint)?null:checkpoint(spec.checkpoint,architecture);
        const member={name:spec.name,...binding,trainable:spec.trainable!==false,seed:(seed+i*104729)%0x80000000,
            overrides:overrides(Object.entries(spec.overrides||{}).map(([k,v])=>k+'='+v)),
            source:spec.source||null,initializer:init?storedCheckpoint(init):null,current:null,init_attempts:0};
        if(!member.trainable && !init)fail('Frozen anchors require a checkpoint');
        const config=bindingConfig(common,member);validateConfig(config,architecture);
        return member;
    });
    if(new Set(members.map(m=>m.name)).size!==members.length)fail('Member names must be unique');
    if(!members.some(m=>m.trainable))fail('At least one league member must be trainable');
    const selfplay=number(options.selfplay??(members.length===1?1:0.5));
    if(!Number.isFinite(selfplay) || selfplay<0 || selfplay>1 || (members.length===1 && selfplay!==1))fail('selfplay must be in [0,1], and 1 for a single-member league');
    const steps=number(options.steps??10485760);
    if(!Number.isSafeInteger(steps) || steps<1)fail('steps must be a positive integer');
    return {version:VERSION,rules_sha:RULES.sha256,created:timestamp(),phase:'creating',round:0,seed,architecture,
        binary:path.resolve(ROOT,options.binary||'puffer'),evaluator:path.resolve(ROOT,options.evaluator||'build/pokemon/pokemon-league-eval'),
        config:common,schedule:{steps,selfplay},members,pending:null,rounds:[]};
}
async function initialize(directory,state) {
    for(const member of state.members) {
        if(member.current) {verifyStored(member.current,state.architecture);continue;}
        const config=bindingConfig(state.config,member);
        const attempt=++member.init_attempts;saveState(directory,state);
        const label='initial-a'+String(attempt).padStart(3,'0');
        console.log('Creating '+member.name+' ('+(member.initializer?'checkpoint-seeded':'fresh random')+', '+member.team+', lead='+member.lead+')');
        if(member.initializer) {
            verifyStored(member.initializer,state.architecture);
            member.current=publish(directory,member,label,member.initializer.path,config);
        } else {
            // One zero-learning-rate minibatch uses exactly the native random
            // initializer and serializer. It is not charged as league training.
            const initConfig={...config,'vec.total_agents':'8','vec.num_threads':'1','train.horizon':'8','train.minibatch_size':'64',
                'train.learning_rate':'0','train.emag_kl_coef':'0','train.total_timesteps':'64',
                'env.force_core_combos':'0','env.force_core_prob':'0','env.reset_state_prob':'0','env.max_updates':'1',
                'base.cudagraphs':'-1','base.checkpoint_interval':'1','base.eval_epoch_mult':'0'};
            const cmd=trainCommand(directory,state,member,initConfig,'init-'+member.name+'-a'+attempt,'None',64);
            jsonWrite(path.join(directory,'plans',cmd.config['base.run_id']+'.json'),cmd);
            await run(cmd.args,cmd.log);
            checkpoint(cmd.checkpoint,state.architecture);
            member.current=publish(directory,member,label,cmd.checkpoint,config);
        }
        saveState(directory,state);
    }
    state.phase='ready';saveState(directory,state);
    console.log('League ready: '+state.members.length+' members; no training rounds run.');
}
export function scheduleRound(directory,state,steps,selfplay) {
    const round=state.round+1;
    if(!Number.isSafeInteger(steps) || steps<1 || !Number.isFinite(selfplay) || selfplay<0 || selfplay>1)fail('Invalid round steps/selfplay');
    const snapshot=state.members.map(m=>({name:m.name,team:m.team,lead:m.lead,trainable:m.trainable,current:m.current}));
    for(const member of snapshot)verifyStored(member.current,state.architecture);
    const plans={};
    for(const member of state.members.filter(m=>m.trainable)) {
        const config=bindingConfig(state.config,member);validateConfig(config,state.architecture);
        if(number(config['env.force_core_combos'])) {
            const previous=checkpoint(member.current.path,state.architecture).config;
            const pool=value=>value==='all'?'all':String(value).split(',').map(number).sort((a,b)=>a-b).join(',');
            if(number(previous['env.core_seed'])===number(config['env.core_seed']) && pool(previous['env.core_pool'])===pool(config['env.core_pool']) &&
               number(previous['env.force_core_combos']) && previous['env.core_next_assigned']!==undefined) {
                config['env.core_start_assigned']=previous['env.core_next_assigned'];
                config['env.core_start_drafted']=previous['env.core_next_drafted'];
            }
        }
        const batch=number(config['vec.total_agents'])*number(config['train.horizon']);
        if(steps%batch)fail('Round steps must be divisible by total_agents*horizon='+batch+' for '+member.name+' (no silently shortened budgets)');
        const opponents=snapshot.filter(m=>m.name!==member.name);
        if(selfplay<1 && !opponents.length)fail('No round-robin opponents; use selfplay=1');
        const expertEnvs=Math.trunc(Math.fround(Math.fround(1-selfplay)*(number(config['vec.total_agents'])/2)));
        if(selfplay<1 && expertEnvs<opponents.length)fail('Too few expert environments for every opponent; increase vec.total_agents or reduce selfplay');
        const learnerAgents=number(config['vec.total_agents'])-expertEnvs;
        if(number(config['train.minibatch_size'])>learnerAgents*number(config['train.horizon']))fail('minibatch_size exceeds learner transitions per rollout for '+member.name);
        const perBank=Math.floor(expertEnvs/Math.max(1,opponents.length)),extra=expertEnvs%Math.max(1,opponents.length);
        plans[member.name]={config,opponents:opponents.map(m=>m.name),attempts:0,learner_steps:steps*learnerAgents/number(config['vec.total_agents']),
            environment_counts:{selfplay:number(config['vec.total_agents'])/2-expertEnvs,
                opponents:Object.fromEntries(opponents.map((m,i)=>[m.name,selfplay<1?perBank+(i<extra?1:0):0]))}};
    }
    return {round,steps,selfplay,schedule:'sequential',started:timestamp(),snapshot,plans,completed:{}};
}
function planMember(directory,state,member,pending,attempt) {
    const plan=pending.plans[member.name];
    const bank=path.join(directory,'rounds','r'+String(pending.round).padStart(6,'0'),member.name+'-opponents.ini');
    const opponents=plan.opponent_snapshot || state.members.filter(m=>plan.opponents.includes(m.name))
        .map(m=>({name:m.name,team:m.team,lead:m.lead,current:pending.completed[m.name]||m.current}));
    const runID='r'+String(pending.round).padStart(6,'0')+'-'+member.name+'-a'+String(attempt).padStart(3,'0');
    return {bank,opponents,...trainCommand(directory,state,member,plan.config,runID,
        pending.snapshot.find(m=>m.name===member.name).current.path,pending.steps,pending.selfplay<1?bank:'None',pending.selfplay)};
}
async function trainRounds(directory,state,options) {
    if(state.phase==='creating')await initialize(directory,state);
    const rounds=number(options.rounds??1);
    if(!Number.isInteger(rounds) || rounds<1)fail('rounds must be a positive integer');
    if(state.pending && ((options.steps!==undefined && number(options.steps)!==state.pending.steps) ||
       (options.selfplay!==undefined && number(options.selfplay)!==state.pending.selfplay)))fail('Cannot change an unfinished round; resume it with its saved schedule');
    const steps=number(options.steps??state.pending?.steps??state.schedule.steps);
    const selfplay=number(options.selfplay??state.pending?.selfplay??state.schedule.selfplay);
    for(let i=0;i<rounds;i++) {
        if(!state.pending) {state.pending=scheduleRound(directory,state,steps,selfplay);saveState(directory,state);}
        const pending=state.pending;
        for(const frozen of pending.snapshot)verifyStored(frozen.current,state.architecture);
        // Also upgrade unfinished legacy rounds: already completed members become
        // current immediately; unfinished stints bind the latest roster below.
        for(const member of state.members)if(pending.completed[member.name]) {
            verifyStored(pending.completed[member.name],state.architecture);
            member.current=pending.completed[member.name];
        }
        pending.schedule='sequential';saveState(directory,state);
        console.log('Round '+pending.round+': '+pending.steps+' total agent-steps/member, current-selfplay env share '+pending.selfplay);
        for(const member of state.members.filter(m=>m.trainable)) {
            if(pending.completed[member.name]) {verifyStored(pending.completed[member.name],state.architecture);continue;}
            const plan=pending.plans[member.name];
            // Persist the stint's opponent versions before launching. A retry
            // uses exactly these versions, even after process interruption.
            if(!plan.opponent_snapshot)plan.opponent_snapshot=state.members.filter(m=>plan.opponents.includes(m.name))
                .map(m=>({name:m.name,team:m.team,lead:m.lead,current:m.current}));
            for(const opponent of plan.opponent_snapshot)verifyStored(opponent.current,state.architecture);
            const attempt=++plan.attempts;saveState(directory,state);
            const cmd=planMember(directory,state,member,pending,attempt);
            if(pending.selfplay<1)atomic(cmd.bank,nativeManifest(cmd.opponents,state.architecture));
            jsonWrite(path.join(directory,'plans',cmd.config['base.run_id']+'.json'),cmd);
            console.log('  Training '+member.name+' vs '+cmd.opponents.length+' frozen peers; log '+cmd.log);
            await run(cmd.args,cmd.log);
            const trained=checkpoint(cmd.checkpoint,state.architecture);
            if(trained.team!==member.team || trained.lead!==member.lead)fail('Trainer lost member team/lead binding');
            const label='r'+String(pending.round).padStart(6,'0')+'-a'+String(attempt).padStart(3,'0');
            pending.completed[member.name]=publish(directory,member,label,cmd.checkpoint,trained.config);
            member.current=pending.completed[member.name];
            saveState(directory,state);
            console.log('  Published '+member.name+' immediately; the next member trains against this version.');
        }
        state.round=pending.round;state.schedule={steps:pending.steps,selfplay:pending.selfplay};
        pending.finished=timestamp();state.rounds.push(pending);state.pending=null;saveState(directory,state);
        console.log('Completed sequential round '+state.round+'.');
        if(options.evaluate)await evaluate(directory,state,{games:options.evaluate});
    }
}
function selectMembers(state,text) {
    if(!text)return state.members;
    const names=text.split(',');if(new Set(names).size!==names.length)fail('Duplicate member selection');
    return names.map(name=>state.members.find(m=>m.name===name)||fail('Unknown member '+name));
}
async function evaluate(directory,state,options) {
    if(state.phase!=='ready')fail('Finish league creation before evaluation');
    const games=number(options.games??128),seed=number(options.seed??state.seed);
    if(!Number.isInteger(games) || games<2 || games%2)fail('games must be positive and even (balanced seats)');
    if(!Number.isInteger(seed) || seed<0 || seed>0x7fffffff)fail('Invalid evaluation seed');
    const members=selectMembers(state,options.members);
    if(members.length<2)fail('Evaluation needs at least two members');
    for(const member of members)verifyStored(member.current,state.architecture);
    const plan={round:state.round,games,seed,sampling:options.deterministic?'argmax':'stochastic',
        members:members.map(m=>({name:m.name,current:m.current}))};
    const id='r'+String(state.round).padStart(6,'0')+'-'+sha(JSON.stringify(plan)).slice(0,12);
    const file=path.join(directory,'evaluations',id+'.json');
    const result=exists(file)?readJSON(file):{...plan,pairs:[],started:timestamp()};
    const commands=[];
    for(let i=0;i<members.length;i++)for(let j=i+1;j<members.length;j++) {
        const a=members[i],b=members[j];
        if(result.pairs.some(p=>p.a===a.name && p.b===b.name))continue;
        const pairSeed=(seed+i*104729+j*7919)>>>0;
        const args=[state.evaluator,'eval',a.current.path,b.current.path,'--games='+games,'--seed='+pairSeed,'--profile-both-json'];
        if(options.deterministic)args.push('--deterministic');
        const log=path.join(directory,'evaluations',id+'-'+a.name+'-vs-'+b.name+'.log');
        commands.push({args,log});
        if(options['dry-run'])continue;
        console.log('Evaluating '+a.name+' vs '+b.name+' ('+games+' games, balanced seats)');
        await run(args,log);
        const matches=[...fs.readFileSync(log,'utf8').matchAll(/games=(\d+) W=(\d+) D=(\d+) L=(\d+) score=[\d.]+ conservative_95%=\[[^\]]+\] timeouts=(\d+)/g)];
        const match=matches.at(-1);if(!match || number(match[1])!==games)fail('Missing complete evaluation result in '+log);
        const wins=number(match[2]),draws=number(match[3]),losses=number(match[4]);
        if(wins+draws+losses!==games)fail('Evaluation game counts do not add up');
        result.pairs.push({a:a.name,b:b.name,games,wins,draws,losses,score:(wins+draws/2)/games,timeouts:number(match[5]),log});
        jsonWrite(file,result);
    }
    if(options['dry-run']) {console.log(JSON.stringify({plan,commands},null,2));return;}
    result.finished=timestamp();jsonWrite(file,result);
    console.log('Evaluation saved: '+file);
    for(const member of members) {
        let score=0,n=0;
        for(const pair of result.pairs) {
            if(pair.a===member.name) {score+=pair.score;n++;}
            if(pair.b===member.name) {score+=1-pair.score;n++;}
        }
        console.log('  '+member.name+': mean peer score '+(score/n).toFixed(4));
    }
}
function status(state) {
    console.log('League '+state.phase+'; completed round '+state.round+'; '+state.members.length+' members; H='+state.architecture.hidden+' L='+state.architecture.layers);
    console.log('Default round: '+state.schedule.steps+' total agent-steps/member; current-selfplay env share '+state.schedule.selfplay);
    if(state.pending)console.log('Unfinished round '+state.pending.round+': '+Object.keys(state.pending.completed).length+'/'+Object.keys(state.pending.plans).length+' members complete');
    for(const member of state.members)console.log('  '+member.name+(member.trainable?'':' [frozen]')+' | '+member.team+' | lead='+member.lead+' | '+(member.current?.path||'not initialized'));
}
function help() {
    console.log(`Pokemon semantic free-pick league

  league.sh create DIR [--master PATH|None] [--new-policies N] [--teams smogon|experts-three|FILE]
                       [--anchor PATH ...] [--seed N] [--selfplay FRACTION]
                       [--steps N] [--defer-init] [--live|--quiet] [--dry-run] [-- section.key=value ...]
  league.sh train DIR [--rounds N] [--steps N] [--selfplay FRACTION]
                      [--evaluate GAMES] [--live|--quiet] [--dry-run]
  league.sh resume DIR [same options as train]
  league.sh eval DIR [--games N] [--members NAME,...] [--seed N] [--deterministic]
  league.sh export DIR [--members NAME,...] [--output FILE] [--force]
  league.sh status DIR

--master seeds every new trainable member; None gives independent random policies.
--new-policies counts unrestricted members (first named master). Default: 2, or 1
with --teams. smogon adds all 13 sample compositions with fixed leads; moves stay free.
experts-three supplies the three compositions from the previous master experiment.
Custom JSON: {"members":[{"name":"jynx","team":[...],"lead":"Jynx","overrides":{...}}]}.
Members train in roster order and publish immediately: later members face the updated
earlier members. Opponents freeze only within a member's stint (including retries),
mixed with current self-play. Steps count total agent transitions, not just learner
transitions; environment counts are saved in the round plan. Budgets must divide
total_agents*horizon exactly. Optimizer state restarts for each member/round.
Resume keeps completed members and retries an interrupted member from its round-start
checkpoint. Earlier checkpoints, incomplete attempts, and old leagues are retained.
Interactive terminals show the native dashboard and retain logs automatically.
--live forces this mode (requires util-linux script); --quiet logs output only.
--defer-init creates the league plan without GPU work; train initializes it first.
See ocean/pokemon/FREEPICK.md for core-reset/master experiments and examples.`);
}
function parseArgs(argv) {
    const positional=[],options={},raw=[];let tail=false;
    const flags=new Set(['dry-run','force','deterministic','live','quiet','defer-init']);
    for(let i=0;i<argv.length;i++) {
        const arg=argv[i];
        if(tail){raw.push(arg);continue;}
        if(arg==='--'){tail=true;continue;}
        if(!arg.startsWith('--')){positional.push(arg);continue;}
        const eq=arg.indexOf('='),key=arg.slice(2,eq<0?undefined:eq);
        let value=flags.has(key)?true:eq<0?argv[++i]:arg.slice(eq+1);
        if(value===undefined || (typeof value==='string' && value.startsWith('--')))fail('Missing value for --'+key);
        if(key==='anchor')(options.anchor??=[]).push(value);
        else {if(key in options)fail('Repeated option --'+key);options[key]=value;}
    }
    return {positional,options,raw};
}
export async function main(argv=process.argv.slice(2)) {
    if(!argv.length || argv.includes('--help') || argv[0]==='help') {help();return;}
    const {positional,options,raw}=parseArgs(argv);
    const [command,dir]=positional;
    if(!dir || positional.length!==2)fail('Expected COMMAND DIR; see --help');
    const allowed={create:['master','new-policies','teams','anchor','seed','selfplay','steps','dry-run','binary','evaluator','defer-init','live','quiet'],
        train:['rounds','steps','selfplay','evaluate','dry-run','live','quiet'],resume:['rounds','steps','selfplay','evaluate','dry-run','live','quiet'],
        eval:['games','members','seed','deterministic','dry-run'],export:['members','output','force'],status:[]};
    if(!allowed[command])fail('Unknown command '+command);
    for(const key of Object.keys(options))if(!allowed[command].includes(key))fail('Unsupported '+command+' option --'+key);
    if(options.live && options.quiet)fail('Choose --live or --quiet, not both');
    liveOutput=!options.quiet && (Boolean(options.live) || Boolean(process.stdout.isTTY));
    if(raw.length && command!=='create')fail('Training configuration is saved at creation; create a new league to change common overrides');
    const directory=path.resolve(ROOT,dir);
    if(command==='create') {
        const state=planCreate(options,raw);
        if(options['dry-run']) {console.log(JSON.stringify(state,null,2));return;}
        if(exists(directory) && fs.readdirSync(directory).length)fail('Directory is not empty; use resume for an existing league');
        fs.mkdirSync(directory,{recursive:true});const unlock=lock(directory);
        try {
            saveState(directory,state);
            if(options['defer-init'])console.log('League prepared: '+state.members.length+' members; no GPU work. Run train to initialize and train.');
            else await initialize(directory,state);
        }finally{unlock();}
        return;
    }
    const state=loadState(directory);
    if(command==='status'){status(state);return;}
    if(command==='export') {
        if(state.phase!=='ready')fail('Finish creation before export');
        const selected=selectMembers(state,options.members);
        for(const member of selected)verifyStored(member.current,state.architecture);
        const out=options.output?path.resolve(ROOT,options.output):path.join(directory,'native.ini');
        if(exists(out) && !options.force)fail('Output exists; use --force to replace '+out);
        atomic(out,nativeManifest(selected,state.architecture));
        console.log('Exported '+selected.length+' current frozen opponents (completed round '+state.round+
            (state.pending?', partial round '+state.pending.round:'')+'): '+out);
        return;
    }
    if(options['dry-run']) {
        if(command==='eval'){await evaluate(directory,state,options);return;}
        if(state.phase!=='ready')fail('Resume initialization before planning a training round');
        const pending=state.pending||scheduleRound(directory,state,number(options.steps??state.schedule.steps),number(options.selfplay??state.schedule.selfplay));
        console.log(JSON.stringify({round:pending.round,steps:pending.steps,selfplay:pending.selfplay,schedule:'sequential',
            note:'Opponent paths for unstarted stints are previews; they bind the latest roster when each stint starts.',
            commands:state.members.filter(m=>m.trainable && !pending.completed[m.name]).map(m=>planMember(directory,state,m,pending,pending.plans[m.name].attempts+1))},null,2));
        return;
    }
    const unlock=lock(directory);
    try {
        const latest=loadState(directory);
        if(command==='eval')await evaluate(directory,latest,options);
        else if(command==='resume' && latest.phase==='creating')await initialize(directory,latest);
        else await trainRounds(directory,latest,options);
    }finally{unlock();}
}
if(process.argv[1] && path.resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
    for(const signal of ['SIGINT','SIGTERM'])process.on(signal,()=>{interrupted=true;if(child)child.kill('SIGTERM');});
    main().catch(error=>{console.error('Pokemon league: '+error.message);process.exitCode=interrupted?130:1;});
}
