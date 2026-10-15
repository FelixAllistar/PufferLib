"""Verify accelerated logits and identical real-game trajectories against scalar CPU."""
import ctypes as C
import json
from pathlib import Path
from oak_benchmark import adapter, load_team, ROOT

out=ROOT/'reports/pokemon_benchmarks/20261014'
team,_=load_team('zam_egg_mie',out)
packed=(C.c_uint8*30)(*[x for mon in team for x in mon])
libraries=[adapter(),adapter('benchmark_adapter_blas.so')]
for lib in libraries:
    lib.bench_logits.argtypes=[C.c_void_p,C.c_void_p]
maximum=0;steps=0
for checkpoint in [
    ROOT/'leagues/pokemon/ou_leads_rr50m/members/master/r000003-a001/weights.bin',
    ROOT/'leagues/pokemon/free4/members/master/r000011-a001/weights.bin']:
    handles=[lib.bench_create(str(checkpoint).encode(),0) for lib in libraries]
    try:
        for game in range(16):
            for lib,h in zip(libraries,handles):
                assert lib.bench_reset(h,99173+game,game%2,packed)==0
            result=0
            while result==0:
                states=[];logits=[]
                for lib,h in zip(libraries,handles):
                    state=(C.c_uint8*384)();lib.bench_state(h,state);states.append(bytes(state))
                    values=(C.c_float*169)();lib.bench_logits(h,values);logits.append(list(values))
                assert states[0]==states[1],'Changed game/draft trajectory'
                maximum=max(maximum,max(abs(a-b) for a,b in zip(*logits)))
                legal=(C.c_uint8*256)();n=libraries[0].bench_legal(handles[0],1-game%2,legal)
                choice=legal[steps%n]
                results=[]
                for lib,h in zip(libraries,handles):
                    selected=C.c_int();r=lib.bench_step(h,lib.bench_map(h,choice),C.byref(selected))
                    results.append((r,selected.value))
                assert results[0]==results[1],'Changed chosen action'
                result=results[0][0];assert result!=4;steps+=1
    finally:
        for lib,h in zip(libraries,handles):lib.bench_destroy(h)
assert maximum<1e-3,maximum
report={'games':32,'transitions':steps,'max_absolute_logit_error':maximum,'actions_and_native_states_identical':True}
(out/'blas-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
