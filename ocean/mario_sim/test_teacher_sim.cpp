// Practice curriculum teacher on the CPU path (headless): group construction from the v3 practice bank,
// reset sampling, attempt outcomes, natural-play failure crediting, the host refresh and its cadence,
// and the .teacher / .practice sidecars. Then mazes, pipes and route states (maze_20261110): loop tracking,
// the route table, route transitions as attempt outcomes and as game-actor failures, the v3 route states
// against a native replay of the reference movie, and real-game attempts along the movie's inputs.
// The shared arithmetic itself is checked by ocean/retro/tests/test_teacher.cpp; this test checks how
// mario_sim feeds and uses it.
#define SMB_HEADLESS
#include "mario_sim.h"
#include "test_common.h"
#include "route_movie.h"
#include <set>

static const char* fpt_teacher_bank="build/mario_sim/practice_v3/bank.bin";
static const char* fpt_route_movie="/mnt/d/puffer_media/mario_tas/smb_warpless_aknell.fm2";
static Ini fpt_teacher_ini(int teacher) {
    Ini ini={};puf_ini_load_file(&ini,"config/mario_sim.ini");
    dict_set(puf_ini_section(&ini,"vec",0),"total_agents",4);
    auto* game=puf_ini_section(&ini,"game",0);dict_set(game,"practice_fraction",.25);
    dict_set(game,"max_frames",17);dict_set(game,"practice_max_frames",13);
    dict_set_str(game,"practice_bank",fpt_teacher_bank);dict_set(game,"practice_teacher",teacher);
    // The cases below are written for a 512-px win distance; the training config uses a longer one.
    dict_set(game,"practice_success_distance",512);
    // ... and for the plain gate (no lookahead, no zero-win fade) on the v3 bank; dense_route_tests sets its own.
    dict_set(game,"practice_gate_lookahead",1);dict_set(game,"practice_gate_patience",0);dict_set(game,"practice_dense_share",0);
    // ... and round 0's dense chains (no frame gap, rank power 1); dense_segment_tests sets the review-round values.
    dict_set(game,"practice_dense_gap",0);dict_set(game,"practice_dense_rank_power",1);
    return ini;
}
struct FptTeacherEnvs {
    Env env[4]={};float obs[4][FPT_OBS],actions[4]={},rewards[4]={},terminals[4]={};
    void open(Ini* ini) {
        fpt_configure(ini,"train",nullptr);
        for(int i=0;i<4;i++) {
            env[i].rng=i;env[i].agents[0].observations=obs[i];env[i].agents[0].actions=&actions[i];
            env[i].agents[0].rewards=&rewards[i];env[i].agents[0].terminals=&terminals[i];
            puf_init(&env[i],puf_ini_section(ini,"env",0));puf_reset(&env[i]);
            fpt_check(env[i].is_fpg==(i==3)&&env[i].is_practice==(i==2),"50/25/25 pool allocation failed");
        }
    }
    void close(){for(auto& e:env)puf_close(&e);}
};
static uint64_t fpt_entry_key(const SmbBankEntry& e,int width) {
    const auto* m=e.scene.initial.ram;
    uint32_t area=((m[0x75f]*4u+m[0x75c])<<16)|(m[0xe7]+256u*m[0xe8]);
    return ((uint64_t)area<<16)|(uint64_t)(fpt_x(m,0)/width);
}
static void fpt_after(Env& e) {
    smb_after_frame(&e,e.state,&fpt_host->task,&fpt_host->time,&fpt_host->curriculum,fpt_host->table.data(),
        &fpt_host->game,&fpt_host->practice,&fpt_host->teacher.device);
}

