"""Read-only checkpoint qualification against native full-state reference."""
import argparse
import ctypes as C
import json
from pathlib import Path
import numpy as np
import entity_agent as agent

def main():
    ap=argparse.ArgumentParser();ap.add_argument('checkpoint');ap.add_argument('--steps',type=int,default=720)
    ap.add_argument('--stochastic',action='store_true')
    args=ap.parse_args();build=Path(__file__).resolve().parents[1]/'build/entity_export'
    l=C.CDLL(str(build/'oracle.so'));ptr=C.c_void_p;i=C.c_int
    for name,params,ret in [
        ('oracle_create',[i],ptr),('oracle_snapshot',[ptr],ptr),('oracle_string_free',[ptr],None),
        ('oracle_view',[ptr,i,ptr],None),('oracle_step',[ptr,ptr],None),
        ('oracle_model',[C.c_char_p],ptr),('oracle_forward',[ptr,ptr,ptr],None),
        ('export_action',[ptr,i,ptr,i,C.POINTER(agent.Action)],None),
        ('export_debug',[ptr,ptr,ptr],None),('export_free',[ptr],None),
        ('oracle_rng',[ptr,C.c_uint32],None),('export_rng',[ptr],C.c_uint32)]:
        f=getattr(l,name);f.argtypes=params;f.restype=ret
    max_error=0.0
    for seed in (7,42):
        ctx=l.oracle_create(seed)
        controllers=[agent.Controller(build/'entity_bridge.so') for _ in range(2)]
        models=[agent.EntityModel(args.checkpoint) for _ in range(2)]
        refs=[l.oracle_model(str(Path(args.checkpoint).resolve()).encode()) for _ in range(2)]
        assert all(refs)
        for step in range(args.steps-1):
            raw=l.oracle_snapshot(ctx)
            state=json.loads(C.string_at(raw));l.oracle_string_free(raw)
            pair=(agent.Action*2)()
            for p in range(2):
                public={k:v for k,v in state.items() if k not in ('privates','done')}
                public.update(player=p,private=state['privates'][p])
                actual=controllers[p].observe(public)
                expected=np.empty(1424,np.float32);l.oracle_view(ctx,p,expected.ctypes.data)
                np.testing.assert_array_equal(actual,expected,err_msg=f'obs seed={seed} step={step} p={p}')
                logits=models[p].forward(actual);ref=np.empty(1979,np.float32)
                l.oracle_forward(refs[p],expected.ctypes.data,ref.ctypes.data)
                max_error=max(max_error,float(np.max(np.abs(logits-ref))))
                np.testing.assert_allclose(logits,ref,atol=.002,rtol=.0003)
                # Align each seat's CPU RNG, then compare both its outputs and advance.
                det=int(not args.stochastic)
                l.oracle_rng(ctx,controllers[p].lib.export_rng(controllers[p].ctx))
                result=controllers[p].act(p,logits,bool(det))
                l.export_action(ctx,p,logits.ctypes.data,det,C.byref(pair[p]))
                assert result==agent.action_json(pair[p]),(seed,step,p,result,agent.action_json(pair[p]))
                aa=np.empty(47,np.float32);am=np.empty(1978,np.uint8)
                ba=np.empty_like(aa);bm=np.empty_like(am)
                controllers[p].lib.export_debug(controllers[p].ctx,aa.ctypes.data,am.ctypes.data)
                l.export_debug(ctx,ba.ctypes.data,bm.ctypes.data)
                np.testing.assert_array_equal(aa,ba);np.testing.assert_array_equal(am,bm)
                assert l.export_rng(ctx)==controllers[p].lib.export_rng(controllers[p].ctx)
            l.oracle_step(ctx,C.addressof(pair))
        print('PASS public/native observations, actions, masks, RNG; network logits',seed,args.steps-1,
              'stochastic',args.stochastic,'max_error',max_error,flush=True)
        for c in controllers:c.close()
        l.export_free(ctx)

if __name__=='__main__':main()
