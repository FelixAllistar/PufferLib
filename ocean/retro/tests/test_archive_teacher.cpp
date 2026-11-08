// Curriculum teacher inside the retro reset archive (2026-11-07, reports/retro/teacher_20261107/SPEC.md): variants,
// group identity, eviction rules, death chain and failure crediting, attempt outcomes, teacher selection and the v2
// archive format. Uses the same ROM fixture as test_archive.cpp; never writes ROM RAM.
#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#include <set>
#include <unistd.h>
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static bool near(double a,double b,double tol=1e-12){return std::fabs(a-b)<=tol;}
static std::vector<char> core(const Nes_State& s){Mem_Writer w;retro_check(s.write(w));return {w.data(),w.data()+w.size()};}
static void clear_bank(){
    auto& b=retro_archive_bank();
    b.cells.clear();b.index.clear();b.groups.clear();b.group_index.clear();b.cdf.clear();b.cdf_steps=0;b.cdf_eligible=0;
    b.snapshots=0;b.death_events=b.chain_deposits=b.attempt_wins=b.attempt_losses=0;b.capacity=100000;
}
static int add_group(unsigned area,int section,double prob){
    auto& b=retro_archive_bank();int g=retro_archive_group(b,area,section);b.groups[g].prob=prob;return g;
}
static void add_cell(uint64_t key,int group,int states,std::shared_ptr<RetroArchiveSnapshot> s){
    auto& b=retro_archive_bank();RetroArchiveCell c;c.key=key;c.level=b.groups[group].level;c.group=group;
    for(int i=0;i<states;i++)c.states.push_back(s);
    retro_archive_sync(c);b.index[key]=b.cells.size();b.cells.push_back(c);b.snapshots+=states;
}
static RetroArchiveCell* cell(uint64_t key){
    auto& b=retro_archive_bank();auto it=b.index.find(key);return it==b.index.end()?nullptr:&b.cells[it->second];
}
static void check_consistent(){
    auto& b=retro_archive_bank();size_t total=0;
    check(b.index.size()==b.cells.size(),"index size");
    for(size_t i=0;i<b.cells.size();i++){check(b.index.at(b.cells[i].key)==i,"index points elsewhere");total+=b.cells[i].states.size();
        check(!b.cells[i].states.empty()&&b.cells[i].state==b.cells[i].states[0],"cell without state or stale mirror");}
    check(total==b.snapshots,"snapshot count is not exact");
}
int main(){try{
    Dict cfg={};dict_set_str(&cfg,"spawn_levels","all");dict_set(&cfg,"frameskip",4);
    dict_set(&cfg,"archive_enabled",1);dict_set(&cfg,"archive_teacher",1);
    dict_set(&cfg,"archive_start",.5);dict_set(&cfg,"archive_end",.5);dict_set(&cfg,"archive_full_game_share",.3);
    dict_set(&cfg,"archive_capacity_mb",64);dict_set(&cfg,"archive_capture_interval",4096);
    dict_set(&cfg,"archive_chain_interval",4);dict_set(&cfg,"archive_chain_depth",32);dict_set(&cfg,"archive_chain_skip",8);
    Env e={};puf_init(&e,&cfg);float obs[OBS_SIZE],action=0,reward=0,done=0;
    e.agents[0]={obs,&action,&reward,&done,nullptr,0};e.spawn_pin=1;e.cur_spawn=0;
    auto& b=retro_archive_bank();auto& a=*e.archive;
    check(b.teacher_enabled&&b.variants==8&&b.chain_interval==4&&a.chain.size()==32&&b.chain_skip==8,"teacher options not applied");
    check(b.capacity==size_t(64.0*1024*1024/(sizeof(RetroArchiveSnapshot)+64)),"teacher capacity is not in snapshots");
    // Validation: start + full share > 1 and archive_practice are rejected; invalid teacher parameters throw.
    for(int bad=0;bad<4;bad++){
        Dict c={};dict_set_str(&c,"spawn_levels","1-1");dict_set(&c,"archive_enabled",1);dict_set(&c,"archive_teacher",1);
        dict_set(&c,"archive_start",.8);dict_set(&c,"archive_end",.1);
        if(bad==0)dict_set(&c,"archive_full_game_share",.3);
        if(bad==1){dict_set(&c,"archive_start",.5);dict_set(&c,"archive_lp_slow",.5);}
        if(bad==2){dict_set(&c,"archive_start",.5);dict_set(&c,"archive_variants",65);}
        if(bad==3){dict_set(&c,"archive_start",.5);dict_set(&c,"archive_group_width",16);}
        Env x={};bool rejected=false;try{puf_init(&x,&c);}catch(const std::exception&){rejected=true;}
        puf_close(&x);dict_clear(&c);check(rejected,"invalid teacher configuration accepted");
    }
    // Restore the fixture's bank options (the rejected inits above may have run up to their checks only).
    check(b.variants==8&&b.teacher_enabled,"rejected configuration changed the bank");

    // Fixture: a playable, rendered 1-1 state reached without RAM modification (mode 0 level start).
    clear_bank();a.lane=.95;puf_reset(&e);check(a.mode==0,"lane .95 must be a level start");
    for(int i=0;i<10;i++)puf_step(&e);
    check(retro_playable_area_state(e.emu->low_mem()),"fixture must be playable");

    // 1. Variants: the same cell three times with archive_variants = 2 holds two states; snapshots stays exact.
    {
        clear_bank();b.variants=2;
        retro_archive_capture(&e);check(b.cells.size()==1&&b.cells[0].states.size()==1&&b.snapshots==1,"first capture");
        retro_archive_capture(&e);check(b.cells.size()==1&&b.cells[0].states.size()==2&&b.snapshots==2,"second capture adds a variant");
        auto before=b.cells[0].states;
        retro_archive_capture(&e);  // visits = 2: replacement probability max(2/2, 1/32) = 1
        check(b.cells.size()==1&&b.cells[0].states.size()==2&&b.snapshots==2&&b.cells[0].visits==2,"third capture exceeded variants");
        int changed=(b.cells[0].states[0]!=before[0])+(b.cells[0].states[1]!=before[1]);
        check(changed==1,"reservoir replacement must replace exactly one slot");
        for(int i=0;i<40;i++)retro_archive_capture(&e);
        check(b.cells[0].states.size()==2&&b.snapshots==2,"repeated captures exceeded variants");
        check_consistent();b.variants=8;
    }
    // 2. Group identity: the rebuilt area key equals retro_area_key on live captures; section and group key as specified.
    int identities=0;
    for(int spawn:{0,2,15}) {
        clear_bank();e.cur_spawn=spawn;a.lane=.95;puf_reset(&e);
        for(int i=0;i<10;i++)puf_step(&e);
        if(retro_archive_capturable(&e)<0)continue;
        unsigned live=retro_area_key(e.emu->low_mem());
        retro_archive_capture(&e);check(b.cells.size()==1,"identity capture failed");
        auto& c=b.cells[0];auto& st=c.states[0]->start;
        check(retro_archive_snapshot_area(st)==live&&(unsigned)(c.key&0xffffffffu)==live,"rebuilt area key differs from retro_area_key");
        auto& g=b.groups.at(c.group);int section=retro_progress_x(st.x)/b.group_width;
        check(g.area==live&&g.section==section&&g.level==(int)(live>>24)&&g.level==spawn
            &&g.key==(((uint64_t)live<<16)|(uint64_t)(section&0xffff))&&b.group_index.at(g.key)==(size_t)c.group,"group identity");
        identities++;
    }
    check(identities>=2,"too few capturable level starts for the identity check");
    e.cur_spawn=0;a.lane=.95;puf_reset(&e);for(int i=0;i<10;i++)puf_step(&e);
    clear_bank();retro_archive_capture(&e);auto held=b.cells[0].states[0];auto held_core=core(held->start.state);
    unsigned area11=retro_archive_snapshot_area(held->start);

    // 3. Capacity eviction: rules a, b, c with hand-set group probabilities; the inserting cell is never touched.
    {
        // a. Multi-state cells: smallest group prob, ties broken by more states.
        clear_bank();
        int g0=add_group(1u<<24|0x8000,0,.1),g1=add_group(1u<<24|0x8000,1,.3),g2=add_group(1u<<24|0x8000,2,.05),
            g3=add_group(1u<<24|0x8000,3,.2),g4=add_group(1u<<24|0x8000,4,.1);
        add_cell(10,g1,3,held);add_cell(11,g0,2,held);add_cell(12,g4,3,held);add_cell(13,g3,2,held);add_cell(14,g2,1,held);
        b.capacity=b.snapshots;
        check(retro_archive_insert(&e,held,99,0,area11),"rule a insert");
        check(cell(12)->states.size()==2&&cell(11)->states.size()==2&&cell(10)->states.size()==3&&cell(13)->states.size()==2
            &&cell(14)&&cell(99)&&b.snapshots==b.capacity,"rule a (tie on prob -> more states) evicted the wrong state");
        check_consistent();
        // The inserting cell is skipped even when its group has the smallest prob.
        b.groups[g1].prob=.01;
        check(retro_archive_insert(&e,held,10,0,area11),"rule a protected insert");
        check(cell(10)->states.size()==4&&cell(13)->states.size()==2&&cell(11)->states.size()+cell(12)->states.size()==3
            &&b.snapshots==b.capacity,"rule a touched the inserting cell or the wrong group");
        check_consistent();
        // b. All single-state: groups with more than one cell; smallest prob loses a random cell, never the inserting one.
        for(int trial=0;trial<24;trial++) {
            clear_bank();
            int h0=add_group(2u<<24|0x8000,0,.1),h1=add_group(2u<<24|0x8000,1,.05),h2=add_group(2u<<24|0x8000,2,.01);
            add_cell(20,h0,1,held);add_cell(21,h0,1,held);add_cell(22,h1,1,held);add_cell(23,h1,1,held);add_cell(24,h2,1,held);
            b.capacity=b.snapshots;
            bool protect=trial%2;
            check(retro_archive_insert(&e,held,protect?22:98,0,area11),"rule b insert");
            check(cell(20)&&cell(21)&&cell(24)&&(cell(22)!=nullptr)+(cell(23)!=nullptr)==1
                &&b.snapshots==b.capacity,"rule b evicted outside the smallest multi-cell group");
            if(protect)check(cell(22)&&cell(22)->states.size()==2&&!cell(23),"rule b touched the inserting cell");
            check_consistent();
        }
        // c. Single-cell groups only: the smallest-prob group's cell goes, the group turns ineligible and keeps its stats.
        for(int protect=0;protect<2;protect++) {
            clear_bank();
            int k0=add_group(3u<<24|0x8000,0,.1),k1=add_group(3u<<24|0x8000,1,.05),k2=add_group(3u<<24|0x8000,2,.2);
            for(auto g:{k0,k1,k2}){b.groups[g].eligible=1;b.groups[g].attempts=7;b.groups[g].wins=3;}
            add_cell(30,k0,1,held);add_cell(31,k1,1,held);add_cell(32,k2,1,held);b.capacity=b.snapshots;
            b.cdf.assign(3,1.0f);
            check(retro_archive_insert(&e,held,protect?31:97,0,area11),"rule c insert");
            int gone=protect?k0:k1;
            check(!cell(protect?30:31)&&cell(protect?31:30)&&cell(32)&&b.snapshots==b.capacity,"rule c evicted the wrong cell");
            check(b.groups[gone].eligible==0&&b.groups[gone].attempts==7&&b.groups[gone].wins==3&&b.cdf.empty(),"rule c group bookkeeping");
            if(protect)check(cell(31)->states.size()==2,"rule c did not store into the inserting cell");
            check_consistent();
        }
        // The active snapshot of a running episode survives eviction (shared ownership).
        clear_bank();auto active=retro_archive_snapshot(&e);std::weak_ptr<RetroArchiveSnapshot> weak=active;
        add_cell(40,add_group(area11,0,.5),1,active);b.capacity=1;
        check(retro_archive_insert(&e,held,41,0,area11)&&cell(41)&&!cell(40)&&!weak.expired()&&active->start.world==1,
            "eviction released an active snapshot");
        active.reset();check(weak.expired(),"evicted snapshot leaked");
    }

    // 4. Death chain: mode 1 from 1-1 holding Right+B without jumping dies on the first Goomba.
    {
        clear_bank();b.steps=5000;a.start=a.end=0;b.full_game_share=1;a.lane=.5;puf_reset(&e);
        check(a.mode==1&&e.world==1&&e.stage==1,"death chain fixture must be a full game from 1-1");
        std::vector<RetroChainEntry> ring;int decision=-1;
        for(int i=0;i<400&&!b.death_events;i++) {
            ring.assign(a.chain.begin(),a.chain.end());int count=a.chain_count;
            if(count)check(count<=(int)a.chain.size(),"ring count");
            action=retro_mask_action(RETRO_BTN_RIGHT|RETRO_BTN_B);puf_step(&e);
            if(b.death_events){decision=a.last_failure_decision;
                check(decision<e.episode_decisions-20,"failure must be referenced to the dying onset, not the life-counter drop");
                std::vector<RetroChainEntry> live;for(auto& c:ring)if(c.state)live.push_back(c);ring=live;}
        }
        check(b.death_events==1&&decision>0,"no natural-play failure event from the first Goomba");
        std::set<RetroArchiveSnapshot*> deposited,skipped;
        for(auto& c:ring)(c.decision<=decision-b.chain_skip?deposited:skipped).insert(c.state.get());
        check(!deposited.empty()&&!skipped.empty(),"fixture must deposit some entries and skip the newest");
        check(b.chain_deposits==deposited.size(),"chain_deposits differs from entries older than chain_skip");
        check(a.chain_count==0,"ring not cleared after the failure");
        for(auto& c:a.chain)check(!c.state,"ring still holds snapshots");
        size_t stored=0;
        for(auto& c:b.cells)for(auto& s:c.states){check(deposited.count(s.get())&&!skipped.count(s.get()),"bank holds a skipped or foreign snapshot");stored++;}
        check(stored==deposited.size()&&b.snapshots==stored,"deposited entries missing from the bank");
        // Failure crediting: area = last_area_key, x = that frontier's last_x, kernel over the section and upstream ones.
        unsigned area=e.last_area_key;int x=a.last_failure_x;
        check(x>0&&x<600&&(area>>24)==0,"failure position must be the first Goomba, before the dying animation");
        int section=retro_progress_x(x)/b.group_width,credited=0;
        for(auto& g:b.groups) {
            int d=section-g.section;double want=g.area==area?teacher_kernel(d,b.teacher):0;
            check(near(g.deaths,want),"group failure mass differs from the kernel weight");
            if(want>0){check(g.death_stamp==b.steps.load()&&g.death_stamp>5000,"death stamp");credited++;}
        }
        check(credited>0&&credited==(int)b.groups.size(),"1-1 groups at/upstream of the death carry no mass");
        check_consistent();
    }

    // 5. Attempt outcomes from a captured 1-1 cell (mode 2).
    {
        clear_bank();a.start=a.end=1;b.full_game_share=0;b.steps=10000;
        add_cell(900,retro_archive_cell_group(b,*held),1,held);int g=b.cells[0].group;
        int threshold=retro_progress_x(held->start.x)+b.success_distance;
        auto attempt=[&](int pattern,int cap,int limit){
            b.attempt_decisions=cap;a.lane=0;puf_reset(&e);
            check(a.mode==2&&a.attempt_pending&&a.attempt_group==g&&a.attempt_x0==retro_progress_x(held->start.x)
                &&a.attempt_area==area11&&b.groups[g].last_selected==b.steps,"attempt record at selection");
            uint64_t w=b.attempt_wins,l=b.attempt_losses;int at=-1,x=0,before=0,prev=retro_progress_x(e.x_pos);
            for(int i=0;i<limit;i++) {
                int buttons=RETRO_BTN_B|(pattern==2?RETRO_BTN_LEFT:RETRO_BTN_RIGHT);
                if(pattern==1&&i%20>=10)buttons|=RETRO_BTN_A;
                action=retro_mask_action(buttons);puf_step(&e);
                if(at<0&&b.attempt_wins+b.attempt_losses!=w+l){at=i+1;x=retro_progress_x(e.x_pos);before=prev;
                    check(!a.attempt_pending&&a.attempt_decisions==at,"attempt still pending");}
                prev=retro_progress_x(e.x_pos);
                if(done)break;
            }
            check(b.attempt_wins+b.attempt_losses==w+l+1,"attempt must be recorded exactly once");
            struct{int at,wins,x,before;} r={at,(int)(b.attempt_wins-w),x,before};return r;
        };
        auto win=attempt(1,600,200);
        check(win.wins==1&&win.x>=threshold&&win.before<threshold,"Right+B+A did not record a win when success_distance was passed");
        check(b.groups[g].attempts==1&&b.groups[g].wins==1,"win not recorded in the group");
        auto left=attempt(2,20,60);
        check(left.at==20&&left.wins==0,"holding Left must record a loss at the decision cap");
        auto death=attempt(0,600,200);
        check(death.at>0&&death.at<600&&death.wins==0&&death.x<threshold&&b.death_events==0,"a death must record a loss (and mode 2 credits no failure)");
        check(b.groups[g].attempts==3&&b.groups[g].wins==1&&b.attempt_wins==1&&b.attempt_losses==2,"attempt counters");
        TeacherGroup expect;teacher_record(expect,1,1,b.teacher);teacher_record(expect,1,0,b.teacher);teacher_record(expect,1,0,b.teacher);
        check(near(b.groups[g].fast,expect.fast)&&near(b.groups[g].slow,expect.slow),"attempt EMAs differ from teacher_record");
    }

    // 6. Selection: sampled levels follow prob within 1% over 20k draws; spawn levels gate groups; lanes route shares.
    {
        clear_bank();b.steps=1000000;a.start=a.end=1;b.full_game_share=0;
        int levels[5]={0,5,10,15,20};
        for(int k=0;k<5;k++) {
            unsigned area=((unsigned)levels[k]<<24)|0x8000;int g=add_group(area,k,0);
            auto& G=b.groups[g];G.deaths=k==0?3:k==1?1:0;G.death_stamp=b.steps;G.last_selected=k*1000;
            G.attempts=k==2?8:2;G.wins=k==2?4:1;if(k==2){G.fast=.3;G.slow=.01;}
            add_cell(500+k,g,1,held);add_cell(600+k,g,2,held);
        }
        unsigned char sw[32],sl[32];int sn=e.spawn_n;memcpy(sw,e.spawn_w,32);memcpy(sl,e.spawn_l,32);
        e.spawn_n=4;for(int k=0;k<4;k++){e.spawn_w[k]=levels[k]/4+1;e.spawn_l[k]=levels[k]%4+1;}
        std::vector<TeacherGroup> expect=b.groups;for(int k=0;k<5;k++)expect[k].eligible=k<4;
        check(teacher_priorities(expect,b.steps,b.teacher)==4,"expected eligible groups");
        int counts[32]={},id=-1;
        for(int i=0;i<20000;i++){a.lane=0;retro_archive_choose(&e,&id);check(a.mode==2,"teacher draw left the archive lane");counts[id]++;}
        check(b.cdf_eligible==4&&b.groups[4].eligible==0&&b.groups[4].prob==0,"refresh eligibility");
        for(int k=0;k<5;k++)check(near(b.groups[k].prob,expect[k].prob),"refresh priorities differ from teacher_priorities");
        for(int k=0;k<4;k++)check(std::fabs(counts[levels[k]]/20000.0-expect[k].prob)<.01,"sampled level shares differ from prob");
        check(counts[20]==0,"a group outside spawn_levels was chosen");
        // No eligible group: falls back exactly like the legacy mode 2 -> mode 0 path.
        e.spawn_n=1;e.spawn_w[0]=8;e.spawn_l[0]=4;b.cdf.clear();
        const RetroStart* level_start=retro_rom().starts[0].get();e.start=level_start;id=7;a.lane=0;
        retro_archive_choose(&e,&id);check(a.mode==0&&id==7&&e.start==level_start&&!a.attempt_pending,"no-eligible fallback");
        e.spawn_n=sn;memcpy(e.spawn_w,sw,32);memcpy(e.spawn_l,sl,32);b.cdf.clear();
        // Lanes with start = end = .5 and the fixed full-game share .3: [0,.5) archive, [.5,.8) full game, rest level starts.
        a.start=a.end=.5;b.full_game_share=.3;
        double lanes[6]={.0,.49,.51,.79,.81,.99};int modes[6]={2,2,1,1,0,0};
        for(int k=0;k<6;k++){a.lane=lanes[k];id=9;retro_archive_choose(&e,&id);
            check(a.mode==modes[k]&&(modes[k]!=1||(id==0&&e.start==retro_rom().starts[0].get()))&&(modes[k]!=0||id==9),"fixed full-game share routed a lane wrongly");}
        b.full_game_share=.3;
    }

    // 7. Serialization: v2 round trip, v1 compatibility, corrupt and wrong-ROM rejection.
    {
        std::string path="/tmp/retro_archive_teacher_test_"+std::to_string(getpid())+".gz";
        clear_bank();
        // A second distinct snapshot: the fixture a few decisions later.
        a.start=a.end=.5;b.full_game_share=.3;a.lane=.95;puf_reset(&e);check(a.mode==0,"lane .95 must be a level start");
        action=0;for(int i=0;i<10;i++)puf_step(&e);
        for(int i=0;i<4;i++){action=retro_mask_action(RETRO_BTN_RIGHT);puf_step(&e);}
        check(retro_archive_capturable(&e)==0,"second snapshot must be a capturable 1-1 state");
        auto later=retro_archive_snapshot(&e);
        // g1 has no cells: groups are never deleted, keep their stats and load ineligible.
        int g0=retro_archive_cell_group(b,*held),g1=add_group(area11,7,.25);
        check(retro_archive_cell_group(b,*later)==g0,"fixture snapshots must share a section");
        add_cell(700,g0,1,held);cell(700)->states.push_back(later);cell(700)->states.push_back(held);b.snapshots+=2;
        add_cell(701,g0,1,later);
        auto& c=*cell(700);c.visits=11;c.selected=12;c.failures=13;c.successes=14;
        auto& G=b.groups[g0];G.attempts=9;G.wins=4;G.fast=.2;G.slow=.05;G.deaths=2.5;G.death_stamp=77;G.last_selected=88;G.prob=.4;
        b.groups[g1].attempts=3;b.groups[g1].deaths=.5;
        auto groups=b.groups;auto cells=b.cells;
        retro_archive_file(path.c_str(),true,.25);
        clear_bank();retro_archive_file(path.c_str(),false);
        check(b.cells.size()==2&&b.snapshots==4&&b.resumed_phase==.25,"v2 round trip cells/snapshots/phase");
        for(auto& want:cells) {
            auto* got=cell(want.key);check(got&&got->states.size()==want.states.size(),"v2 lost variants");
            check(got->visits==want.visits&&got->selected==want.selected&&got->failures==want.failures
                &&got->successes==want.successes&&got->level==want.level,"v2 lost cell stats");
            for(size_t k=0;k<want.states.size();k++)
                check(got->states[k]->start.x==want.states[k]->start.x&&core(got->states[k]->start.state)==core(want.states[k]->start.state)
                    &&!memcmp(got->states[k]->start.pixels,want.states[k]->start.pixels,sizeof(want.states[k]->start.pixels))
                    &&!memcmp(&got->states[k]->ledger,&want.states[k]->ledger,sizeof(RetroArchiveLedger)),"v2 changed a snapshot");
            check(b.groups.at(got->group).key==groups.at(want.group).key,"v2 cell group");
        }
        check(b.groups.size()==groups.size(),"v2 group count");
        for(size_t k=0;k<groups.size();k++) {
            auto& x=b.groups[k];auto& y=groups[k];
            check(x.key==y.key&&x.area==y.area&&x.level==y.level&&x.section==y.section&&x.attempts==y.attempts&&x.wins==y.wins
                &&x.fast==y.fast&&x.slow==y.slow&&x.deaths==y.deaths&&x.prob==y.prob&&b.group_index.at(x.key)==k,"v2 lost group stats");
            check(x.death_stamp==0&&x.last_selected==0,"step stamps must restart after a load");
        }
        check(b.groups[g0].eligible==1&&b.groups[g1].eligible==0&&b.cdf.empty(),"eligibility after load");
        check(core(cell(700)->states[0]->start.state)==held_core,"held snapshot core changed");
        // Corrupt and wrong-ROM files are rejected and leave the bank as it was.
        auto fingerprint=retro_rom().fingerprint;retro_rom().fingerprint^=1;bool rejected=false;
        try{retro_archive_file(path.c_str(),false);}catch(const std::exception&){rejected=true;}retro_rom().fingerprint=fingerprint;
        check(rejected&&b.cells.size()==2&&b.snapshots==4,"mismatched ROM archive accepted");
        {FILE* f=fopen(path.c_str(),"wb");fputs("broken",f);fclose(f);}rejected=false;
        try{retro_archive_file(path.c_str(),false);}catch(const std::exception&){rejected=true;}
        check(rejected&&b.cells.size()==2,"corrupt archive accepted");
        // A truncated v2 file (cells but no group table) is rejected too.
        {
            retro_archive_file(path.c_str(),true,0);
            gzFile in=gzopen(path.c_str(),"rb");std::vector<char> all;char buf[65536];int n;
            while((n=gzread(in,buf,sizeof(buf)))>0)all.insert(all.end(),buf,buf+n);gzclose(in);
            gzFile out=gzopen(path.c_str(),"wb1");gzwrite(out,all.data(),(unsigned)(all.size()-sizeof(TeacherGroup)));gzclose(out);
            rejected=false;try{retro_archive_file(path.c_str(),false);}catch(const std::exception&){rejected=true;}
            check(rejected,"truncated v2 archive accepted");
        }
        // v1, written here in the pre-2026-11-07 layout: one snapshot per cell, no group table.
        {
            gzFile f=gzopen(path.c_str(),"wb1");
            auto put=[&](const void* p,size_t n){check(gzwrite(f,p,(unsigned)n)==(int)n,"v1 write");};
            std::shared_ptr<RetroArchiveSnapshot> v1states[2]={held,later};
            uint64_t header[5]={RETRO_ARCHIVE_V1_MAGIC,retro_rom().fingerprint,2,sizeof(RetroArchiveLedger),0x01020304};
            double phase=.5;put(header,sizeof(header));put(&phase,sizeof(phase));
            for(int k=0;k<2;k++) {
                auto& s=*v1states[k];auto& st=s.start;
                uint64_t stats[7]={(uint64_t)800+k,5u+k,6,7,8,0,0};put(stats,sizeof(stats));
                put(&s.ledger,sizeof(s.ledger));put(st.pixels,sizeof(st.pixels));put(st.palette,sizeof(st.palette));
                int metadata[7]={st.world,st.stage,st.area,st.data,st.x,st.rta_offset_frames,0};put(metadata,sizeof(metadata));
                auto bytes=core(st.state);uint32_t size=bytes.size();put(&size,sizeof(size));put(bytes.data(),size);
            }
            check(gzclose(f)==Z_OK,"v1 close");
            clear_bank();b.groups.push_back(TeacherGroup{});  // must be replaced, not kept
            retro_archive_file(path.c_str(),false);
            check(b.cells.size()==2&&b.snapshots==2&&b.resumed_phase==.5,"v1 load");
            for(auto& c:b.cells)check(c.states.size()==1&&c.visits>=5&&c.selected==6&&c.failures==7&&c.successes==8,"v1 cell");
            check(core(cell(800)->states[0]->start.state)==held_core,"v1 snapshot core changed");
            std::set<uint64_t> keys;
            for(auto& c:b.cells){auto& g=b.groups.at(c.group);keys.insert(g.key);
                check(g.area==area11&&g.section==retro_progress_x(c.states[0]->start.x)/b.group_width&&g.attempts==0&&g.wins==0
                    &&g.deaths==0&&g.eligible==1,"v1 groups not rebuilt from cells with zero stats");}
            check(b.groups.size()==keys.size()&&b.group_index.size()==keys.size(),"v1 group count");
            check_consistent();
        }
        unlink(path.c_str());
    }
    puf_close(&e);dict_clear(&cfg);
    puts("PASS teacher archive: variants, group identity, eviction rules a/b/c, death chain + failure kernel, attempt win/cap/death, teacher selection + spawn gate + lanes, v2/v1 archives + rejection");
    return 0;
}catch(const std::exception& ex){fprintf(stderr,"FAIL: %s\n",ex.what());return 1;}}
