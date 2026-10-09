#define FPG_HEADLESS
#include "../mario_fpg.h"
#include <assert.h>
int main(void) {
    Dict d={0};fpg_load_bank(&d);FpgConfig c=fpg_config(&d);FpgCurriculum curriculum={0};uint32_t rng=901;
    int successes=0;
    for(int version=1;version<=2;version++) {
    c.contract_version=version;
    for(int split=0;split<3;split++)for(int tier=0;tier<4;tier++)for(int n=0;n<128;n++) {
        c.split=split;c.fixed_tier=tier;FpgState s;fpg_reset_task(&s,&c,fpg_bank,fpg_bank_count,&curriculum,&rng);
        const FpgCase* sample=&fpg_bank[s.case_index];int offset=sample->length-s.reset_remaining;
        FpgState original=s;
        for(int i=0;i<FPG_OBS;i++)assert(isfinite(fpg_observation(&s,&c,i)));
        for(int t=offset;t<sample->length;t++) {
            float r=fpg_step_task(&s,&c,sample->frames[t].action);
            assert(r==(t==sample->length-1?1.0f:0.0f));
        }
        assert(s.status==FPG_SUCCESS);successes++;
        for(int t=offset;t<sample->length;t++)fpg_step_task(&original,&c,sample->frames[t].action);
        assert(!memcmp(&s,&original,sizeof(s)));
    }
    }
    // Ordinary 100-point flag grabs must never count as FPG success.
    FpgCourse course={73,0,36,8,8,2};FpgWorld w;fpg_generate(&course,&w);
    FpgBody b=fpg_initial(&course);b.x=36*16+6;b.y=144;b.vx=0;b.vy=1;b.motion=2;
    fpg_physics(&b,&w,0);assert(b.routine==4&&b.grab_y==145&&b.flag_y>48);assert(fpg_outcome(&b)==FPG_NORMAL_FLAG);
    c.fixed_tier=-1;c.adaptive=1;int counts[4]={};
    for(int n=0;n<10000;n++)counts[fpg_choose_tier(&c,&curriculum,&rng)]++;
    assert(counts[0]>6500);for(int k=1;k<4;k++)assert(counts[k]>800);
    for(int k=0;k<3;k++){curriculum.tries[k]=100;curriculum.success[k]=0.9f;}
    memset(counts,0,sizeof(counts));for(int n=0;n<10000;n++)counts[fpg_choose_tier(&c,&curriculum,&rng)]++;
    assert(counts[3]>6500);
    curriculum.success[1]=0.1f;memset(counts,0,sizeof(counts));for(int n=0;n<10000;n++)counts[fpg_choose_tier(&c,&curriculum,&rng)]++;
    assert(counts[1]>6500); // forgetting sends practice back to the weak tier.
    printf("FPG task: %d augmented suffixes solved; ordinary flags rejected; replay and adaptive retention passed\n",successes);
    return 0;
}
