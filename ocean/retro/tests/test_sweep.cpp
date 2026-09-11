#include "retro.h"
#include "retro_sweep_config.h"

static void require(bool ok,const char* message) {
    if(!ok) throw std::runtime_error(message);
}

static void reward_tests() {
    Env e={}; e.potential_gamma=0.9998f; e.completion_reward=10;
    e.death_penalty=0.125f; e.reward_scale=1.0f/16; e.checkpoint_reward=0.125f; e.checkpoint_distance=128;
    float smallest=1,largest=-1;
    for(int a=0;a<=10;a++) for(int b=0;b<=10;b++)
        for(int advances=0;advances<=1;advances++)
            for(int dead=0;dead<=1;dead++) for(int done=0;done<=1;done++) for(int checkpoints=0;checkpoints<=27;checkpoints++) {
                float scaled=retro_rom_reward(&e,a/10.0f,b/10.0f,advances,dead,done,0,checkpoints);
                smallest=std::min(smallest,scaled); largest=std::max(largest,scaled);
                e.reward_scale=1;
                float raw=retro_rom_reward(&e,a/10.0f,b/10.0f,advances,dead,done,0,checkpoints);
                e.reward_scale=1.0f/16;
                require(scaled==raw/16,"reward components are not scaled together");
                require(scaled==std::max(-1.0f,std::min(1.0f,scaled)),"trainer clip alters scaled reward");
            }
    require(smallest>=-0.0703125f&&largest<=0.8984375f,"reward safety bound violated");
    require(retro_rom_reward(&e,0,0,1,false,true,0)==0.625f,"wrong scaled completion bonus");
    require(retro_rom_reward(&e,0,0,0,true,true,0)==-0.0078125f,"wrong scaled death penalty");
    require(retro_progress(&e,1,40)==0,"spawn earns checkpoint reward");
    require(retro_progress(&e,1,167)==0&&retro_progress(&e,1,168)==1,"checkpoint boundary incorrect");
    require(retro_progress(&e,1,40)==0&&retro_progress(&e,1,168)==0,"backtracking farms checkpoints");
    require(retro_progress(&e,2,2000)==0,"area arrival pays for teleport distance");
    require(retro_progress(&e,2,2128)==1,"new area exploration not counted");
    require(retro_progress(&e,1,40)==0&&retro_progress(&e,1,168)==0,"area reentry farms checkpoints");
    require(e.progress_pixels==256,"novel distance not accumulated across areas");
    require(retro_rom_reward(&e,0,0,0,true,true,0,2)>retro_rom_reward(&e,0,0,0,false,true,0),
        "two checkpoints plus death should beat zero-progress timeout");
    require(retro_progress(&e,2,65535)<=27&&retro_progress(&e,2,65535)==0,"coordinate wrap jackpot is unbounded");
    printf("PASS: common 1/16 reward scale preserves ratios; clip-safe range [%.6f, %.6f]\n",smallest,largest);
}

static void config_tests() {
    Ini ini={}; puf_ini_load_env(&ini,"retro",0,nullptr);
    long ordinary=puf_ini_get(&ini,"train","total_timesteps");
    puf_ini_put(&ini,"train.gamma","0.9993");
    retro_configure(&ini,"train");
    require(puf_ini_get(&ini,"env","potential_gamma")==puf_ini_get(&ini,"train","gamma"),"gamma coupling failed");
    require(puf_ini_get(&ini,"train","total_timesteps")==ordinary,"ordinary training budget changed");
    puf_ini_put(&ini,"base.load_model_path","None"); puf_ini_put(&ini,"env.spawn_levels","all");
    retro_configure(&ini,"sweep");
    require(puf_ini_get(&ini,"train","total_timesteps")==puf_ini_get(&ini,"sweep","trial_timesteps"),"sweep budget not applied");
    require(puf_ini_get(&ini,"base","checkpoint_interval")==0,"checkpoint lottery remains");
    require(puf_ini_get(&ini,"sweep","downsample")==1,"intermediate score comparisons remain");
    for(int horizon=64;horizon<=256;horizon*=2) {
        long agents=puf_ini_get(&ini,"vec","total_agents"),mb=puf_ini_get(&ini,"train","minibatch_size");
        require(mb%horizon==0&&mb<=agents*horizon,"invalid sweep minibatch shape");
        require((long)puf_ini_get(&ini,"sweep","trial_timesteps")%(agents*horizon)==0,"unequal rounded trial budgets");
    }
    puf_ini_put(&ini,"base.load_model_path","latest"); bool rejected=false;
    try { retro_configure(&ini,"sweep"); } catch(...) { rejected=true; }
    require(rejected,"moving latest anchor accepted");
    puf_ini_put(&ini,"base.load_model_path","None"); puf_ini_put(&ini,"env.reward_scale","2"); rejected=false;
    try { retro_configure(&ini,"sweep"); } catch(...) { rejected=true; }
    require(rejected,"reward clipping regression accepted");
    puf_ini_put(&ini,"env.reward_scale","0.0625"); puf_ini_put(&ini,"env.checkpoint_reward","1"); rejected=false;
    try { retro_configure(&ini,"train"); } catch(...) { rejected=true; }
    require(rejected,"checkpoint clipping regression accepted");
    puf_ini_put(&ini,"env.checkpoint_reward","0.125");
    puf_ini_put(&ini,"env.reward_scale","0.0625"); puf_ini_put(&ini,"env.frameskip","4"); rejected=false;
    try { retro_configure(&ini,"sweep"); } catch(...) { rejected=true; }
    require(rejected,"changed control contract accepted");
    puf_ini_free(&ini);
    printf("PASS: coupled gamma, equal budgets, valid shapes, fresh trials and fixed ROM controls\n");
}

static void panel_tests() {
    float row[65]={}; row[63]=10; row[64]=10000;
    require(retro_panel_action(row,0,true)==63,"value head mistaken for an action");
    memset(row,0,sizeof(row)); row[0]=1000; row[64]=10000;
    require(retro_panel_action(row,0xffffffffu,false)==0,"stable sampling/action-zero mapping failed");
    require(retro_panel_progress(40,40)==0&&retro_panel_progress(40,0)==0,"idling/retreat earns progress");
    require(retro_panel_progress(40,3440)==1&&retro_panel_progress(40,65535)==1,"progress is unbounded");
    require(retro_panel_score(0,64,0)==0,"empty panel gets a positive score");
    for(int clears=0;clears<64;clears++)
        require(retro_panel_score(clears+1,64,0)>retro_panel_score(clears,64,1),"progress can outweigh an additional clear");
    printf("PASS: panel score strictly prioritizes clears, then bounded progress; no score/coins/survival input\n");
}
int main() {
    try { reward_tests(); config_tests(); panel_tests(); return 0; }
    catch(const std::exception& e) { fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
