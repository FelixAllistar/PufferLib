#include "cuda_replay.h"
#include "generated_logic.h"
__global__ void replay(SmbLogic* states,const uint8_t* data,const SmbTraceCase* cases,
        const int* offsets,const SmbTraceFrame* frames,SmbReplayResult* results,int count,int begin,int end) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=count)return;
    SmbLogic* s=&states[i];SmbReplayResult result;
    if(!begin){result={-1,-1,0};result.fault=smb_scene_reset(s,&cases[i].scene);}
    else result=results[i];
    if(result.fault||result.field>=0){results[i]=result;return;}
    for(unsigned t=begin;t<cases[i].frames&&t<(unsigned)end&&!result.fault;t++) {
        const auto& f=frames[offsets[i]+t];result.fault=smb_logic_frame(s,data,f.buttons);
        if(result.fault){result.frame=t;break;}
        for(int k=0;k<SMB_RAM;k++)if(s->ram[k]!=f.ram[k]){result.frame=t;result.field=k;break;}
        if(result.field>=0)break;
        if(s->pc!=f.pc||s->a!=f.a||s->x!=f.x||s->y!=f.y||(s->p&207)!=f.p||s->sp!=f.sp
            ||s->timing.timestamp!=f.timestamp||s->timing.video_frame!=f.video_frame
            ||s->timing.control!=f.control||s->timing.mask!=f.mask) {
            result.frame=t;result.field=SMB_RAM*2;break;
        }
        // Independently compare the bit pattern of every debug observation.
        // Avoid a per-thread 8 KiB temporary while retaining the full contract.
        for(int k=0;k<SMB_DEBUG_OBS;k++) {
            float native=(float)s->ram[k]*(1.0f/256.0f);
            float expected=(float)f.ram[k]/256.0f;
            if(__float_as_uint(native)!=__float_as_uint(expected)){result.frame=t;result.field=k+SMB_RAM;break;}
        }
        if(result.field>=0)break;
    }
    results[i]=result;
}
