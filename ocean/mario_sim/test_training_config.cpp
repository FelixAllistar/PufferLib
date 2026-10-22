#include "mario_sim.h"
#include "training_config.h"
#include <assert.h>

int main(){
    Ini ini={};puf_ini_load_env(&ini,"mario_sim",0,nullptr);
    assert(!smb_training_config_error(&ini,2));
    puf_ini_put(&ini,"vec.total_agents","32768");
    puf_ini_put(&ini,"policy.hidden_size","64");
    puf_ini_put(&ini,"policy.num_layers","1");
    puf_ini_put(&ini,"train.horizon","16");
    puf_ini_put(&ini,"train.minibatch_size","8192");
    assert(!smb_training_config_error(&ini,2));
    printf("32K actors / 16 horizon / 64x1: estimated %.0f MiB\n",smb_training_memory_mb(&ini,2));
    puf_ini_put(&ini,"train.horizon","256");
    assert(smb_training_config_error(&ini,2)); // Huge rollout cannot fit.
    puf_ini_put(&ini,"train.horizon","16");
    puf_ini_put(&ini,"train.minibatch_size","1000");
    assert(smb_training_config_error(&ini,2)); // Invalid sequence division.
    puf_ini_put(&ini,"train.minibatch_size","8192");
    puf_ini_put(&ini,"policy.hidden_size","48");
    assert(smb_training_config_error(&ini,2)); // CPU/GPU policy compatibility.
    puf_ini_put(&ini,"policy.hidden_size","64");
    puf_ini_put(&ini,"train.vtrace","1");
    puf_ini_put(&ini,"train.vtrace_rho_clip","1");
    puf_ini_put(&ini,"train.vtrace_c_clip","2");
    assert(smb_training_config_error(&ini,2));
    puf_ini_put(&ini,"train.vtrace","0");
    puf_ini_put(&ini,"sweep.memory_limit_mb","0");
    puf_ini_put(&ini,"train.horizon","256");
    puf_ini_put(&ini,"train.minibatch_size","1024");
    puf_ini_put(&ini,"policy.num_layers","3");
    assert(smb_training_config_error(&ini,2)); // Graph bound independent of VRAM.
    Dict early={};dict_set(&early,"agent_steps",100);dict_set(&early,"uptime",1);
    dict_set(&early,"SPS",100);smb_training_progress=tmpfile();assert(smb_training_progress);
    smb_training_log(&early); // No completed episodes/loss snapshot is allowed.
    fclose(smb_training_progress);smb_training_progress=nullptr;dict_clear(&early);
    puf_ini_free(&ini);
    puts("Mario sweep preflight passed");
}
