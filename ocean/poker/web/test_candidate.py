import hashlib,json,pathlib,shutil,subprocess,statistics,time
root=pathlib.Path.cwd();out=root/'build/poker/pr_try';pr=out/'source/ocean/poker'
old_sha='59e06c0f92aa6f62909522d5d8b24eaba5ad8cfb';new_sha='d5c1a654d2245e24ccc11df83e327eca5e73093a'
out.mkdir(parents=True,exist_ok=True)
files=['rules.h','equity.h','preflop_minraise.h','river_value.h']
for mode in ['old','new']:
 dst=out/mode;dst.mkdir(parents=True,exist_ok=True)
 for n in ['core.h','fullhouse.h','native.c']:shutil.copy2(root/'ocean/poker'/n,dst/n)
 for n in ['bridge.c','learning.h']:shutil.copy2(root/'ocean/poker/web'/n,dst/n)
 for n in files:
  old_bytes=subprocess.check_output(['git','show',f'{old_sha}:ocean/poker/{n}'])
  new_bytes=subprocess.check_output(['git','show',f'{new_sha}:ocean/poker/{n}'])
  if n!='equity.h':assert old_bytes==new_bytes,n
  (dst/n).write_bytes(new_bytes if mode=='new' else old_bytes)
 (dst/'test_equity.c').write_bytes(subprocess.check_output(['git','show',f'{new_sha}:ocean/poker/test_equity.c']))
code=r'''
#include <stdio.h>
#include <time.h>
#include "bridge.c"
static unsigned long long hash=1469598103934665603ULL;
static void mix(const void *p,size_t n){const unsigned char *b=p;for(size_t i=0;i<n;i++){hash^=b[i];hash*=1099511628211ULL;}}
int main(int argc,char **argv){
 if(argc>1&&!strcmp(argv[1],"equity")){
  Poker fixtures[32];
  for(int h=0;h<32;h++){for(int p=0;p<2;p++)web_stack(p,20000);web_open(1731+h*731,h+1);while(game.street[0]<3){if(!web_act(0))return 4;}fixtures[h]=game;}
  uint64_t rng=175133;clock_t begin=clock();
  for(int k=0;k<30000;k++){PokerEquity e;poker_equity(&fixtures[k%32],0,&rng,128,1,NULL,&e);mix(&e,sizeof(e));}
  double seconds=(clock()-begin)/(double)CLOCKS_PER_SEC;
  printf("{\"cpu_seconds\":%.9f,\"calls\":30000,\"hash\":\"%016llx\",\"rng\":\"%016llx\"}\n",seconds,hash,(unsigned long long)rng);return 0;
 }
 int opponent=argc>1?atoi(argv[1]):1;long long profit=0,steps=0,rivers=0;
 for(int h=0;h<2000;h++){
  for(int p=0;p<2;p++)web_stack(p,20000);web_open(987451u+731u*h,h+1);
  while(game.actor[0]!=POKER_DONE){int p=game.actor[0],policy=p?opponent:7;int32_t a=web_bot(policy);rivers+=game.street[0]==3;mix(&a,sizeof(a));mix(&policies[p],sizeof(policies[p]));if(!web_act(a))return 3;mix(&game,sizeof(game));steps++;}
  long long net=(long long)game.stack[0][0]-20000;profit+=net;
  printf("%d %lld %016llx\n",h,net,hash);
 }
 fprintf(stderr,"{\"hands\":2000,\"decisions\":%lld,\"river_decisions\":%lld,\"net_chips\":%lld,\"bb_per_100\":%.6f}\n",steps,rivers,profit,profit/2000.0);
}
'''
model='-DPOKER_FULLHOUSE_MODEL="'+str(root/'build/poker/fullhouse_weights.h')+'"'
for mode in ['old','new']:
 dst=out/mode;(dst/'trial.c').write_text(code)
 subprocess.run(['clang-19','-O3','-ffp-contract=off','-std=c11','-DPOKER_PLAYERS=2','-DPOKER_BATCH=1','-DPOKER_WIDE_CHIPS=1',model,str(dst/'trial.c'),'-lm','-o',str(dst/'trial')],check=True)
report={'old_commit':'59e06c0f92aa6f62909522d5d8b24eaba5ad8cfb','new_commit':'d5c1a654d2245e24ccc11df83e327eca5e73093a','mode':'standard sampled HU river; exact/calibration/CFR/clone off','paired_fields':[]}
for field,name in [(1,'Rules'),(4,'Fullhouse')]:
 results=[]
 for mode in ['old','new']:
  r=subprocess.run([str(out/mode/'trial'),str(field)],check=True,capture_output=True,text=True,timeout=180)
  (out/f'{mode}-{name}.txt').write_text(r.stdout)
  results.append(r);print(mode,name,r.stderr.strip(),flush=True)
 assert results[0].stdout==results[1].stdout,'Action/RNG/settlement trace diverged'
 stats=json.loads(results[1].stderr);stats.update(opponent=name,exact_old_new_trace_match=True,trace_sha256=hashlib.sha256(results[1].stdout.encode()).hexdigest(),paired_strength_difference_bb_per_100=0)
 report['paired_fields'].append(stats)
report['river_equity_microbenchmark']={}
for repeat in range(5):
 for mode in (['old','new'] if repeat%2==0 else ['new','old']):
  r=json.loads(subprocess.check_output([str(out/mode/'trial'),'equity'],text=True));report['river_equity_microbenchmark'].setdefault(mode,[]).append(r)
old=report['river_equity_microbenchmark']['old'];new=report['river_equity_microbenchmark']['new'];assert all(x['hash']==old[0]['hash'] and x['rng']==old[0]['rng'] for x in old+new)
report['river_equity_microbenchmark']['old_median_cpu_seconds']=statistics.median(x['cpu_seconds'] for x in old)
report['river_equity_microbenchmark']['new_median_cpu_seconds']=statistics.median(x['cpu_seconds'] for x in new)
report['river_equity_microbenchmark']['speedup']=statistics.median(x['cpu_seconds'] for x in old)/statistics.median(x['cpu_seconds'] for x in new)
report['river_equity_microbenchmark']['identical_outputs_and_rng']=True
report['source_sha256']={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [root/'ocean/poker/core.h',root/'ocean/poker/fullhouse.h',root/'ocean/poker/web/bridge.c',root/'build/poker/fullhouse_weights.h']}
report['limits']='CPU time on a shared host; primitive river-equity timing, not whole-bot latency. No live Slumbot run. Paired matches use 200 BB fixed buy-ins and alternating buttons, not a calibrated strength estimate.'
(out/'trial.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2),flush=True)