// ---- Mazes, pipes and route states (reports/mario_sim/maze_20261110/SPEC.md) ----
// build_practice_bank's useful(): live gameplay, Mario on screen and standing (or swimming) at x >= 128.
static bool fpt_on_ground(const uint8_t* m) {
    int y=fpt_y(m,0);
    return m[0x770]==1&&m[0x772]==3&&m[0xe]==8&&fpt_x(m,0)>=128&&y>=32&&y<208&&(m[0x74e]==0?(y<176):m[0x1d]==0);
}
static uint32_t fpt_area_of(const uint8_t* m) {return ((uint32_t)(m[0x75f]*4+m[0x75c])<<16)|(uint32_t)(m[0xe7]+256*m[0xe8]);}
// The ROM-import comparison of rom_check.h: RAM, registers and the frame clock.
static bool fpt_same_logic(const SmbLogic& a,const SmbLogic& b) {
    return !memcmp(a.ram,b.ram,SMB_RAM)&&a.pc==b.pc&&a.a==b.a&&a.x==b.x&&a.y==b.y&&(a.p&207)==(b.p&207)&&a.sp==b.sp
        &&a.timing.timestamp==b.timing.timestamp&&a.timing.video_frame==b.timing.video_frame
        &&a.timing.control==b.timing.control&&a.timing.mask==b.timing.mask;
}
static void maze_tests() {
    // 1. Tracking: loop-backs are same-area drops of >= 900 px between consecutive tracked frames; raw_x is uncapped.
    {
        SmbGameConfig c;SmbGameProgress q={};SmbLogic s={};auto* m=s.ram;
        m[0x770]=1;m[0x772]=3;m[0xe]=8;m[0xb5]=1;m[0x75f]=3;m[0x75c]=3;m[0xe7]=0x12;m[0xe8]=0xa2;
        auto at=[&](int x){m[0x6d]=(uint8_t)(x/256);m[0x86]=(uint8_t)(x%256);smb_game_track_progress(&s,&q,&c);};
        at(1300);at(276);fpt_check(q.maze_loops==1&&q.raw_x==276,"a 1,024-px same-area drop is not one maze loop");
        q={};at(1300);m[0xe]=7;at(800);m[0xe]=8;at(276);fpt_check(q.maze_loops==0,"a drop across an untracked frame counted as a loop");
        q={};at(1300);at(401);fpt_check(q.maze_loops==0,"an 899-px drop counted as a loop");
        q={};at(1300);at(400);fpt_check(q.maze_loops==1,"a 900-px drop is not a loop");
        q={};at(1300);m[0xe7]=0x13;at(276);m[0xe7]=0x12;fpt_check(q.maze_loops==0,"a drop into another area counted as a loop");
        q={};at(3500);at(3600);fpt_check(q.raw_x==3600&&q.last_x==3400&&q.maze_loops==0,"raw_x is capped or a rise counted");
    }
    // 2. Route table from a hand-built bank.
    {
        auto entry=[](int stage,int data,int frame,bool route) {
            SmbBankEntry e={};e.stage=stage;e.source_frame=frame;e.flags=SMB_BANK_PRACTICE|(route?SMB_BANK_ROUTE:0);
            auto* m=e.scene.initial.ram;m[0x75f]=stage/4;m[0x75c]=stage%4;m[0xe7]=data&255;m[0xe8]=data>>8;return e;
        };
        auto key=[](int stage,int data){return ((uint32_t)stage<<16)|(uint32_t)data;};
        // Stage 31 route areas by frame: A A B A (+ a non-route C in between); stage 30: A' E A' with A' = A's pointer.
        std::vector<SmbBankEntry> e={entry(31,0xa3fc,40,true),entry(31,0xaec2,30,true),entry(31,0xa111,25,false),entry(31,0xa3fc,10,true),
            entry(31,0xa3fc,20,true),entry(30,0xa3fc,70,true),entry(30,0xa3fc,50,true),entry(30,0xa500,60,true)};
        std::vector<uint32_t> from,to;smb_route_pairs(e,&from,&to);
        std::vector<uint32_t> want_from={key(30,0xa3fc),key(30,0xa500),key(31,0xa3fc),key(31,0xaec2)};
        std::vector<uint32_t> want_to={key(30,0xa500),key(30,0xa3fc),key(31,0xaec2),key(31,0xa3fc)};
        fpt_check(from==want_from&&to==want_to,"route table from hand-built entries is wrong");
        SmbTeacherDevice d={};d.pairs=(int)from.size();d.pair_from=from.data();d.pair_to=to.data();
        fpt_check(smb_route_pair(&d,key(31,0xa3fc),key(31,0xaec2))&&smb_route_pair(&d,key(31,0xaec2),key(31,0xa3fc))
            &&!smb_route_pair(&d,key(31,0xaec2),key(31,0xaec2))&&!smb_route_pair(&d,key(30,0xa3fc),key(31,0xa3fc))
            &&smb_route_known(&d,key(30,0xa500))&&!smb_route_known(&d,key(31,0xa111))&&!smb_route_known(&d,key(29,0xa3fc)),"route lookups wrong");
        for(auto& x:e)x.flags&=~SMB_BANK_ROUTE;
        smb_route_pairs(e,&from,&to);fpt_check(from.empty()&&to.empty(),"entries without the route flag made pairs");
        printf("route table: hand-built bank gives %zu pairs as expected\n",want_from.size());
    }

    Ini ini=fpt_teacher_ini(1);
    dict_set(puf_ini_section(&ini,"game",0),"max_frames",100000);dict_set(puf_ini_section(&ini,"game",0),"practice_max_frames",6000);
    FptTeacherEnvs envs;envs.open(&ini);
    auto& h=*fpt_host;auto& t=h.teacher;const size_t G=t.groups.size();const int width=t.group_width,U=t.params.upstream_groups;
    uint32_t* selected=t.counters.data();uint32_t* attempts=selected+G;uint32_t* wins=selected+2*G;
    uint32_t* weights=selected+3*G;uint32_t* events=selected+4*G;
    fpt_check(h.practice.loop_terminal==1&&t.counters.size()==4*G+SMB_TEACHER_EVENTS&&t.device.events==events
        &&t.device.pairs==(int)t.pair_from.size()&&t.device.pairs>0&&t.device.pair_from==t.pair_from.data(),"route table or loop_terminal not configured");
    auto& dev=t.device;const SmbTeacherDevice real_table=dev;uint32_t from[1],to[1];
    // A one-pair test table; the on-route loop table is switched off with it (dev = real_table restores both).
    auto table=[&](int n,uint32_t a,uint32_t b){dev.pairs=n;dev.pair_from=from;dev.pair_to=to;from[0]=a;to[0]=b;dev.loops=0;};
    auto other=[](uint32_t k,uint32_t data){return (k&0xffff0000u)|data;};   // a fake area of the same stage

    // 3./4. Practice attempts. The test does not advance the engine: k1/x1 are the reset state's area and x.
    auto& p=envs.env[2];
    auto attempt=[&](const char* why,int expect,int dead,auto&& setup) {   // expect: 1 win, 0 loss, -1 none
        puf_reset(&p);int g=p.teacher_group;uint32_t a=attempts[g],w=wins[g];std::vector<uint32_t> ev(events,events+4);
        uint32_t k1=p.teacher_area;int x1=fpt_x(p.state->ram,0);
        fpt_check(p.game.tracking&&p.game.last_key==k1&&p.game.raw_x==x1&&p.teacher_track_deaths==0&&!p.game.deaths,"practice reset not tracked");
        setup(k1,x1);fpt_after(p);
        if(expect<0)fpt_check(attempts[g]==a&&wins[g]==w&&p.teacher_pending,why);
        else fpt_check(attempts[g]==a+1&&wins[g]==w+(uint32_t)expect&&!p.teacher_pending,why);
        fpt_check(p.episode.status==(dead?SMB_EPISODE_DEAD:SMB_EPISODE_ACTIVE),why);
        fpt_check(std::equal(ev.begin(),ev.end(),events),"a practice actor counted failure events");
    };
    attempt("forward route pair did not record a win",1,0,[&](uint32_t k1,int x1){
        table(1,other(k1,0x1111),k1);p.game.tracking=0;p.game.last_key=other(k1,0x1111);p.game.raw_x=x1;});
    attempt("wrong exit (known area, not a pair) did not record a loss and end the episode",0,1,[&](uint32_t k1,int x1){
        table(1,k1,other(k1,0x2222));p.game.tracking=0;p.game.last_key=other(k1,0x2222);p.game.raw_x=x1;});
    h.practice.loop_terminal=0;
    attempt("wrong exit with loop_terminal=0 did not record a loss or ended the episode",0,0,[&](uint32_t k1,int x1){
        table(1,k1,other(k1,0x2222));p.game.tracking=0;p.game.last_key=other(k1,0x2222);p.game.raw_x=x1;});
    attempt("loop with loop_terminal=0 did not record a loss or ended the episode",0,0,[&](uint32_t k1,int x1){
        table(0,0,0);p.game.last_key=k1;p.game.raw_x=x1+900;});
    h.practice.loop_terminal=1;
    attempt("leaving an off-route area into a route area was not neutral",-1,0,[&](uint32_t k1,int x1){
        table(1,k1,other(k1,0x5555));p.game.tracking=0;p.game.last_key=other(k1,0x2222);p.game.raw_x=x1;});
    attempt("an unknown area produced an outcome",-1,0,[&](uint32_t k1,int x1){
        table(1,other(k1,0x3333),other(k1,0x4444));p.game.tracking=0;p.game.last_key=other(k1,0x2222);p.game.raw_x=x1;});
    attempt("a same-area gap 300 px back did not record a loss",0,1,[&](uint32_t k1,int x1){
        table(0,0,0);p.game.tracking=0;p.game.last_key=k1;p.game.raw_x=x1+300;});
    attempt("a same-area gap 255 px back produced an outcome",-1,0,[&](uint32_t k1,int x1){
        table(0,0,0);p.game.tracking=0;p.game.last_key=k1;p.game.raw_x=x1+255;});
    attempt("a same-area gap 600 px forward did not win by distance",1,0,[&](uint32_t k1,int x1){
        table(0,0,0);p.game.tracking=0;p.game.last_key=k1;p.game.raw_x=x1-600;p.teacher_x0=x1-600;});
    attempt("a loop-back did not record a loss and end the episode",0,1,[&](uint32_t k1,int x1){
        table(0,0,0);p.game.last_key=k1;p.game.raw_x=x1+900;});
    fpt_check(p.game.maze_loops==1,"loop not counted on the practice actor");
    attempt("a gap with a lost life produced a transition outcome",-1,0,[&](uint32_t k1,int x1){
        table(1,other(k1,0x1111),k1);p.game.tracking=0;p.game.last_key=other(k1,0x1111);p.game.raw_x=x1;p.teacher_track_deaths=1;});
    attempt("a gap without a previous area produced an outcome",-1,0,[&](uint32_t k1,int x1){
        table(1,other(k1,0x1111),k1);p.game.tracking=0;p.game.last_key=0;p.game.raw_x=x1;});
    attempt("a regained area in another stage produced an outcome",-1,0,[&](uint32_t k1,int x1){
        uint32_t k0=(k1^0x10000u);table(1,k0,k1);p.game.tracking=0;p.game.last_key=k0;p.game.raw_x=x1;});
    printf("practice attempts: forward win, wrong exit loss (terminal on/off), loop loss (terminal on/off), off-route-exit/unknown/255/lost-life/no-area/other-stage none, 300 back loss, 600 forward win\n");

    // 5. Game actor failures: integer kernel at area k0, section x0 / width; rewards and the episode untouched.
    auto& gm=envs.env[0];
    auto place=[&](uint32_t index) {
        gm.episode.scene=index;gm.episode.status=SMB_EPISODE_ACTIVE;
        fpt_check(!smb_practice_load(gm.state,&gm.episode,&gm.game,(int)h.runtime_bank[index].stage,&h.game,h.runtime_bank.data()),"game placement fault");
        gm.teacher_track_deaths=gm.teacher_route_loops=gm.teacher_loop_land=0;fpt_check(gm.game.tracking,"placed game actor not tracked");
    };
    auto failure=[&](const char* why,uint32_t index,int loop,int wrong,auto&& setup) {
        place(index);setup();uint32_t k0=gm.game.last_key;int x0=gm.game.raw_x;
        Env copy=gm;SmbLogic copy_state=*gm.state;copy.state=&copy_state;
        float expected=smb_after_frame(&copy,&copy_state,&h.task,&h.time,&h.curriculum,h.table.data(),&h.game,&h.practice,nullptr);
        std::vector<uint32_t> before(t.counters);
        float got=smb_after_frame(&gm,gm.state,&h.task,&h.time,&h.curriculum,h.table.data(),&h.game,&h.practice,&t.device);
        fpt_check(got==expected&&gm.episode.status==copy.episode.status&&!memcmp(&gm.game,&copy.game,sizeof(gm.game))
            &&!memcmp(&gm.episode,&copy.episode,sizeof(gm.episode)),"a route failure changed the game actor's reward or episode");
        int section=x0/width,credited=0;
        for(size_t g=0;g<G;g++) {
            int d=section-t.groups[g].section;uint32_t w=t.groups[g].area==k0&&d>=0&&d<=U?(uint32_t)(U+1-d):0;credited+=w>0;
            fpt_check(weights[g]-before[3*G+g]==w,why);
            fpt_check(selected[g]==before[g]&&attempts[g]==before[G+g]&&wins[g]==before[2*G+g],"failure crediting touched other counters");
        }
        fpt_check(events[0]==before[4*G]+1&&events[1]==before[4*G+1]+(uint32_t)loop&&events[2]==before[4*G+2]+(uint32_t)wrong,why);
        return credited;
    };
    // A practice entry in a group with sections upstream of it: put the game actor there.
    size_t loop_group=0;while(loop_group<G&&t.groups[loop_group].section<3)loop_group++;
    fpt_check(loop_group<G,"no group with upstream sections");uint32_t index=t.offsets[loop_group];
    int credited=failure("a loop-back did not credit the integer kernel at (k0, x0/width)",index,1,0,[&]{
        gm.game.raw_x=fpt_x(gm.state->ram,0)+900;});
    fpt_check(credited>=1,"loop credited no group");
    failure("a wrong exit did not count as a failure",index,0,1,[&]{
        uint32_t k1=gm.game.last_key;table(1,k1,other(k1,0x2222));gm.game.tracking=0;gm.game.last_key=other(k1,0x2222);});
    failure("a same-area jump back did not count as a wrong exit",index,0,1,[&]{
        table(0,0,0);gm.game.tracking=0;gm.game.raw_x=fpt_x(gm.state->ram,0)+256;});
    // A lost life during the gap: no transition, no new death this frame, nothing credited.
    {
        place(index);uint32_t k1=gm.game.last_key;table(1,k1,other(k1,0x2222));
        gm.game.tracking=0;gm.game.last_key=other(k1,0x2222);gm.game.deaths=1;std::vector<uint32_t> before(t.counters);fpt_after(gm);
        fpt_check(t.counters==before,"a gap with a lost life credited a transition");
    }
    dev=real_table;
    // A death at x = 3600 credits section 14, not the capped 13.
    {
        size_t g14=G;for(size_t g=0;g<G;g++)if(t.groups[g].section==14){g14=g;break;}
        fpt_check(g14<G,"no group at section 14");
        uint32_t key=t.groups[g14].area,before=weights[g14];
        failure("a death at x=3600 did not credit section 14",t.offsets[g14],0,0,[&]{
            gm.game.last_key=key;gm.game.raw_x=3600;gm.game.last_x=3400;gm.game.tracking=0;gm.state->ram[0x75a]=1;gm.game.lives=2;});
        fpt_check(weights[g14]-before==(uint32_t)(U+1),"the section-14 group did not get the full kernel weight");
        printf("game failures: loop credited %d groups; death at x=3600 credited section 14 of area %08x\n",credited,key);
    }

    // R1. Signed x: x 1000, then three wrapped frames (page 0xFC), then 5. One loop, no false win, credit at
    // section 3, and the area's reward frontier is not raised to the cap.
    auto put=[&](Env& e,int x){e.state->ram[0x6d]=(uint8_t)((x>>8)&255);e.state->ram[0x86]=(uint8_t)(x&255);fpt_after(e);};
    auto kernel=[&](uint32_t key,int section,const std::vector<uint32_t>& before,const char* why) {
        for(size_t g=0;g<G;g++) {
            int d=section-t.groups[g].section;uint32_t w=t.groups[g].area==key&&d>=0&&d<=U?(uint32_t)(U+1-d):0;
            fpt_check(weights[g]-before[3*G+g]==w,why);
        }
    };
    {
        table(0,0,0);puf_reset(&p);int g=p.teacher_group;uint32_t a=attempts[g],w=wins[g];
        p.teacher_x0=600;p.game.raw_x=1000;put(p,1000);
        fpt_check(p.teacher_pending,"x 1000 decided the attempt");
        for(int k=0;k<3;k++)put(p,-1024+0xE8+k);
        put(p,5);
        fpt_check(attempts[g]==a+1&&wins[g]==w&&p.game.maze_loops==1,"a wrapped loop-back recorded a win or not exactly one loop");
        size_t gs=0;while(gs<G&&!(t.groups[gs].section<=1))gs++;fpt_check(gs<G,"no group in the first sections");
        place(t.offsets[gs]);uint32_t key=gm.game.last_key;int start=gm.game.raw_x;fpt_check(start<1000,"placement past x 1000");
        std::vector<uint32_t> before(t.counters);gm.game.raw_x=1000;put(gm,1000);
        for(int k=0;k<3;k++)put(gm,-1024+0xE8+k);
        put(gm,5);
        int frontier=-1;for(int i=0;i<gm.game.frontier_count;i++)if(gm.game.frontiers[i].key==key)frontier=gm.game.frontiers[i].x;
        fpt_check(gm.game.maze_loops==1&&frontier==1000,"wrapped frames raised the frontier or loops miscounted");
        fpt_check(events[0]==before[4*G]+1&&events[1]==before[4*G+1]+1,"wrapped loop-back not one credited loop");
        kernel(key,3,before,"wrapped loop-back not credited at section 3");
        printf("R1 signed x: wrapped loop-back = 1 loop, no win, credit section 3, frontier stays %d\n",frontier);
    }
    dev=real_table;
    // R2. The on-route loop table: exactly the 8-4 entry before the movie's first loop.
    fpt_check(t.loop_key.size()==1&&t.loop_key[0]==0x1fa3fc&&dev.loops==1,"loop table is not exactly one 8-4 entry");
    uint32_t loop_index=UINT32_MAX;
    for(uint32_t k=t.offsets[0];k<t.offsets[G];k++)if(h.runtime_bank[k].flags&SMB_BANK_ROUTE_LOOP)loop_index=k;
    fpt_check(loop_index!=UINT32_MAX&&fpt_x(h.runtime_bank[loop_index].scene.initial.ram,0)==t.loop_x[0],"loop entry not in the runtime bank");
    {
        // (b) practice: the first on-route loop is passed, a second one at the same place without a gap loses.
        int g=-1;for(size_t k=0;k<G;k++)if(loop_index>=t.offsets[k]&&loop_index<t.offsets[k+1])g=(int)k;
        std::vector<SmbBankEntry> one(1,h.runtime_bank[loop_index]);p.episode.scene=0;
        fpt_check(!smb_practice_load(p.state,&p.episode,&p.game,31,&h.game,one.data()),"loop entry load fault");
        p.episode.scene=loop_index;p.practice.stage=31;p.teacher_group=g;p.teacher_pending=1;p.teacher_frames=0;
        p.teacher_x0=fpt_x(p.state->ram,0);p.teacher_area=fpt_area_of(p.state->ram);p.teacher_track_deaths=0;
        p.teacher_route_loops=p.teacher_loop_land=0;
        uint32_t a=attempts[g],w=wins[g],passed=events[3];
        put(p,1282);put(p,258);
        fpt_check(p.teacher_pending&&events[3]==passed+1&&p.episode.status==SMB_EPISODE_ACTIVE&&p.teacher_loop_land==259,"first on-route loop not passed");
        put(p,1282);put(p,258);
        fpt_check(attempts[g]==a+1&&wins[g]==w&&p.episode.status==SMB_EPISODE_DEAD&&events[3]==passed+1,"second on-route loop did not lose");
    }
    auto loop_game=[&](const char* why,int second,int walk_to,int death_section) {
        place(loop_index);gm.game.lives=2;gm.state->ram[0x75a]=2;std::vector<uint32_t> before(t.counters);
        put(gm,1282);put(gm,258);
        fpt_check(events[3]==before[4*G+3]+1&&t.counters[4*G+1]==before[4*G+1]&&events[0]==before[4*G],"game actor: first on-route loop not passed");
        before=t.counters;
        if(second){put(gm,1282);put(gm,258);}
        else {for(int k=0;k<20;k++)put(gm,258+(walk_to-258)*(k+1)/20);gm.state->ram[0x75a]=1;put(gm,walk_to);}
        fpt_check(events[0]==before[4*G]+1&&events[1]==before[4*G+1]+(uint32_t)second&&events[3]==before[4*G+3],why);
        kernel(0x1fa3fc,death_section,before,why);
    };
    loop_game("a second on-route loop was not a failure credited at section 5",1,0,5);
    loop_game("a death at x 270 after a passed loop was not credited at section 5 (x 1294)",0,270,5);
    loop_game("a death at x 600 after a passed loop was not credited at section 2",0,600,2);
    {
        // (d) A 4-4 loop is never passed.
        uint32_t i44=UINT32_MAX;for(uint32_t k=t.offsets[0];k<t.offsets[G];k++)if(h.runtime_bank[k].stage==15&&(h.runtime_bank[k].flags&SMB_BANK_ROUTE))i44=k;
        place(i44);std::vector<uint32_t> before(t.counters);gm.game.raw_x=1025;put(gm,1025);put(gm,1);
        fpt_check(events[1]==before[4*G+1]+1&&events[3]==before[4*G+3]&&events[0]==before[4*G]+1,"a 4-4 loop was passed");
    }
    printf("R2 on-route loop: 8-4 x>=%d; passed once, second loop = loss/failure at section 5, deaths after it at sections 5 and 2, 4-4 never passed\n",t.loop_x[0]);

    // 6. The v3 bank: route states in every stage, groups per stage, the 8-4 pairs, and (with the movie) groups
    // in every 256-px section the movie passes on the ground in 4-4, 7-4 and 8-4.
    SmbBank bank(fpt_teacher_bank);int route_states[32]={},route_groups[32]={},groups[32]={};
    const SmbBankEntry* first=nullptr;std::map<uint32_t,const SmbBankEntry*> by_frame;
    for(const auto& e:bank.entries)if(e.flags&SMB_BANK_ROUTE) {
        route_states[e.stage]++;by_frame[e.source_frame]=&e;
        if(!first||e.source_frame<first->source_frame)first=&e;
        fpt_check(((e.flags>>SMB_PRACTICE_BAND_SHIFT)&255)<SMB_PRACTICE_BANDS&&(e.flags&SMB_BANK_PRACTICE),"route entry flags wrong");
    }
    std::set<uint64_t> keys;for(const auto& g:t.groups)keys.insert(g.key);
    for(size_t g=0;g<G;g++) {
        groups[t.groups[g].level]++;bool route=false;
        for(uint32_t k=t.offsets[g];k<t.offsets[g+1];k++)route|=(h.runtime_bank[k].flags&SMB_BANK_ROUTE)!=0;
        route_groups[t.groups[g].level]+=route;
    }
    printf("v3 bank: %zu entries, %zu groups; stage route_states/groups(with route states):",bank.entries.size(),G);
    for(int st=0;st<32;st++) {
        printf(" %d-%d=%d/%d(%d)",st/4+1,st%4+1,route_states[st],groups[st],route_groups[st]);
        fpt_check(route_states[st]>0&&groups[st]>0,"a stage has no route states");
    }
    printf("\nroute pairs (%zu):",t.pair_from.size());
    std::vector<std::pair<uint32_t,uint32_t>> pairs84;
    for(size_t i=0;i<t.pair_from.size();i++) {
        printf(" %06x->%06x",t.pair_from[i],t.pair_to[i]);
        if((t.pair_from[i]>>16)==31||(t.pair_to[i]>>16)==31)pairs84.push_back({t.pair_from[i],t.pair_to[i]});
        fpt_check((t.pair_from[i]>>16)==(t.pair_to[i]>>16),"a route pair crosses stages");
    }
    printf("\n");
    fpt_check(pairs84==std::vector<std::pair<uint32_t,uint32_t>>{{0x1fa3fc,0x1faec2},{0x1faec2,0x1fa3fc}},"8-4 route pairs are not exactly a3fc->aec2, aec2->a3fc");
    if(!std::filesystem::exists(fpt_route_movie)) {
        printf("SKIP movie checks (6: on-ground sections, 7: real-game attempts): %s not found\n",fpt_route_movie);
        envs.close();puf_ini_free(&ini);return;
    }
    // Native replay of the movie from the earliest route state (1-1, never edited): every route entry must equal the
    // replayed state, up to the small-Mario edit; record on-ground sections, and the states for the real-game checks.
    auto masks=smb_movie_masks(fpt_route_movie,-2);const uint8_t* world=h.bank->worlds.data();
    fpt_check(first&&first->stage==0&&!(first->flags&SMB_BANK_EDITED),"earliest route state is not an unedited 1-1 state");
    SmbLogic s;fpt_check(!smb_scene_reset(&s,&first->scene),"route reset fault");
    std::set<uint64_t> ground;std::map<uint32_t,SmbLogic> movie;int compared=0;uint64_t frames=0;
    for(size_t f=first->source_frame;f<masks.size();f++) {
        if(f>first->source_frame){fpt_check(!smb_native_frame(&s,world,masks[f]),"native movie replay fault");frames++;}
        const auto* m=s.ram;
        auto it=by_frame.find((uint32_t)f);
        if(it!=by_frame.end()) {
            SmbLogic expect=s;
            if(it->second->flags&SMB_BANK_EDITED){expect.ram[0x754]=1;expect.ram[0x756]=0;}
            else fpt_check(m[0x754]==1&&m[0x756]==0,"an unedited route state is not small Mario");
            fpt_check(fpt_same_logic(expect,it->second->scene.initial),"a route state differs from the native movie replay");
            movie[(uint32_t)f]=s;compared++;
        }
        int stage=m[0x75f]*4+m[0x75c];
        if((stage==15||stage==27||stage==31)&&smb_game_tracked(m)&&fpt_on_ground(m))
            ground.insert(((uint64_t)fpt_area_of(m)<<16)|(uint64_t)(fpt_x(m,0)/width));
    }
    fpt_check(s.ram[0x75f]==7&&s.ram[0x75c]==3&&s.ram[0x770]==2,"native movie replay does not end in 8-4 victory");
    fpt_check(compared==(int)by_frame.size(),"not every route state was compared");
    int missing=0;for(uint64_t k:ground)if(!keys.count(k)){missing++;printf("  no group at %06x section %d\n",(unsigned)(k>>16),(int)(k&0xffff));}
    fpt_check(!missing,"a 4-4/7-4/8-4 section the movie passes on the ground has no group");
    printf("native movie replay: %llu frames, %d route states equal the replay, %zu on-ground 4-4/7-4/8-4 sections all have groups\n",
        (unsigned long long)frames,compared,ground.size());

    // 7. Real-game attempts on the CPU path, no policy: the movie's own inputs from a route state's source frame.
    auto real=[&](const SmbBankEntry& e,bool unedited,int* decided_frames,int* loops,uint32_t* area,int* x)->int {
        uint32_t index=UINT32_MAX;
        for(uint32_t k=t.offsets[0];k<t.offsets[G];k++)if((h.runtime_bank[k].flags&SMB_BANK_ROUTE)&&h.runtime_bank[k].source_frame==e.source_frame
            &&h.runtime_bank[k].stage==e.stage)index=k;
        fpt_check(index!=UINT32_MAX,"route entry not in the runtime bank");
        int g=-1;for(size_t k=0;k<G;k++)if(index>=t.offsets[k]&&index<t.offsets[k+1])g=(int)k;
        std::vector<SmbBankEntry> one(1,h.runtime_bank[index]);if(unedited)one[0].scene.initial=movie.at(e.source_frame);
        p.episode.scene=0;fpt_check(!smb_practice_load(p.state,&p.episode,&p.game,(int)e.stage,&h.game,one.data()),"route load fault");
        p.episode.scene=index;p.practice.stage=(int)e.stage;
        const auto* m=p.state->ram;p.teacher_group=g;p.teacher_pending=1;p.teacher_frames=0;p.teacher_x0=fpt_x(m,0);
        p.teacher_area=fpt_area_of(m);p.teacher_track_deaths=0;p.teacher_route_loops=p.teacher_loop_land=0;
        uint32_t a=attempts[g],w=wins[g];size_t f=e.source_frame+1;
        for(;p.teacher_pending&&f<masks.size();f++){fpt_check(!smb_native_frame(p.state,world,masks[f]),"native attempt fault");fpt_after(p);}
        *decided_frames=(int)(f-e.source_frame-1);*loops=p.game.maze_loops;*area=fpt_area_of(m);*x=fpt_x(m,0);
        if(p.teacher_pending){fpt_check(f==masks.size()&&attempts[g]==a,"pending attempt recorded an outcome");return -1;}   // movie ended first
        fpt_check(attempts[g]==a+1,"real-game attempt recorded no outcome");
        return (int)(wins[g]-w);
    };
    // Last route state before the movie's n-th untracked gap in the stage (pipe entries) or before a given frame.
    auto before=[&](int stage,uint32_t frame)->const SmbBankEntry* {
        const SmbBankEntry* best=nullptr;
        for(const auto& [f,e]:by_frame)if((int)e->stage==stage&&f<frame)best=e;
        return best;
    };
    // Gaps (tracked -> untracked -> tracked again) in 8-4 from the replay: re-run the 8-4 part.
    std::vector<std::pair<uint32_t,uint32_t>> gaps;   // (frame the gap starts, frame tracking returns)
    {
        const SmbBankEntry* e84=nullptr;for(const auto& [f,e]:by_frame)if(e->stage==31){e84=e;break;}
        SmbLogic r=movie.at(e84->source_frame);bool was=true;uint32_t start=0;
        for(size_t f=e84->source_frame+1;f<masks.size();f++) {
            fpt_check(!smb_native_frame(&r,world,masks[f]),"native 8-4 replay fault");bool now=smb_game_tracked(r.ram);
            if(was&&!now)start=(uint32_t)f;if(!was&&now)gaps.push_back({start,(uint32_t)f});was=now;
            if(r.ram[0x770]!=1)break;
        }
    }
    int cases=0;
    auto report=[&](const char* name,const SmbBankEntry* e,int expect_win,int expect_loops,int expect_passed=0) {
        int frames,loops,x;uint32_t area;bool unedited=false;std::vector<uint32_t> ev(events,events+4);
        int won=real(*e,false,&frames,&loops,&area,&x);
        if(won!=expect_win) {
            printf("  %s: edited state (frame %u) gave %s after %d frames at %06x x=%d (status %d, deaths %d, maze_loops=%d); "
                "retrying with the unedited movie state\n",name,e->source_frame,won<0?"no outcome before the movie ended":won?"win":"loss",
                frames,area,x,p.episode.status,p.game.deaths,loops);
            if(won<0)p.teacher_pending=0;
            ev.assign(events,events+4);unedited=true;won=real(*e,true,&frames,&loops,&area,&x);
        }
        printf("  %s: route state frame %u x=%d %s -> %s after %d frames at %06x x=%d, maze_loops=%d\n",name,e->source_frame,
            fpt_x(e->scene.initial.ram,0),unedited?"(unedited)":"(small-Mario edit)",won?"WIN":"LOSS",frames,area,x,loops);
        fpt_check(won==expect_win&&loops==expect_loops&&events[1]==ev[1]&&events[3]==ev[3]+(uint32_t)expect_passed,name);cases++;
    };
    fpt_check(gaps.size()>=3,"8-4 replay has fewer than three pipe gaps");
    printf("real-game attempts along the movie (8-4 gaps:");for(auto [a,b]:gaps)printf(" %u-%u",a,b);printf("):\n");
    // 8-4 first pipe: the movie itself loops 1282 -> 261 before entering the pipe; that on-route loop is passed
    // (events[3] + 1, no loop failure) and the attempt wins by distance at x 1848.
    report("8-4 before pipe 1 (on-route loop passed, distance win)",before(31,gaps[0].first),1,1,1);
    report("8-4 before pipe 2 (same-area forward jump, distance win)",before(31,gaps[1].first),1,0);
    report("8-4 before pipe 3 (a3fc -> aec2, forward route pair)",before(31,gaps[2].first),1,0);
    // 4-4: the first maze check is at x = 1024 (wrong floor: 1025 -> 1); a state 600 px before it wins by distance.
    {
        const SmbBankEntry* e44=nullptr;
        for(const auto& [f,e]:by_frame)if(e->stage==15&&fpt_x(e->scene.initial.ram,0)<1024-256&&fpt_x(e->scene.initial.ram,0)+t.success_distance>1040)e44=e;
        fpt_check(e44,"no 4-4 route state before the first maze check");
        report("4-4 across the first maze check (distance win)",e44,1,0);
    }
    printf("real-game attempts: %d cases as expected\n",cases);
    envs.close();puf_ini_free(&ini);
}

