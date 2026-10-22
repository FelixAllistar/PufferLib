from pathlib import Path
import json,configparser,subprocess,shutil,hashlib
root=Path(Path('/tmp/mario_finals_eval_path').read_text())
subprocess.run(['python3','/tmp/mario_finals_compare.py'],check=True)
r=json.loads((root/'comparison/summary.json').read_text())
report={'all_validated':r['all_validated'],'models':{},'native_metrics':{}}
for label,m in r['models'].items():
 tests=[m['panels'][f'level_{w}_{l}'] for w in range(1,9) for l in range(1,5)]
 if not all(x['valid'] for x in tests):continue
 summaries=[x['summary'] for x in tests];wins=[x['rom_successes'] for x in summaries]
 zero=[f'{i//4+1}-{i%4+1}' for i,x in enumerate(wins) if x==0]
 report['models'][label]={'clears':sum(wins),'attempts':1024,'rate':sum(wins)/1024,'levels_with_any_clears':sum(x>0 for x in wins),'perfect_levels':sum(x==32 for x in wins),'zero_clear_levels':zero,'water':{level:m['panels']['level_'+level.replace('-','_')]['summary']['rom_successes'] for level in ['2-2','7-2']},'per_level_clears':wins,'per_level_deaths':sum(x['deaths'] for x in summaries),'per_level_timeouts':sum(x['timeouts'] for x in summaries)}
for label,m in r['models'].items():
    incomplete=[]
    for name,panel in m['panels'].items():
        if not name.startswith('level_') or not panel['valid']:continue
        stats=panel['summary'];gap=stats['flag_contacts']-stats['rom_successes']
        if gap>0:incomplete.append({'level':name[6:].replace('_','-'),'flag_contacts':stats['flag_contacts'],'completed_exits':stats['rom_successes'],'deaths':stats['deaths'],'timeouts':stats['timeouts']})
    if label in report['models']:report['models'][label]['levels_with_more_flag_contacts_than_completed_exits']=incomplete
for i,f in enumerate(sorted(root.glob('sweep*.ini'))):
 c=configparser.ConfigParser();c.read(f);m=c['metrics'];pixels=float(m['env/game_progress_pixels']);depth=float(m['env/curriculum_frames']);game=.75*pixels/3400;fpg=.25*max(0,(depth-3)/422)
 report['native_metrics'][f'run{i}_10b']={'reported_score':float(m['env/sweep_score']),'score_from_formula':game+fpg,'game_term':game,'fpg_term':fpg,'fpg_success_rate':float(m['env/fpg_perf']),'elapsed_hours':float(m['uptime'])/3600}
a=root/'local_results_partial/run0_10b/new_game_1_1/episodes.jsonl';b=root/'results/run0_10b/new_game_1_1/episodes.jsonl'
if a.exists() and b.exists():
 before=[json.loads(s) for s in a.read_text().splitlines()];after=[json.loads(s) for s in b.read_text().splitlines()]
 report['local_remote_golden_1_1_all_32_attempts_match']=before==after
 assert report['local_remote_golden_1_1_all_32_attempts_match']
for label,m in r['models'].items():
 if not label.startswith('run'):continue
 first=root/'results'/label/'new_game_1_1/episodes.jsonl';repeat=root/'results'/label/'level_1_1/episodes.jsonl'
 assert [json.loads(s) for s in first.read_text().splitlines()]==[json.loads(s) for s in repeat.read_text().splitlines()]
(root/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
(root/'README.md').write_text('# Final 10B checkpoint check-in\n\nBoth finished trials were downloaded with SHA-256 verification. Frozen CPU ROM evaluations use the same seed, evaluator binary, physical configuration, episode limits and sampled actions as the historical 2.15B and 6.44B comparisons. The continuing GPU sweep was retained; remote evaluation initially used CPU cores 46 and 47, then cores 45–47 after measuring available CPU quota.\n\nFull game/FPG panel and all 32 per-level comparisons: [comparison/README.md](comparison/README.md). Each final model has 1,216 test episodes: 32 continuing games, 32 fresh 1-1 tests, two FPG panels of 64 attempts, and 32 attempts on each of 32 levels. The duplicate 1-1 panels check reproducibility.\n\n`analysis.json` contains totals, water-level results, zero-clear levels, and training score decomposition. `download_manifest.json` records checkpoint provenance and hashes. `results/` retains raw trajectories, cases, logs and commands; `comparison/summary.json` links the two historical source reports. Invalid parity panels remain explicitly invalid.\n\nThe training score uses FPG curriculum depth rather than current success rate, so retained curriculum progress can hide skill loss. Training starts uniformly at the 32 natural level entrances; extra FPG curriculum starts cover only 1-1. Encounter-specific reset states are a candidate experiment, not a demonstrated fix.\n')
for source,name in [('/tmp/mario_finals_compare.py','compare.py'),(__file__,'analyze.py')]:shutil.copyfile(source,root/name)
print('ANALYSIS',json.dumps(report,indent=2))
