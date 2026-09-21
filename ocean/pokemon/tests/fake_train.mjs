#!/usr/bin/env node
// Workflow test double only. Never used by production/default league commands.
import fs from 'node:fs';
import path from 'node:path';
import {parameterCount,formatIni} from '../freeleague.mjs';
const args=process.argv.slice(2);
if(args[0]==='eval') {
    const games=Number(args.find(a=>a.startsWith('--games=')).split('=')[1]);
    console.log(`A=fake B=fake games=${games} W=${games} D=0 L=0 score=1.0000 conservative_95%=[0,1] timeouts=0 sampling=stochastic`);
} else {
    const config=Object.fromEntries(args.slice(2).map(arg=>{const eq=arg.indexOf('=');return [arg.slice(0,eq),arg.slice(eq+1)];}));
    const run=config['base.run_id'];
    if(process.env.PK_LEAGUE_TEST_OUTPUT) {
        console.log('FAKE_NATIVE_STDOUT tty='+Boolean(process.stdout.isTTY));
        console.error('FAKE_NATIVE_STDERR');
    }
    if(process.env.PK_LEAGUE_TEST_FAIL && run.includes(process.env.PK_LEAGUE_TEST_FAIL))process.exit(7);
    const count=parameterCount(Number(config['policy.hidden_size']),Number(config['policy.num_layers']));
    let weights=config['base.load_model_path']==='None'?Buffer.alloc(count*4):fs.readFileSync(config['base.load_model_path']);
    weights.writeFloatLE(weights.readFloatLE(0)+Number(config['base.seed'])/1000000+0.001,0);
    const dir=path.join(config['base.checkpoint_dir'],'pokemon',run);fs.mkdirSync(dir,{recursive:true});
    fs.writeFileSync(path.join(dir,config['train.total_timesteps'].padStart(16,'0')+'.bin'),weights);
    fs.writeFileSync(path.join(dir,'config.ini'),formatIni(config));
}