// ---- Exit reward, gate signal and progress cap (reports/mario_sim/exit_reward_20261111/SPEC.md) ----
static void exit_gate_tests() {
    // 1. arrival: first arrival -1, continuous 0, a gap landing 300 px past the frontier 300, behind it 0.
    {
        SmbGameConfig c;SmbGameProgress q={};SmbLogic s={};auto* m=s.ram;
        m[0x770]=1;m[0x772]=3;m[0xe]=8;m[0xb5]=1;m[0x75f]=1;m[0x75c]=1;m[0xe7]=0x47;m[0xe8]=0xae;
        auto at=[&](int x){m[0x6d]=(uint8_t)(x/256);m[0x86]=(uint8_t)(x%256);smb_game_track_progress(&s,&q,&c);};
        auto gap=[&](int x){m[0xe]=3;at(x);fpt_check(!q.tracking,"gap frame tracked");m[0xe]=8;at(x);};
        at(500);fpt_check(q.arrival==-1,"first arrival is not -1");
        at(510);fpt_check(q.arrival==0,"continuous step is not 0");
        gap(810);fpt_check(q.arrival==300,"gap 300 px past the frontier is not 300");
        gap(600);fpt_check(q.arrival==0,"gap behind the frontier is not 0");
        printf("arrival: first -1, continuous 0, gap +300 past frontier 300, gap behind frontier 0\n");
    }
    // 2. Progress cap: 3400 -> 3528 in 16-px steps earns one 128-px checkpoint with progress_cap 8192, none at 3400.
    {
        for(int cap:{8192,3400}) {
            SmbGameConfig c;c.progress_cap=cap;SmbGameProgress q={};SmbLogic s={};auto* m=s.ram;
            m[0x770]=1;m[0x772]=3;m[0xe]=8;m[0xb5]=1;m[0x75f]=7;m[0x75c]=0;m[0xe7]=0x01;m[0xe8]=0xaa;
            auto at=[&](int x){m[0x6d]=(uint8_t)(x/256);m[0x86]=(uint8_t)(x%256);return smb_game_track_progress(&s,&q,&c);};
            int earned=at(3400);for(int x=3416;x<=3528;x+=16)earned+=at(x);
            fpt_check(earned==(cap==8192?1:0),"progress cap checkpoints wrong");
        }
        printf("progress cap: 3400 -> 3528 earns 1 checkpoint at cap 8192, 0 at cap 3400\n");
    }
    // 3. Route order on a hand-built bank: level 3 groups A (frames 50), B (off-route), C (frame 10); level 4 group D.
    {
        std::vector<TeacherGroup> gs(4);gs[0].level=3;gs[1].level=3;gs[2].level=3;gs[3].level=4;
        std::vector<SmbBankEntry> bank(6);std::vector<uint32_t> offsets={0,2,3,5,6};
        uint32_t frames[6]={70,50,20,10,90,5};int route[6]={1,1,0,1,0,1};
        for(int i=0;i<6;i++){bank[i].source_frame=frames[i];bank[i].flags=SMB_BANK_PRACTICE|(route[i]?SMB_BANK_ROUTE:0);}
        std::vector<int32_t> next;std::vector<uint8_t> on;smb_route_order(gs,offsets,bank,&next,&on);
        fpt_check(on==std::vector<uint8_t>{1,0,1,1}&&next==std::vector<int32_t>{-1,-1,0,-1},"hand-built route order wrong");
        printf("route order: hand-built C(10) -> A(50) -> exit, off-route B, other-level D alone\n");
    }

    Ini ini=fpt_teacher_ini(1);
    dict_set(puf_ini_section(&ini,"game",0),"max_frames",100000);dict_set(puf_ini_section(&ini,"game",0),"practice_max_frames",6000);
    FptTeacherEnvs envs;envs.open(&ini);
    auto& h=*fpt_host;auto& t=h.teacher;const size_t G=t.groups.size();
    uint32_t* events=t.counters.data()+4*G;
    fpt_check(h.game.progress_cap==8192&&h.game.exit_reward==0.5f&&t.params.weight_gate==0.3&&t.params.weight_deaths==0.4
        &&t.params.weight_lp==0.2&&t.params.weight_stale==0.1&&t.counters.size()==4*G+5,"exit/gate configuration not parsed");
    auto& dev=t.device;const SmbTeacherDevice real_table=dev;uint32_t from[1],to[1];
    auto table=[&](int n,uint32_t a,uint32_t b){dev.pairs=n;dev.pair_from=from;dev.pair_to=to;from[0]=a;to[0]=b;};
    auto other=[](uint32_t k,uint32_t data){return (k&0xffff0000u)|data;};
    auto& gm=envs.env[0];auto& p=envs.env[2];
    auto place=[&](uint32_t index) {
        gm.episode.scene=index;
        fpt_check(!smb_practice_load(gm.state,&gm.episode,&gm.game,(int)h.runtime_bank[index].stage,&h.game,h.runtime_bank.data()),"placement fault");
        gm.teacher_track_deaths=gm.teacher_route_loops=gm.teacher_loop_land=0;fpt_check(gm.game.tracking,"placed actor not tracked");
    };
    // One frame at x after an untracked gap (tracking cleared); returns the frame's reward.
    auto land=[&](Env& e,int x)->float {
        e.game.tracking=0;e.state->ram[0x6d]=(uint8_t)((x>>8)&255);e.state->ram[0x86]=(uint8_t)(x&255);
        return smb_after_frame(&e,e.state,&h.task,&h.time,&h.curriculum,h.table.data(),&h.game,&h.practice,&t.device);
    };
    // 4. Exit reward on a game actor placed on a route state (area k, x0).
    uint32_t index=UINT32_MAX;
    for(uint32_t k=t.offsets[0];k<t.offsets[G];k++)if((h.runtime_bank[k].flags&SMB_BANK_ROUTE)&&h.runtime_bank[k].stage==4
        &&fpt_x(h.runtime_bank[k].scene.initial.ram,0)<1500){index=k;break;}
    fpt_check(index!=UINT32_MAX,"no 2-1 route state");
    auto expect_pay=[&](const char* why,float paid,uint32_t counted,auto&& setup) {
        place(index);int x0=gm.game.raw_x;uint32_t k=gm.game.last_key;float before_return=gm.episode.episode_return;
        uint32_t e4=events[4],exits=gm.game.forward_exits;
        float r=setup(k,x0);
        fpt_check(r==paid&&gm.episode.episode_return-before_return==paid&&events[4]==e4+counted&&gm.game.forward_exits==exits+(int)counted,why);
    };
    expect_pay("a 600-px same-area gap onto new ground did not pay once",0.5f,1,[&](uint32_t,int x0){return land(gm,x0+600);});
    expect_pay("re-taking the same jump paid again",0.5f,1,[&](uint32_t,int x0){
        float r=land(gm,x0+600);gm.game.raw_x=x0;gm.game.last_x=x0;return r+land(gm,x0+600);});
    expect_pay("a forward pair into a new area did not pay",0.5f,1,[&](uint32_t k,int x0){
        table(1,other(k,0x1111),k);gm.game.last_key=other(k,0x1111);gm.game.frontier_count=0;return land(gm,x0);});
    expect_pay("a forward pair into an area with a frontier beyond the landing paid",0.0f,0,[&](uint32_t k,int x0){
        table(1,other(k,0x1111),k);gm.game.frontiers[0]={k,x0+500};gm.game.last_key=other(k,0x1111);return land(gm,x0);});
    expect_pay("a gap after a lost life paid",0.0f,0,[&](uint32_t,int x0){gm.game.deaths=1;return land(gm,x0+600);});
    expect_pay("a 255-px same-area gap paid",0.0f,0,[&](uint32_t,int x0){return land(gm,x0+255);});
    h.game.exit_reward=0;
    expect_pay("exit_reward = 0 paid",0.0f,1,[&](uint32_t,int x0){return land(gm,x0+600);});
    h.game.exit_reward=0.5f;dev=real_table;
    {   // Practice actors get it too (and win the attempt by distance).
        puf_reset(&p);int x1=fpt_x(p.state->ram,0);uint32_t e4=events[4];float r=land(p,x1+600);
        fpt_check(r==0.5f&&events[4]==e4+1&&!p.teacher_pending,"practice actor not paid for a forward exit");
    }
    printf("exit reward: same-area 600 px gap pays 0.5 once, repeat 0, new-area pair pays, pair behind frontier 0, lost life 0, 255 px 0, exit_reward=0 pays 0, practice actor paid\n");

    // 5. Route order of the v3 bank: the 8-4 chain; the last 7-4 group has no next.
    {
        auto group_of=[&](uint32_t area,int section){for(size_t g=0;g<G;g++)if(t.groups[g].area==area&&t.groups[g].section==section)return (int)g;return -1;};
        size_t on=0;for(uint8_t r:t.on_route)on+=r;
        std::vector<int> has_prev(G,0);for(size_t g=0;g<G;g++)if(t.next[g]>=0){has_prev[t.next[g]]++;
            fpt_check(t.on_route[g]&&t.on_route[t.next[g]]&&t.groups[t.next[g]].level==t.groups[g].level,"next links off-route or across stages");}
        int heads=0,last=0;
        for(int stage=0;stage<32;stage++) {
            int stage_heads=0,stage_last=0;
            for(size_t g=0;g<G;g++)if(t.on_route[g]&&t.groups[g].level==stage){stage_heads+=!has_prev[g];stage_last+=t.next[g]<0;}
            fpt_check(stage_heads==1&&stage_last==1,"a stage's route order is not one chain");heads+=stage_heads;last+=stage_last;
        }
        int g84=group_of(0x1fa3fc,4),g87=group_of(0x1fa3fc,7);
        fpt_check(g84>=0&&g87>=0&&t.next[g84]==g87,"8-4 a3fc section 4 is not followed by section 7");
        printf("route order: %zu of %zu groups on-route, one chain per stage (%d)\n8-4 chain:",on,G,heads);
        int g=-1;for(size_t k=0;k<G;k++)if(t.on_route[k]&&t.groups[k].level==31&&!has_prev[k])g=(int)k;
        while(g>=0){printf(" %04x:%d",t.groups[g].area&0xffff,t.groups[g].section);g=t.next[g];}
        printf(" -> exit\n");
        int last74=-1;uint32_t best=0;
        for(size_t k=0;k<G;k++)if(t.on_route[k]&&t.groups[k].level==27) {
            uint32_t f=UINT32_MAX;for(uint32_t i=t.offsets[k];i<t.offsets[k+1];i++)if(h.runtime_bank[i].flags&SMB_BANK_ROUTE)f=std::min(f,h.runtime_bank[i].source_frame);
            if(f>=best){best=f;last74=(int)k;}
        }
        fpt_check(last74>=0&&t.next[last74]<0,"the last 7-4 group has a next");
        printf("last 7-4 group %04x:%d has no next (level exit follows)\n",t.groups[last74].area&0xffff,t.groups[last74].section);

        // 6. Gate end to end: hand-set table; 8-4 a3fc section 4 at 0/1000 with its next at 500/1000.
        std::vector<TeacherGroup> saved=t.groups;uint64_t now=1000000;
        for(size_t k=0;k<G;k++) {
            auto& x=t.groups[k];x=TeacherGroup{x.key,x.area,x.level,x.section,x.eligible,x.reserved};
            if((int)k==g84){teacher_record(x,1000,0,t.params);x.last_selected=now;continue;}
            if((int)k==g87){teacher_record(x,1000,500,t.params);x.last_selected=now;continue;}
            teacher_record(x,5,k%2?2:4,t.params);teacher_record(x,5,k%2?4:2,t.params);   // p = 7/12, progress > 0
            x.deaths=1+(double)(k%13);x.death_stamp=now;x.last_selected=(k%17)*1000;
        }
        smb_teacher_gates(t);
        size_t top=0;for(size_t k=0;k<G;k++)if(t.gate[k]>t.gate[top])top=k;
        double want=(1001.0/1002.0)*(501.0/1002.0);
        fpt_check((int)top==g84&&fabs(t.gate[g84]-want)<1e-12,"8-4 section 4 is not the top gate");
        teacher_priorities(t.groups,now,t.params,&t.gate);double with_gate=t.groups[g84].prob;
        TeacherParams off=t.params;off.weight_gate=0;teacher_priorities(t.groups,now,off,&t.gate);double without=t.groups[g84].prob;
        fpt_check(with_gate>=10*without,"gate did not raise the 8-4 pipe group tenfold");
        printf("gate: 8-4 a3fc:4 top gate %.6f, prob %.6f with weight_gate=0.3 vs %.6f with 0 (x%.1f)\n",t.gate[g84],with_gate,without,with_gate/without);
        t.groups=saved;smb_teacher_gates(t);teacher_priorities(t.groups,0,t.params,&t.gate);teacher_cdf(t.groups,&t.cdf);
    }
    // 7. A .teacher sidecar written before this change (round-1 test_runtime, same layout) loads and refreshes with
    // the gate on.
    {
        const char* old="build/mario_sim/fpg/teacher_pre_exit.bin";
        if(!std::filesystem::exists(std::string(old)+".teacher"))printf("SKIP pre-change sidecar: %s.teacher not found\n",old);
        else {
            auto saved=smb_read_teacher(old,h.practice_bank_hash);size_t matched,recorded=0;
            {std::lock_guard<std::mutex> guard(t.lock);matched=smb_teacher_restore(t,saved);}
            fpt_teacher_refresh_now();double total=0;size_t gated=0;
            for(size_t g=0;g<G;g++){total+=t.groups[g].prob;recorded+=t.groups[g].attempts;gated+=t.gate[g]>0;}
            fpt_check(saved.size()==G&&matched==G&&recorded>0&&fabs(total-1)<1e-9&&t.cdf.back()==1.0f,"pre-change sidecar did not load and refresh");
            printf("pre-change sidecar: %zu groups restored (%zu attempts), refreshed with the gate on (%zu groups with gate > 0)\n",matched,recorded,gated);
        }
    }
    // 8. Real engine: the movie from the earliest route state with a game actor and the teacher on; every exit reward
    // with its place.
    if(!std::filesystem::exists(fpt_route_movie)){printf("SKIP movie exit rewards: %s not found\n",fpt_route_movie);envs.close();puf_ini_free(&ini);return;}
    {
        uint32_t first=UINT32_MAX;
        for(uint32_t k=t.offsets[0];k<t.offsets[G];k++)if((h.runtime_bank[k].flags&SMB_BANK_ROUTE)&&(first==UINT32_MAX||h.runtime_bank[k].source_frame<h.runtime_bank[first].source_frame))first=k;
        fpt_check(first!=UINT32_MAX&&!(h.runtime_bank[first].flags&SMB_BANK_EDITED),"earliest route state missing or edited");
        auto masks=smb_movie_masks(fpt_route_movie,-2);const uint8_t* world=h.bank->worlds.data();
        place(first);gm.game.lives=gm.state->ram[0x75a];
        std::map<int,int> per_stage;int paid=0;float total=0;std::vector<std::string> list;
        size_t f=h.runtime_bank[first].source_frame+1;
        for(;f<masks.size()&&gm.episode.status==SMB_EPISODE_ACTIVE;f++) {
            uint32_t k0=gm.game.last_key;int x0=gm.game.raw_x;uint32_t e4=events[4];
            fpt_check(!smb_native_frame(gm.state,world,masks[f]),"native movie fault");
            float r=smb_after_frame(&gm,gm.state,&h.task,&h.time,&h.curriculum,h.table.data(),&h.game,&h.practice,&t.device);
            if(events[4]!=e4) {
                int stage=(int)(gm.game.last_key>>16);per_stage[stage]++;paid++;total+=r;char line[160];
                snprintf(line,sizeof(line),"%d-%d frame %zu: %04x x=%d -> %04x x=%d (arrival %d)",stage/4+1,stage%4+1,f,k0&0xffff,x0,
                    gm.game.last_key&0xffff,gm.game.raw_x,gm.game.arrival);
                list.push_back(line);
            }
        }
        printf("movie exit rewards (%d, episode status %d at frame %zu, deaths %d):\n",paid,gm.episode.status,f,gm.game.deaths);
        for(auto& l:list)printf("  %s\n",l.c_str());
        std::map<int,int> want={{1,1},{5,1},{13,1},{25,1},{31,4}};   // 1-2, 2-2, 4-2, 7-2 once; 8-4 four times
        fpt_check(per_stage==want&&gm.game.deaths==0,"movie exit rewards differ from 1-2/2-2/4-2/7-2 once and 8-4 four times");
        fpt_check(gm.game.forward_exits==paid,"forward_exits differs from events[4]");
    }
    envs.close();puf_ini_free(&ini);
}

