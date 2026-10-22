#include "../training_config.h"
#include <cassert>

int main() {
    Ini ini={};puf_ini_load_env(&ini,"retro",0,nullptr);
    // Validate the native merged config, including inherited global ranges.
    // Reading only retro.ini misses sweep dimensions from default.ini.
    // The all-levels restart sweep (reports/retro/restart_plan_20261021/).
    const char* varying[]={"env.archive_start","env.archive_end","env.archive_anneal_fraction",
        "env.archive_priority","env.archive_capture_interval","env.archive_cell_width",
        "env.archive_level_focus","env.archive_depth_priority","env.maze_loop_penalty",
        "train.learning_rate","train.ent_coef"};
    int varying_count=0;
    for(int i=0;i<ini.num_sections;i++) {
        Dict* section=&ini.sections[i];
        if(strncmp(section->name,"sweep.",6))continue;
        double lo=dict_get(section,"min"),hi=dict_get(section,"max");
        if(lo==hi) {
            const char* path=section->name+6;const char* dot=strrchr(path,'.');assert(dot);
            char name[64];snprintf(name,sizeof(name),"%.*s",(int)(dot-path),path);
            assert(lo==puf_ini_get(&ini,name,dot+1)||!strcmp(path,"train.total_timesteps"));
            continue;
        }
        bool allowed=false;for(auto key:varying)allowed|=!strcmp(section->name+6,key);
        assert(allowed&&"unexpected inherited sweep dimension");varying_count++;
    }
    assert(varying_count==11);
    assert(puf_ini_get(&ini,"train","anneal_lr")==0);
    assert(puf_ini_get(&ini,"train","anneal_ent_coef")==0);
    puf_ini_put(&ini,"train.total_timesteps","30000000");
    const char* error=retro_training_config_error(&ini,4);
    if(error)fprintf(stderr,"baseline rejected: %s\n",error);
    assert(!error);
    printf("Pixel baseline: estimated %.0f MiB\n",retro_training_memory_mb(&ini,4));

    puf_ini_put(&ini,"vec.total_agents","32768");
    puf_ini_put(&ini,"train.horizon","8");
    puf_ini_put(&ini,"base.async","0");
    puf_ini_put(&ini,"train.minibatch_size","8192");
    assert(!retro_training_config_error(&ini,4));
    printf("32K actors, H8, synchronous: estimated %.0f MiB\n",retro_training_memory_mb(&ini,4));
    puf_ini_put(&ini,"train.horizon","512");
    assert(retro_training_config_error(&ini,4));

    puf_ini_put(&ini,"vec.total_agents","128");
    puf_ini_put(&ini,"train.horizon","8");
    puf_ini_put(&ini,"train.minibatch_size","32768");
    assert(retro_training_config_error(&ini,4));
    puf_ini_put(&ini,"train.minibatch_size","1024");
    puf_ini_put(&ini,"train.replay_ratio","0.25");
    assert(retro_training_config_error(&ini,4));
    puf_ini_put(&ini,"train.replay_ratio","2");
    assert(!retro_training_config_error(&ini,4));
    puf_ini_put(&ini,"train.vtrace","1");
    puf_ini_put(&ini,"train.vtrace_rho_clip","1");
    puf_ini_put(&ini,"train.vtrace_c_clip","2");
    assert(retro_training_config_error(&ini,4));
    puf_ini_put(&ini,"train.vtrace_c_clip","1");
    puf_ini_put(&ini,"train.total_timesteps","512");
    assert(retro_training_config_error(&ini,4));

    puf_ini_put(&ini,"train.total_timesteps","30000000");
    puf_ini_put(&ini,"sweep.memory_limit_mb","0");
    puf_ini_put(&ini,"vec.total_agents","1024");
    puf_ini_put(&ini,"train.horizon","512");
    puf_ini_put(&ini,"train.replay_ratio","4");
    assert(retro_training_config_error(&ini,4));
    puf_ini_put(&ini,"base.cudagraphs","-1");
    assert(!retro_training_config_error(&ini,4));

    Dict early={};dict_set(&early,"agent_steps",100);dict_set(&early,"uptime",1);
    dict_set(&early,"SPS",100);retro_training_progress=tmpfile();assert(retro_training_progress);
    retro_training_log(&early);
    fclose(retro_training_progress);retro_training_progress=nullptr;dict_clear(&early);
    puf_ini_free(&ini);
    puts("Retro sweep preflight passed");
}
