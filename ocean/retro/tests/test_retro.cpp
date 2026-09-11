#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#include "nes_emu/abstract_file.h"
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
    RetroRom& rom=retro_rom();
    require(e.emu->chr_cache_identity()==rom.seed.chr_cache_identity(),"CHR cache not shared");
    std::set<int> data;
    for(int i=0;i<32;i++) {
        e.cur_spawn=i; puf_reset(&e);
        require(e.emu->chr_cache_identity()==rom.seed.chr_cache_identity(),"reset detached immutable CHR cache");
        require(e.world==i/4+1&&e.stage==i%4+1,"wrong level label");
        int selected=retro_start_area(rom,e.world,e.stage);
        int base=rom.cart.prg()[0x9cb4-0x8000+e.world-1];
        if(rom.cart.prg()[0x9cbc-0x8000+base+selected]==0x29) selected++;
        require(e.area==selected+1,"wrong level area");
        data.insert(e.start->data);
        float reset_obs[OBS_SIZE]; memcpy(reset_obs,obs,sizeof(obs));
        auto reset_state=saved(*e.emu);
        for(int f=0;f<17;f++) retro_frame(&e,RETRO_BTN_RIGHT|RETRO_BTN_A);
        puf_reset(&e);
        require(saved(*e.emu)==reset_state,"reset state mismatch");
        require(!memcmp(obs,reset_obs,sizeof(obs)),"reset observation contains stale framebuffer");
    }
    require(data.size()>20,"stage labels alias level data");
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
    puf_close(&e);
    printf("PASS: 32 starts (%zu level data pointers), exact reset images, single-level spawn, horizon and return\n",data.size());
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
        for(int i=0;i<n;i++) { act[i]=retro_random(&rng)%64; puf_step(&a[i]); }
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
}
int main() {
    try {
        scalar_tests();
        Dict cfg={}; dict_set_str(&cfg,"spawn_levels","all"); dict_set(&cfg,"frameskip",1);
        level_and_reset_tests(&cfg); core_parity_tests(); vector_tests(&cfg);
        dict_clear(&cfg);
        return 0;
    } catch(const std::exception& e) { fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