// ---- Dense route states (reports/mario_sim/dense_route_20261111/SPEC.md) ----
static const char* fpt_dense_bank="build/mario_sim/practice_v4/bank.bin";
static Ini fpt_dense_ini(int teacher,const char* bank,double share) {
    Ini ini=fpt_teacher_ini(teacher);auto* game=puf_ini_section(&ini,"game",0);
    dict_set_str(game,"practice_bank",bank);dict_set(game,"practice_dense_share",share);
    dict_set(game,"max_frames",100000);dict_set(game,"practice_max_frames",6000);
    return ini;
}
struct FptGroupSnapshot {
    std::vector<uint64_t> keys;std::vector<uint32_t> area,offsets;std::vector<int32_t> level,section,eligible,reserved;
    std::vector<double> prob;std::vector<float> cdf;
};
static FptGroupSnapshot fpt_snapshot(size_t count) {
    auto& t=fpt_host->teacher;FptGroupSnapshot s;
    for(size_t g=0;g<count;g++) {
        const auto& x=t.groups[g];s.keys.push_back(x.key);s.area.push_back(x.area);s.level.push_back(x.level);
        s.section.push_back(x.section);s.eligible.push_back(x.eligible);s.reserved.push_back(x.reserved);s.prob.push_back(x.prob);
    }
    s.offsets.assign(t.offsets.begin(),t.offsets.begin()+count+1);s.cdf=t.cdf;return s;
}
static void dense_route_tests() {
    if(!std::filesystem::exists(fpt_dense_bank)){printf("SKIP dense route tests: %s not found\n",fpt_dense_bank);return;}
    // 1. The v4 bank: v3's entries unchanged as a prefix, then the dense entries.
    SmbBank v3(fpt_teacher_bank),v4(fpt_dense_bank);size_t n3=v3.entries.size();
    fpt_check(v4.entries.size()>n3&&!memcmp(v4.entries.data(),v3.entries.data(),n3*sizeof(SmbBankEntry)),"v4 does not start with v3's entries");
    std::map<int,std::vector<uint32_t>> frames;size_t dense_entries=0,edited=0;
    for(size_t k=n3;k<v4.entries.size();k++) {
        const auto& e=v4.entries[k];const auto* m=e.scene.initial.ram;
        fpt_check((e.flags&(SMB_BANK_DENSE|SMB_BANK_ROUTE|SMB_BANK_PRACTICE))==(SMB_BANK_DENSE|SMB_BANK_ROUTE|SMB_BANK_PRACTICE)
            &&(e.stage==15||e.stage==27||e.stage==31)&&m[0x75f]*4u+m[0x75c]==e.stage&&smb_game_tracked(m)
            &&!(e.flags&SMB_BANK_ROUTE_LOOP)&&((e.flags>>SMB_PRACTICE_BAND_SHIFT)&255)<SMB_PRACTICE_BANDS,"dense entry flags, stage or tracking wrong");
        auto& list=frames[(int)e.stage];
        fpt_check(list.empty()||(e.source_frame>list.back()&&(e.source_frame-list.back())%4==0),"dense frames not ascending in steps of 4");
        list.push_back(e.source_frame);dense_entries++;edited+=(e.flags&SMB_BANK_EDITED)!=0;
    }
    for(size_t k=0;k<n3;k++)fpt_check(!(v4.entries[k].flags&SMB_BANK_DENSE),"a v3 entry carries the dense flag");
    printf("v4 bank: %zu entries = v3's %zu + %zu dense (%zu small-Mario edits):",v4.entries.size(),n3,dense_entries,edited);
    for(const auto& [stage,list]:frames)printf(" %d-%d=%zu (frames %u..%u)",stage/4+1,stage%4+1,list.size(),list.front(),list.back());
    printf("\n");

    // 2. Groups: v4 with dense_share 0 equals v3; with 0.3 v3's groups come first, then one group per dense entry.
    FptGroupSnapshot base,zero;size_t G3;
    {Ini ini=fpt_dense_ini(1,fpt_teacher_bank,0);FptTeacherEnvs e;e.open(&ini);G3=fpt_host->teacher.groups.size();base=fpt_snapshot(G3);e.close();puf_ini_free(&ini);}
    {Ini ini=fpt_dense_ini(1,fpt_dense_bank,0);FptTeacherEnvs e;e.open(&ini);auto& t=fpt_host->teacher;
     fpt_check(t.groups.size()==G3&&t.regular==G3,"v4 with dense_share 0 has a different group count");zero=fpt_snapshot(G3);
     fpt_check(zero.keys==base.keys&&zero.area==base.area&&zero.level==base.level&&zero.section==base.section&&zero.eligible==base.eligible
         &&zero.reserved==base.reserved&&zero.offsets==base.offsets&&zero.prob==base.prob&&zero.cdf==base.cdf,"v4 with dense_share 0 differs from v3");
     for(size_t k=t.offsets[0];k<t.offsets[G3];k++)fpt_check(!(fpt_host->runtime_bank[k].flags&SMB_BANK_DENSE),"dense entry loaded with dense_share 0");
     e.close();puf_ini_free(&ini);}
    {Ini ini=fpt_dense_ini(0,fpt_dense_bank,0);FptTeacherEnvs e;e.open(&ini);
     size_t practice=0;for(const auto& x:fpt_host->runtime_bank){fpt_check(!(x.flags&SMB_BANK_DENSE),"legacy loader copied a dense entry");practice+=(x.flags&SMB_BANK_PRACTICE)!=0;}
     fpt_check(practice==n3&&!fpt_host->teacher.enabled,"legacy loader did not take exactly v3's entries from v4");
     for(int r=0;r<50;r++)puf_reset(&e.env[2]);
     e.close();puf_ini_free(&ini);}
    printf("groups: v4 with dense_share 0 = v3 (%zu groups, keys, offsets, start-up probabilities); legacy loader takes v3's %zu entries\n",G3,n3);

    Ini ini=fpt_dense_ini(1,fpt_dense_bank,0.3);FptTeacherEnvs envs;envs.open(&ini);
    auto& h=*fpt_host;auto& t=h.teacher;const size_t G=t.groups.size();uint32_t* events=t.counters.data()+4*G;
    {
        auto now=fpt_snapshot(G3);
        fpt_check(now.keys==base.keys&&now.area==base.area&&now.level==base.level&&now.section==base.section&&now.reserved==base.reserved
            &&now.offsets==base.offsets&&t.regular==G3&&G==G3+dense_entries&&G<=4096,"regular groups changed with dense_share 0.3");
        double dense_prob=0,total=0;int prev_stage=-1;uint32_t prev_frame=0;
        for(size_t g=0;g<G;g++) {
            const auto& x=t.groups[g];total+=x.prob;
            fpt_check(t.dense[g]==(g>=G3)&&x.eligible==1&&x.reserved==(g>=G3?1:0),"dense marks wrong");
            if(g<G3)continue;
            const auto& e=h.runtime_bank[t.offsets[g]];const auto* m=e.scene.initial.ram;dense_prob+=x.prob;
            fpt_check(t.offsets[g+1]==t.offsets[g]+1&&(e.flags&SMB_BANK_DENSE)&&x.key==((1ull<<63)|((uint64_t)e.stage<<32)|e.source_frame)
                &&x.area==((e.stage<<16)|(uint32_t)(m[0xe7]+256*m[0xe8]))&&x.level==(int)e.stage&&x.section==std::max(0,smb_game_x(m))/t.group_width
                &&t.dense_frame[g]==e.source_frame,"dense group is not a one-entry group with the specified key");
            fpt_check((int)e.stage>prev_stage||e.source_frame>prev_frame,"dense groups not in (stage, frame) order");
            fpt_check(t.dense_next[g]==(g+1<G&&t.groups[g+1].level==x.level?(int)(g+1):-1),"dense chain wrong");
            prev_stage=(int)e.stage;prev_frame=e.source_frame;
            fpt_check(!t.on_route[g]&&t.next[g]==-1,"a dense group is on the regular chain");
        }
        for(size_t g=0;g<G3;g++)fpt_check(t.next[g]<(int)G3,"a regular group points at a dense group");
        fpt_check(fabs(total-1)<1e-9&&fabs(dense_prob-0.3)<1e-9,"start-up probabilities do not give the dense set 0.3");
        printf("groups: dense_share 0.3 -> %zu regular (= v3) + %zu dense one-entry groups, start-up dense mass %.6f\n",G3,G-G3,dense_prob);
    }

    // 3. Gate with hand-set statistics on a small host.
    {
        SmbTeacherHost x;x.params=t.params;x.params.lp_min_attempts=5;
        x.groups.resize(9);for(auto& g:x.groups){g.eligible=1;g.level=31;}
        // Regular R0 -> R1 (on-route), R2 off-route; dense D0..D5 (groups 3..8) on one chain.
        x.regular=3;x.on_route={1,1,0,0,0,0,0,0,0};x.next={1,-1,-1,-1,-1,-1,-1,-1,-1};
        x.dense={0,0,0,1,1,1,1,1,1};x.dense_next={-1,-1,-1,4,5,6,7,8,-1};
        auto set=[&](int g,uint64_t a,uint64_t w){x.groups[g].attempts=a;x.groups[g].wins=w;};
        set(0,40,10);set(1,30,20);set(2,30,0);
        set(3,10,0);set(4,10,2);set(5,3,3);set(6,10,8);set(7,10,5);set(8,10,1);
        auto pl=[](double w,double a){return (w+1)/(a+2);};
        x.lookahead=3;x.patience=0;
        fpt_check(smb_route_gate(x,3)==teacher_gate(x.groups[3],&x.groups[6],x.params)&&fabs(smb_route_gate(x,3)-(1-pl(0,10))*pl(8,10))<1e-15,
            "lookahead 3 did not pick the best of the next three (ignoring the one under lp_min_attempts)");
        fpt_check(smb_route_gate(x,8)==teacher_gate(x.groups[8],nullptr,x.params)&&fabs(smb_route_gate(x,8)-(1-pl(1,10)))<1e-15,"end of the dense chain is not factor 1");
        x.lookahead=1;fpt_check(smb_route_gate(x,3)==teacher_gate(x.groups[3],&x.groups[4],x.params),"lookahead 1 is not the next group");
        x.lookahead=3;set(4,3,0);set(6,4,4);fpt_check(smb_route_gate(x,3)==0,"candidates under lp_min_attempts were not ignored");
        set(4,10,2);set(6,10,8);
        // Regular groups: today's teacher_gate whatever the lookahead; off-route 0.
        for(int la:{1,3}){x.lookahead=la;fpt_check(smb_route_gate(x,0)==teacher_gate(x.groups[0],&x.groups[1],x.params)
            &&smb_route_gate(x,1)==teacher_gate(x.groups[1],nullptr,x.params)&&smb_route_gate(x,2)==0,"regular gate differs from teacher_gate");}
        // Fade: 0 wins of 2000 attempts with patience 2000 halves the gate; one win restores it.
        set(3,2000,0);x.patience=0;double full=smb_route_gate(x,3);x.patience=2000;double faded=smb_route_gate(x,3);
        fpt_check(full>0&&fabs(faded-full/2)<1e-15,"patience 2000 did not halve a zero-win group of 2000 attempts");
        set(3,2000,1);double won=smb_route_gate(x,3);x.patience=0;
        fpt_check(won==smb_route_gate(x,3),"a win did not restore the full gate");
        printf("gate: lookahead 3 best-of-next-three %.6f, end of chain %.6f, fade %.6f -> %.6f at 0/2000, restored by one win\n",
            teacher_gate(x.groups[4],&x.groups[6],x.params),teacher_gate(x.groups[8],nullptr,x.params),full,faded);
    }
    // 4. Probabilities with hand-set gates: sum 1, dense mass s, regular part (1-s) times the single call on the regular
    // groups, and a dense group with gate 0.9 above one with gate 0.
    {
        SmbTeacherHost x;x.params=t.params;x.dense_share=0.3;x.regular=4;
        x.groups.resize(8);for(size_t g=0;g<8;g++){x.groups[g].eligible=1;x.groups[g].last_selected=100*g;x.groups[g].deaths=(double)(g%3);}
        x.dense={0,0,0,0,1,1,1,1};x.gate={0.4,0.1,0.0,0.2,0.9,0.0,0.3,0.0};
        x.dense_next={-1,-1,-1,-1,5,6,7,-1};smb_dense_segments(x);  // one segment, rank power 1: round 0's mixture
        smb_teacher_probabilities(x,1000);
        std::vector<TeacherGroup> regular(x.groups.begin(),x.groups.begin()+4);std::vector<double> regular_gate(x.gate.begin(),x.gate.begin()+4);
        teacher_priorities(regular,1000,x.params,&regular_gate);
        double total=0,dense=0;for(size_t g=0;g<8;g++){total+=x.groups[g].prob;if(g>=4)dense+=x.groups[g].prob;else fpt_check(fabs(x.groups[g].prob-0.7*regular[g].prob)<1e-15,"regular part is not (1-s) times the single call");}
        // Groups 5 and 7 tie on gate 0; group 4 has gate 0.9 and the same death mass as 7 (deaths do not count in the dense set).
        fpt_check(fabs(total-1)<1e-12&&fabs(dense-0.3)<1e-12&&x.groups[4].prob>x.groups[5].prob&&x.groups[4].prob>x.groups[7].prob
            &&x.groups[0].eligible==1&&x.groups[4].eligible==1,"dense mixture wrong");
        printf("probabilities: sum %.15f, dense %.15f, regular = 0.7 x single call, dense gate 0.9 -> %.4f vs gate 0 -> %.4f\n",total,dense,x.groups[4].prob,x.groups[5].prob);
    }
    // 5. Sidecars: round trip with dense groups; a v3 sidecar against the v4 bank.
    {
        const char* path="build/mario_sim/fpg/teacher_dense_test.bin";{std::ofstream out(path);out<<"dense weights";}
        std::filesystem::remove(std::string(path)+".teacher");
        for(size_t g=0;g<G;g+=7){teacher_record(t.groups[g],3,1+g%2,t.params);t.groups[g].deaths=1;}
        fpt_cpu_save(path,nullptr);auto stored=smb_read_teacher(path,h.practice_bank_hash);
        fpt_check(stored.size()==G&&!memcmp(stored.data(),t.groups.data(),G*sizeof(TeacherGroup)),"dense sidecar round trip failed");
        std::vector<TeacherGroup> reference=t.groups;for(auto& g:t.groups){g.attempts=g.wins=0;g.fast=g.slow=g.deaths=0;}
        {std::lock_guard<std::mutex> guard(t.lock);fpt_check(smb_teacher_restore(t,stored)==G,"dense sidecar did not restore every group");}
        for(size_t g=0;g<G;g++)fpt_check(t.groups[g].attempts==reference[g].attempts&&t.groups[g].wins==reference[g].wins,"dense statistics did not restore");
        const char* old="build/mario_sim/fpg/teacher_pre_exit.bin";
        if(!std::filesystem::exists(std::string(old)+".teacher"))printf("SKIP v3 sidecar: %s.teacher not found\n",old);
        else {
            std::string why;try{smb_read_teacher(old,h.practice_bank_hash);}catch(const std::exception& e){why=e.what();}
            fpt_check(!why.empty(),"a v3 sidecar was accepted with the v4 bank");
            std::string load_why;try{fpt_cpu_load(old,&ini);}catch(const std::exception& e){load_why=e.what();}
            dict_set(&smb_game_options,"practice_resume",0);bool resumed=true;try{fpt_cpu_load(old,&ini);}catch(const std::exception&){resumed=false;}
            dict_set(&smb_game_options,"practice_resume",1);
            fpt_check(!load_why.empty()&&resumed,"v3 sidecar handling differs from refuse / skip with practice_resume = 0");
            printf("sidecars: dense round trip %zu groups; v3 sidecar with v4 bank refused (\"%s\"; load hook: \"%s\"); practice_resume=0 skips it\n",
                G,why.c_str(),load_why.c_str());
        }
    }
    // 6. Real engine along the movie: the 8-4 pipe entry and the 4-4 platform step-off from dense states.
    if(!std::filesystem::exists(fpt_route_movie)){printf("SKIP dense movie checks: %s not found\n",fpt_route_movie);envs.close();puf_ini_free(&ini);return;}
    {
        auto masks=smb_movie_masks(fpt_route_movie,-2);const uint8_t* world=h.bank->worlds.data();auto& p=envs.env[2];
        auto dense_at=[&](int stage,uint32_t lo,uint32_t hi)->int {
            for(size_t g=G3;g<G;g++)if(t.groups[g].level==stage&&t.dense_frame[g]>=lo&&t.dense_frame[g]<=hi)return (int)g;
            return -1;
        };
        // Unedited movie states at the needed frames: a native replay from the earliest route state (1-1, unedited).
        std::map<uint32_t,SmbLogic> movie;std::set<uint32_t> wanted;
        uint32_t first=UINT32_MAX;for(uint32_t k=t.offsets[0];k<t.offsets[G3];k++)if((h.runtime_bank[k].flags&SMB_BANK_ROUTE)&&(first==UINT32_MAX||h.runtime_bank[k].source_frame<h.runtime_bank[first].source_frame))first=k;
        int g84=dense_at(31,64989,64992),g44=dense_at(15,31668,31676);
        fpt_check(g84>=0&&g44>=0,"no dense state near the 8-4 pipe entry or in the 4-4 fall");
        wanted.insert(t.dense_frame[g84]);wanted.insert(t.dense_frame[g44]);
        {SmbLogic s;fpt_check(!smb_scene_reset(&s,&h.runtime_bank[first].scene),"replay reset fault");
         for(size_t f=h.runtime_bank[first].source_frame+1;f<=*wanted.rbegin();f++){fpt_check(!smb_native_frame(&s,world,masks[f]),"replay fault");if(wanted.count((uint32_t)f))movie[(uint32_t)f]=s;}}
        struct Outcome{int decided,won,frames,exits,pipe;float reward;int x;uint32_t area;};
        // Run a practice attempt from dense group g (edited bank state, or the unedited movie state) with inputs(f, k).
        auto attempt=[&](int g,bool unedited,int limit,auto&& inputs)->Outcome {
            const auto& e=h.runtime_bank[t.offsets[g]];std::vector<SmbBankEntry> one(1,e);if(unedited)one[0].scene.initial=movie.at(e.source_frame);
            p.episode.scene=0;fpt_check(!smb_practice_load(p.state,&p.episode,&p.game,(int)e.stage,&h.game,one.data()),"dense load fault");
            p.episode.scene=t.offsets[g];p.practice.stage=(int)e.stage;const auto* m=p.state->ram;
            p.teacher_group=g;p.teacher_pending=1;p.teacher_frames=0;p.teacher_x0=fpt_x(m,0);p.teacher_area=fpt_area_of(m);
            p.teacher_track_deaths=0;p.teacher_route_loops=p.teacher_loop_land=0;
            uint32_t a=t.counters[G+g],w=t.counters[2*G+g],e4=events[4];Outcome o={};
            for(int k=0;k<limit&&p.teacher_pending&&p.episode.status==SMB_EPISODE_ACTIVE;k++) {
                fpt_check(!smb_native_frame(p.state,world,inputs(e.source_frame+1+k,k)),"dense attempt fault");
                o.pipe|=m[0xe]==3;
                o.reward+=smb_after_frame(&p,p.state,&h.task,&h.time,&h.curriculum,h.table.data(),&h.game,&h.practice,&t.device);o.frames=k+1;
            }
            o.decided=t.counters[G+g]==a+1;o.won=(int)(t.counters[2*G+g]-w);o.exits=(int)(events[4]-e4);o.x=smb_game_x(m);o.area=fpt_area_of(m);
            return o;
        };
        auto movie_inputs=[&](uint32_t f,int){return masks[f];};
        auto show=[&](const char* name,int g,const char* start,const Outcome& o) {
            printf("  %s: dense frame %u (%s) -> %s after %d frames, exits %d, reward %.3f, pipe entered %d, at %06x x=%d\n",name,t.dense_frame[g],start,
                !o.decided?"UNDECIDED":o.won?"WIN":"LOSS",o.frames,o.exits,o.reward,o.pipe,o.area,o.x);
        };
        printf("dense real-engine attempts (8-4 pipe entry at frame 64993; nearest dense frame %u):\n",t.dense_frame[g84]);
        auto o=attempt(g84,false,400,movie_inputs);show("8-4 movie inputs",g84,"small-Mario edit",o);
        bool unedited84=!(o.decided&&o.won&&o.exits==1);
        if(unedited84){o=attempt(g84,true,400,movie_inputs);show("8-4 movie inputs",g84,"unedited movie state",o);}
        fpt_check(o.decided&&o.won&&o.exits==1&&o.frames<=200,"8-4 dense state before the pipe did not win with one exit reward");
        o=attempt(g44,false,2400,movie_inputs);show("4-4 movie inputs",g44,"small-Mario edit",o);
        bool unedited44=!(o.decided&&o.won);
        if(unedited44){o=attempt(g44,true,2400,movie_inputs);show("4-4 movie inputs",g44,"unedited movie state",o);}
        fpt_check(o.decided&&o.won&&o.exits==0,"4-4 dense state in the fall did not win by distance");
        // Report only: the edited (small-Mario) states with trivial scripted inputs.
        printf("report only (small-Mario edits, scripted inputs):\n");
        for(uint32_t lo:{64989u,64985u}) {
            int g=dense_at(31,lo,lo);if(g<0)continue;
            o=attempt(g,false,600,[](uint32_t,int k){return k<30?0x20:0;});show("8-4 hold Down 30 frames, then nothing",g,"small-Mario edit",o);
        }
        for(size_t g=G3;g<G;g++)if(t.groups[g].level==15&&t.dense_frame[g]>=31660&&t.dense_frame[g]<=31680) {
            o=attempt((int)g,false,2400,[](uint32_t,int){return 0x82;});show("4-4 hold Right+B",(int)g,"small-Mario edit",o);
        }
        // The training config wins on 1100 px, not the 512 this test pins: the 4-4 cases again at 1100 (report only).
        int pinned=t.device.success_distance;t.device.success_distance=1100;
        printf("report only at practice_success_distance=1100 (training config):\n");
        o=attempt(g44,false,2400,movie_inputs);show("4-4 movie inputs",g44,"small-Mario edit",o);
        for(size_t g=G3;g<G;g++)if(t.groups[g].level==15&&t.dense_frame[g]>=31660&&t.dense_frame[g]<=31680) {
            o=attempt((int)g,false,2400,[](uint32_t,int){return 0x82;});show("4-4 hold Right+B",(int)g,"small-Mario edit",o);
        }
        t.device.success_distance=pinned;
    }
    envs.close();puf_ini_free(&ini);
}

