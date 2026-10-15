#define SMB_HEADLESS
#include "mario_sim.h"
#include "test_common.h"
static void check_sweep_score() {
    Log log={};Dict metrics={};
    log.game_episodes=4;log.game_progress_pixels=4*3400;
    log.fpg_episodes=20;log.curriculum_frames=20*214;
    puf_log(&log,&metrics);
    fpt_check(fabs(dict_get(&metrics,"sweep_score")-.875)<1e-6,"sweep score mixes task denominators");
    log.episode_return=1e6;log.target_frames=1e6;log.successes=20;
    puf_log(&log,&metrics);
    fpt_check(fabs(dict_get(&metrics,"sweep_score")-.875)<1e-6,"reward or timing allowance changed sweep score");
    log.game_progress_pixels*=2;
    puf_log(&log,&metrics);
    fpt_check(fabs(dict_get(&metrics,"sweep_score")-1.625)<1e-6,"sweep score caps multi-area game progress");
    // vec_log normalizes all counters by the combined episode count first.
    for(size_t k=0;k<sizeof(Log)/sizeof(float);k++)((float*)&log)[k]/=24;
    puf_log(&log,&metrics);
    fpt_check(fabs(dict_get(&metrics,"sweep_score")-1.625)<1e-6,"normalized logs changed sweep score");
    log={};log.fpg_episodes=20;log.curriculum_frames=60;
    puf_log(&log,&metrics);
    fpt_check(dict_get(&metrics,"sweep_score")==0,"initial FPG depth counted as advancement");
    log={};puf_log(&log,&metrics);
    fpt_check(dict_get(&metrics,"sweep_score")==0,"empty log has nonzero sweep score");
    dict_clear(&metrics);
}
int main() {
    try {
        check_sweep_score();
        Ini ini={};puf_ini_load_file(&ini,"config/mario_sim.ini");
        dict_set(puf_ini_section(&ini,"vec",0),"total_agents",4);
        dict_set(puf_ini_section(&ini,"game",0),"max_frames",17);
        dict_set(puf_ini_section(&ini,"fpg",0),"max_frames",13);
        dict_set(puf_ini_section(&ini,"fpg",0),"curriculum_resume",1);
        fpt_configure(&ini,"train",nullptr);
        Env envs[4]={};float obs[4][FPT_OBS],actions[4]={},rewards[4]={},terminals[4]={};
        for(int i=0;i<4;i++) {
            auto* e=&envs[i];e->rng=i;e->agents[0].observations=obs[i];e->agents[0].actions=&actions[i];
            e->agents[0].rewards=&rewards[i];e->agents[0].terminals=&terminals[i];
            puf_init(e,puf_ini_section(&ini,"env",0));puf_reset(e);
            fpt_check(e->is_fpg==(i==3),"CPU pool allocation wrong");
        }
        fpt_check(fpt_host->task.max_frames==13&&fpt_host->curriculum.window==16&&fpt_host->time.bonus==.1f,"FPG section was not parsed");
        fpt_check(fpt_host->game.max_frames==17&&fpt_host->game.time_bonus==.1f,"game section was not parsed");
        for(int step=0;step<100;step++)for(auto& e:envs)puf_step(&e);
        for(int i=0;i<3;i++)fpt_check(envs[i].log.game_episodes==5&&envs[i].log.fpg_episodes==0,"game timeout/lifecycle wrong");
        fpt_check(envs[3].log.fpg_episodes>0&&envs[3].log.game_episodes==0,"FPG lifecycle wrong");
        Log log={};for(auto& e:envs)for(size_t k=0;k<sizeof(Log)/sizeof(float);k++)((float*)&log)[k]+=((float*)&e.log)[k];
        Dict metrics={};puf_log(&log,&metrics);fpt_check(dict_get(&metrics,"game_timeouts")==1,"mixed metrics used combined episode denominator");dict_clear(&metrics);
        fpt_test_old_progress("build/mario_sim/fpg_only_test_modes.bin",fpt_host->bank->header.payload_hash,fpt_host->curriculum);
        fpt_cpu_load("build/mario_sim/fpg_only_test_modes.bin",&ini);
        fpt_check(fpt_host->curriculum.depths[envs[3].curriculum.level]==96,"CPU legacy curriculum restore failed");
        const char* path="build/mario_sim/mixed_test_cpu_checkpoint.bin";{std::ofstream out(path);out<<"CPU mixed test";}
        fpt_cpu_save(path,nullptr);auto saved=fpt_read_progress(path);fpt_check(saved.progress.size()==1,"CPU checkpoint included game actors");
        for(auto& e:envs)puf_close(&e);puf_ini_free(&ini);
        printf("{\"CPU_modes\":true,\"config_sections\":true,\"mixed_metrics\":true,\"sweep_score\":true,\"legacy_resume\":true,\"failures\":0}\n");return 0;
    }catch(const std::exception& e){fprintf(stderr,"CPU modes test: %s\n",e.what());return 1;}
}
