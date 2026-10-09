#define SMB_HEADLESS
#include "mario_sim.h"
#include "test_common.h"

static void promotion_controls() {
    FptCurriculumConfig c={};c.enabled=c.adaptive=1;c.window=4;c.confirmations=2;c.threshold=.75f;
    FptCurriculumProgress p;fpt_curriculum_init(&p,&c);
    for(int k=0;k<4;k++)fpt_check(!fpt_curriculum_result(&p,&c,k!=3),"promoted before confirmation window");
    fpt_check(p.streak==1&&p.level==0,"first mastery window was lost");
    for(int k=0;k<4;k++)fpt_curriculum_result(&p,&c,0);
    fpt_check(p.streak==0&&p.level==0,"failed window did not reset mastery streak");
    for(int k=0;k<8;k++)fpt_curriculum_result(&p,&c,k%4!=3);
    fpt_check(p.level==1&&p.promotions==1&&p.attempts==0,"two successful windows did not advance one band");
    p.sampled_level=0;auto saved=p;
    for(int k=0;k<64;k++)fpt_curriculum_result(&p,&c,1);
    fpt_check(!memcmp(&p,&saved,sizeof(p)),"easier practice caused promotion");
    p.sampled_level=p.level;c.adaptive=0;saved=p;
    for(int k=0;k<64;k++)fpt_curriculum_result(&p,&c,1);
    fpt_check(!memcmp(&p,&saved,sizeof(p)),"fixed evaluation advanced the curriculum");
    c.adaptive=1;p.level=p.sampled_level=FPT_CURRICULUM_LEVELS-1;
    for(int k=0;k<64;k++)fpt_curriculum_result(&p,&c,1);
    fpt_check(p.level==FPT_CURRICULUM_LEVELS-1&&p.streak<=c.confirmations,"curriculum exceeded final band");
}
int main() {
    try {
        promotion_controls();Dict options={};dict_set(&options,"curriculum",1);dict_set(&options,"curriculum_window",2);
        dict_set(&options,"curriculum_confirmations",2);dict_set(&options,"curriculum_threshold",1);
        Env env={};float obs[FPT_OBS],action=0,reward=0,terminal=0;
        env.agents[0].observations=obs;env.agents[0].actions=&action;env.agents[0].rewards=&reward;env.agents[0].terminals=&terminal;
        puf_init(&env,&options);puf_reset(&env);auto teacher=fpt_test_teachers()[0];
        fpt_check(fpt_host->eligible.size()==423&&fpt_host->table[env.episode.scene].best_frames==3,"first reset did not start at the easy frontier");
        fpt_check(env.state->ram[0x1d]==0,"first curriculum level is not naturally grounded");
        auto changed_times=fpt_host->table;changed_times[0].best_frames--;auto changed_config=fpt_host->curriculum;bool changed_rejected=false;
        try{fpt_curriculum_indices(*fpt_host->bank,changed_times,&changed_config);}catch(const std::exception&){changed_rejected=true;}
        fpt_check(changed_rejected,"altered time estimates silently changed curriculum difficulty");
        unsigned decisions=0,successes=0,final_finishes=0,practice=0;
        while(env.curriculum.level<FPT_CURRICULUM_LEVELS-1||final_finishes<2) {
            fpt_check(decisions<20000,"scripted curriculum failed to reach final band");
            int remaining=fpt_host->table[env.episode.scene].best_frames,offset=425-remaining+env.episode.frames;
            int level=env.curriculum.level,sampled=env.curriculum.sampled_level;
            fpt_check(offset>=0&&offset<425&&remaining>=3&&remaining<=fpt_host->curriculum.depths[sampled],"curriculum sampled outside its band");
            if(sampled)fpt_check(remaining>fpt_host->curriculum.depths[sampled-1],"curriculum band overlaps an easier band");
            int b=teacher.frames[offset].buttons;action=(float)((b&3)|((b>>2)&60));puf_step(&env);decisions++;
            if(terminal) {
                fpt_check(reward>1&&env.curriculum.level>=level&&env.curriculum.level<=level+1,"teacher success/promotion mismatch");
                successes++;practice+=sampled<level;final_finishes+=sampled==FPT_CURRICULUM_LEVELS-1;
            }else fpt_check(reward==0,"curriculum introduced dense reward");
        }
        fpt_check(env.curriculum.promotions==FPT_CURRICULUM_LEVELS-1&&practice>0,"missing promotion or retention coverage");
        const char* checkpoint="build/mario_sim/fpg/curriculum_test_checkpoint.bin";
        {std::ofstream out(checkpoint,std::ios::binary);out<<"curriculum test checkpoint";}
        fpt_cpu_save(checkpoint,nullptr);auto saved=fpt_read_progress(checkpoint);auto expected=env.curriculum;
        fpt_check(saved.progress.size()==1&&!memcmp(&saved.progress[0],&expected,sizeof(expected)),"checkpoint progress roundtrip failed");
        Ini ini={};dict_set(puf_ini_section(&ini,"env",1),"curriculum_resume",1);env.curriculum={};fpt_cpu_load(checkpoint,&ini);
        fpt_check(env.curriculum.level==expected.level&&env.curriculum.attempts==expected.attempts
            &&env.curriculum.wins==expected.wins&&env.curriculum.streak==expected.streak,"CPU checkpoint did not restore mastery state");
        bool rejected=false;try{fpt_check_resume(saved,fpt_host->bank->header.payload_hash^1);}catch(const std::exception&){rejected=true;}
        fpt_check(rejected,"curriculum from another bank was accepted");
        {std::ofstream out(checkpoint,std::ios::app);out<<'X';}
        rejected=false;try{fpt_read_progress(checkpoint);}catch(const std::exception&){rejected=true;}
        fpt_check(rejected,"curriculum accepted different model weights");
        puf_close(&env);dict_clear(&options);puf_ini_free(&ini);
        // Evaluation freezes both advancement and easier replay, independent of
        // train settings, so its rate is measured at one declared difficulty.
        Ini eval={};dict_set_str(puf_ini_section(&eval,"env",1),"mode","fpg");
        dict_set(puf_ini_section(&eval,"fpg",1),"curriculum",1);dict_set(puf_ini_section(&eval,"fpg",0),"curriculum_eval_frames",32);
        fpt_configure(&eval,"eval",nullptr);fpt_setup(puf_ini_section(&eval,"env",0));
        fpt_check(!fpt_host->curriculum.adaptive&&fpt_host->curriculum.replay_fraction==0
            &&fpt_host->curriculum.depths[fpt_host->curriculum.initial_level]==32,"evaluation did not freeze requested difficulty");
        fpt_host.reset();fpt_configure(&eval,"train",nullptr);puf_ini_free(&eval);
        printf("{\"curriculum_levels\":%d,\"decisions\":%u,\"teacher_successes\":%u,\"promotions\":%d,\"practice_episodes\":%u,\"checkpoint_roundtrip\":true,\"fixed_evaluation\":true,\"failures\":0}\n",
            FPT_CURRICULUM_LEVELS,decisions,successes,FPT_CURRICULUM_LEVELS-1,practice);return 0;
    }catch(const std::exception& e){fprintf(stderr,"curriculum CPU test: %s\n",e.what());return 1;}
}
