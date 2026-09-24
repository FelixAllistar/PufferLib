#include "retro.h"
#include "retro_sweep_config.h"

static void require(bool ok,const char* message) {
    if(!ok) throw std::runtime_error(message);
}

static void reward_tests() {
    Env e={}; e.completion_reward=10;
    e.death_penalty=0.125f; e.reward_scale=1.0f/16; e.checkpoint_reward=0.125f; e.checkpoint_distance=128;
    float smallest=1,largest=-1;
        for(int advances=0;advances<=1;advances++)
            for(int dead=0;dead<=1;dead++) for(int checkpoints=0;checkpoints<=27;checkpoints++) {
                float scaled=retro_rom_reward(&e,advances,dead,0,checkpoints);
                smallest=std::min(smallest,scaled); largest=std::max(largest,scaled);
                e.reward_scale=1;
                float raw=retro_rom_reward(&e,advances,dead,0,checkpoints);
                e.reward_scale=1.0f/16;
                require(scaled==raw/16,"reward components are not scaled together");
                require(scaled==std::max(-1.0f,std::min(1.0f,scaled)),"trainer clip alters scaled reward");
            }
    require(smallest==-0.0078125f&&largest==0.8359375f,"reward safety bound violated");
    require(retro_rom_reward(&e,1,false,0)==0.625f,"wrong scaled completion bonus");
    require(retro_rom_reward(&e,0,true,0)==-0.0078125f,"wrong scaled death penalty");
    require(retro_progress(&e,1,40)==0,"spawn earns checkpoint reward");
    require(retro_progress(&e,1,167)==0&&retro_progress(&e,1,168)==1,"checkpoint boundary incorrect");
    require(retro_progress(&e,1,40)==0&&retro_progress(&e,1,168)==0,"backtracking farms checkpoints");
    require(retro_progress(&e,2,2000)==0,"area arrival pays for teleport distance");
    require(retro_progress(&e,2,2128)==1,"new area exploration not counted");
    require(retro_progress(&e,1,40)==0&&retro_progress(&e,1,168)==0,"area reentry farms checkpoints");
    require(e.progress_pixels==256,"novel distance not accumulated across areas");
    require(retro_rom_reward(&e,0,true,0,2)>retro_rom_reward(&e,0,false,0),
        "two checkpoints plus death should beat zero-progress timeout");
    require(retro_progress(&e,2,65535)<=27&&retro_progress(&e,2,65535)==0,"coordinate wrap jackpot is unbounded");
    printf("PASS: common 1/16 reward scale preserves ratios; clip-safe range [%.6f, %.6f]\n",smallest,largest);
}

