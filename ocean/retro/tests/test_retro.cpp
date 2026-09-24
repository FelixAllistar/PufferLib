#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#include "retro_playback.h"
#include "observation_reference.h"
#include "nes_emu/abstract_file.h"
#include "nes_emu/Nes_Apu.h"
#include <omp.h>
#include <set>

static void require(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
static std::vector<char> saved(Nes_Emu& emu) {
    Nes_State s; emu.save_state(&s); Mem_Writer writer;
    retro_check(s.write(writer)); return std::vector<char>(writer.data(),writer.data()+writer.size());
}
static bool equal_image(const Nes_Emu& a,const Nes_Emu& b) {
    const auto& x=a.frame(); const auto& y=b.frame();
    for(int r=0;r<240;r++) for(int c=0;c<256;c++)
        if(x.palette[x.pixels[r*x.pitch+c]]!=y.palette[y.pixels[r*y.pitch+c]]) return false;
    return true;
}
static void scalar_tests() {
    std::set<int> masks;
    for(int i=0;i<64;i++) { int m=retro_action_mask(i); masks.insert(m); require(retro_mask_action(m)==i,"controller roundtrip"); }
    require(masks.size()==64&&masks.count(64|3)&&masks.count(3),"missing button combinations");
    require(retro_ntsc_rom_fingerprint(RETRO_SMB1_NTSC_INES_FNV)
        &&retro_ntsc_rom_fingerprint(RETRO_SMB1_NTSC_NES2_FNV),"verified NTSC header variants rejected");
    require(!retro_ntsc_rom_fingerprint(0x31d802e3779199daull)
        &&!retro_ntsc_rom_fingerprint(0),"PAL/unrecognized ROM accepted as NTSC");
    unsigned char m[2048]={}; m[0x14]=1; m[0x1b]=0x30; m[0x1d]=3;
    require(robs_flagget(m),"flagpole slot 5 missing");
    m[0x1b]=0x2f; require(!robs_flagget(m),"vine mistaken for flag");
    memset(m,0,sizeof(m));
    m[0x23]=1; m[0x6d]=m[0x73]=2; m[0xb5]=m[0xbb]=1;
    m[0x86]=10; m[0x8c]=20; m[0xce]=20; m[0xd4]=40;
    RetroScalars scalars={}; float obs[OBS_SIZE]; retro_ego_ent(obs,m,&scalars);
    require(obs[104]==10.0f/256&&obs[105]==20.0f/256,"powerup page/offset slots disagree");
    require(retro_level_advance(4,2,8,1),"wrong warp not recognized");
    require(!retro_level_advance(4,2,4,1),"backward transition marked as clear");
    Env e={}; bool rejected=false;
    try { retro_parse_spawns(&e,"1-1,9-2"); } catch(...) { rejected=true; }
    require(rejected,"bad spawn silently accepted");
}
static void level_and_reset_tests(Dict* cfg) {
    Env e={}; e.rng=73; puf_init(&e,cfg);
    float obs[OBS_SIZE],act=0,rew=0,done=0;
    e.agents[0]={obs,&act,&rew,&done,nullptr,0}; e.spawn_pin=1;
    static_assert(alignof(unaligned_uint32_t)==1,"pixel words must permit byte alignment");
    // Snapshot padding must not depend on the destination's previous contents.
    apu_state_t audio_a,audio_b;
    memset(&audio_a,0x55,sizeof(audio_a)); memset(&audio_b,0xaa,sizeof(audio_b));
    Nes_Apu audio;
    audio.reset(); audio.save_state(&audio_a); audio.save_state(&audio_b);
    require(!memcmp(&audio_a,&audio_b,sizeof(audio_a)),"audio snapshot has uninitialized bytes");
    RetroRom& rom=retro_rom();
    require(e.emu->chr_cache_identity()==rom.seed.chr_cache_identity(),"CHR cache not shared");
    std::set<int> data;
    for(int i=0;i<32;i++) {
        e.cur_spawn=i; puf_reset(&e);
        require(retro_observation_matches_reference(&e,obs),"reset observation differs from staged reference");
        require(e.emu->chr_cache_identity()==rom.seed.chr_cache_identity(),"reset detached immutable CHR cache");
        require(e.world==i/4+1&&e.stage==i%4+1,"wrong level label");
        require(e.start->rta_offset_frames==0,"saved playable reset differs from autosplitter RTA start");
        int selected=retro_start_area(rom,e.world,e.stage);
        int base=rom.cart.prg()[RETRO_WORLD_OFFSETS-0x8000+e.world-1];
        if(rom.cart.prg()[RETRO_AREA_OFFSETS-0x8000+base+selected]==0x29) selected++;
        require(e.area==selected+1,"wrong level area");
        data.insert(e.start->data);
        float reset_obs[OBS_SIZE]; memcpy(reset_obs,obs,sizeof(obs));
        auto reset_state=saved(*e.emu);
        for(int f=0;f<17;f++) retro_frame(&e,RETRO_BTN_RIGHT|RETRO_BTN_A);
        puf_reset(&e);
        auto restored=saved(*e.emu);
        if(restored!=reset_state) {
            fprintf(stderr,"reset mismatch level=%d-%d bytes=%zu/%zu\n",
                i/4+1,i%4+1,reset_state.size(),restored.size());
            int differences=0;
            for(size_t j=0;j<std::min(reset_state.size(),restored.size());j++) {
                if(reset_state[j]!=restored[j] && differences++<16)
                    fprintf(stderr,"byte %zu: %02x -> %02x\n",j,
                        (unsigned char)reset_state[j],(unsigned char)restored[j]);
            }
        }
        require(restored==reset_state,"reset state mismatch");
        require(!memcmp(obs,reset_obs,sizeof(obs)),"reset observation contains stale framebuffer");
    }
    require(data.size()>20,"stage labels alias level data");
    require(retro_observation_synthetic_parity(&e),"synthetic palette observation mismatch");
    retro_parse_spawns(&e,"8-4"); e.cur_spawn=0; puf_reset(&e);
    require(e.world==8&&e.stage==4,"single-level spawn ignored");
    e.max_frames=9; e.frameskip=4;
    float sum=0; int frames=0;
    for(int i=0;i<3;i++) { puf_step(&e); sum+=rew; frames+=e.last_frames; }
    require(done==1&&e.log.truncations==1&&e.log.deaths==0,"horizon classified as death");
    require(frames==9&&e.log.episode_length==9,"extra emulated frames at horizon");
    require(fabs(e.log.episode_return-sum)<1e-6,"episode return is not summed");
    require(e.log.frames==9&&e.log.decisions==3,"episode metrics use rollout windows");
    // Actual ROM trajectories: a failed exploration attempt should beat idle.
    retro_parse_spawns(&e,"1-1"); e.cur_spawn=0; e.max_frames=4000; e.frameskip=1;
    float returns[2];
    for(int moving=0;moving<2;moving++) {
        puf_reset(&e); e.log={}; done=0;
        require(e.progress_pixels==0&&e.frontier_count==1,"reset retains earned frontiers");
        act=moving?retro_mask_action(RETRO_BTN_RIGHT|RETRO_BTN_B):0;
        while(!done) puf_step(&e);
        returns[moving]=e.log.episode_return;
        if(moving) require(e.log.deaths==1&&e.log.checkpoints>=2,"run-right trajectory missed checkpoints/death");
        else require(e.log.truncations==1&&e.log.checkpoints==0,"idle timeout earns milestones");
    }
        require(returns[1]>returns[0],"actual ROM exploration loses to camping");
    printf("PASS: ROM idle return %.8f; run-right/death return %.8f\n",returns[0],returns[1]);
    // With every event weight disabled, moving, idling, death and timeout
    // must all be exactly zero, including the terminal/reset transition.
    e.completion_reward=e.completion_time_bonus=e.pipe_segment_bonus=0;
    e.death_penalty=e.checkpoint_reward=e.score_scale=e.coin_reward=0;
    e.idle_penalty=0;
    e.completion_time_target_bonus=0;
    for(int moving=0;moving<2;moving++) {
        puf_reset(&e); e.log={}; done=0;
        act=moving?retro_mask_action(RETRO_BTN_RIGHT|RETRO_BTN_B):0;
        while(!done) {
            puf_step(&e);
            require(rew==0,"implicit movement/terminal reward remains with every weight zero");
        }
        require(e.log.episode_return==0,"zero-weight episode has nonzero return");
    }
    puts("PASS: no implicit reward on movement, idling, death or timeout");
    puf_close(&e);
    printf("PASS: 32 starts (%zu level data pointers), exact reset images, single-level spawn, horizon and return\n",data.size());
}
static void natural_start_timing_tests() {
    Dict cfg={}; dict_set_str(&cfg,"spawn_levels","1-1");
    Env e={}; puf_init(&e,&cfg); puf_reset(&e);
    Nes_Emu raw; retro_check(raw.set_cart(&retro_rom().cart));
    std::vector<unsigned char> pixels(Nes_Emu::buffer_width*256);
    raw.set_pixels(pixels.data()+8*Nes_Emu::buffer_width,Nes_Emu::buffer_width);
    const unsigned char* m=raw.low_mem();
    bool title=false,started=false;
    for(int i=0;i<600;i++) {
        retro_check(raw.emulate_frame(0));
        if(i>30&&m[0x770]==0&&m[0x772]==3) { title=true; break; }
    }
    require(title,"unmodified cold boot did not reach title");
    unsigned long title_frame=raw.video_frame_count();
    retro_check(raw.emulate_frame(RETRO_BTN_START));
    for(int i=0;i<1200;i++) {
        int previous=m[0xe];
        retro_check(raw.emulate_frame(0));
        if(m[0x770]==1&&m[0x772]>=3&&m[0xe]==8&&previous<8) { started=true; break; }
    }
    require(started&&robs_time(m)==400&&robs_x(m)==40,"normal new game has wrong RTA start state");
    require(e.start->rta_offset_frames==0&&e.tick==0,"intro skip dropped timed frames");
    if(saved(*e.emu)!=saved(raw)) {
        auto prepared=saved(*e.emu),natural=saved(raw);
        fprintf(stderr,"start mismatch: core frames %lu/%lu, sizes %zu/%zu\n",
            e.emu->video_frame_count(),raw.video_frame_count(),prepared.size(),natural.size());
        int reported=0;
        for(int i=0;i<2048;i++) if(e.emu->low_mem()[i]!=m[i]&&reported++<16)
            fprintf(stderr,"RAM %04x prepared=%02x natural=%02x\n",i,e.emu->low_mem()[i],m[i]);
        reported=0;
        for(size_t i=0;i<std::min(prepared.size(),natural.size());i++) if(prepared[i]!=natural[i]&&reported++<16)
            fprintf(stderr,"state offset %zu prepared=%02x natural=%02x\n",i,(unsigned char)prepared[i],(unsigned char)natural[i]);
        throw std::runtime_error("saved start changes CPU/PPU/APU/RAM/RNG relative to a normal new game");
    }
    const auto& frame=raw.frame();
    for(int y=0;y<240;y++) for(int x=0;x<256;x++)
        require(e.start->palette[e.start->pixels[y*256+x]]==frame.palette[frame.pixels[y*frame.pitch+x]],
            "saved start screenshot differs from natural RTA start");
    unsigned long start_frame=raw.video_frame_count();
    int last_time=400,last_decrement=0,last_bus_reset=0,decrements=0,bus_resets=0;
    for(int f=1;f<=168;f++) {
        int old_bus=m[0x77f];
        retro_check(raw.emulate_frame(0)); retro_frame(&e,0);
        require(saved(*e.emu)==saved(raw),"post-intro continuation differs from cold-boot game");
        require(raw.video_frame_count()-start_frame==(unsigned long)f,"NES video counter differs from elapsed frames");
        int time=robs_time(m);
        if(time!=last_time) {
            require(time==last_time-1,"HUD countdown skipped a unit");
            if(last_decrement) require(f-last_decrement==24,"HUD timer is not NTSC 24-frame timing");
            last_time=time; last_decrement=f; decrements++;
        }
        if(m[0x77f]>old_bus) {
            require(old_bus==0&&m[0x77f]==20,"frame-rule interval is not NTSC 21-frame timing");
            if(last_bus_reset) require(f-last_bus_reset==21,"frame-rule interval changed");
            last_bus_reset=f; bus_resets++;
        }
    }
    require(decrements>=6&&bus_resets>=7,"insufficient active timer samples");
    printf("PASS: unmodified cold boot == saved RTA start at frame %lu (%lu after START), offset=0; HUD=24f, bus=21f\n",
        start_frame,start_frame-title_frame);
    puf_close(&e); dict_clear(&cfg);
}
static void core_parity_tests() {
    RetroRom& rom=retro_rom();
    Nes_Emu reference,shared;
    retro_check(reference.set_cart(&rom.cart)); retro_check(shared.set_cart(&rom.cart,&rom.seed));
    shared.set_idle_skip(true);
    require(reference.chr_cache_identity()!=shared.chr_cache_identity(),"reference must use private cache");
    std::vector<unsigned char> pa(Nes_Emu::buffer_width*256),pb(pa.size());
    reference.set_pixels(pa.data()+8*Nes_Emu::buffer_width,Nes_Emu::buffer_width);
    shared.set_pixels(pb.data()+8*Nes_Emu::buffer_width,Nes_Emu::buffer_width);
    unsigned int rng=98765; int frames=0;
    for(int level=0;level<32;level++) {
        reference.load_state(rom.starts[level]->state); shared.load_state(rom.starts[level]->state);
        for(int f=0;f<2048;f++) {
            // Exercise moving/jumping trajectories, backward running, idle,
            // arbitrary combinations, death continuation and pause/release.
            int buttons=retro_action_mask(retro_random(&rng)%64);
            if(f<768) buttons=RETRO_BTN_RIGHT|RETRO_BTN_B|((f%48<28)?RETRO_BTN_A:0);
            else if(f<1024) buttons=RETRO_BTN_LEFT|RETRO_BTN_B|((f%37<19)?RETRO_BTN_A:0);
            else if(f>=1280&&f<1536) buttons=0;
            if(f==1600||f==1620) buttons=RETRO_BTN_START;
            retro_check(reference.emulate_frame(buttons,0));
            retro_check(f%4==3?shared.emulate_frame(buttons,0):shared.emulate_skip_frame_fast(buttons,0));
            require(!reference.error_count()&&!shared.error_count(),"unsupported opcode in parity trajectory");
            require(!memcmp(reference.low_mem(),shared.low_mem(),2048),"full/skip RAM differs");
            if(f%4==3) {
                require(equal_image(reference,shared),"full/skip visible image differs");
                require(saved(reference)==saved(shared),"full/skip serialized state differs");
            }
            // Roundtrip one side only, then verify its subsequent evolution.
            if(f%128==127) { Nes_State s; shared.save_state(&s); shared.load_state(s); }
            frames++;
        }
    }
    printf("PASS: %d ROM frames across 32 starts, idle fold on/off, shared/private CHR, full/skip render, save/load parity\n",frames);
}
static void vector_tests(Dict* cfg) {
    Dict vec={}; dict_set(&vec,"total_agents",32); dict_set(&vec,"num_buffers",2);
    int n,starts[2],counts[2]; Env* a=my_vec_init(&n,starts,counts,&vec,cfg); Env* b=my_vec_init(&n,starts,counts,&vec,cfg);
    std::vector<float> oa(n*OBS_SIZE),ob(n*OBS_SIZE),act(n),ra(n),rb(n),ta(n),tb(n);
    std::set<unsigned> seeds; unsigned int rng=73,coverage=0;
    for(int i=0;i<n;i++) {
        require(a[i].rng!=0,"zero vector seed"); seeds.insert(a[i].rng);
        a[i].agents[0]={oa.data()+i*OBS_SIZE,&act[i],&ra[i],&ta[i],nullptr,0};
        b[i].agents[0]={ob.data()+i*OBS_SIZE,&act[i],&rb[i],&tb[i],nullptr,0};
        a[i].max_frames=b[i].max_frames=71; puf_reset(&a[i]); puf_reset(&b[i]);
        for(int j=0;j<128;j++) { Env copy=a[i]; copy.rng=retro_random(&rng); retro_pick_spawn(&copy); coverage|=1u<<copy.cur_spawn; }
    }
    require(seeds.size()==32&&coverage==0xffffffffu,"missing vector seed/spawn coverage");
    a[0].display=new RetroDisplay{}; b[0].display=new RetroDisplay{};
    for(int s=0;s<160;s++) {
        for(int i=0;i<n;i++) {
            act[i]=retro_random(&rng)%64; puf_step(&a[i]);
            // Read before the next environment reuses this worker's image.
            require(retro_observation_matches_reference(&a[i],oa.data()+i*OBS_SIZE),
                "observation differs from staged reference");
        }
        #pragma omp parallel for num_threads(4) schedule(static)
        for(int i=0;i<n;i++) puf_step(&b[i]);
        require(oa==ob&&ra==rb&&ta==tb,"worker count changes observations/rewards/terminals");
        require(!memcmp(a[0].display->pixels,b[0].display->pixels,sizeof(a[0].display->pixels))
            &&!memcmp(a[0].display->palette,b[0].display->palette,sizeof(a[0].display->palette)),
            "watched image changes when other environments reuse worker scratch");
    }
    my_vec_close(a); my_vec_close(b);
    dict_clear(&vec);
    printf("PASS: actual vector constructor, all spawn coverage, 1/4-worker reproducibility through resets\n");
    puts("PASS: 5,120 float32 observations bit-identical to staged reference");
}
static void playback_tests(Dict* cfg) {
    Env e={}; e.rng=73; puf_init(&e,cfg);
    float obs[OBS_SIZE],act=retro_mask_action(RETRO_BTN_RIGHT|RETRO_BTN_B),rew=0,done=0;
    e.agents[0]={obs,&act,&rew,&done,nullptr,0}; e.spawn_pin=1; e.cur_spawn=0;
    Nes_Emu raw; retro_check(raw.set_cart(&retro_rom().cart));
    std::vector<unsigned char> pixels(Nes_Emu::buffer_width*256);
    raw.set_pixels(pixels.data()+8*Nes_Emu::buffer_width,Nes_Emu::buffer_width);
    for(int skip:{1,4}) {
        puf_reset(&e); e.log={}; e.max_frames=10000; e.frameskip=skip;
        // Stand in for an already-advanced run: the chosen new-game start is
        // 1-1, but the current ROM is at the validated 2-1 start. Losing a life
        // must remain in 2-1; only game over may return to the selected 1-1.
        const auto& later=retro_rom().starts[retro_level_id(2,1)]->state;
        e.emu->load_state(later); e.reset_image=false; retro_sync_from_emu(&e);
        if(e.rom_blocks) require(e.emu->set_rom_blocks(true),"playback test failed to restore compiled ROM blocks");
        raw.load_state(later);
        int lives=e.life,respawns=0,frames=0; bool saw_death=false,game_over=false;
        RetroPlayback playback;
        while(frames<10000) {
            bool clear_rnn=retro_playback_step(&e,&playback);
            for(int f=0;f<e.last_frames;f++) retro_check(raw.emulate_frame(RETRO_BTN_RIGHT|RETRO_BTN_B,0));
            frames+=e.last_frames;
            if(done) {
                require(robs_gameover(raw.low_mem()),"playback reset before the ROM used its final life");
                require(clear_rnn&&!playback.waiting_respawn,"game over did not reset recurrent playback state");
                require(e.world==1&&e.stage==1&&e.tick==0&&e.life==lives,"game over did not return to the selected start");
                game_over=true; break;
            }
            require(saved(*e.emu)==saved(raw),"playback restored or changed the live ROM before game over");
            require(e.tick==frames&&e.log.n==0&&!e.reset_image,"life loss silently reset the playback episode");
            require(std::isfinite(rew),"non-finite playback reward");
            for(float value:obs) require(std::isfinite(value),"non-finite playback input");
            saw_death|=robs_dead(raw.low_mem());
            if(clear_rnn) {
                respawns++;
                require(e.world==2&&e.stage==1,"respawn returned to the original selected level");
                require(e.life==lives-respawns,"respawn replenished lives or cleared recurrent state twice");
                require(raw.low_mem()[0xe]==8&&!robs_dying(raw.low_mem()),"recurrent state cleared during the death animation");
            }
        }
        require(saw_death&&game_over&&respawns==lives,"natural deaths/respawns/game over were not all exercised");
        require(e.log.deaths==lives+1&&e.log.n==1,"playback counted a life loss more than once");
        printf("PASS: playback frameskip=%d: %d raw-ROM-identical frames, %d natural respawns, game-over restart\n",skip,frames,respawns);
    }
    // The training entry point still ends at the first death and restores all
    // lives. Playback must never silently change training's episode semantics.
    puf_reset(&e); e.log={}; e.frameskip=1; done=0; int training_lives=e.life;
    for(int i=0;i<10000&&!done;i++) puf_step(&e);
    require(done&&e.log.n==1&&e.log.deaths==1&&e.tick==0&&e.life==training_lives,
        "training no longer ends at the first death");
    // Explicit single-life viewing matches that same path.
    puf_reset(&e); e.log={}; done=0; RetroPlayback single={true,false};
    for(int i=0;i<10000&&!done;i++) retro_playback_step(&e,&single);
    require(done&&e.log.n==1&&e.log.deaths==1&&e.tick==0,"--single-life did not retain training-style resets");
    // The inspector's explicit short test horizon must still exercise resets.
    puf_reset(&e); e.log={}; e.max_frames=9; e.frameskip=4; RetroPlayback bounded;
    for(int i=0;i<3;i++) retro_playback_step(&e,&bounded);
    require(done&&e.log.truncations==1&&e.log.episode_length==9,"bounded inspector playback lost its regression reset");
    puf_close(&e);
    puts("PASS: training/single-life death boundaries and bounded inspector resets unchanged");
}
static void practice_tests() {
    Dict cfg={}; dict_set_str(&cfg,"spawn_levels","1-1");
    dict_set_str(&cfg,"practice_replay","ocean/retro/practice/pipe_exit.inputs");
    dict_set(&cfg,"terminate_on_clear",1); dict_set(&cfg,"completion_on_rta_split",1);
    dict_set(&cfg,"completion_time_min_frames",0); dict_set(&cfg,"completion_time_max_frames",1800);
    dict_set(&cfg,"max_frames",1800); dict_set(&cfg,"completion_reward",0);
    dict_set(&cfg,"completion_time_bonus",16); dict_set(&cfg,"death_penalty",0);
    dict_set(&cfg,"checkpoint_reward",0); dict_set(&cfg,"reward_scale",0.0625);
    Env e={}; puf_init(&e,&cfg);
    std::vector<float> obs(OBS_SIZE); float action=0,reward=0,done=0;
    e.agents[0]={obs.data(),&action,&reward,&done,nullptr,0}; puf_reset(&e);
    require(e.practice&&e.tick==0&&e.rta.start_offset==0,"practice did not reset segment clock");
    require(e.time==376&&e.emu->low_mem()[0x77f]==19,"practice modified HUD/bus phase");
    require(e.coins==7&&e.episode_coin_events==0,"saved HUD coins counted as fresh pickups");
    for(int i=0;i<256*240;i++) require(e.start->palette[e.start->pixels[i]]==15,"practice image is not black");
    auto snapshot=saved(*e.emu); auto reset_obs=obs;
    Nes_Emu raw; retro_check(raw.set_cart(&retro_rom().cart));
    std::vector<unsigned char> pixels(Nes_Emu::buffer_width*256);
    raw.set_pixels(pixels.data()+8*Nes_Emu::buffer_width,Nes_Emu::buffer_width);
    raw.set_idle_skip(false); raw.load_state(retro_rom().starts[0]->state);
    FILE* file=fopen("ocean/retro/practice/pipe_exit.inputs","r");
    require(file,"missing practice fixture"); unsigned long long hash; int count,mask;
    require(fscanf(file,"RETRO_PRACTICE_V1 %llx %d",&hash,&count)==2,"bad fixture header");
    for(int i=0;i<count;i++) { require(fscanf(file,"%d",&mask)==1,"truncated fixture"); retro_check(raw.emulate_frame(mask)); }
    fclose(file);
    require(saved(raw)==snapshot,"practice snapshot differs from uninterrupted reference prefix");
    bool emerged=false;
    for(int i=0;i<300;i++) {
        action=retro_mask_action(RETRO_BTN_RIGHT|RETRO_BTN_B|((i%48>=10)?RETRO_BTN_A:0));
        retro_check(raw.emulate_frame(retro_action_mask((int)action))); puf_step(&e);
        require(!done&&saved(raw)==saved(*e.emu),"practice continuation changed emulated state");
        require(reward==0,"nonterminal practice transition paid reward with event weights disabled");
        emerged |= e.x_pos>2600&&e.emu->low_mem()[0xe]==8;
    }
    require(emerged,"practice never emerged from the pipe");
    puf_reset(&e);
    require(saved(*e.emu)==snapshot&&obs==reset_obs,"practice reset state/image drifted");
    e.max_frames=1; puf_step(&e);
    require(done&&e.log.episode_length==1,"practice timeout frame accounting changed");
    Dict metrics={}; puf_log(&e.log,&metrics);
    require(dict_find(&metrics,"segment_seconds")&&!dict_find(&metrics,"rta_seconds")
        &&dict_get(&metrics,"rta_valid")==0,"practice mislabeled as full-run RTA");
    require(dict_get(&metrics,"hud_coins")==7&&dict_get(&metrics,"coin_events")==0,
        "HUD coins and new pickups not separated");
    require(!dict_find(&metrics,"area_transition_rewards")&&dict_find(&metrics,"novel_areas"),
        "transition event counter still labeled as reward");
    dict_clear(&metrics);
    // Log includes only the terminal episode, not the earlier manual-reset prefix.
    // Reset counters above discard that unfinished attempt, so it records one frame.
    puf_close(&e); dict_clear(&cfg);
    puts("PASS: black-screen practice exact prefix/state/image, natural emergence, repeatable resets");
}
int main(int argc,char** argv) {
    try {
        scalar_tests();
        Dict cfg={}; dict_set_str(&cfg,"spawn_levels","all"); dict_set(&cfg,"frameskip",1);
        playback_tests(&cfg);
        if(argc!=2||strcmp(argv[1],"--playback-only")) {
            natural_start_timing_tests(); level_and_reset_tests(&cfg); core_parity_tests(); vector_tests(&cfg); practice_tests();
        }
        dict_clear(&cfg);
        return 0;
    } catch(const std::exception& e) { fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
