// Practice curriculum teacher on the CPU path (headless): group construction from the v2 practice bank,
// reset sampling, attempt outcomes, natural-play failure crediting, the host refresh and its cadence,
// and the .teacher / .practice sidecars. The shared arithmetic itself is checked by
// ocean/retro/tests/test_teacher.cpp; this test checks how mario_sim feeds and uses it.
#define SMB_HEADLESS
#include "mario_sim.h"
#include "test_common.h"
#include <set>

static const char* fpt_teacher_bank="build/mario_sim/practice_v2/bank.bin";
static Ini fpt_teacher_ini(int teacher) {
    Ini ini={};puf_ini_load_file(&ini,"config/mario_sim.ini");
    dict_set(puf_ini_section(&ini,"vec",0),"total_agents",4);
    auto* game=puf_ini_section(&ini,"game",0);dict_set(game,"practice_fraction",.25);
    dict_set(game,"max_frames",17);dict_set(game,"practice_max_frames",13);
    dict_set_str(game,"practice_bank",fpt_teacher_bank);dict_set(game,"practice_teacher",teacher);
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
        game.episode.status=SMB_EPISODE_ACTIVE;game.game.last_key=key;game.game.last_x=x;game.game.tracking=0;
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
        for(Env* e:{&p,&envs.env[3]}){e->game.last_key=key;e->game.last_x=x;e->state->ram[0x75a]=1;e->game.lives=2;e->episode.status=SMB_EPISODE_ACTIVE;fpt_after(*e);}
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
        teacher_priorities(expect,t.agent_steps,t.params);std::vector<float> expect_cdf;teacher_cdf(expect,&expect_cdf);
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
            &&dict_find(&metrics,"teacher_8_4_share")&&dict_find(&metrics,"teacher_1_1_share")&&!dict_find(&metrics,"teacher_1_2_share"),"teacher metrics missing");
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
