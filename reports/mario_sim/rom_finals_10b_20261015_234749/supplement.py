from pathlib import Path
import json,csv,hashlib,subprocess,shutil
root=Path(Path('/tmp/mario_finals_eval_path').read_text());panel_dir=root/'results/run0_10b/level_5_2';tail=root/'rom_only_5_2_tail'
original=[json.loads(s) for s in (panel_dir/'episodes.jsonl').read_text().splitlines()]
new=[json.loads(s) for s in (tail/'episodes.jsonl').read_text().splitlines()]
assert len(original)==31 and len(new)==3
assert [c['episode'] for c in new]==[29,30,31]
assert all(not c['failure'] and c['rom_status'] in [1,2,3] for c in original[:30]+new)
keys=set(original[29])-{'native_status'}
assert {k:original[29][k] for k in keys}=={k:new[0][k] for k in keys}
def traces(path,episode):
 rows=list(csv.DictReader(path.open()));return [r for r in rows if r['episode']==str(episode)]
assert traces(panel_dir/'trajectories.csv',29)==traces(tail/'trajectories.csv',29)
a=traces(panel_dir/'trajectories.csv',30);b=traces(tail/'trajectories.csv',30)
assert a==b[:len(a)]
assert original[30]['failure']=='native_state' and original[30]['field']==2048
combined=original[:30]+new[1:]
assert len(combined)==32 and [c['episode'] for c in combined]==list(range(32))
(panel_dir/'rom_complete_episodes.jsonl').write_text(''.join(json.dumps(c)+'\n' for c in combined))
wins=[c for c in combined if c['rom_status']==1]
summary={'rom_valid':True,'native_parity_valid':False,'episodes_requested':32,'episodes_completed':32,'rom_successes':len(wins),'deaths':sum(c['rom_status']==2 for c in combined),'timeouts':sum(c['rom_status']==3 for c in combined),'mean_success_frames':sum(c['frames'] for c in wins)/len(wins) if wins else 0,'parity_checked_complete_episodes':30,'rom_only_completed_episodes':[30,31],'control_episode_29_exact_case_and_trace_match':True,'episode_30_trace_before_parity_failure_matches':True,'native_failure':original[30],'source_tail':str(tail),'checkpoint_sha256':json.loads((root/'results/summary.json').read_text())['models']['run0_10b']['sha256']}
(panel_dir/'rom_completion.json').write_text(json.dumps(summary,indent=2)+'\n')
r=json.loads((root/'results/summary.json').read_text());r['models']['run0_10b']['panels']['level_5_2']['rom_completion']=summary
(root/'results/summary.json').write_text(json.dumps(r,indent=2)+'\n')
subprocess.run(['python3','/tmp/mario_finals_finish.py'],check=True,stdout=subprocess.DEVNULL)
comparison=json.loads((root/'comparison/summary.json').read_text());totals={}
for label,m in comparison['models'].items():
 wins=[];zero=[];perfect=0
 for stage in range(32):
  name=f'level_{stage//4+1}_{stage%4+1}';p=m['panels'][name]
  if p['valid']:s=p['summary']
  else:
   assert label=='run0_10b' and name=='level_5_2' and p['rom_completion']['rom_valid']
   s=p['rom_completion']
  count=s['rom_successes'];wins.append(count)
  if count==0:zero.append(f'{stage//4+1}-{stage%4+1}')
 totals[label]={'rom_valid':True,'native_parity_valid':all(p['valid'] for p in m['panels'].values()),'clears':sum(wins),'attempts':1024,'rate':sum(wins)/1024,'levels_any_clear':sum(x>0 for x in wins),'perfect_levels':sum(x==32 for x in wins),'zero_clear_levels':zero,'per_level_clears':wins}
analysis=json.loads((root/'analysis.json').read_text());analysis['rom_complete_totals']=totals;analysis['native_parity_failures']=[{'model':'run0_10b','level':'5-2','episode':30,'frame':2010,'field':2048,'description':'CPU register state mismatch; RAM matched at first divergence.'}]
(root/'analysis.json').write_text(json.dumps(analysis,indent=2)+'\n')
text=(root/'comparison/README.md').read_text();lines=[]
for line in text.splitlines():
 if line.startswith('| 5-2 |'):
  cells=line.split('|');assert cells[4].strip()=='INVALID';cells[4]=f' {summary["rom_successes"]}/32† ';line='|'.join(cells)
 if line.startswith('| run0_10b | INVALID |'):
  t=totals['run0_10b'];line=f'| run0_10b | {t["clears"]}/1024† | {t["levels_any_clear"]}/32 | {t["perfect_levels"]}/32 |'
 line=line.replace('Simulator/ROM parity failures are reported as invalid tests.','The † score is completed in the actual ROM, with a failed simulator parity check recorded separately.')
 lines.append(line)
text='\n'.join(lines)+'\n\n† Run 0, 5-2: 30 attempts completed with paired simulator/ROM checks. Attempt 30 then hit a CPU-register mismatch at frame 2,010 while RAM matched. Attempts 30 and 31 were completed directly in the ROM using the same reset, policy, per-episode RNG seeds and 6,000-frame limit. A repeat of attempt 29 matched the paired run exactly, including every sampled trace row; attempt 30 also matched its trace up to the original failure. The completed ROM score is 10/32. The strict parity check remains failed; no simulator match is claimed for these two completed attempts. Original diagnostic: `../results/run0_10b/level_5_2/episodes.jsonl`; completion: `../results/run0_10b/level_5_2/rom_completion.json`.\n'
(root/'comparison/ROM_RESULTS.md').write_text(text)
readme=(root/'README.md').read_text();readme=readme.replace('Full game/FPG panel and all 32 per-level comparisons: [comparison/README.md](comparison/README.md).','Completed actual-ROM scores, including all 32 per-level comparisons: [comparison/ROM_RESULTS.md](comparison/ROM_RESULTS.md). The strict simulator/ROM report, which retains the failed 5-2 parity panel, is [comparison/README.md](comparison/README.md).')
readme+='\nA simulator CPU-register mismatch was detected in run 0, 5-2, episode 30 at frame 2,010; RAM matched at first divergence. Its two unfinished attempts were completed directly in the ROM, with a repeated paired-run control and matching pre-divergence trajectory. This completes actual-ROM scoring without claiming full simulator parity. Run 0: 331/1,024 per-level clears; run 1: 436/1,024.\n'
(root/'README.md').write_text(readme)
for source,name in [(__file__,'supplement.py'),('/tmp/mario_5_2_rom_finish.py','build_rom_tail.py')]:shutil.copyfile(source,root/name)
print(json.dumps({'rom_complete_totals':totals,'supplement':summary},indent=2))
