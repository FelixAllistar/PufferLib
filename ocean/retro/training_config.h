#pragma once
#include "../../src/ini.h"
#include "retro_policy_shape.h"
#include <cmath>
#include <csignal>
#include <cstdio>

// Three observation copies in asynchronous FP32 training dominate Retro's
// memory use. Include encoder/recurrent caches and bounded graph overhead too.
static inline double retro_training_memory_mb(Ini* ini, int precision) {
    double a=puf_ini_get(ini,"vec","total_agents");
    double t=puf_ini_get(ini,"train","horizon");
    double m=puf_ini_get(ini,"train","minibatch_size");
    double h=puf_ini_get(ini,"policy","hidden_size");
    double l=puf_ini_get(ini,"policy","num_layers");
    double slots=1+(puf_ini_get(ini,"base","async")!=0);
    double image=0,partials=0;
    for(auto c:RETRO_CONVS) {
        image+=c.oh*c.ow*c.oc;
        double splits=std::ceil(m*c.oh*c.ow/1024.0);
        partials=std::fmax(partials,splits*c.oc*(c.k*c.k*c.ic+1));
    }
    double encoder=image+RETRO_RAM_INPUTS+RETRO_RAM_HIDDEN+RETRO_FUSED_FEATURES+h;
    double bytes=(slots+1)*a*t*(precision*(RETRO_POLICY_INPUTS+3+64)+8);
    bytes+=precision*(a*(encoder+(3*l+4)*h)
        +m*(encoder+image+RETRO_FUSED_FEATURES+RETRO_RAM_HIDDEN+(10*l+4)*h+130)
        +(slots+2)*a*l*h+(m/t)*(2*l+1)*h);
    bytes+=4*(partials+a*RETRO_POLICY_INPUTS);
    bytes+=20*retro_policy_weights((int)h,(int)l);
    if(puf_ini_get(ini,"base","cudagraphs")>=0)
        bytes+=std::floor(puf_ini_get(ini,"train","replay_ratio")*a*t/m)*l*t*4000;
    return 1.08*bytes/(1024*1024)+768;
}

static inline const char* retro_training_config_error(Ini* ini, int precision) {
    const char* sections[]={"vec","vec","vec","policy","policy","train","train","train"};
    const char* keys[]={"total_agents","num_buffers","num_threads","hidden_size","num_layers",
        "horizon","minibatch_size","total_timesteps"};
    for(int i=0;i<8;i++) {
        double x=puf_ini_get(ini,sections[i],keys[i]);
        if(!std::isfinite(x)||x<1||x>1e12||std::floor(x)!=x)return "invalid integer training shape";
    }
    long long a=puf_ini_get(ini,"vec","total_agents"),b=puf_ini_get(ini,"vec","num_buffers");
    long long n=puf_ini_get(ini,"vec","num_threads"),t=puf_ini_get(ini,"train","horizon");
    long long m=puf_ini_get(ini,"train","minibatch_size"),h=puf_ini_get(ini,"policy","hidden_size");
    long long l=puf_ini_get(ini,"policy","num_layers");
    if(a>1000000000/t||a%b||n<b||n%b)return "invalid actor/buffer/thread layout";
    if(t%(precision==2?8:4)||m%t||m/t>a||a%(m/t))return "invalid minibatch division or vector alignment";
    if(h<32||h>1024||h%32||l>8)return "unsupported pixel policy shape";
    if(strcmp(puf_ini_get_str(ini,"policy","architecture"),"puffernet"))return "Retro requires puffernet";
    if(puf_ini_get(ini,"train","gpus")!=1)return "Retro sweep requires one GPU per trial";
    double replay=puf_ini_get(ini,"train","replay_ratio");
    if(!std::isfinite(replay)||replay<=0||replay*a*t/m<1)return "zero optimizer updates";
    if(puf_ini_get(ini,"train","total_timesteps")<a*t)return "budget smaller than one rollout";
    if(puf_ini_get(ini,"train","vtrace")&&puf_ini_get(ini,"train","vtrace_c_clip")>
            puf_ini_get(ini,"train","vtrace_rho_clip"))return "vtrace c must not exceed rho";
    double limit=puf_ini_get(ini,"sweep","max_train_graph_work");
    if(limit>0&&puf_ini_get(ini,"base","cudagraphs")>=0&&std::floor(replay*a*t/m)*l*t>limit)
        return "training graph exceeds max_train_graph_work";
    limit=puf_ini_get(ini,"sweep","memory_limit_mb");
    if(limit>0&&retro_training_memory_mb(ini,precision)>limit)return "estimated VRAM exceeds memory_limit_mb";
    return nullptr;
}