static void config_tests() {
    Ini ini={}; puf_ini_load_env(&ini,"retro",0,nullptr);
    // Fixed test fixture, independent of the user's current experiment.
    puf_ini_put(&ini,"vec.total_agents","64");
    puf_ini_put(&ini,"train.minibatch_size","2048");
    puf_ini_put(&ini,"sweep.trial_timesteps","16777216");
    puf_ini_put(&ini,"sweep.metric","distance");
    puf_ini_put(&ini,"env.frameskip","1");
    puf_ini_put(&ini,"env.backend","quicknes");
    puf_ini_put(&ini,"env.completion_reward","10");
    puf_ini_put(&ini,"env.completion_time_bonus","0");
    puf_ini_put(&ini,"env.death_penalty","0.125");
    puf_ini_put(&ini,"env.checkpoint_reward","0.125");
    puf_ini_put(&ini,"env.checkpoint_distance","128");
    puf_ini_put(&ini,"env.score_scale","0");
    // The reward fixture intentionally keeps the new optional terms disabled
    // so the historical clip-safe bounds above remain directly comparable.
    puf_ini_put(&ini,"env.coin_reward","0");
    puf_ini_put(&ini,"env.idle_penalty","0");
    puf_ini_put(&ini,"env.idle_grace_decisions","8");
    puf_ini_set(puf_ini_section(&ini,"env",0),"area_transition_reward","0");
    puf_ini_put(&ini,"env.pipe_segment_bonus","0");
    puf_ini_put(&ini,"env.reward_scale","0.0625");
    puf_ini_put(&ini,"train.reward_clip","1");
    long ordinary=puf_ini_get(&ini,"train","total_timesteps");
    puf_ini_put(&ini,"train.gamma","0.9993");
    retro_configure(&ini,"train");
    require(!dict_find(puf_ini_section(&ini,"env",0),"potential_gamma"),"removed shaping option regenerated");
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
    puf_ini_put(&ini,"base.load_model_path","checkpoints/fixed-policy.bin");
    retro_configure(&ini,"sweep");
    require(!strcmp(puf_ini_get_str(&ini,"base","load_model_path"),"checkpoints/fixed-policy.bin"),
        "fixed finetune checkpoint was replaced");
    puf_ini_put(&ini,"base.load_model_path","None");
    puf_ini_put(&ini,"sweep.metric","typo"); rejected=false;
    try { retro_configure(&ini,"sweep"); } catch(...) { rejected=true; }
    require(rejected,"unknown sweep metric accepted");
    puf_ini_put(&ini,"sweep.metric","distance");
    puf_ini_put(&ini,"env.completion_time_bonus","-1"); rejected=false;
    try { retro_configure(&ini,"train"); } catch(...) { rejected=true; }
    require(rejected,"negative completion speed bonus accepted");
    puf_ini_put(&ini,"env.completion_time_bonus","5"); rejected=false;
    try { retro_configure(&ini,"train"); } catch(...) { rejected=true; }
    require(rejected,"speed bonus omitted from reward clipping bound");
    puf_ini_put(&ini,"env.completion_time_bonus","0");
    puf_ini_put(&ini,"env.coin_reward","-1"); rejected=false;
    try { retro_configure(&ini,"train"); } catch(...) { rejected=true; }
    require(rejected,"negative coin reward accepted");
    puf_ini_put(&ini,"env.coin_reward","0");
    puf_ini_put(&ini,"env.idle_penalty","-1"); rejected=false;
    try { retro_configure(&ini,"train"); } catch(...) { rejected=true; }
    require(rejected,"negative idle penalty accepted");
    puf_ini_put(&ini,"env.idle_penalty","0");
    puf_ini_put(&ini,"env.idle_grace_decisions","1.5"); rejected=false;
    try { retro_configure(&ini,"train"); } catch(...) { rejected=true; }
    require(rejected,"fractional idle grace accepted");
    puf_ini_put(&ini,"env.idle_grace_decisions","8");
    puf_ini_set(puf_ini_section(&ini,"env",0),"area_transition_reward","1"); rejected=false;
    try { retro_configure(&ini,"train"); } catch(...) { rejected=true; }
    require(rejected,"removed area transition reward accepted");
    puf_ini_put(&ini,"env.area_transition_reward","0");
    puf_ini_put(&ini,"base.load_model_path","None"); puf_ini_put(&ini,"env.reward_scale","2"); rejected=false;
    try { retro_configure(&ini,"sweep"); } catch(...) { rejected=true; }
    require(rejected,"reward clipping regression accepted");
    puf_ini_put(&ini,"env.reward_scale","0.0625"); puf_ini_put(&ini,"env.checkpoint_reward","1"); rejected=false;
    try { retro_configure(&ini,"train"); } catch(...) { rejected=true; }
    require(rejected,"checkpoint clipping regression accepted");
    puf_ini_put(&ini,"env.checkpoint_reward","0.125");
    puf_ini_put(&ini,"env.reward_scale","0.0625"); puf_ini_put(&ini,"env.frameskip","4");
    puf_ini_put(&ini,"train.reward_clip","0");
    puf_ini_put(&ini,"env.spawn_levels","1-1");
    puf_ini_put(&ini,"env.completion_time_bonus","159");
    puf_ini_put(&ini,"sweep.metric","speed");
    retro_configure(&ini,"sweep");
    require(puf_ini_get(&ini,"train","reward_clip")==0&&puf_ini_get(&ini,"env","frameskip")==4
        &&!strcmp(puf_ini_get_str(&ini,"env","spawn_levels"),"1-1"),"sweep overwrote the selected task");
    puf_ini_put(&ini,"env.spawn_levels","1-1,2-3"); retro_configure(&ini,"sweep");
    puf_ini_put(&ini,"train.reward_clip","-1"); rejected=false;
    try { retro_configure(&ini,"sweep"); } catch(...) { rejected=true; }
    require(rejected,"negative reward clip accepted");
    puf_ini_put(&ini,"train.reward_clip","0");
    puf_ini_put(&ini,"env.frameskip","1.5"); rejected=false;
    try { retro_configure(&ini,"sweep"); } catch(...) { rejected=true; }
    require(rejected,"fractional frameskip accepted");
    puf_ini_free(&ini);
    printf("PASS: learner-only gamma, fixed budgets, explicit warm starts, selected levels/controls, optional clipping\n");
}