// ---- Review round 1: dense chains end at a frame gap; budget per segment (SPEC.md, "Review round 1") ----
static void dense_segment_tests() {
    if(!std::filesystem::exists(fpt_dense_bank)){printf("SKIP dense segment tests: %s not found\n",fpt_dense_bank);return;}
    // 1. Gap. Round 0's chains with gap 0: consecutive dense groups of a stage, three segments (one per stage).
    std::vector<int32_t> chain0;
    {
        Ini ini=fpt_dense_ini(1,fpt_dense_bank,0.3);FptTeacherEnvs e;e.open(&ini);auto& t=fpt_host->teacher;size_t G=t.groups.size();
        for(size_t g=t.regular;g<G;g++)fpt_check(t.dense_next[g]==(g+1<G&&t.groups[g+1].level==t.groups[g].level?(int)(g+1):-1),"gap 0 chain differs from round 0's");
        fpt_check(t.segments==3,"gap 0 does not give one segment per dense stage");
        chain0=t.dense_next;e.close();puf_ini_free(&ini);
    }
    Ini ini=fpt_dense_ini(1,fpt_dense_bank,0.15);auto* game=puf_ini_section(&ini,"game",0);
    dict_set(game,"practice_dense_gap",32);dict_set(game,"practice_dense_rank_power",0.5);
    dict_set(game,"practice_gate_lookahead",3);dict_set(game,"practice_gate_patience",5000);
    FptTeacherEnvs envs;envs.open(&ini);auto& t=fpt_host->teacher;const size_t G=t.groups.size();
    std::map<int,int> per_stage;
    printf("dense segments at gap 32: %d\n",t.segments);
    for(size_t g=t.regular;g<G;) {
        size_t e=g;while(e+1<G&&t.dense_segment[e+1]==t.dense_segment[g])e++;
        printf("  segment %d: %d-%d frames %u..%u, %zu groups\n",t.dense_segment[g],t.groups[g].level/4+1,t.groups[g].level%4+1,t.dense_frame[g],t.dense_frame[e],e-g+1);
        per_stage[t.groups[g].level]++;g=e+1;
    }
    for(size_t g=t.regular;g<G;g++) {
        bool same=g+1<G&&t.groups[g+1].level==t.groups[g].level;uint32_t step=same?t.dense_frame[g+1]-t.dense_frame[g]:0;
        fpt_check(t.dense_next[g]==(same&&step<=32?(int)(g+1):-1),"gap 32 chain link wrong");
        if(same&&step>4)printf("  gap in %d-%d: frames %u -> %u (%u frames)%s\n",t.groups[g].level/4+1,t.groups[g].level%4+1,
            t.dense_frame[g],t.dense_frame[g+1],step,step>32?", chain ends":"");
    }
    fpt_check(per_stage[31]==5,"8-4 does not split into 5 segments at gap 32");
    int pipe1=-1;
    for(uint32_t pipe:{64993u,65430u,65795u,66661u}) {
        int last=-1;for(size_t g=t.regular;g<G;g++)if(t.groups[g].level==31&&t.dense_frame[g]<pipe)last=(int)g;
        fpt_check(last>=0&&t.dense_next[last]==-1,"the last dense group before an 8-4 pipe still links into the next room");
        if(pipe==64993u)pipe1=last;
        printf("  8-4 transition at movie frame %u: last dense group frame %u ends its chain\n",pipe,t.dense_frame[last]);
    }
    printf("  segments per stage: 4-4=%d 7-4=%d 8-4=%d\n",per_stage[15],per_stage[27],per_stage[31]);

    // 2. Gate at the end of the chain before pipe 1: 0 wins of 100 -> teacher_gate(g, nullptr) * 5000/5100, not 0.
    {
        auto set=[&](int g,uint64_t a,uint64_t w){t.groups[g].attempts=a;t.groups[g].wins=w;};
        int g1=pipe1,g0=pipe1-1,gm=pipe1-2;
        fpt_check(t.dense_next[g0]==g1&&t.dense_next[gm]==g0,"the groups before pipe 1 are not chained");
        set(g1,100,0);set(g0,10,0);set(gm,10,0);
        double expect1=(1-1.0/102)*5000.0/5100;
        fpt_check(smb_route_gate(t,g1)==teacher_gate(t.groups[g1],nullptr,t.params)*(5000.0/5100)&&fabs(smb_route_gate(t,g1)-expect1)<1e-15,
            "end-of-chain gate before pipe 1 wrong");
        fpt_check(smb_route_gate(t,g0)==teacher_gate(t.groups[g0],&t.groups[g1],t.params)*(5000.0/5010),"gate of the group before the chain end wrong");
        // Two back: candidates g0 (p 1/12) and g1 (p 1/102), the lookahead stops at the chain end; best = g0.
        fpt_check(smb_route_gate(t,gm)==teacher_gate(t.groups[gm],&t.groups[g0],t.params)*(5000.0/5010),"lookahead before the chain end wrong");
        printf("gate before 8-4 pipe 1 (frame %u, 0/100): %.9f = (1-1/102)*5000/5100; frame %u (0/10): %.9f; frame %u: %.9f\n",
            t.dense_frame[g1],smb_route_gate(t,g1),t.dense_frame[g0],smb_route_gate(t,g0),t.dense_frame[gm],smb_route_gate(t,gm));
        set(g1,0,0);set(g0,0,0);set(gm,0,0);
    }
    // Start-up mass per segment on the real bank.
    {
        std::vector<double> mass(t.segments,0);double dense=0,total=0;
        for(size_t g=0;g<G;g++){total+=t.groups[g].prob;if(t.dense[g]){mass[t.dense_segment[g]]+=t.groups[g].prob;dense+=t.groups[g].prob;}}
        for(double m:mass)fpt_check(fabs(m-0.15/t.segments)<1e-12,"start-up segment mass is not s/K");
        fpt_check(fabs(total-1)<1e-12&&fabs(dense-0.15)<1e-12,"start-up dense mass is not 0.15");
        printf("start-up: dense mass %.12f, each of %d segments %.12f\n",dense,t.segments,mass[0]);
    }
    envs.close();puf_ini_free(&ini);

    // 3. Budget with hand-set statistics: three regular groups, dense segments A (3 groups), B (2), C (3).
    {
        SmbTeacherHost x;x.params=TeacherParams{};x.params.weight_gate=0.3;teacher_validate(x.params);
        const size_t n=11;x.regular=3;x.dense_share=0.15;x.dense_rank_power=0.5;x.groups.resize(n);
        x.dense={0,0,0,1,1,1,1,1,1,1,1};x.dense_next={-1,-1,-1,4,5,-1,7,-1,9,10,-1};smb_dense_segments(x);
        fpt_check(x.segments==3,"hand-built segments wrong");
        x.gate={0.2,0.0,0.4, 0.3,0.1,0.0, 0.9,0.5, 0.0,0.6,0.2};
        for(size_t g=0;g<n;g++){x.groups[g].eligible=1;x.groups[g].deaths=(double)(g%4);x.groups[g].last_selected=50*g;
            x.groups[g].attempts=10+g;x.groups[g].wins=g%5;x.groups[g].fast=0.1*(g%3);x.groups[g].slow=0.05*(g%2);}
        // Segment B: lp and staleness tied (no attempts, same last selection), only the gate differs.
        for(size_t g:{6,7}){x.groups[g].attempts=x.groups[g].wins=0;x.groups[g].fast=x.groups[g].slow=0;x.groups[g].last_selected=500;}
        smb_teacher_probabilities(x,1000);
        double total=0,dense=0,seg[3]={0,0,0};
        for(size_t g=0;g<n;g++){total+=x.groups[g].prob;if(x.dense[g]){dense+=x.groups[g].prob;seg[x.dense_segment[g]]+=x.groups[g].prob;}}
        fpt_check(fabs(total-1)<1e-12&&fabs(dense-0.15)<1e-12,"budget: sum or dense sum wrong");
        for(double m:seg)fpt_check(fabs(m-0.05)<1e-12,"budget: a segment does not get s/K");
        // y = (prob K/s - floor/n_B)/(1 - floor) = (0.2 rank_lp + 0.1 rank_stale + 0.7 rank_gate)/D with lp and stale
        // tied at rank 1, so y2/y1 = 0.3 + 0.7 r2 and the top two gate-only priorities have the ratio 1/r2.
        double y1=(x.groups[6].prob*3/0.15-0.05/2)/0.95,y2=(x.groups[7].prob*3/0.15-0.05/2)/0.95,r2=(y2/y1-0.3)/0.7;
        fpt_check(fabs(1/r2-sqrt(2.0))<1e-12,"rank power 0.5 does not give the top two gate groups a ratio of sqrt(2)");
        printf("budget: sum %.15f, dense %.15f, segments %.15f %.15f %.15f, top-two gate-only ratio %.15f (sqrt 2 = %.15f)\n",
            total,dense,seg[0],seg[1],seg[2],1/r2,sqrt(2.0));
        // One segment with rank power 1 equals round 0's mixture.
        SmbTeacherHost y;y.params=x.params;y.groups=x.groups;y.gate=x.gate;y.dense=x.dense;y.regular=x.regular;y.dense_share=x.dense_share;y.dense_rank_power=1;y.dense_next={-1,-1,-1,4,5,6,7,8,9,10,-1};smb_dense_segments(y);
        fpt_check(y.segments==1,"one-segment host wrong");
        smb_teacher_probabilities(y,1000);
        std::vector<TeacherGroup> a=x.groups,b=x.groups;
        for(size_t g=0;g<n;g++)a[g].eligible=!x.dense[g];teacher_priorities(a,1000,x.params,&x.gate);
        TeacherParams q=x.params;q.weight_deaths=0;q.weight_lp=0.2;q.weight_stale=0.1;q.weight_gate=0.7;q.floor=0.05;
        for(size_t g=0;g<n;g++)b[g].eligible=x.dense[g];teacher_priorities(b,1000,q,&x.gate);
        for(size_t g=0;g<n;g++)fpt_check(y.groups[g].prob==0.85*a[g].prob+0.15*b[g].prob,"one segment with rank power 1 differs from round 0");
        printf("budget: one segment with rank power 1 equals round 0's mixture bit for bit\n");
    }
}