static volatile sig_atomic_t retro_training_stop_requested;
static FILE* retro_training_progress;
static void retro_request_training_stop(int){retro_training_stop_requested=1;}
static inline void retro_training_start(Ini* ini) {
#ifdef RETRO_ARCHIVE_AVAILABLE
    retro_archive_bank().budget=puf_ini_get(ini,"train","total_timesteps");
#endif
    if(puf_ini_get(ini,"base","result_fd")<=0)return;
    signal(SIGUSR1,retro_request_training_stop);
    char path[4096];snprintf(path,sizeof(path),"%s/retro/%s.progress.csv",
        puf_ini_get_str(ini,"base","log_dir"),puf_ini_get_str(ini,"base","run_id"));
    retro_training_progress=fopen(path,"w");
    if(!retro_training_progress){perror(path);exit(1);}
    fprintf(retro_training_progress,"steps,seconds,sps,entropy,kl,value,clipfrac,vram_used_gb,archive_cells,archive_phase,level_steps,full_steps,archive_steps\n");
    fflush(retro_training_progress);
}
static inline void retro_training_log(Dict* log) {
#ifdef RETRO_ARCHIVE_AVAILABLE
    auto& bank=retro_archive_bank();
    std::lock_guard<std::mutex> lock(bank.mutex);
    dict_set(log,"archive/cells",bank.cells.size());
    for(int i=0;i<3;i++)dict_set(log,i==0?"archive/level_steps":i==1?"archive/full_steps":"archive/reset_steps",bank.mode_steps[i].load());
#endif
    if(!retro_training_progress)return;
    const char* keys[]={"agent_steps","uptime","SPS","loss/entropy","loss/kl",
        "loss/value","loss/clipfrac","util/vram_used_gb"};
    double values[8];
    for(int i=0;i<8;i++){auto* item=dict_find(log,keys[i]);values[i]=item?item->value:NAN;}
    fprintf(retro_training_progress,"%.0f,%.6f,%.3f,%.9g,%.9g,%.9g,%.9g,%.6f",
        values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7]);
#ifdef RETRO_ARCHIVE_AVAILABLE
    fprintf(retro_training_progress,",%zu,%.6f,%llu,%llu,%llu\n",bank.cells.size(),
        std::min(1.0,std::max(bank.resumed_phase,(double)bank.steps.load()/std::max(1.0,bank.budget*bank.anneal))),
        (unsigned long long)bank.mode_steps[0].load(),(unsigned long long)bank.mode_steps[1].load(),
        (unsigned long long)bank.mode_steps[2].load());
#else
    fprintf(retro_training_progress,",0,0,0,0,0\n");
#endif
    fflush(retro_training_progress);
}

#define PUF_SWEEP_VALIDATE(ini) retro_training_config_error(ini, USE_BF16?2:4)
#define PUF_TRAIN_START(ini) retro_training_start(ini)
#define PUF_TRAIN_STOP() (retro_training_stop_requested!=0)
#define PUF_TRAIN_LOG(log) retro_training_log(log)
#define PUF_SWEEP_ACTUAL_BUDGET 1

#ifdef RETRO_ARCHIVE_AVAILABLE
static void retro_archive_checkpoint(const char* path,Ini* ini) {
    std::string metadata=std::string(path)+".ini";
    FILE* file=fopen((metadata+".tmp").c_str(),"w");
    if(!file)throw std::runtime_error("retro: checkpoint metadata write failed");
    puf_ini_write(file,ini);bool failed=ferror(file);if(fclose(file))failed=true;
    if(failed||rename((metadata+".tmp").c_str(),metadata.c_str()))throw std::runtime_error("retro: checkpoint metadata publish failed");
    auto* enabled=dict_find(puf_ini_section(ini,"env",0),"archive_enabled");
    if(!enabled||!enabled->value)return;
    RetroArchiveEpisode schedule;schedule.anneal=puf_ini_get(ini,"env","archive_anneal_fraction");
    retro_archive_file((std::string(path)+".archive.gz").c_str(),true,retro_archive_phase(schedule));
}
static void retro_archive_load(const char* path,Ini* ini) {
    auto* enabled=dict_find(puf_ini_section(ini,"env",0),"archive_enabled");
    if(!enabled||!enabled->value)return;
    std::string archive=std::string(path)+".archive.gz";
    FILE* f=fopen(archive.c_str(),"rb");if(!f)return;fclose(f);
    retro_archive_file(archive.c_str(),false);
    fprintf(stderr,"[retro] loaded %zu archived cells from %s\n",retro_archive_bank().cells.size(),archive.c_str());
}
#define PUF_CHECKPOINT_HOOK(path,ini) retro_archive_checkpoint(path,ini)
#define PUF_LOAD_HOOK(path,ini) retro_archive_load(path,ini)
#endif
