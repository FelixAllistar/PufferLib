#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#include <unistd.h>
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static std::vector<char> bytes(Nes_Emu* emu){Nes_State s;emu->save_state(&s);Mem_Writer w;retro_check(s.write(w));return {w.data(),w.data()+w.size()};}
int main(){try{
    Dict cfg={};dict_set_str(&cfg,"spawn_levels","all");dict_set(&cfg,"frameskip",4);
    dict_set(&cfg,"archive_enabled",1);dict_set(&cfg,"archive_start",1);dict_set(&cfg,"archive_end",.1);
    dict_set(&cfg,"archive_capacity_mb",1);dict_set(&cfg,"archive_capture_interval",64);
    Env e={};puf_init(&e,&cfg);float obs[OBS_SIZE],action=0,reward=0,done=0;
    e.agents[0]={obs,&action,&reward,&done,nullptr,0};e.spawn_pin=1;e.cur_spawn=0;e.archive->lane=0;
    puf_reset(&e);auto& bank=retro_archive_bank();bank.budget=1000;
    // Reach a playable, rendered state without RAM modification.
    for(int i=0;i<10;i++)puf_step(&e);
    check(retro_playable_area_state(e.emu->low_mem()),"fixture must be playable");
    e.rewarded_levels=1;e.pipe_phase=3; // Reward ledger only; never edit ROM RAM.
    retro_archive_capture(&e);check(bank.cells.size()==1,"capture failed");
    auto held=bank.cells[0].state;auto original=bytes(e.emu);std::vector<float> expected(obs,obs+OBS_SIZE);
    puf_reset(&e);check(e.archive->mode==2&&bytes(e.emu)==original,"restore changed exact ROM state");
    // Episode age/frontier observations intentionally restart, like ordinary resets.
    expected[49]=0;expected[63]=robs_c01(e.x_pos/3200.0f);
    check(!memcmp(obs,expected.data(),OBS_SIZE*sizeof(float)),"restore changed reset observation");
    check(e.rewarded_levels==1&&e.pipe_phase==3,"reward ledger lost");
    check(!memcmp(e.frontiers,held->ledger.frontiers,sizeof(e.frontiers)),"frontier ledger lost");
    check(retro_track_progress(&e)==0&&retro_record_area_transition(&e,e.emu->low_mem())==0,"restore repaid novelty");
    std::string path="/tmp/retro_archive_test_"+std::to_string(getpid())+".gz";
    retro_archive_file(path.c_str(),true,.75);bank.cells.clear();bank.index.clear();
    retro_archive_file(path.c_str(),false);check(bank.cells.size()==1&&bank.resumed_phase==.75,"archive roundtrip lost schedule/cells");
    puf_reset(&e);check(bytes(e.emu)==original,"serialized core state differs");
    // Same controls after restore produce identical core bytes and observations.
    for(int i=0;i<16;i++){action=retro_mask_action(RETRO_BTN_RIGHT|RETRO_BTN_B);puf_step(&e);}
    auto future=bytes(e.emu);std::vector<float> future_obs(obs,obs+OBS_SIZE);
    puf_reset(&e);for(int i=0;i<16;i++)puf_step(&e);
    check(bytes(e.emu)==future&&!memcmp(obs,future_obs.data(),OBS_SIZE*sizeof(float)),"restored rollout diverged");
    auto fingerprint=retro_rom().fingerprint;retro_rom().fingerprint^=1;bool rejected=false;
    try{retro_archive_file(path.c_str(),false);}catch(...){rejected=true;}retro_rom().fingerprint=fingerprint;
    check(rejected,"mismatched ROM archive accepted");
    {FILE* f=fopen(path.c_str(),"wb");fputs("broken",f);fclose(f);}rejected=false;
    try{retro_archive_file(path.c_str(),false);}catch(...){rejected=true;}check(rejected,"corrupt archive accepted");unlink(path.c_str());
    // End phase assigns all nonarchive lanes to full games from 1-1.
    bank.steps=1000;e.archive->lane=.5;e.cur_spawn=20;puf_reset(&e);
    check(e.archive->mode==1&&e.episode_spawn==0&&e.world==1&&e.stage==1,"full-game anneal endpoint wrong");
    bank.steps=0;bank.resumed_phase=0;e.archive->start=.4;e.archive->lane=.95;e.cur_spawn=20;puf_reset(&e);
    check(e.archive->mode==0&&e.episode_spawn==20,"initial level-start lane wrong");
    // Keep an active snapshot alive through bounded-bank eviction/replacement.
    bank.capacity=1;e.archive->lane=0;puf_reset(&e);auto active=e.archive->state;
    for(int i=0;i<200;i++){action=retro_mask_action(RETRO_BTN_RIGHT|RETRO_BTN_B|((i%48>=10)?RETRO_BTN_A:0));puf_step(&e);}
    check(bank.cells.size()<=1&&active&&active->start.world==1,"archive eviction invalidated active state");
    check(bank.mode_steps[2]>0,"archive decision accounting missing");
    // A natural terminal still exposes terminal=1 after automatic restore.
    e.max_frames=e.tick+1;puf_step(&e);check(done==1&&e.tick==0,"archive reset hid terminal boundary");
    // Level focus: an always-failing level with as many cells as an easy one is
    // chosen far more often at focus 1; focus 0 keeps per-cell sampling.
    {
        auto snapshot=held;bank.cells.clear();bank.index.clear();bank.capacity=64;
        bank.steps=0;bank.resumed_phase=0;e.archive->start=1;e.archive->end=1;e.archive->lane=0;
        for(int i=0;i<8;i++){RetroArchiveCell c;c.key=100+i;c.level=i<4?0:15;c.state=snapshot;
            (i<4?c.successes:c.failures)=50;c.selected=1000;c.last=0;bank.index[c.key]=bank.cells.size();bank.cells.push_back(c);}
        auto share=[&](double focus){
            e.archive->level_focus=focus;int hard=0,id=-1;
            for(int i=0;i<4000;i++){retro_archive_choose(&e,&id);check(e.archive->mode==2,"level-focus test left archive lane");hard+=id==15;}
            return hard/4000.0;};
        double base=share(0),focused=share(1);
        check(base>.45&&base<.8,"focus 0 changed per-cell level balance");
        check(focused>.85,"focus 1 did not favor the failing level");
        e.archive->state.reset();
    }
    // Progress: an x underflow (stepping left of 4-4's start) is x=0, not a
    // jackpot; one decision earns at most the step cap; areas longer than
    // 3400 px keep earning.
    {
        e.frontier_count=0;e.progress_pixels=0;e.checkpoint_distance=128;unsigned key=(15u<<16)|0x9000;
        check(retro_progress(&e,key,40)==0&&e.progress_pixels==0,"first arrival must be a baseline");
        check(retro_progress(&e,key,65534)==0&&e.progress_pixels==0,"x underflow paid progress");
        retro_progress(&e,key,1040);check(e.progress_pixels==RETRO_PROGRESS_STEP_CAP,"glitch jump exceeded step cap");
        int earned=0;for(int x=1052;x<=5996;x+=12)earned+=retro_progress(&e,key,x);
        check(e.progress_pixels==RETRO_PROGRESS_STEP_CAP+(5996-1040)&&earned==e.progress_pixels/128,"long area stopped earning");
        check(retro_progress(&e,key,500)==0,"moving backwards paid progress");
    }
    // Maze loops: a one-frame 1024 px drop in one area is a loop; a death-like
    // gap of untracked frames is not; ordinary movement never is.
    {
        e.frontier_count=0;e.progress_pixels=0;e.tick=0;unsigned key=(15u<<16)|0x9000;
        auto walk=[&](int x){e.tick++;retro_progress(&e,key,x);return retro_maze_loop(&e,key,x);};
        int loops=0;for(int x=0;x<=1040;x+=8)loops+=walk(x);
        check(loops==0,"walking forward counted a loop");
        e.tick+=40;check(walk(16)==0,"death-like gap counted as a loop");
        for(int x=16;x<=1040;x+=8)walk(x);
        check(walk(16)==1,"one-frame 1024 px drop was not a loop");
        check(walk(500)==0&&walk(400)==0,"moving left counted a loop");
    }
    // Depth priority: with equal statistics, lower cells (y band 5) are chosen
    // more often than upper ones (band 1) at depth 1; depth 0 keeps them equal.
    {
        auto snapshot=held;bank.cells.clear();bank.index.clear();
        for(int i=0;i<8;i++){RetroArchiveCell c;c.key=200+i+(uint64_t(i<4?1:5)<<42);c.level=15;c.state=snapshot;
            c.selected=1000;c.last=0;bank.index[c.key]=bank.cells.size();bank.cells.push_back(c);}
        auto low=[&](double depth){e.archive->level_focus=0;e.archive->depth=depth;int n=0,id=-1;
            for(int i=0;i<4000;i++){retro_archive_choose(&e,&id);n+=((e.archive->feedback_key>>42)&15)==5;}
            return n/4000.0;};
        double flat=low(0),deep=low(1);
        check(flat>.45&&flat<.55,"depth 0 changed cell balance");
        check(deep>.55,"depth 1 did not favor lower cells");
        e.archive->depth=0;e.archive->state.reset();
    }
    // End-first curriculum: at phase 0 with backward 4, deep cells (depth 1000) are chosen far
    // more often than shallow ones (depth 100) of the same level; backward 0 keeps them equal.
    {
        auto snapshot=held;bank.cells.clear();bank.index.clear();bank.steps=0;bank.resumed_phase=0;
        for(int i=0;i<8;i++){RetroArchiveCell c;c.key=300+i;c.level=15;c.state=snapshot;c.depth=i<4?100:1000;
            c.selected=1000;c.last=0;bank.index[c.key]=bank.cells.size();bank.cells.push_back(c);}
        auto deep=[&](double backward){e.archive->level_focus=0;e.archive->depth=0;e.archive->backward=backward;int n=0,id=-1;
            for(int i=0;i<4000;i++){retro_archive_choose(&e,&id);n+=e.archive->feedback_key>=304;}
            return n/4000.0;};
        double flat=deep(0),early=deep(4);
        check(flat>.45&&flat<.55,"backward 0 changed cell balance");
        check(early>.9,"backward 4 did not favor deep cells at phase 0");
        e.archive->backward=0;e.archive->state.reset();
    }
    puf_close(&e);dict_clear(&cfg);puts("PASS: archive exact state/image, deterministic continuation, reward ledger, persistence/corruption, mixture, eviction lifetime, terminal boundary, level focus, progress wrap/step cap, maze loops, depth priority, end-first curriculum");return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