static void rta_timing_tests() {
    unsigned char m[2048]={}; RetroRtaClock clock={};
    retro_rta_reset(&clock,1,1,0);
    m[0x770]=1; m[0x772]=3; m[0xe]=8; m[0x7a0]=7;
    require(!retro_rta_update(&clock,400,m,7,0,1),"same-level pipe/black screen counted as RTA clear");
    m[0x75c]=1; m[0x7a0]=0;
    require(!retro_rta_update(&clock,1800,m,8,0,1),"level-number change split before black screen");
    m[0x7a0]=7;
    require(retro_rta_update(&clock,1805,m,8,0,1)&&clock.first_split_frames==1805,
        "black-screen RTA split missed");
    require(!retro_rta_update(&clock,1806,m,8,7,1),"duplicate RTA split");
    // Warp zones use the entrance event, rather than an unrelated black screen.
    m[0x75f]=3; m[0x75c]=0; m[0xe]=0;
    require(!retro_rta_update(&clock,3000,m,0,0,1),"warp split too early");
    m[0xe]=7;
    require(retro_rta_update(&clock,3010,m,0,7,1)&&clock.split_frames==1205
        &&clock.split_total_frames==3010&&clock.first_split_frames==1805,
        "warp segment/cumulative timing incorrect");
    retro_rta_reset(&clock,8,4,2); m[0x75f]=7; m[0x75c]=3; m[0x770]=2;
    require(retro_rta_update(&clock,2200,m,8,0,1),"final axe did not finish RTA timer");
    require(retro_rta_elapsed(clock,2300)==2202&&!retro_rta_update(&clock,2300,m,8,0,1),
        "RTA timer advances after final axe or loses reset offset");
    Log normalized={}; normalized.n=1; normalized.episode_length=9999;
    normalized.rta_count=0.5f; normalized.rta_frame_sum=750;
    Dict metrics={}; puf_log(&normalized,&metrics);
    require(fabs(dict_get(&metrics,RETRO_RTA_COMPARABLE?"rta_seconds":"sim_seconds")-retro_frame_seconds(1500))<1e-6,
        "completed-only average includes failures or averages per-env averages");
    require(dict_get(&metrics,RETRO_RTA_COMPARABLE?"rta_clear_rate":"split_clear_rate")==0.5,"RTA success denominator incorrect");
    require(RETRO_RTA_COMPARABLE&&dict_get(&metrics,"rta_valid")==1
        &&dict_find(&metrics,"rta_seconds")&&!dict_find(&metrics,"sim_seconds"),
        "verified NTSC timing not published under RTA metrics");
    dict_clear(&metrics); normalized.rta_count=0; normalized.rta_frame_sum=0;
    puf_log(&normalized,&metrics);
    require(dict_get(&metrics,RETRO_RTA_COMPARABLE?"rta_seconds":"sim_seconds")==0
        &&dict_get(&metrics,RETRO_RTA_COMPARABLE?"rta_clear_rate":"split_clear_rate")==0,
        "empty RTA average is nonfinite or appears successful");
    dict_clear(&metrics);
    printf("PASS: RTA black-screen/warp/axe events, frozen final clock, success-only weighted mean\n");
}
static void speed_reward_tests() {
    rta_timing_tests();
    // Fail before loading the ROM if someone accidentally restores the HUD
    // targets alongside the RTA objective, or selects two timing endpoints.
    Dict bad={}; dict_set(&bad,"completion_on_rta_split",1);
    for(const char* key:{"completion_on_next_playable","area_transition_reward","area_transition_timer_bonus","completion_time_target_bonus"}) {
        dict_set(&bad,key,1); Env invalid={}; bool rejected=false;
        try { puf_init(&invalid,&bad); } catch(const std::runtime_error&) { rejected=true; }
        require(rejected,"conflicting RTA/HUD reward configuration accepted");
        dict_set(&bad,key,0);
    }
    dict_clear(&bad);
    char clock[64]; retro_clock_text(clock,sizeof(clock),1013);
    require(!strcmp(clock,"0:16.856"),"viewer simulated clock conversion incorrect");
    require(retro_clear_speed(1042,3000,800,1400)==retro_clear_speed(1042,6000,800,1400),
        "speed shaping changes with timeout");
    require(retro_clear_speed(800,3000,800,1400)==1&&retro_clear_speed(1400,3000,800,1400)==0,
        "speed window endpoints incorrect");
    for(int frame=801;frame<=1400;frame++)
        require(retro_clear_speed(frame-1,3000,800,1400)>retro_clear_speed(frame,3000,800,1400),
            "waiting improves or flattens reward inside speed window");
    for(int frame=1201;frame<=3000;frame++)
        require(retro_clear_speed(frame-1,3000,1200,3000)>retro_clear_speed(frame,3000,1200,3000),
            "waiting improves or flattens configured RTA reward");
    unsigned char flag_state[2048]={};
    flag_state[0x770]=1; flag_state[0x772]=3; flag_state[0xe]=4;
    require(retro_flag_contact(flag_state),"first flag contact missed");
    flag_state[0xe]=8;
    require(!retro_flag_contact(flag_state),"ordinary gameplay marked as a clear");
    Env e={}; e.completion_reward=10;
    e.completion_time_bonus=5; e.death_penalty=1; e.checkpoint_reward=0.125f;
    e.coin_reward=0.1f; e.idle_penalty=0.02f; e.idle_grace_decisions=8;
    e.reward_scale=0.0625f;
    auto cleared=[&](int frames,int points=0) {
        return retro_rom_reward(&e,1,false,points,0,retro_clear_speed(frames,3000));
    };
    require(cleared(1000)>cleared(2000)&&cleared(2000)>cleared(3000),"faster clears do not pay more");
    require(cleared(1000)==cleared(1000,999999),"Mario points still affect speed reward");
    require(cleared(3000)==0.625f&&cleared(4000)==0.625f,"late clear loses its completion reward");
    require(retro_clear_speed(0,3000)==1&&retro_clear_speed(-1,3000)==1,"speed bonus exceeds its bound");
    require(retro_rom_reward(&e,0,false,999999)==0,"idling/points pays reward");
    require(retro_rom_reward(&e,0,false,0,0,1)==0,"speed bonus paid without clearing");
    require(retro_coin_delta(10,11)==1&&retro_coin_delta(99,0)==1,"coin counter delta incorrect");
    require(retro_coin_delta(12,11)==0&&retro_coin_delta(98,5)==7,"coin loss/wrap handling incorrect");
    require(retro_rom_reward(&e,0,false,0,0,0,1)==0.00625f,"coin-only reward incorrect");
    require(retro_rom_reward(&e,0,false,0,0,0,-60)==0,"coin loss paid as a wrapped reward");
    require(retro_rom_reward(&e,0,false,999999,0,0,0)==0,"Mario points still pay with score_scale=0");
    require(retro_rom_reward(&e,0,false,0,0,0,0,1)==-0.00125f,"idle penalty incorrect");
    require(retro_rom_reward(&e,0,false,0,0,0,0,0)==0,"idle reward changed without idle event");
    unsigned char area_state[2048]={};
    area_state[0x0770]=1; area_state[0x0772]=3; area_state[0x000e]=8;
    area_state[0x075f]=0; area_state[0x075c]=0; area_state[0x0760]=0;
    area_state[0x00e7]=0x34; area_state[0x00e8]=0x80;
    Env areas={}; areas.last_area_key=retro_area_key(area_state);
    areas.area_transition_key_count=1; areas.area_transition_keys[0]=areas.last_area_key;
    unsigned int start_area=areas.last_area_key;
    area_state[0x0760]=1; area_state[0x00e7]=0x78;
    require(retro_record_area_transition(&areas,area_state)==2,"novel area transition not recorded");
    require(areas.episode_area_transitions==1&&areas.episode_novel_areas==1,"area transition diagnostics incorrect");
    require(retro_record_area_transition(&areas,area_state)==0,"stationary area double-counted");
    area_state[0x0760]=0; area_state[0x00e7]=0x34;
    require(retro_record_area_transition(&areas,area_state)==1,"return area transition not logged");
    require(retro_area_key(area_state)==start_area,"area key changed unexpectedly");
    require(areas.episode_area_transitions==2&&areas.episode_novel_areas==1,"repeated destination paid again");
    area_state[0x000e]=0x0b;
    area_state[0x0760]=2; area_state[0x00e7]=0x99;
    require(retro_record_area_transition(&areas,area_state)==0,"non-playable transition was rewarded");
    area_state[0x000e]=8;
    Env full={}; full.last_area_key=start_area; full.area_transition_key_count=256;
    full.area_transition_keys[0]=start_area;
    for(int i=1;i<256;i++) full.area_transition_keys[i]=0x10000000u+(unsigned)i;
    require(retro_record_area_transition(&full,area_state)==1&&full.episode_novel_areas==0,
        "full area frontier turned an unremembered destination into a reward");
    int streak=0;
    for(int i=0;i<8;i++) require(retro_idle_event(&streak,100,100,0,false,8,false)==0,"idle grace charged early");
    require(streak==8,"idle grace streak not tracked");
    require(retro_idle_event(&streak,100,100,0,false,8,false)==1,"idle grace never charged");
    require(retro_idle_event(&streak,100,100,0,false,8,false)==1,"idle charge is not per decision");
    require(retro_idle_event(&streak,100,101,0,false,8,false)==0&&streak==0,"movement did not reset idle streak");
    require(retro_idle_event(&streak,101,101,1,false,8,false)==0&&streak==0,"coin pickup did not reset idle streak");
    require(retro_idle_event(&streak,101,101,0,true,8,false)==0&&streak==0,"level advance did not reset idle streak");
    streak=20;
    require(retro_idle_event(&streak,101,101,0,false,8,true)==0&&streak==0,"terminal event charged idle penalty");
    require(retro_rom_reward(&e,0,true,0)==-0.0625f,"death penalty changed");
    require(retro_rom_reward(&e,0,false,0,1)==0.0078125f,"incorrect progress scale");
    require(cleared(3000)>retro_rom_reward(&e,0,true,0,27),"full-level partial progress beats a clear");
    Log log={}; log.n=10; log.clears=2; log.clear_frame_sum=3600; log.coin_events=12; log.idle_steps=34;
    Dict metrics={}; puf_log(&log,&metrics);
    require(dict_get(&metrics,"clear_frames")==1800,"clear time denominator includes failed episodes");
    require(dict_get(&metrics,"coin_events")==12&&dict_get(&metrics,"idle_steps")==34,"reward diagnostics missing");
    dict_clear(&metrics); log.clears=0; log.clear_frame_sum=0; puf_log(&log,&metrics);
    require(dict_get(&metrics,"clear_frames")==0,"no-clear metric must be finite");
    dict_clear(&metrics);
    puts("PASS: faster clears win; points/stalling do not pay; bounded clear-only bonus; conditional clear-time metric");
}

