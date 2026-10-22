#define SMB_HEADLESS
#include "mario_sim.h"
#include "test_common.h"

int main() {
    try {
        SmbPracticeProgress p={};p.stage=14;
        for(int i=0;i<8;i++)smb_practice_result(&p,1);
        fpt_check(p.level[14]==1,"practice mastery did not move the start back");
        p.sampled=0;for(int i=0;i<24;i++)smb_practice_result(&p,1);
        fpt_check(p.level[14]==1&&p.attempts[14]==0,"easy replay promoted difficulty");
        p.sampled=1;for(int i=0;i<16;i++)smb_practice_result(&p,0);
        fpt_check(p.level[14]==0,"practice forgetting did not lower difficulty");
        Ini ini={};puf_ini_load_file(&ini,"config/mario_sim.ini");
        dict_set(puf_ini_section(&ini,"vec",0),"total_agents",4);
        auto* game=puf_ini_section(&ini,"game",0);dict_set(game,"practice_fraction",.25);
        dict_set(game,"max_frames",17);dict_set(game,"practice_max_frames",13);
        dict_set_str(game,"practice_bank","build/mario_sim/practice/bank.bin");
        fpt_configure(&ini,"train",nullptr);
        Env env[4]={};float obs[4][FPT_OBS],actions[4]={},rewards[4]={},terminals[4]={};
        for(int i=0;i<4;i++) {
            env[i].rng=i;env[i].agents[0].observations=obs[i];env[i].agents[0].actions=&actions[i];
            env[i].agents[0].rewards=&rewards[i];env[i].agents[0].terminals=&terminals[i];
            puf_init(&env[i],puf_ini_section(&ini,"env",0));puf_reset(&env[i]);
            fpt_check(env[i].is_fpg==(i==3)&&env[i].is_practice==(i==2),"50/25/25 pool allocation failed");
        }
        auto& e=env[2];
        fpt_check(e.game.progress_pixels==0&&e.episode.episode_return==0,"mid-level reset paid progress");
        for(int t=0;t<100;t++)for(auto& x:env)puf_step(&x);
        fpt_check(e.log.practice_episodes>0&&!e.log.game_episodes&&!e.log.fpg_episodes,"practice metrics contaminated ordinary tests");
        fpt_check(env[0].log.game_episodes==5&&env[1].log.game_episodes==5,"ordinary game horizon changed");
        e.practice.level[14]=2;e.practice.attempts[14]=3;e.practice.wins[14]=1;
        const char* checkpoint="build/mario_sim/fpg/practice_test.bin";{std::ofstream out(checkpoint);out<<"practice weights";}
        fpt_cpu_save(checkpoint,nullptr);auto saved=smb_read_practice(checkpoint,fpt_host->practice_bank_hash);
        fpt_check(saved.size()==1&&saved[0].level[14]==2&&saved[0].wins[14]==1,"practice checkpoint roundtrip failed");
        e.practice={};fpt_cpu_load(checkpoint,&ini);
        fpt_check(e.practice.level[14]==2&&e.practice.attempts[14]==3,"practice restore lost mastery");
        bool rejected=false;try{smb_read_practice(checkpoint,fpt_host->practice_bank_hash^1);}catch(const std::exception&){rejected=true;}
        fpt_check(rejected,"practice sidecar accepted a different bank");
        // First-life practice ends at death; ordinary games retain their lives.
        e.episode.status=0;e.game.deaths=0;e.state->ram[0xe]=11;
        smb_after_frame(&e,e.state,&fpt_host->task,&fpt_host->time,&fpt_host->curriculum,
            fpt_host->table.data(),&fpt_host->game,&fpt_host->practice);
        fpt_check(e.episode.status==SMB_EPISODE_DEAD,"encounter practice continued after first death");
        for(auto& x:env)puf_close(&x);
        fpt_configure(&ini,"eval",nullptr);fpt_setup(puf_ini_section(&ini,"env",0));
        fpt_check(fpt_host->practice.fraction==0,"ordinary evaluation silently used late-level starts");
        fpt_host.reset();puf_ini_free(&ini);
        puts("PASS practice pools, no reset rewards, metric isolation, curriculum retention, sidecars and ordinary evaluation");return 0;
    }catch(const std::exception& e){fprintf(stderr,"practice test: %s\n",e.what());return 1;}
}
