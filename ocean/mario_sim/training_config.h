#pragma once
#include "../../src/ini.h"
#include "policy_shape.h"
#include <math.h>
#include <signal.h>
#include <stdio.h>

// Include semantic encoder caches and all three asynchronous rollout copies.
// Graph/context overhead is deliberately conservative; the worker also measures
// actual resident memory and throughput after initialization.
static inline double smb_training_memory_mb(Ini* ini, int precision) {
    double a=puf_ini_get(ini,"vec","total_agents");
    double t=puf_ini_get(ini,"train","horizon");
    double m=puf_ini_get(ini,"train","minibatch_size");
    double h=puf_ini_get(ini,"policy","hidden_size");
    double l=puf_ini_get(ini,"policy","num_layers");
    double slots=1+(puf_ini_get(ini,"base","async")!=0);
    double updates=floor(puf_ini_get(ini,"train","replay_ratio")*a*t/m);
    double bytes=(slots+1)*a*t*(precision*(FPT_OBS+4)+8+64);
    bytes+=precision*(a*(7117+h+(3*l+4)*h)
        +m*(12979+h+(10*l+4)*h+2*65)
        +(slots+2)*a*l*h+(m/t)*(2*l+1)*h);
    bytes+=4*(a*32+m*(32+49+210)); // pooling indices and reduction scratch
    bytes+=a*(sizeof(Env)+sizeof(SmbLogic)+4*FPT_OBS+128);
    bytes+=20*fpt_policy_weight_count((int)h,(int)l);
    // Graph nodes scale with unrolled recurrent training work, independently of
    // parameter count. Bound this separately so even small models cannot hang
    // for minutes constructing enormous graphs.
    if(puf_ini_get(ini,"base","cudagraphs")>=0)bytes+=updates*l*t*20000;
    return (1.12*bytes)/(1024*1024)+1024;
}

static inline const char* smb_training_config_error(Ini* ini, int precision) {
    const char* sections[]={"vec","vec","vec","policy","policy","train","train","train"};
    const char* keys[]={"total_agents","num_buffers","num_threads","hidden_size","num_layers",
        "horizon","minibatch_size","total_timesteps"};
    for(int i=0;i<8;i++){
        double x=puf_ini_get(ini,sections[i],keys[i]);
        if(!isfinite(x)||x<1||x>1e12||floor(x)!=x)return "invalid integer training shape";
    }
    long long a=puf_ini_get(ini,"vec","total_agents"),b=puf_ini_get(ini,"vec","num_buffers");
    long long n=puf_ini_get(ini,"vec","num_threads"),t=puf_ini_get(ini,"train","horizon");
    long long m=puf_ini_get(ini,"train","minibatch_size"),h=puf_ini_get(ini,"policy","hidden_size");
    long long l=puf_ini_get(ini,"policy","num_layers");
    if(a>1000000000/t||a%b||n<b||n%b)return "invalid actor/buffer/thread layout";
#ifdef PUFFER_GPU_ENV
    if(b!=1)return "CUDA simulator requires one vector buffer";
#endif
    if(t%(precision==2?8:4)||m%t||m/t>a||a%(m/t))return "invalid minibatch division or vector alignment";
    if(h<32||h>1024||h%32||l>8)return "unsupported semantic policy shape";
    if(strcmp(puf_ini_get_str(ini,"policy","architecture"),"puffernet"))return "Mario requires puffernet";
    if(puf_ini_get(ini,"train","gpus")!=1)return "Mario sweep worker requires one GPU per trial";
    double replay=puf_ini_get(ini,"train","replay_ratio");
    if(!isfinite(replay)||replay<=0||replay*a*t/m<1)return "zero optimizer updates";
    if(puf_ini_get(ini,"train","total_timesteps")<a*t)return "budget smaller than one rollout";
    if(puf_ini_get(ini,"train","vtrace")&&puf_ini_get(ini,"train","vtrace_c_clip")>
            puf_ini_get(ini,"train","vtrace_rho_clip"))return "vtrace c must not exceed rho";
    double limit=puf_ini_get(ini,"sweep","max_train_graph_work");
    if(limit>0&&puf_ini_get(ini,"base","cudagraphs")>=0&&floor(replay*a*t/m)*l*t>limit)
        return "training graph exceeds max_train_graph_work";
    limit=puf_ini_get(ini,"sweep","memory_limit_mb");
    if(limit>0&&smb_training_memory_mb(ini,precision)>limit)return "estimated VRAM exceeds memory_limit_mb";
    return NULL;
}

static volatile sig_atomic_t smb_training_stop_requested;
static FILE* smb_training_progress;
static void smb_request_training_stop(int){smb_training_stop_requested=1;}
static inline void smb_training_start(Ini* ini){
    if(puf_ini_get(ini,"base","result_fd")<=0)return;
    signal(SIGUSR1,smb_request_training_stop);
    char path[4096];snprintf(path,sizeof(path),"%s/mario_sim/%s.progress.csv",
        puf_ini_get_str(ini,"base","log_dir"),puf_ini_get_str(ini,"base","run_id"));
    smb_training_progress=fopen(path,"w");
    if(!smb_training_progress){perror(path);exit(1);}
    fprintf(smb_training_progress,"steps,seconds,sps,score,entropy,kl,value,clipfrac,vram_used_gb\n");
    fflush(smb_training_progress);
}
static inline void smb_training_log(Dict* log){
    if(!smb_training_progress)return;
    // Early logs can contain no completed episodes, so env/* is optional.
    const char* keys[]={"agent_steps","uptime","SPS","env/sweep_score","loss/entropy",
        "loss/kl","loss/value","loss/clipfrac","util/vram_used_gb"};
    double values[9];
    for(int i=0;i<9;i++){auto* item=dict_find(log,keys[i]);values[i]=item?item->value:NAN;}
    fprintf(smb_training_progress,"%.0f,%.6f,%.3f,%.9g,%.9g,%.9g,%.9g,%.9g,%.6f\n",
        values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7],values[8]);
    fflush(smb_training_progress);
}

#define PUF_SWEEP_VALIDATE(ini) smb_training_config_error(ini, USE_BF16?2:4)
#define PUF_TRAIN_START(ini) smb_training_start(ini)
#define PUF_TRAIN_STOP() (smb_training_stop_requested!=0)
#define PUF_TRAIN_LOG(log) smb_training_log(log)
#define PUF_SWEEP_ACTUAL_BUDGET 1
