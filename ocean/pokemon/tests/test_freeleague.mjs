import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {ROOT,planCreate,constraint,parameterCount,parseIni,bindingConfig} from '../freeleague.mjs';
const here=path.dirname(fileURLToPath(import.meta.url));
const cli=path.join(here,'../freeleague.mjs'),fake=path.join(here,'fake_train.mjs');
function command(args,code=0,env={}) {
    const result=spawnSync(process.execPath,[cli,...args],{cwd:ROOT,encoding:'utf8',env:{...process.env,...env},timeout:30000});
    assert.equal(result.status,code,result.stdout+'\n'+result.stderr);return result;
}
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
        ()=>planCreate({'new-policies':33}),()=>planCreate({'new-policies':1,selfplay:0.5})])assert.throws(fn);
});
test('sequential publication, failure/resume, seed cloning, anchors, selective export and eval',()=>{
    const root=fs.mkdtempSync(path.join(os.tmpdir(),'pokemon-league-test-'));
    const dir=path.join(root,'league');
    try {
        command(['create',dir,'--new-policies','3','--steps','4096','--binary',fake,'--evaluator',fake,'--',
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
        assert.deepEqual(state.rounds[0].plans.free_03.opponent_snapshot.map(m=>m.current),state.members.slice(0,2).map(m=>m.current));
        assert.deepEqual(commandPlan('r000001-free_03-a001').opponents.map(m=>m.current),state.members.slice(0,2).map(m=>m.current));
        const firstRound=state;
        command(['train',dir]);state=read();
        assert.equal(state.round,2);
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
    }finally{fs.rmSync(root,{recursive:true,force:true});}
});