static void panel_tests() {
    require(retro_panel_objective("speed",1,32,0,0,1600,3000,1600)
        >retro_panel_objective("speed",32,32,1,9999,1700,3000,1700),"clear rate still outweighs speed");
    require(retro_panel_objective("speed",1,32,0,0,1800,3000,1800)
        ==retro_panel_objective("speed",32,32,1,9999,1800,3000,1800),"clear count/distance affects speed rank");
    require(retro_panel_objective("speed",32,32,0,0,2999,3000,1800)
        >retro_panel_objective("speed",32,32,0,0,1801,3000,1801),"mean overwhelms one-frame PB improvement");
    require(retro_panel_objective("speed",32,32,0,0,1900,3000,1800)
        >retro_panel_objective("speed",32,32,0,0,2000,3000,1800),"mean does not break PB ties");
    require(retro_panel_objective("speed",0,32,1,9999,0,3000)==-1,"failure earns progress reward");
    require(retro_panel_objective("speed",1,32,0,0,3000,3000,3000)==0,"last-frame clear does not outrank failure");
    require(retro_panel_distance(40,20)==0&&retro_panel_distance(40,4040)==4000,"distance must be relative, nonnegative and uncapped");
    require(retro_panel_objective("distance",0,64,0.2,680)==680,"distance selection ignored");
    require(retro_panel_objective("distance",8,64,0.2,680)==680,"clears contaminated distance objective");
    require(retro_panel_objective("perf",1,64,0.2,680)==retro_panel_score(1,64,0.2),"perf objective changed");
    require(retro_panel_objective("score",1,64,0.2,680)==retro_panel_score(1,64,0.2),"score alias changed");
    bool rejected=false;
    try { retro_panel_objective("typo",0,64,0.2,680); } catch(...) { rejected=true; }
    require(rejected,"unknown metric accepted");
    float row[65]={}; row[63]=10; row[64]=10000;
    require(retro_panel_action(row,0,true)==63,"value head mistaken for an action");
    memset(row,0,sizeof(row)); row[0]=1000; row[64]=10000;
    require(retro_panel_action(row,0xffffffffu,false)==0,"stable sampling/action-zero mapping failed");
    require(retro_panel_progress(40,40)==0&&retro_panel_progress(40,0)==0,"idling/retreat earns progress");
    require(retro_panel_progress(40,3440)==1&&retro_panel_progress(40,65535)==1,"progress is unbounded");
    require(retro_panel_score(0,64,0)==0,"empty panel gets a positive score");
    for(int clears=0;clears<64;clears++)
        require(retro_panel_score(clears+1,64,0)>retro_panel_score(clears,64,1),"progress can outweigh an additional clear");
    printf("PASS: distance objective is forward pixels; perf/score remain clear-first; unknown metrics rejected\n");
}
int main() {
    try { reward_tests(); speed_reward_tests(); config_tests(); panel_tests(); return 0; }
    catch(const std::exception& e) { fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
