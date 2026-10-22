from pathlib import Path
import subprocess,hashlib,json,shlex
repo=Path('/home/felix/puffertank/pufferlib');root=Path(Path('/tmp/mario_finals_eval_path').read_text());s=(repo/'ocean/mario_sim/sim2rom.cpp').read_text()
def edit(a,b):
 global s
 assert a in s,a
 s=s.replace(a,b)
edit('// Frozen policy, independent recurrent states, matched naturally reached resets.','// ROM-only episodes 29-31 of the failed 5-2 panel. Episode 29 is a checked control.')
edit('for(int index=0;index<episodes;index++)','for(int index=29;index<episodes;index++)')
start=s.index('                float no[FPT_OBS],ro[FPT_OBS];');end=s.index('                if(!(t%16))traces',start)
s=s[:start]+'''                float ro[FPT_OBS];fpt_observe(&real,world,&rh,ro);
                const float* rl=fpt_policy_logits(rom_policy,ro);
                int ra=choose(rl,&br,deterministic);
'''+s[end:]
edit('                fpt_rom_require(!smb_native_frame(&native,world,smb_action_buttons(na)),"native simulation fault");\n','')
edit('                bad_field=fpt_rom_difference(native,real);if(bad_field>=0){failure="native_state";break;}\n','')
edit('                float nr=after(native,&a),rr=after(real,&b);\n                if(nr!=rr||a.status!=b.status){failure="outcome";break;}\n                if(a.status)break;', '                after(real,&b);a=b;\n                if(b.status)break;')
edit('bool complete=completed==(unsigned)episodes,passed=!mismatches&&complete;', 'bool complete=completed==(unsigned)(episodes-29),passed=complete;')
edit('<<",\\\"episodes_requested\\\":"<<episodes','<<",\\\"native_parity_checked\\\":false,\\\"first_episode\\\":29,\\\"episodes_requested\\\":"<<(episodes-29)')
edit('<<",\\\"native_successes\\\":"<<wins_native<<",\\\"rom_successes\\\":"<<wins_rom','<<",\\\"native_successes\\\":null,\\\"rom_successes\\\":"<<wins_rom')
edit('<<",\\\"native_status\\\":"<<a.status','<<",\\\"native_status\\\":null"')
source=root/'rom_only_tail.cpp';source.write_text(s);binary=root/'rom_only_tail'
cmd=['clang++','-O2','-std=c++17','-Iocean/mario_sim','-Isrc','-Iraylib-5.5_linux_amd64/include','-Iocean/retro/nes_emu',str(source),'build/mario_sim/fpg/policy.o','build/mario_sim/runtime/logic_cpu.a','build/retro/libquicknes.a','raylib-5.5_linux_amd64/lib/libraylib.a','-lGL','-lm','-ldl','-lpthread','-fopenmp','-o',str(binary)]
subprocess.run(cmd,cwd=repo,check=True)
model=repo/'checkpoints/mario_sim/sweep_1792016021935_0000/0000009999745024.bin'
command=['nice','-n','15',str(binary),str(model),str(root/'levels.ini'),str(root/'rom_only_5_2_tail'),'32','137','0','level:5-2','0','clear']
(root/'rom_only_launch.json').write_text(json.dumps({'command':command,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'purpose':'Complete two unfinished actual-ROM episodes while preserving the native parity failure; repeat episode 29 as a control.'},indent=2)+'\n')
with (root/'rom_only_5_2_tail.log').open('w') as log:subprocess.run(command,cwd=repo,stdout=log,stderr=subprocess.STDOUT,check=True)
print((root/'rom_only_5_2_tail/summary.json').read_text(),flush=True)