int main() {
    try {
        Ini ini=fpt_teacher_ini(1);FptTeacherEnvs envs;envs.open(&ini);
        auto& h=*fpt_host;auto& t=h.teacher;size_t G=t.groups.size();const int width=t.group_width,U=t.params.upstream_groups;
        fpt_check(t.enabled&&t.device.enabled&&width==256&&U==6&&t.refresh_steps==256&&t.success_distance==512&&t.attempt_frames==2400,"teacher config not parsed");
        uint32_t* selected=t.counters.data();uint32_t* attempts=selected+G;uint32_t* wins=selected+2*G;
        uint32_t* weights=selected+3*G;uint32_t* events=selected+4*G;

        // 1. Groups: every practice entry in exactly one contiguous group, unique ascending keys.
        SmbBank bank(fpt_teacher_bank);std::set<uint64_t> expected_keys;std::map<int,int> groups_per_stage;
        for(const auto& e:bank.entries)expected_keys.insert(fpt_entry_key(e,width));
        fpt_check(G==expected_keys.size()&&G>0&&G<=4096,"group count differs from the bank's distinct (area, section) keys");
        size_t first=h.runtime_bank.size()-bank.entries.size();
        fpt_check(t.offsets.size()==G+1&&t.offsets[0]==first&&t.offsets[G]==h.runtime_bank.size(),"group offsets do not span the practice entries");
        std::multiset<uint32_t> bank_ids,group_ids;for(const auto& e:bank.entries)bank_ids.insert(e.source_id);
        for(size_t g=0;g<G;g++) {
            const auto& x=t.groups[g];
            fpt_check(t.offsets[g]<t.offsets[g+1],"empty or non-contiguous group");
            fpt_check(g==0||t.groups[g-1].key<x.key,"group keys not unique and ascending");
            fpt_check(x.eligible==1&&x.area==(uint32_t)(x.key>>16)&&x.section==(int)(x.key&0xffff)&&x.level==(int)(x.area>>16)
                &&t.area[g]==x.area&&t.section[g]==x.section,"group identity fields wrong");
            groups_per_stage[x.level]++;
            for(uint32_t k=t.offsets[g];k<t.offsets[g+1];k++) {
                const auto& e=h.runtime_bank[k];const auto* m=e.scene.initial.ram;
                fpt_check(fpt_entry_key(e,width)==x.key&&(int)e.stage==x.level&&(int)(m[0x75f]*4+m[0x75c])==x.level
                    &&(uint32_t)(m[0xe7]+256*m[0xe8])==(x.area&0xffff)&&fpt_x(m,0)/width==x.section,"entry in the wrong group");
                group_ids.insert(e.source_id);
            }
        }
        fpt_check(group_ids==bank_ids,"practice entries not each in exactly one group");
        fpt_check(t.cdf.size()==G&&t.cdf.back()==1.0f,"initial CDF does not end at 1");
        for(const auto& x:t.groups)fpt_check(fabs(x.prob-1.0/G)<1e-12,"first refresh is not uniform");
        printf("groups=%zu entries=%zu per_stage:",G,bank.entries.size());
        for(auto [stage,n]:groups_per_stage)printf(" %d-%d=%d",stage/4+1,stage%4+1,n);printf("\n");

        // 2. Reset sampling. All mass on one group: every reset lands in its entry range and is counted.
        auto& p=envs.env[2];std::vector<float> real=t.cdf;size_t target=G/2;
        for(size_t g=0;g<G;g++)t.cdf[g]=g<target?0.0f:1.0f;
        std::vector<uint32_t> before(selected,selected+G);
        for(int r=0;r<200;r++) {
            puf_reset(&p);const auto& e=p.episode;
            fpt_check(p.teacher_group==(int)target&&e.scene>=t.offsets[target]&&e.scene<t.offsets[target+1],"reset outside the forced group");
            const auto* m=p.state->ram;
            fpt_check(p.teacher_pending==1&&p.teacher_frames==0&&p.teacher_x0==fpt_x(m,0)
                &&p.teacher_area==(((uint32_t)(m[0x75f]*4+m[0x75c])<<16)|(uint32_t)(m[0xe7]+256*m[0xe8]))
                &&p.teacher_area==t.groups[target].area,"attempt record not set from the reset state");
            fpt_check(e.frames==0&&e.status==SMB_EPISODE_ACTIVE&&e.episode_return==0&&e.world==0&&p.practice.stage==t.groups[target].level
                &&p.game.stage==p.practice.stage&&p.game.progress_pixels==0&&!memcmp(m,h.runtime_bank[e.scene].scene.initial.ram,SMB_RAM),
                "teacher reset differs from the practice reset initialisation");
        }
        for(size_t g=0;g<G;g++)fpt_check(selected[g]-before[g]==(g==target?200u:0u),"selection counter wrong");
        // Real CDF from teacher_priorities with hand-set statistics: 20k draws match prob within 1%.
        std::vector<TeacherGroup> saved_groups=t.groups;
        for(size_t g=0;g<G;g++){auto& x=t.groups[g];x.deaths=(double)(g%7);x.death_stamp=0;x.last_selected=(g%5)*1000;
            for(size_t k=0;k<g%4;k++)teacher_record(x,8,(uint32_t)(g%3),t.params);}
        teacher_priorities(t.groups,1000000,t.params);teacher_cdf(t.groups,&t.cdf);
        fpt_check(t.cdf.back()==1.0f,"hand-set CDF does not end at 1");
        std::vector<int> draws(G,0);before.assign(selected,selected+G);const int N=20000;
        for(int r=0;r<N;r++){puf_reset(&p);draws[p.teacher_group]++;}
        double worst=0,pmin=1,pmax=0;
        for(size_t g=0;g<G;g++) {
            worst=std::max(worst,fabs(draws[g]/(double)N-t.groups[g].prob));pmin=std::min(pmin,t.groups[g].prob);pmax=std::max(pmax,t.groups[g].prob);
            fpt_check(selected[g]-before[g]==(uint32_t)draws[g],"selection counter differs from draws");
        }
        fpt_check(worst<=0.01&&pmax>2*pmin,"sampled frequencies do not match prob within 1%");
        printf("sampling: 20000 draws, max |freq-prob|=%.5f, prob range %.5f..%.5f\n",worst,pmin,pmax);
        t.groups=saved_groups;t.cdf=real;

        // 3. Attempt outcomes: one record each.
        auto outcome=[&](const char* why,int expect_win,auto&& setup) {
            puf_reset(&p);int g=p.teacher_group;uint32_t a=attempts[g],w=wins[g];setup();fpt_after(p);
            fpt_check(attempts[g]==a+1&&wins[g]==w+(uint32_t)expect_win&&!p.teacher_pending,why);
            if(p.episode.status==SMB_EPISODE_ACTIVE){fpt_after(p);fpt_check(attempts[g]==a+1,"attempt recorded twice");}
        };
        outcome("same-area progress did not record a win",1,[&]{p.teacher_x0=fpt_x(p.state->ram,0)-600;});
        outcome("first death did not record a loss",0,[&]{p.state->ram[0xe]=11;});
        fpt_check(p.episode.status==SMB_EPISODE_DEAD,"practice continued after first death");
        outcome("frame cap did not record a loss",0,[&]{p.teacher_frames=t.attempt_frames-1;});
        fpt_check(p.episode.status==SMB_EPISODE_ACTIVE,"attempt cap changed the episode");
        {puf_reset(&p);int g=p.teacher_group;uint32_t a=attempts[g];fpt_after(p);
         fpt_check(attempts[g]==a&&p.teacher_pending&&p.teacher_frames==1,"an undecided frame recorded an outcome");}

        // 4. Failure crediting: a game actor's life loss credits the groups of its area with U+1-d.
        auto& game=envs.env[0];size_t credited_group=0;
        for(size_t g=0;g<G;g++)if(t.groups[g].section>t.groups[credited_group].section)credited_group=g;
        uint32_t key=t.groups[credited_group].area;int x=t.groups[credited_group].section*width+17,section=x/width;
        std::vector<uint32_t> snapshot(t.counters);
        game.episode.status=SMB_EPISODE_ACTIVE;game.game.last_key=key;game.game.last_x=x;game.game.raw_x=x;game.game.tracking=0;
        game.state->ram[0x75a]=1;game.game.lives=2;int deaths=game.game.deaths;
        fpt_after(game);fpt_check(game.game.deaths==deaths+1,"life loss not detected");
        int nonzero=0;
        for(size_t g=0;g<G;g++) {
            int d=section-t.groups[g].section;uint32_t w=t.groups[g].area==key&&d>=0&&d<=U?(uint32_t)(U+1-d):0;nonzero+=w>0;
            fpt_check(weights[g]-snapshot[3*G+g]==w,"death weight differs from the integer kernel");
            fpt_check(selected[g]==snapshot[g]&&attempts[g]==snapshot[G+g]&&wins[g]==snapshot[2*G+g],"crediting touched other counters");
        }
        fpt_check(events[0]==snapshot[4*G]+1&&nonzero>=1,"death event not counted");
        // Practice and FPG actors never credit failures; neither does a death without a tracked area.
        snapshot=t.counters;
        for(Env* e:{&p,&envs.env[3]}){e->game.last_key=key;e->game.last_x=x;e->game.raw_x=x;e->state->ram[0x75a]=1;e->game.lives=2;e->episode.status=SMB_EPISODE_ACTIVE;fpt_after(*e);}
        game.game.last_key=0;game.state->ram[0x75a]=0;game.game.lives=1;game.episode.status=SMB_EPISODE_ACTIVE;fpt_after(game);
        for(size_t g=0;g<G;g++)fpt_check(weights[g]==snapshot[3*G+g],"non-game actor or untracked death credited a group");
        fpt_check(events[0]==snapshot[4*G],"non-game actor counted a death event");
        printf("crediting: section=%d of area %08x credited %d groups\n",section,key,nonzero);

        // 5. Host refresh: deltas -> EMAs, death mass, staleness, probabilities, CDF.
        t.agent_steps=12345678;std::vector<TeacherGroup> expect=t.groups;std::vector<uint32_t> now=t.counters;
        for(size_t g=0;g<G;g++) {
            uint32_t s=now[g]-t.previous[g],a=now[G+g]-t.previous[G+g],w=now[2*G+g]-t.previous[2*G+g],d=now[3*G+g]-t.previous[3*G+g];
            if(a)teacher_record(expect[g],a,w,t.params);
            if(d)teacher_add_mass(expect[g],d,t.agent_steps,t.params);
            if(s)expect[g].last_selected=t.agent_steps;
        }
        // Gate values recomputed from the route order (teacher_gate of each on-route group and its next).
        std::vector<double> expect_gate(G,0.0);
        for(size_t g=0;g<G;g++)if(t.on_route[g])expect_gate[g]=teacher_gate(expect[g],t.next[g]>=0?&expect[t.next[g]]:nullptr,t.params);
        teacher_priorities(expect,t.agent_steps,t.params,&expect_gate);std::vector<float> expect_cdf;teacher_cdf(expect,&expect_cdf);
        uint64_t refreshes=t.refreshes;fpt_teacher_refresh_now();
        fpt_check(t.refreshes==refreshes+1&&t.previous==t.counters,"refresh did not take the counter snapshot");
        double total=0;
        for(size_t g=0;g<G;g++) {
            const auto& a=t.groups[g];const auto& b=expect[g];total+=a.prob;
            fpt_check(a.fast==b.fast&&a.slow==b.slow&&a.attempts==b.attempts&&a.wins==b.wins&&a.deaths==b.deaths
                &&a.death_stamp==b.death_stamp&&a.last_selected==b.last_selected&&a.prob==b.prob,"refresh arithmetic differs");
        }
        fpt_check(t.cdf==expect_cdf&&t.cdf.back()==1.0f&&fabs(total-1)<1e-9,"refresh CDF wrong");
        double lowest=1;for(const auto& g:t.groups)lowest=std::min(lowest,g.prob);
        fpt_check(t.groups[credited_group].deaths>0&&t.groups[credited_group].prob>lowest,"death mass did not raise a group");
        uint64_t recorded=0;for(const auto& g:t.groups)recorded+=g.attempts;
        fpt_check(recorded==4&&t.death_events==1,"refresh lost attempts or death events");
        // Cadence: every refresh_steps calls per CPU env, checked after a stepped buffer.
        t.refresh_steps=2;refreshes=t.refreshes;uint64_t steps=t.agent_steps;
        for(int round=0;round<8;round++){for(auto& e:envs.env)puf_step(&e);fpt_cpu_post_step(envs.env,4);}
        fpt_check(t.refreshes==refreshes+4&&t.agent_steps==steps+32,"CPU refresh cadence is not per actor");
        t.refresh_steps=256;
        Dict metrics={};Log log={};log.n=1;puf_log(&log,&metrics);
        fpt_check(dict_get(&metrics,"teacher_groups")==G&&dict_find(&metrics,"teacher_entropy")&&dict_find(&metrics,"teacher_top_prob")
            &&dict_find(&metrics,"teacher_loop_events")&&dict_find(&metrics,"teacher_wrong_exit_events")&&dict_find(&metrics,"game_maze_loops")
            &&dict_get(&metrics,"teacher_route_pairs")==(double)t.pair_from.size()&&dict_find(&metrics,"teacher_forward_exits")
            &&dict_find(&metrics,"game_exit_reward")&&dict_find(&metrics,"teacher_top_gate_stage")&&dict_find(&metrics,"teacher_top_gate_section")
            &&dict_find(&metrics,"teacher_top_gate_value")&&dict_find(&metrics,"teacher_gate_share"),"teacher metrics missing");
        // teacher_W_L_share covers exactly the stages present in the bank (all 32 in v3).
        for(int s=0;s<32;s++) {
            bool present=false;for(const auto& e:bank.entries)present|=(int)e.stage==s;
            char k[64];snprintf(k,sizeof(k),"teacher_%d_%d_share",s/4+1,s%4+1);
            fpt_check(present==(dict_find(&metrics,k)!=nullptr),"teacher stage share keys differ from the bank's stages");
        }
        double share=0;for(int i=0;i<h.practice.stage_count;i++){int s=h.practice.stages[i];char k[64];snprintf(k,sizeof(k),"teacher_%d_%d_share",s/4+1,s%4+1);share+=dict_get(&metrics,k);}
        fpt_check(fabs(share-1)<1e-6&&dict_get(&metrics,"sweep_score")==0,"stage shares do not sum to one");dict_clear(&metrics);

        // 6. Sidecars: .teacher round trip and rejection; .practice stays the format with the teacher off.
        const char* checkpoint="build/mario_sim/fpg/teacher_test.bin";{std::ofstream out(checkpoint);out<<"teacher weights";}
        for(const char* suffix:{".teacher",".practice"})std::filesystem::remove(std::string(checkpoint)+suffix);
        fpt_cpu_save(checkpoint,nullptr);
        fpt_check(std::filesystem::exists(std::string(checkpoint)+".teacher")&&!std::filesystem::exists(std::string(checkpoint)+".practice"),"teacher run wrote the wrong sidecar");
        auto stored=smb_read_teacher(checkpoint,h.practice_bank_hash);
        fpt_check(stored.size()==G&&!memcmp(stored.data(),t.groups.data(),G*sizeof(TeacherGroup)),"teacher sidecar round trip failed");
        std::vector<TeacherGroup> reference=t.groups;
        for(auto& g:t.groups){g.fast=g.slow=g.deaths=0;g.attempts=g.wins=0;g.death_stamp=g.last_selected=777;}
        fpt_cpu_load(checkpoint,&ini);
        for(size_t g=0;g<G;g++) {
            const auto& a=t.groups[g];const auto& b=reference[g];
            fpt_check(a.fast==b.fast&&a.slow==b.slow&&a.attempts==b.attempts&&a.wins==b.wins&&a.deaths==b.deaths
                &&a.death_stamp==0&&a.last_selected==0,"teacher statistics did not restore");
        }
        fpt_check(t.cdf.back()==1.0f,"resume did not refresh the CDF");
        bool rejected=false;try{smb_read_teacher(checkpoint,h.practice_bank_hash^1);}catch(const std::exception&){rejected=true;}
        fpt_check(rejected,"teacher sidecar accepted a different bank");
        rejected=false;{std::ofstream out(checkpoint);out<<"other weights";}
        try{smb_read_teacher(checkpoint,h.practice_bank_hash);}catch(const std::exception&){rejected=true;}
        fpt_check(rejected,"teacher sidecar accepted different weights");
        envs.close();puf_ini_free(&ini);

        maze_tests();
        exit_gate_tests();
        dense_route_tests();
        dense_segment_tests();

        // Invalid teacher parameters are rejected.
        Ini bad=fpt_teacher_ini(1);dict_set(puf_ini_section(&bad,"game",0),"practice_lp_slow",.2);
        rejected=false;try{FptTeacherEnvs e;e.open(&bad);e.close();}catch(const std::exception&){rejected=true;}
        fpt_check(rejected,"lp_slow above lp_fast accepted");fpt_host.reset();fpt_references=0;fpt_cpu_envs.clear();puf_ini_free(&bad);

        // practice_teacher = 0 keeps the band machine and the .practice sidecar.
        Ini legacy=fpt_teacher_ini(0);FptTeacherEnvs old;old.open(&legacy);
        fpt_check(!fpt_host->teacher.enabled&&!fpt_host->teacher.device.enabled&&fpt_host->teacher.groups.empty(),"teacher enabled by default");
        {std::ofstream out(checkpoint);out<<"legacy weights";}
        for(const char* suffix:{".teacher",".practice"})std::filesystem::remove(std::string(checkpoint)+suffix);
        old.env[2].practice.level[14]=2;fpt_cpu_save(checkpoint,nullptr);
        fpt_check(std::filesystem::exists(std::string(checkpoint)+".practice")&&!std::filesystem::exists(std::string(checkpoint)+".teacher"),"legacy run wrote the wrong sidecar");
        auto practice=smb_read_practice(checkpoint,fpt_host->practice_bank_hash);
        fpt_check(practice.size()==1&&practice[0].level[14]==2,"legacy practice sidecar round trip failed");
        for(int r=0;r<20;r++){puf_reset(&old.env[2]);fpt_check(!old.env[2].teacher_pending,"legacy reset set a teacher attempt");}
        old.close();
        fpt_configure(&legacy,"eval",nullptr);fpt_setup(puf_ini_section(&legacy,"env",0));
        fpt_check(fpt_host->practice.fraction==0&&!fpt_host->teacher.enabled,"evaluation used practice starts");
        fpt_host.reset();puf_ini_free(&legacy);
        puts("PASS teacher groups, reset sampling, attempt outcomes, failure crediting, refresh and cadence, metrics, sidecars and legacy mode");
        return 0;
    }catch(const std::exception& e){fprintf(stderr,"teacher test: %s\n",e.what());return 1;}
}
