#define FPG_HEADLESS
#include "mario_fpg.h"
#include "policy.h"
int main(int argc,char** argv) {
    if(argc!=4){fprintf(stderr,"usage: eval MODEL CONFIG EPISODES_PER_TIER\n");return 2;}
    Ini ini={0};puf_ini_load_file(&ini,argv[2]);Dict* env=puf_ini_section(&ini,"env",0);
    FpgConfig cfg=fpg_config(env);fpg_load_bank(env);
    int hidden=(int)puf_ini_get(&ini,"policy","hidden_size"),layers=(int)puf_ini_get(&ini,"policy","num_layers");
    int random_policy=!strcmp(argv[1],"random");
    void* policy=random_policy?NULL:fpg_policy_load(argv[1],hidden,layers);if(!random_policy&&!policy){fprintf(stderr,"invalid checkpoint\n");return 2;}
    char* end;long episodes=strtol(argv[3],&end,10);if(*end||episodes<1||episodes>10000)return 2;
    for(int split=1;split<=2;split++) for(int tier=0;tier<4;tier++) for(int deterministic=0;deterministic<=1;deterministic++) {
        cfg.split=split;cfg.fixed_tier=tier;FpgCurriculum curriculum={0};uint32_t rng=fpg_hash(9001u+split*10+tier)|1u;
        int wins=0,normal=0,timeout=0;long frames=0;
        for(int e=0;e<episodes;e++) {
            FpgState s;fpg_reset_task(&s,&cfg,fpg_bank,fpg_bank_count,&curriculum,&rng);
            if(policy)fpg_policy_reset(policy);srand(fpg_hash((unsigned)e+10001u+(unsigned)tier*10000u));
            while(!s.status){float obs[FPG_OBS];for(int i=0;i<FPG_OBS;i++)obs[i]=fpg_observation(&s,&cfg,i);
                fpg_step_task(&s,&cfg,random_policy?rand()%12:fpg_policy_action(policy,obs,deterministic));}
            wins+=s.status==FPG_SUCCESS;normal+=s.status==FPG_NORMAL_FLAG;timeout+=s.status==FPG_TIMEOUT;frames+=s.tick;
        }
        printf("{\"split\":%d,\"tier\":%d,\"deterministic\":%d,\"episodes\":%ld,\"fpg\":%d,\"ordinary_flag\":%d,\"timeouts\":%d,\"mean_frames\":%.5f}\n",split,tier,deterministic,episodes,wins,normal,timeout,(double)frames/episodes);fflush(stdout);
    }
    fpg_policy_free(policy);puf_ini_free(&ini);return 0;
}
