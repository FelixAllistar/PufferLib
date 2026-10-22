import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {ROOT,planCreate,constraint,parameterCount,parseIni,formatIni,bindingConfig,roundLearningRate,roundEntropy} from '../freeleague.mjs';
const here=path.dirname(fileURLToPath(import.meta.url));
const cli=path.join(here,'../freeleague.mjs'),fake=path.join(here,'fake_train.mjs');
test('native 5.0 initialization, fixed peers, continuation, export and evaluation',
    {skip:!process.env.POKEMON_TRAIN_BINARY},()=>{
    const dir=fs.mkdtempSync(path.join(os.tmpdir(),'pokemon-native-league-'));
    try {
        command(['create',dir,'--new-policies','2','--steps','1024','--quiet',
            '--binary',process.env.POKEMON_TRAIN_BINARY,'--',
            'policy.hidden_size=16','policy.num_layers=1','vec.total_agents=16',
            'vec.num_threads=2','train.horizon=8','train.minibatch_size=32',
            'env.core_pool=1,2,3,4'],0,{PUFFER_POKEMON_AUDIT:'1'});
        command(['train',dir,'--quiet'],0,{PUFFER_POKEMON_AUDIT:'1'});
        command(['resume',dir,'--quiet','--evaluate','4'],0,{PUFFER_POKEMON_AUDIT:'1'});
        command(['export',dir]);
        const state=JSON.parse(fs.readFileSync(path.join(dir,'state.json')));
        assert.equal(state.round,2);assert.equal(state.pending,null);
        assert(state.members.every(m=>m.current.path.includes('r000002-a001')));
        const manifest=parseIni(fs.readFileSync(path.join(dir,'native.ini'),'utf8'));
        assert.deepEqual(fs.readFileSync(manifest['native.opponents'],'utf8').trim().split('\n'),
            state.members.map(m=>m.current.path));
        const evaluations=fs.readdirSync(path.join(dir,'evaluations')).filter(x=>x.endsWith('.json'));
        assert.equal(evaluations.length,1);
        assert.equal(JSON.parse(fs.readFileSync(path.join(dir,'evaluations',evaluations[0]))).pairs[0].games,4);
    } finally {fs.rmSync(dir,{recursive:true,force:true});}
});
function command(args,code=0,env={}) {
    const result=spawnSync(process.execPath,[cli,...args],{cwd:ROOT,encoding:'utf8',env:{...process.env,...env},timeout:120000});
    assert.equal(result.status,code,result.stdout+'\n'+result.stderr);return result;
}
test('round-spanning entropy reaches exactly zero and stays off',()=>{
    const base={'train.ent_coef':'0.005','train.anneal_ent_coef':'0','train.min_ent_coef_ratio':'0.1'};
    const schedule={rounds:25,final_ratio:0};
    let previous=0.005;
    for(let round=1;round<=25;round++) {
        const c=roundEntropy(base,schedule,round),start=Number(c['train.ent_coef']);
        assert.equal(c['train.anneal_ent_coef'],'1');
        assert(Math.abs(start-previous)<1e-15);
        const end=start*Number(c['train.min_ent_coef_ratio']);
        assert(end<=start && end>=0);
        previous=end;
    }
    assert(previous<1e-18);
    for(const round of [26,40,60])assert.deepEqual(
        [roundEntropy(base,schedule,round)['train.ent_coef'],roundEntropy(base,schedule,round)['train.anneal_ent_coef']],['0','0']);
    assert.equal(roundEntropy(base,null,1),base);
    for(const options of [{'ent-decay-rounds':-1},{'ent-decay-rounds':1.5},{'ent-final-ratio':0},
        {'ent-decay-rounds':10,'ent-final-ratio':1.5}])assert.throws(()=>planCreate(options));
});
test('round-spanning LR is monotone, continuous at boundaries and holds its floor',()=>{
    const base={'train.learning_rate':'0.0005','train.anneal_lr':'0','train.min_lr_ratio':'0.1'};
    const schedule={rounds:50,final_ratio:0.1};
    let previous=0.0005;
    for(let round=1;round<=52;round++) {
        const c=roundLearningRate(base,schedule,round),start=Number(c['train.learning_rate']);
        const end=start*Number(c['train.min_lr_ratio']);
        assert(Math.abs(start-previous)<1e-15);
        assert(end<=start && end>=0.00005-1e-15);
        assert.equal(c['train.anneal_lr'],'1');
        previous=end;
    }
    assert.equal(previous,0.00005);assert.equal(roundLearningRate(base,null,1),base);
    for(const options of [{'lr-decay-rounds':-1},{'lr-decay-rounds':2.5},{'lr-final-ratio':.1},
        {'lr-decay-rounds':50,'lr-final-ratio':0},{'min-free-gb':-1}])assert.throws(()=>planCreate(options));
});
test('Metamon opt-in is preserved for stints but never runs during initialization',()=>{
    const dir=fs.mkdtempSync(path.join(os.tmpdir(),'pokemon-league-metamon-'));
    try {
        command(['create',dir,'--new-policies','2','--steps','4096','--binary',fake,'--',
            'policy.hidden_size=32','policy.num_layers=1','vec.total_agents=64',
            'train.horizon=64','train.minibatch_size=1024','metamon.enabled=1']);
        const initial=JSON.parse(fs.readFileSync(path.join(dir,'plans','init-master-a1.json')));
        assert.equal(initial.config['metamon.enabled'],'0');
        command(['train',dir]);
        const stint=JSON.parse(fs.readFileSync(path.join(dir,'plans','r000001-master-a001.json')));
        assert.equal(stint.config['metamon.enabled'],'1');
        assert.equal(planCreate().config['metamon.enabled'],'0');
    }finally{fs.rmSync(dir,{recursive:true,force:true});}
});
test('deferred initialization and live PTY output preserve logs and failures',()=>{
    const root=fs.mkdtempSync(path.join(os.tmpdir(),"pokemon-live-'test-"));
    const dir=path.join(root,'league with spaces');
    try {
        command(['create',dir,'--new-policies','2','--steps','4096','--defer-init','--binary',fake,'--evaluator',fake,'--',
            'policy.hidden_size=32','policy.num_layers=1','vec.total_agents=64','train.horizon=64','train.minibatch_size=1024']);
        const read=()=>JSON.parse(fs.readFileSync(path.join(dir,'state.json')));
        assert.equal(read().phase,'creating');
        assert(read().members.every(m=>m.current===null));
        assert(!fs.existsSync(path.join(dir,'work')));
        const env={PK_LEAGUE_TEST_OUTPUT:'1',PK_LEAGUE_TEST_FAIL:'r000001-free_02'};
        const result=command(['train',dir,'--live'],1,env);
        assert.match(result.stdout,/FAKE_NATIVE_STDOUT tty=true/);
        assert.match(result.stdout,/FAKE_NATIVE_STDERR/);
        const log=fs.readFileSync(path.join(dir,'logs','r000001-master-a001.log'),'utf8');
        assert.match(log,/FAKE_NATIVE_STDOUT tty=true/);assert.match(log,/FAKE_NATIVE_STDERR/);
        assert.deepEqual(Object.keys(read().pending.completed),['master']);
        const quiet=command(['resume',dir,'--quiet'],0,{PK_LEAGUE_TEST_OUTPUT:'1'});
        assert.doesNotMatch(quiet.stdout,/FAKE_NATIVE/);
        assert.equal(read().round,1);
        assert.match(fs.readFileSync(path.join(dir,'logs','r000001-free_02-a002.log'),'utf8'),/FAKE_NATIVE_STDOUT tty=false/);
        command(['train',dir,'--live','--quiet'],1);
    }finally{fs.rmSync(root,{recursive:true,force:true});}
});
test('composition planner: all sample leads, free moves, independent members',()=>{
    const plan=planCreate({teams:'smogon',master:'None','new-policies':'2'},['env.force_core_combos=1','env.force_core_prob=0.5']);
    assert.equal(plan.members.length,15);assert.equal(new Set(plan.members.map(m=>m.seed)).size,15);
    assert.equal(plan.members[0].name,'master');assert.equal(plan.members[1].name,'free_02');
    const counts={};
    for(const member of plan.members.slice(2)) {
        counts[member.lead]=(counts[member.lead]||0)+1;
        assert.equal(member.team.split(',').length,6);assert.equal(member.initializer,null);
        const config=bindingConfig(plan.config,member);
        assert.equal(config['env.force_core_combos'],'0');assert.equal(config['env.reset_state_prob'],'0');
        assert.equal(config['env.team_selection'],'1');
    }
    assert.deepEqual(counts,{'65':4,'121':3,'124':3,'94':3});
    assert.equal(bindingConfig(plan.config,plan.members[0])['env.force_core_prob'],'0.5');
    assert.equal(parameterCount(32,1),142240);
    assert.deepEqual(constraint(['Jynx','Mr. Mime'],'Jynx'),{team:'species:124,122',lead:'124'});
    for(const fn of [()=>constraint(['Mewtwo']),()=>constraint(['Tauros',128]),()=>constraint([1,2,3,4,5,6],124),
        ()=>planCreate({'new-policies':33}),()=>planCreate({'new-policies':1,selfplay:0.5}),
        ()=>planCreate({},['train.emag_kl_coef=1']),
        ()=>planCreate({},['selfplay.pfsp_mode=variance'])])assert.throws(fn);
});
test('fixed catalog checkpoint survives league publication and Oak defaults off',()=>{
    const root=fs.mkdtempSync(path.join(os.tmpdir(),'pokemon-catalog-league-'));
    try {
        const seedLeague=path.join(root,'seeds');
        command(['create',seedLeague,'--new-policies','2','--steps','4096','--binary',fake,'--',
            'policy.hidden_size=32','policy.num_layers=1','vec.total_agents=64',
            'train.horizon=64','train.minibatch_size=1024']);
        const initial=JSON.parse(fs.readFileSync(path.join(seedLeague,'state.json')));
        const fixed='421,366,389,395,512,445',seedDir=path.join(root,'fixed');
        fs.mkdirSync(seedDir);
        const weights=path.join(seedDir,'weights.bin');
        fs.copyFileSync(initial.members[0].current.path,weights);
        const config=parseIni(fs.readFileSync(path.join(path.dirname(initial.members[0].current.path),'config.ini'),'utf8'));
        fs.writeFileSync(path.join(seedDir,'config.ini'),formatIni({...config,'env.learner_team':fixed,'env.learner_lead':'121'}));
        const teams=path.join(root,'roster.json');
        fs.writeFileSync(teams,JSON.stringify({members:[
            {name:'master',team:'catalog:'+fixed,lead:121,checkpoint:weights},
            {name:'peer',team:'None',checkpoint:initial.members[1].current.path}]}));
        const league=path.join(root,'league');
        command(['create',league,'--master',weights,'--new-policies','0','--teams',teams,
            '--steps','4096','--binary',fake,'--','vec.total_agents=64','train.horizon=64','train.minibatch_size=1024']);
        const statePath=path.join(league,'state.json');
        let state=JSON.parse(fs.readFileSync(statePath));
        assert.equal(state.members[0].team,fixed);
        assert.equal(state.config['oak.enabled'],'0');
        assert.equal(state.config['metamon.enabled'],'0');
        // A pre-Oak league state must also suppress globally enabled Oak.
        delete state.config['oak.enabled'];delete state.config['metamon.enabled'];fs.writeFileSync(statePath,JSON.stringify(state));
        command(['train',league]);command(['export',league]);
        state=JSON.parse(fs.readFileSync(statePath));
        const saved=parseIni(fs.readFileSync(path.join(path.dirname(state.members[0].current.path),'config.ini'),'utf8'));
        assert.equal(saved['env.learner_team'],fixed);
        assert.equal(saved['env.learner_lead'],'121');
        assert.equal(saved['oak.enabled'],'0');
        assert.equal(saved['metamon.enabled'],'0');
        const manifest=parseIni(fs.readFileSync(path.join(league,'native.ini'),'utf8'));
        assert.equal(manifest['bank.0.team'],fixed);
        assert.equal(planCreate({},['oak.enabled=1']).config['oak.enabled'],'1');
        for(const team of ['catalog:421','catalog:421,421,389,395,512,445','catalog:999999,366,389,395,512,445'])
            assert.throws(()=>constraint(team,121));
        assert.throws(()=>constraint('catalog:'+fixed,128));
    }finally{fs.rmSync(root,{recursive:true,force:true});}
});
test('sequential publication, failure/resume, seed cloning, anchors, selective export and eval',()=>{
    const root=fs.mkdtempSync(path.join(os.tmpdir(),'pokemon-league-test-'));
    const dir=path.join(root,'league');
    try {
        command(['create',dir,'--new-policies','3','--steps','4096','--binary',fake,'--evaluator',fake,
            '--lr-decay-rounds','3','--lr-final-ratio','0.1','--',
            'policy.hidden_size=32','policy.num_layers=1','vec.total_agents=64','train.horizon=64','train.minibatch_size=1024']);
        const read=()=>JSON.parse(fs.readFileSync(path.join(dir,'state.json'),'utf8'));
        const initial=read();assert.equal(initial.phase,'ready');assert.equal(initial.round,0);
        assert.notEqual(initial.members[0].current.sha256,initial.members[1].current.sha256);
        command(['train',dir,'--steps','4097'],1);
        assert.equal(read().pending,null);
        command(['train',dir],1,{PK_LEAGUE_TEST_FAIL:'r000001-free_02'});
        let state=read();assert.equal(state.round,0);
        assert.notDeepEqual(state.members[0].current,initial.members[0].current);
        assert.deepEqual(state.members.slice(1).map(m=>m.current),initial.members.slice(1).map(m=>m.current));
        assert.deepEqual(Object.keys(state.pending.completed),['master']);
        const completed=state.pending.completed.master;
        assert.deepEqual(state.members[0].current,completed);
        const failedOpponents=state.pending.plans.free_02.opponent_snapshot;
        assert.deepEqual(failedOpponents.map(m=>m.current),[completed,initial.members[2].current]);
        const commandPlan=run=>JSON.parse(fs.readFileSync(path.join(dir,'plans',run+'.json')));
        assert.equal(commandPlan('r000001-free_02-a001').opponents[0].current.path,completed.path);
        // Export exposes successful updates even while a later member is failed.
        command(['export',dir,'--members','master','--output',path.join(root,'partial.ini')]);
        assert.equal(parseIni(fs.readFileSync(path.join(root,'partial.ini'),'utf8'))['bank.0.path'],completed.path);
        command(['resume',dir,'--steps','8192'],1);assert.equal(read().round,0);
        command(['resume',dir]);state=read();
        assert.equal(state.round,1);assert.equal(state.pending,null);assert.deepEqual(state.members[0].current,completed);
        assert.equal(state.rounds[0].plans.master.attempts,1);assert.equal(state.rounds[0].plans.free_02.attempts,2);
        assert.equal(state.rounds[0].plans.master.learner_steps,3072);
        assert.deepEqual(state.rounds[0].snapshot.map(m=>m.current),initial.members.map(m=>m.current));
        assert.deepEqual(state.rounds[0].plans.free_02.opponent_snapshot,failedOpponents);
        assert.deepEqual(commandPlan('r000001-free_02-a002').opponents,failedOpponents);
        for(const key of ['train.learning_rate','train.min_lr_ratio'])
            assert.equal(commandPlan('r000001-free_02-a002').config[key],commandPlan('r000001-free_02-a001').config[key]);
        assert.deepEqual(state.rounds[0].plans.free_03.opponent_snapshot.map(m=>m.current),state.members.slice(0,2).map(m=>m.current));
        assert.deepEqual(commandPlan('r000001-free_03-a001').opponents.map(m=>m.current),state.members.slice(0,2).map(m=>m.current));
        const firstRound=state;
        command(['train',dir]);state=read();
        assert.equal(state.round,2);
        const firstLR=state.rounds[0].plans.master.config,nextLR=state.rounds[1].plans.master.config;
        assert(Math.abs(Number(firstLR['train.learning_rate'])*Number(firstLR['train.min_lr_ratio'])-Number(nextLR['train.learning_rate']))<1e-15);
        assert.deepEqual(state.rounds[1].plans.master.opponent_snapshot.map(m=>m.current),firstRound.members.slice(1).map(m=>m.current));
        // Legacy interrupted rounds held completed versions off-roster and had
        // no per-stint snapshot. Resume upgrades them without repeating A.
        command(['train',dir],1,{PK_LEAGUE_TEST_FAIL:'r000003-free_02'});
        const legacy=read(),legacyMaster=legacy.pending.completed.master;
        for(const member of legacy.members)member.current=legacy.pending.snapshot.find(m=>m.name===member.name).current;
        delete legacy.pending.schedule;
        for(const plan of Object.values(legacy.pending.plans))delete plan.opponent_snapshot;
        fs.writeFileSync(path.join(dir,'state.json'),JSON.stringify(legacy));
        command(['resume',dir]);state=read();
        assert.equal(state.round,3);assert.deepEqual(state.members[0].current,legacyMaster);
        assert.equal(state.rounds[2].plans.master.attempts,1);
        assert.deepEqual(state.rounds[2].plans.free_02.opponent_snapshot[0].current,legacyMaster);
        command(['export',dir,'--members','master']);
        const manifest=parseIni(fs.readFileSync(path.join(dir,'native.ini'),'utf8'));
        assert.equal(manifest['native.banks'],'1');assert.equal(manifest['bank.0.path'],state.members[0].current.path);
        assert.equal(fs.readFileSync(manifest['native.opponents'],'utf8'),state.members[0].current.path+'\n');
        command(['export',dir,'--members','master'],1);command(['export',dir,'--members','master','--force']);
        command(['eval',dir,'--games','4']);command(['eval',dir,'--games','4']);
        const files=fs.readdirSync(path.join(dir,'evaluations')).filter(x=>x.endsWith('.json'));
        assert.equal(files.length,1);assert.equal(JSON.parse(fs.readFileSync(path.join(dir,'evaluations',files[0]))).pairs.length,3);
        const seeded=path.join(root,'seeded');
        command(['create',seeded,'--master',state.members[0].current.path,'--new-policies','2','--anchor',state.members[1].current.path,
            '--binary',fake,'--evaluator',fake,'--steps','4096','--','vec.total_agents=64','train.minibatch_size=1024']);
        const seededState=JSON.parse(fs.readFileSync(path.join(seeded,'state.json')));
        assert.equal(seededState.members.length,3);assert.equal(seededState.members[2].trainable,false);
        for(const m of seededState.members.slice(0,2))assert.equal(m.current.sha256,state.members[0].current.sha256);
        command(['train',seeded]);
        const trained=JSON.parse(fs.readFileSync(path.join(seeded,'state.json')));
        assert.equal(trained.members[2].current.sha256,seededState.members[2].current.sha256);
        // Frozen round files must not be silently reused after external mutation.
        fs.appendFileSync(path.join(path.dirname(trained.members[0].current.path),'config.ini'),'\n# mutation\n');
        command(['train',seeded],1);
        assert.equal(JSON.parse(fs.readFileSync(path.join(seeded,'state.json'))).round,1);
        const old={...read(),version:3};
        fs.writeFileSync(path.join(dir,'state.json'),JSON.stringify(old));
        const before=fs.readFileSync(path.join(dir,'state.json'));
        command(['resume',dir],1);
        assert.deepEqual(fs.readFileSync(path.join(dir,'state.json')),before);
    }finally{fs.rmSync(root,{recursive:true,force:true});}
});
