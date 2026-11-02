// Parity check for bc_train.py (2026-11-01): runs the CPU policy over the first N records of a tas_bc_data file in
// order (recurrent state from zero) and prints the 65 outputs (64 action logits + value) per record.
// Usage: policy_logits CHECKPOINT DATA.bin N HIDDEN LAYERS
#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#undef PUFFERLIB_BUILD_MAIN
typedef void RetroPolicy;
extern "C" {
RetroPolicy* retro_cpu_load(const char* path, int hidden, int layers);
const float* retro_cpu_logits(RetroPolicy* policy, const float* obs);
}
int main(int argc,char** argv) {
    if(argc<6) { fprintf(stderr,"usage: policy_logits CHECKPOINT DATA.bin N HIDDEN LAYERS\n"); return 1; }
    RetroPolicy* net=retro_cpu_load(argv[1],atoi(argv[4]),atoi(argv[5]));
    if(!net) { fprintf(stderr,"load failed\n"); return 1; }
    FILE* f=fopen(argv[2],"rb"); int header[2]; if(!f||fread(header,sizeof(int),2,f)!=2) return 1;
    std::vector<float> obs(header[1]); int n=atoi(argv[3]);
    for(int i=0;i<n&&i<header[0];i++) {
        int rec[3]; if(fread(rec,sizeof(int),3,f)!=3||fread(obs.data(),sizeof(float),header[1],f)!=(size_t)header[1]) break;
        const float* out=retro_cpu_logits(net,obs.data());
        for(int k=0;k<65;k++) printf("%.6g%c",out[k],k==64?'\n':' ');
    }
    return 0;
}
