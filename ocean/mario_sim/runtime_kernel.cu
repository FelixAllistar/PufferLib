// Reuse the full verification kernel with separately compiled runtime pages.
// generated_logic.h is the runtime declaration file supplied by -I at build time.
#include "cuda_replay.cu"

extern "C" __constant__ unsigned smb_runtime_contract[]={0x534d5231,1,sizeof(SmbLogic),SMB_DEBUG_OBS};

extern "C" __global__ void smb_runtime_actions(SmbLogic* states,const uint8_t* worlds,
        const uint32_t* world_ids,const float* actions,int count) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i>=count)return;
    float raw=actions[i];int action=(int)raw;
    if(!(raw>=0&&raw<64)||raw!=(float)action){states[i].fault=-5;return;}
    SmbLogic state;
    #pragma unroll 1
    for(unsigned k=0;k<sizeof(state);k++)((uint8_t*)&state)[k]=((const uint8_t*)&states[i])[k];
    smb_logic_frame(&state,worlds+(size_t)world_ids[i]*SMB_PRG,(action&3)|((action&60)<<2));
    #pragma unroll 1
    for(unsigned k=0;k<sizeof(state);k++)((uint8_t*)&states[i])[k]=((const uint8_t*)&state)[k];
}

extern "C" __global__ void smb_runtime_step(SmbLogic* states,const uint8_t* worlds,
        const unsigned* world_ids,const uint8_t* buttons,unsigned* active,int count,int frames) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i>=count)return;
    SmbLogic* s=&states[i];
    const uint8_t* data=worlds+(size_t)world_ids[i]*SMB_PRG;
    unsigned running=0;
    for(int t=0;t<frames&&!s->fault;t++) {
        smb_logic_frame(s,data,buttons[i]);
        running+=s->ram[0x770]==1&&s->ram[0x772]==3&&s->ram[0xe]==8;
    }
    active[i]+=running;
}

extern "C" __global__ void smb_runtime_step_local(SmbLogic* states,const uint8_t* worlds,
        const unsigned* world_ids,const uint8_t* buttons,unsigned* active,int count,int frames) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i>=count)return;
    SmbLogic state;
    #pragma unroll 1
    for(unsigned k=0;k<sizeof(state);k++)((uint8_t*)&state)[k]=((const uint8_t*)&states[i])[k];
    const uint8_t* data=worlds+(size_t)world_ids[i]*SMB_PRG;
    unsigned running=0;
    for(int t=0;t<frames&&!state.fault;t++) {
        smb_logic_frame(&state,data,buttons[i]);
        running+=state.ram[0x770]==1&&state.ram[0x772]==3&&state.ram[0xe]==8;
    }
    active[i]+=running;
    #pragma unroll 1
    for(unsigned k=0;k<sizeof(state);k++)((uint8_t*)&states[i])[k]=((const uint8_t*)&state)[k];
}

extern "C" __global__ void smb_runtime_step_shared(SmbLogic* states,const uint8_t* worlds,
        const unsigned* world_ids,const uint8_t* buttons,unsigned* active,int count,int frames) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i>=count)return;
    extern __shared__ __align__(16) uint8_t storage[];
    SmbLogic* s=(SmbLogic*)storage+threadIdx.x;
    #pragma unroll 1
    for(unsigned k=0;k<sizeof(*s);k++)((uint8_t*)s)[k]=((const uint8_t*)&states[i])[k];
    const uint8_t* data=worlds+(size_t)world_ids[i]*SMB_PRG;
    unsigned running=0;
    for(int t=0;t<frames&&!s->fault;t++) {
        smb_logic_frame(s,data,buttons[i]);
        running+=s->ram[0x770]==1&&s->ram[0x772]==3&&s->ram[0xe]==8;
    }
    active[i]+=running;
    #pragma unroll 1
    for(unsigned k=0;k<sizeof(*s);k++)((uint8_t*)&states[i])[k]=((const uint8_t*)s)[k];
}
