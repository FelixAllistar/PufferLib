#define SMB_HEADLESS
#include "mario_sim.cu"
#include "test_common.h"

// Practice curriculum teacher on the CUDA path: bitwise CPU/CUDA parity (states, envs, attempt records and
// teacher counters) while the CDF is frozen, then 2000 steps of direct and graph launches with the
// stream-ordered refresh live, a forced refresh, and the .teacher sidecar.
// maze: the game actors start on 4-4 route states (and reset to 4-4) and run scripted Right+B with A pulses; the
// bitwise parity window runs at least 256 frames and until the CPU reference has counted a loop failure
// (events[1] > 0). (A 4-4 route state taken just before the movie's x-1024 check loops on the first frame.)
static void teacher_case(int argc,char** argv,unsigned* decisions_out,bool maze=false) {
    const int count=128,steps=2000;int parity_steps=maze?4000:256;
    Ini ini={};puf_ini_load_file(&ini,"config/mario_sim.ini");
    if(argc>1)dict_set_str(puf_ini_section(&ini,"env",0),"engine_module",argv[1]);
    if(argc>2)dict_set_str(puf_ini_section(&ini,"game",0),"reset_bank",(std::string(argv[2])+"/bank.bin").c_str());
    auto* game=puf_ini_section(&ini,"game",0);
    dict_set_str(puf_ini_section(&ini,"env",0),"mode","mixed");dict_set(puf_ini_section(&ini,"vec",0),"total_agents",count);
    dict_set(game,"max_frames",400);dict_set(puf_ini_section(&ini,"fpg",0),"max_frames",41);
    dict_set(game,"practice_fraction",.25);dict_set(game,"practice_max_frames",53);dict_set(game,"practice_teacher",1);
    dict_set(game,"practice_success_distance",48);dict_set(game,"practice_refresh_steps",100000000);
    if(maze){dict_set(game,"fixed_stage",15);dict_set(game,"max_frames",3000);}
    fpt_configure(&ini,"train",nullptr);
    float *obs,*actions,*rewards,*terminals;
    smb_cuda_check(cudaMalloc(&obs,(size_t)count*FPT_OBS*sizeof(float)));
    smb_cuda_check(cudaMalloc(&actions,count*sizeof(float)));smb_cuda_check(cudaMalloc(&rewards,count*sizeof(float)));
    smb_cuda_check(cudaMalloc(&terminals,count*sizeof(float)));
    Env* envs=puf_vec_create(count,puf_ini_section(&ini,"env",0),obs,actions,rewards,terminals);
    cudaStream_t stream;smb_cuda_check(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));puf_bind_stream(stream);puf_reset(envs);
    smb_cuda_check(cudaStreamSynchronize(stream));auto& h=*fpt_host;auto& t=h.teacher;size_t G=t.groups.size();
    fpt_check(t.enabled&&G>0&&t.device.cdf==fpt_teacher_cdf,"teacher not on the device");
    // CPU reference: same arithmetic on host copies of the CDF and group tables, with its own counters.
    fpt_check(h.game.exit_reward==0.5f&&h.game.progress_cap==8192&&t.params.weight_gate==0.3,"exit reward / gate config not loaded");
    std::vector<uint32_t> counters(4*G+SMB_TEACHER_EVENTS,0),device(4*G+SMB_TEACHER_EVENTS);std::vector<float> cdf=t.cdf;
    fpt_check(t.device.pairs==(int)t.pair_from.size()&&t.device.pairs>0&&t.device.pair_from==fpt_teacher_pair_from,"route table not on the device");
    std::vector<uint32_t> device_pairs(2*t.pair_from.size());
    smb_cuda_check(cudaMemcpy(device_pairs.data(),fpt_teacher_pair_from,t.pair_from.size()*sizeof(uint32_t),cudaMemcpyDeviceToHost));
    smb_cuda_check(cudaMemcpy(device_pairs.data()+t.pair_from.size(),fpt_teacher_pair_to,t.pair_to.size()*sizeof(uint32_t),cudaMemcpyDeviceToHost));
    fpt_check(std::equal(t.pair_from.begin(),t.pair_from.end(),device_pairs.begin())&&std::equal(t.pair_to.begin(),t.pair_to.end(),device_pairs.begin()+t.pair_from.size()),"device route table differs");
    fpt_check(t.device.loops==(int)t.loop_key.size()&&t.device.loops==1&&t.device.loop_key==fpt_teacher_loop_key,"on-route loop table not on the device");
    uint32_t device_loop_key;int32_t device_loop_x;
    smb_cuda_check(cudaMemcpy(&device_loop_key,fpt_teacher_loop_key,sizeof(uint32_t),cudaMemcpyDeviceToHost));
    smb_cuda_check(cudaMemcpy(&device_loop_x,fpt_teacher_loop_x,sizeof(int32_t),cudaMemcpyDeviceToHost));
    fpt_check(device_loop_key==t.loop_key[0]&&device_loop_x==t.loop_x[0],"device on-route loop table differs");
    SmbTeacherDevice ref=t.device;ref.cdf=cdf.data();ref.offsets=t.offsets.data();ref.area=t.area.data();ref.section=t.section.data();
    ref.pair_from=t.pair_from.data();ref.pair_to=t.pair_to.data();ref.loop_key=t.loop_key.data();ref.loop_x=t.loop_x.data();
    std::vector<uint32_t> route44;
    for(uint32_t k=t.offsets[0];k<t.offsets[G];k++)if(h.runtime_bank[k].stage==15&&(h.runtime_bank[k].flags&SMB_BANK_ROUTE))route44.push_back(k);
    fpt_check(!maze||route44.size()>0,"no 4-4 route states in the runtime bank");
    ref.selected=counters.data();ref.attempts=ref.selected+G;ref.wins=ref.selected+2*G;ref.death_weights=ref.selected+3*G;ref.events=ref.selected+4*G;
    std::vector<Env> cpu(count),gpu(count);std::vector<SmbLogic> states(count),actual(count);
    std::vector<float> input(count),expected_reward(count),expected_terminal(count),got_reward(count),got_terminal(count),observations(count*FPT_OBS),expected_obs(count*FPT_OBS);
    int practice_actors=0;
    for(int i=0;i<count;i++) {
        auto& e=cpu[i];e.num_agents=1;e.is_fpg=smb_fpg_actor(i,count,&h.game);e.is_practice=smb_practice_actor(i,count,&h.game,&h.practice);
        practice_actors+=e.is_practice;e.episode.rng=smb_seed(h.task.seed,i);
        fpt_check(!fpt_reset_state(&e,&states[i],&h.task,&h.time,h.runtime_bank.data(),h.eligible.data(),h.eligible.size(),h.table.data(),&h.curriculum,&h.game,&h.practice,&ref),"CPU teacher reset fault");
        if(maze&&!e.is_fpg&&!e.is_practice) {
            e.episode.scene=route44[i%route44.size()];
            fpt_check(!smb_practice_load(&states[i],&e.episode,&e.game,15,&h.game,h.runtime_bank.data())&&e.game.tracking,"4-4 route placement fault");
        }
        fpt_observe(&states[i],h.bank->worlds.data(),&e.observation_history,expected_obs.data()+i*FPT_OBS);
    }
    fpt_check(practice_actors==32,"incorrect practice frame allocation");
    smb_cuda_check(cudaMemcpy(device.data(),fpt_teacher_counters,device.size()*sizeof(uint32_t),cudaMemcpyDeviceToHost));
    fpt_check(device==counters,"initial teacher selections differ between CPU and CUDA");
    smb_cuda_check(cudaMemcpy(envs,cpu.data(),count*sizeof(Env),cudaMemcpyHostToDevice));
    smb_cuda_check(cudaMemcpy(fpt_states,states.data(),count*sizeof(SmbLogic),cudaMemcpyHostToDevice));
    cudaGraph_t graph;cudaGraphExec_t executable;
    smb_cuda_check(cudaStreamBeginCapture(stream,cudaStreamCaptureModeThreadLocal));puf_step(envs);
    smb_cuda_check(cudaStreamEndCapture(stream,&graph));smb_cuda_check(cudaGraphInstantiate(&executable,graph,0,0,0));
    uint32_t rng=139;unsigned decisions=0;
    for(int frame=0;frame<parity_steps;frame++) {
        for(int i=0;i<count;i++) {
            auto& e=cpu[i];input[i]=(float)(smb_random(&rng)%64);if(!e.is_fpg&&i%4==0)input[i]=34;
            if(maze&&!e.is_fpg&&!e.is_practice)input[i]=(frame+i*7)%40<20?35:34;   // Right+B, A held half of every 40 frames
            fpt_check(!smb_native_frame(&states[i],h.bank->worlds.data(),smb_action_buttons((int)input[i])),"CPU frame fault");
            expected_reward[i]=smb_after_frame(&e,&states[i],&h.task,&h.time,&h.curriculum,h.table.data(),&h.game,&h.practice,&ref);
            expected_terminal[i]=e.episode.status!=SMB_EPISODE_ACTIVE;
            if(expected_terminal[i])fpt_check(!fpt_reset_state(&e,&states[i],&h.task,&h.time,h.runtime_bank.data(),h.eligible.data(),h.eligible.size(),h.table.data(),&h.curriculum,&h.game,&h.practice,&ref),"CPU auto-reset fault");
            fpt_observe(&states[i],h.bank->worlds.data(),&e.observation_history,expected_obs.data()+i*FPT_OBS);
        }
        smb_cuda_check(cudaMemcpyAsync(actions,input.data(),count*sizeof(float),cudaMemcpyHostToDevice,stream));
        if(frame&1)smb_cuda_check(cudaGraphLaunch(executable,stream));else puf_step(envs);
        smb_cuda_check(cudaStreamSynchronize(stream));
        smb_cuda_check(cudaMemcpy(gpu.data(),envs,count*sizeof(Env),cudaMemcpyDeviceToHost));
        smb_cuda_check(cudaMemcpy(actual.data(),fpt_states,count*sizeof(SmbLogic),cudaMemcpyDeviceToHost));
        smb_cuda_check(cudaMemcpy(got_reward.data(),rewards,count*sizeof(float),cudaMemcpyDeviceToHost));
        smb_cuda_check(cudaMemcpy(got_terminal.data(),terminals,count*sizeof(float),cudaMemcpyDeviceToHost));
        smb_cuda_check(cudaMemcpy(observations.data(),obs,observations.size()*sizeof(float),cudaMemcpyDeviceToHost));
        for(int i=0;i<count;i++) {
            fpt_same_state(states[i],actual[i]);const auto& a=cpu[i];const auto& b=gpu[i];
            fpt_check(!memcmp(&a.game,&b.game,sizeof(a.game))&&!memcmp(&a.practice,&b.practice,sizeof(a.practice))
                &&!memcmp(&a.episode,&b.episode,sizeof(a.episode))&&!memcmp(&a.curriculum,&b.curriculum,sizeof(a.curriculum)),"teacher env state mismatch");
            fpt_check(a.teacher_group==b.teacher_group&&a.teacher_pending==b.teacher_pending&&a.teacher_frames==b.teacher_frames
                &&a.teacher_x0==b.teacher_x0&&a.teacher_area==b.teacher_area&&a.teacher_track_deaths==b.teacher_track_deaths
                &&a.teacher_route_loops==b.teacher_route_loops&&a.teacher_loop_land==b.teacher_loop_land,"teacher attempt record mismatch");
            fpt_check(fpt_near(expected_reward[i],got_reward[i])&&expected_terminal[i]==got_terminal[i],"teacher reward/terminal mismatch");
            for(size_t j=0;j<sizeof(Log)/sizeof(float);j++)fpt_check(fpt_near(((const float*)&a.log)[j],((const float*)&b.log)[j]),"teacher log mismatch");
            for(int k=0;k<FPT_OBS;k++)fpt_check(observations[i*FPT_OBS+k]==expected_obs[i*FPT_OBS+k],"teacher observation mismatch");
            decisions++;
        }
        smb_cuda_check(cudaMemcpy(device.data(),fpt_teacher_counters,device.size()*sizeof(uint32_t),cudaMemcpyDeviceToHost));
        fpt_check(device==counters,"teacher counters differ between CPU and CUDA");
        if(maze&&frame+1>=256&&counters[4*G+1]>0){parity_steps=frame+1;break;}
    }
    uint64_t parity_attempts=0,parity_wins=0;for(size_t g=0;g<G;g++){parity_attempts+=counters[G+g];parity_wins+=counters[2*G+g];}
    uint64_t parity_failures=counters[4*G],parity_loops=counters[4*G+1],parity_wrong_exits=counters[4*G+2],parity_passed=counters[4*G+3],parity_exits=counters[4*G+4];
    fpt_check(parity_attempts>0&&t.refreshes==0,"parity window recorded no attempts or refreshed early");
    if(maze) {
        fpt_check(parity_loops>0,"scripted 4-4 play produced no loop failure on the CPU");
        fprintf(stderr,"mode=teacher-maze 4-4 route_states=%zu parity_frames=%d parity_attempts=%llu parity_failures=%llu parity_loops=%llu "
            "parity_wrong_exits=%llu parity_route_loops_passed=%llu parity_forward_exits=%llu parity=passed\n",route44.size(),parity_steps,(unsigned long long)parity_attempts,
            (unsigned long long)parity_failures,(unsigned long long)parity_loops,(unsigned long long)parity_wrong_exits,(unsigned long long)parity_passed,
            (unsigned long long)parity_exits);
        smb_cuda_check(cudaGraphExecDestroy(executable));smb_cuda_check(cudaGraphDestroy(graph));puf_close(envs);
        cudaStreamDestroy(stream);cudaFree(obs);cudaFree(actions);cudaFree(rewards);cudaFree(terminals);puf_ini_free(&ini);
        *decisions_out+=decisions;return;
    }
    // Live refresh: direct calls every refresh_steps, graph replays through the captured host node.
    t.refresh_steps=64;
    for(int frame=parity_steps;frame<steps;frame++) {
        for(int i=0;i<count;i++)input[i]=(float)(smb_random(&rng)%64);
        smb_cuda_check(cudaMemcpyAsync(actions,input.data(),count*sizeof(float),cudaMemcpyHostToDevice,stream));
        if(frame&1)smb_cuda_check(cudaGraphLaunch(executable,stream));else puf_step(envs);
    }
    smb_cuda_check(cudaStreamSynchronize(stream));uint64_t automatic=t.refreshes;
    fpt_check(automatic>=2&&t.agent_steps>0,"no stream-ordered refresh ran");
    fpt_teacher_refresh_now();
    smb_cuda_check(cudaMemcpy(device.data(),fpt_teacher_counters,device.size()*sizeof(uint32_t),cudaMemcpyDeviceToHost));
    std::vector<float> device_cdf(G);smb_cuda_check(cudaMemcpy(device_cdf.data(),fpt_teacher_cdf,G*sizeof(float),cudaMemcpyDeviceToHost));
    uint64_t selected=0,attempts=0,wins=0,recorded=0;
    for(size_t g=0;g<G;g++) {
        selected+=device[g];attempts+=device[G+g];wins+=device[2*G+g];recorded+=t.groups[g].attempts;
        fpt_check(device[G+g]<=device[g]&&device[2*G+g]<=device[G+g],"attempts exceed selections or wins exceed attempts");
    }
    fpt_check(selected>0&&t.cdf.back()==1.0f&&device_cdf==t.cdf&&recorded==attempts&&t.previous==device
        &&t.death_events==device[4*G]&&t.loop_events==device[4*G+1]&&t.wrong_exit_events==device[4*G+2]&&t.route_loops_passed==device[4*G+3]&&t.forward_exits==device[4*G+4],
        "forced refresh did not fold the device counters");
    // Sidecar from the CUDA path.
    const char* path="build/mario_sim/teacher_test_checkpoint.bin";{std::ofstream out(path);out<<"teacher test";}
    std::filesystem::remove(std::string(path)+".practice");
    fpt_gpu_save(path,nullptr);auto saved=smb_read_teacher(path,h.practice_bank_hash);
    fpt_check(saved.size()==G&&!std::filesystem::exists(std::string(path)+".practice"),"CUDA teacher sidecar missing");
    for(auto& g:t.groups)g.attempts=g.wins=0;
    fpt_gpu_load(path,&ini);recorded=0;for(const auto& g:t.groups)recorded+=g.attempts;
    fpt_check(recorded==attempts,"CUDA teacher resume lost statistics");
    fprintf(stderr,"mode=teacher actors=%d practice=%d groups=%zu parity_steps=%d steps=%d parity_attempts=%llu parity_wins=%llu "
        "parity_failures=%llu parity_loops=%llu parity_wrong_exits=%llu parity_route_loops_passed=%llu parity_forward_exits=%llu route_pairs=%zu "
        "selected=%llu attempts=%llu wins=%llu death_events=%llu loop_events=%llu wrong_exit_events=%llu route_loops_passed=%llu forward_exits=%llu stream_refreshes=%llu agent_steps=%llu entropy=%.4f parity=passed\n",
        count,practice_actors,G,parity_steps,steps,(unsigned long long)parity_attempts,(unsigned long long)parity_wins,
        (unsigned long long)parity_failures,(unsigned long long)parity_loops,(unsigned long long)parity_wrong_exits,(unsigned long long)parity_passed,(unsigned long long)parity_exits,t.pair_from.size(),
        (unsigned long long)selected,(unsigned long long)attempts,(unsigned long long)wins,(unsigned long long)t.death_events,
        (unsigned long long)t.loop_events,(unsigned long long)t.wrong_exit_events,(unsigned long long)t.route_loops_passed,(unsigned long long)t.forward_exits,
        (unsigned long long)automatic,(unsigned long long)t.agent_steps,teacher_entropy(t.groups));
    smb_cuda_check(cudaGraphExecDestroy(executable));smb_cuda_check(cudaGraphDestroy(graph));puf_close(envs);
    cudaStreamDestroy(stream);cudaFree(obs);cudaFree(actions);cudaFree(rewards);cudaFree(terminals);puf_ini_free(&ini);
    *decisions_out+=decisions;
}

// Real reset banks, both execution paths, auto-resets, semantic history and
// mixed checkpoint persistence. The reference runs the translated CPU engine.
int main(int argc,char** argv) {
    try {
        const int count=128,steps=384;unsigned decisions=0,resets=0;
        for(const char* mode:{"game","fpg","mixed","practice"}) {
            Ini ini={};puf_ini_load_file(&ini,"config/mario_sim.ini");
            if(argc>1)dict_set_str(puf_ini_section(&ini,"env",0),"engine_module",argv[1]);
            if(argc>2)dict_set_str(puf_ini_section(&ini,"game",0),"reset_bank",(std::string(argv[2])+"/bank.bin").c_str());
            bool practice=!strcmp(mode,"practice");
            dict_set_str(puf_ini_section(&ini,"env",0),"mode",practice?"mixed":mode);
            dict_set(puf_ini_section(&ini,"vec",0),"total_agents",count);
            dict_set(puf_ini_section(&ini,"game",0),"max_frames",97);
            dict_set(puf_ini_section(&ini,"fpg",0),"max_frames",41);
            dict_set(puf_ini_section(&ini,"game",0),"practice_fraction",practice?.25:0);
            dict_set(puf_ini_section(&ini,"game",0),"practice_max_frames",53);
            // The band machine; the teacher has its own case below.
            dict_set(puf_ini_section(&ini,"game",0),"practice_teacher",0);
            dict_set(puf_ini_section(&ini,"fpg",0),"curriculum_resume",1);
            if(!strcmp(mode,"game")) {
                // Full games must not depend on any FPG assets.
                dict_set_str(puf_ini_section(&ini,"fpg",0),"reset_bank","missing-fpg-bank");
                dict_set_str(puf_ini_section(&ini,"fpg",0),"time_table","missing-fpg-table");
            }
            fpt_configure(&ini,"train",nullptr);
            float *obs,*actions,*rewards,*terminals;
            smb_cuda_check(cudaMalloc(&obs,(size_t)count*FPT_OBS*sizeof(float)));
            smb_cuda_check(cudaMalloc(&actions,count*sizeof(float)));smb_cuda_check(cudaMalloc(&rewards,count*sizeof(float)));
            smb_cuda_check(cudaMalloc(&terminals,count*sizeof(float)));
            Env* envs=puf_vec_create(count,puf_ini_section(&ini,"env",0),obs,actions,rewards,terminals);
            cudaStream_t stream;smb_cuda_check(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));puf_bind_stream(stream);puf_reset(envs);
            smb_cuda_check(cudaStreamSynchronize(stream));auto& h=*fpt_host;
            std::vector<Env> cpu(count),gpu(count);std::vector<SmbLogic> states(count),actual(count);
            std::vector<float> input(count),expected_reward(count),expected_terminal(count),got_reward(count),got_terminal(count),observations(count*FPT_OBS),expected_obs(count*FPT_OBS);
            uint32_t seen_stages=0;int fpg_actors=0,practice_actors=0;
            for(int i=0;i<count;i++) {
                auto& e=cpu[i];e.num_agents=1;e.is_fpg=smb_fpg_actor(i,count,&h.game);fpg_actors+=e.is_fpg;
                e.is_practice=smb_practice_actor(i,count,&h.game,&h.practice);practice_actors+=e.is_practice;
                e.episode.rng=smb_seed(h.task.seed,i);
                fpt_check(!fpt_reset_state(&e,&states[i],&h.task,&h.time,h.runtime_bank.data(),h.eligible.data(),h.eligible.size(),h.table.data(),&h.curriculum,&h.game,&h.practice),"CPU reset fault");
                if(!e.is_fpg&&!e.is_practice) {
                    // Cover every original water, castle, underground and ground
                    // stage on both backends, regardless of the random sample.
                    auto game=h.game;game.fixed_stage=i%32;
                    fpt_check(!smb_game_reset(&states[i],&e.episode,&e.game,&game,h.runtime_bank.data()),"stage reset fault");
                    seen_stages|=1u<<e.game.stage;
                }
                fpt_observe(&states[i],h.bank->worlds.data(),&e.observation_history,expected_obs.data()+i*FPT_OBS);
            }
            fpt_check(fpg_actors==(!strcmp(mode,"game")?0:!strcmp(mode,"fpg")?128:32),"incorrect frame allocation");
            fpt_check(practice_actors==(practice?32:0),"incorrect practice frame allocation");
            if(strcmp(mode,"fpg"))fpt_check(seen_stages==0xffffffffu,"did not cover 32 stages");
            smb_cuda_check(cudaMemcpy(envs,cpu.data(),count*sizeof(Env),cudaMemcpyHostToDevice));
            smb_cuda_check(cudaMemcpy(fpt_states,states.data(),count*sizeof(SmbLogic),cudaMemcpyHostToDevice));
            cudaGraph_t graph;cudaGraphExec_t executable;
            smb_cuda_check(cudaStreamBeginCapture(stream,cudaStreamCaptureModeThreadLocal));puf_step(envs);
            smb_cuda_check(cudaStreamEndCapture(stream,&graph));smb_cuda_check(cudaGraphInstantiate(&executable,graph,0,0,0));
            uint32_t rng=137;
            for(int frame=0;frame<steps;frame++) {
                for(int i=0;i<count;i++) {
                    auto& e=cpu[i];input[i]=(float)(smb_random(&rng)%64);
                    if(!e.is_fpg&&i%4==0)input[i]=34; // Right+B: exercise positive progress rewards.
                    fpt_check(!smb_native_frame(&states[i],h.bank->worlds.data(),smb_action_buttons((int)input[i])),"CPU frame fault");
                    expected_reward[i]=smb_after_frame(&e,&states[i],&h.task,&h.time,&h.curriculum,h.table.data(),&h.game,&h.practice);
                    expected_terminal[i]=e.episode.status!=SMB_EPISODE_ACTIVE;
                    if(expected_terminal[i]) {
                        resets++;fpt_check(!fpt_reset_state(&e,&states[i],&h.task,&h.time,h.runtime_bank.data(),h.eligible.data(),h.eligible.size(),h.table.data(),&h.curriculum,&h.game,&h.practice),"CPU auto-reset fault");
                    }
                    fpt_observe(&states[i],h.bank->worlds.data(),&e.observation_history,expected_obs.data()+i*FPT_OBS);
                }
                smb_cuda_check(cudaMemcpyAsync(actions,input.data(),count*sizeof(float),cudaMemcpyHostToDevice,stream));
                if(frame&1)smb_cuda_check(cudaGraphLaunch(executable,stream));else puf_step(envs);
                smb_cuda_check(cudaStreamSynchronize(stream));
                smb_cuda_check(cudaMemcpy(gpu.data(),envs,count*sizeof(Env),cudaMemcpyDeviceToHost));
                smb_cuda_check(cudaMemcpy(actual.data(),fpt_states,count*sizeof(SmbLogic),cudaMemcpyDeviceToHost));
                smb_cuda_check(cudaMemcpy(got_reward.data(),rewards,count*sizeof(float),cudaMemcpyDeviceToHost));
                smb_cuda_check(cudaMemcpy(got_terminal.data(),terminals,count*sizeof(float),cudaMemcpyDeviceToHost));
                smb_cuda_check(cudaMemcpy(observations.data(),obs,observations.size()*sizeof(float),cudaMemcpyDeviceToHost));
                for(int i=0;i<count;i++) {
                    fpt_same_state(states[i],actual[i]);const auto& a=cpu[i];const auto& b=gpu[i];
                    fpt_check(a.is_fpg==b.is_fpg&&!memcmp(&a.curriculum,&b.curriculum,sizeof(a.curriculum))&&!memcmp(&a.game,&b.game,sizeof(a.game)),"mode/controller state mismatch");
                    fpt_check(a.is_practice==b.is_practice&&!memcmp(&a.practice,&b.practice,sizeof(a.practice)),"practice controller mismatch");
                    fpt_check(!memcmp(&a.episode,&b.episode,sizeof(a.episode)),"episode state mismatch");
                    fpt_check(fpt_near(expected_reward[i],got_reward[i])&&expected_terminal[i]==got_terminal[i],"reward/terminal mismatch");
                    for(size_t j=0;j<sizeof(Log)/sizeof(float);j++)fpt_check(fpt_near(((const float*)&a.log)[j],((const float*)&b.log)[j]),"log mismatch");
                    for(int k=0;k<FPT_OBS;k++)fpt_check(std::isfinite(observations[i*FPT_OBS+k])&&observations[i*FPT_OBS+k]==expected_obs[i*FPT_OBS+k],"semantic observation mismatch");
                    decisions++;
                }
            }
            if(strcmp(mode,"fpg")) {
                float checkpoints=0;for(const auto& e:cpu)checkpoints+=e.log.game_checkpoints;
                fpt_check(checkpoints>0,"game actors did not exercise checkpoint rewards");
            }
            if(!strcmp(mode,"mixed")||practice) {
                const char* path="build/mario_sim/mixed_test_checkpoint.bin";
                {std::ofstream out(path);out<<"mixed test";}
                fpt_gpu_save(path,nullptr);auto saved=fpt_read_progress(path);
                fpt_check(saved.progress.size()==32,"checkpoint included full-game actors");
                fpt_gpu_load(path,&ini);smb_cuda_check(cudaMemcpy(gpu.data(),envs,count*sizeof(Env),cudaMemcpyDeviceToHost));
                for(int i=0;i<(practice?64:96);i++)fpt_check(!memcmp(&gpu[i].episode,&cpu[i].episode,sizeof(SmbEpisode))&&!memcmp(&gpu[i].observation_history,&cpu[i].observation_history,sizeof(FptObservationHistory)),"curriculum restore changed a game actor");
                if(practice) {
                    auto saved_practice=smb_read_practice(path,h.practice_bank_hash);
                    fpt_check(saved_practice.size()==32,"practice checkpoint included other actors");
                    smb_cuda_check(cudaMemcpy(got_reward.data(),rewards,count*sizeof(float),cudaMemcpyDeviceToHost));
                    smb_cuda_check(cudaMemcpy(got_terminal.data(),terminals,count*sizeof(float),cudaMemcpyDeviceToHost));
                    smb_cuda_check(cudaMemcpy(actual.data(),fpt_states,count*sizeof(SmbLogic),cudaMemcpyDeviceToHost));
                    smb_cuda_check(cudaMemcpy(observations.data(),obs,observations.size()*sizeof(float),cudaMemcpyDeviceToHost));
                    for(int i=64;i<96;i++) {
                        fpt_check(!memcmp(gpu[i].practice.level,cpu[i].practice.level,128),"practice mastery did not restore");
                        fpt_check(got_reward[i]==0&&got_terminal[i]==0,"practice restore left stale reward/terminal");
                        FptObservationHistory history={};float expected[FPT_OBS];
                        fpt_observe(&actual[i],h.bank->worlds.data(),&history,expected);
                        for(int k=0;k<FPT_OBS;k++)fpt_check(observations[i*FPT_OBS+k]==expected[k],"practice resume observation mismatch");
                    }
                }
                for(int i=96;i<count;i++)fpt_check(gpu[i].curriculum.level==cpu[i].curriculum.level&&gpu[i].curriculum.attempts==cpu[i].curriculum.attempts,"mixed progress did not restore");
                // The previous FPG-only checkpoint remains a valid warm start.
                fpt_test_old_progress("build/mario_sim/fpg_only_test_runtime.bin",fpt_host->bank->header.payload_hash,fpt_host->curriculum);
                fpt_gpu_load("build/mario_sim/fpg_only_test_runtime.bin",&ini);
                smb_cuda_check(cudaMemcpy(gpu.data(),envs,count*sizeof(Env),cudaMemcpyDeviceToHost));
                for(int i=96;i<count;i++)fpt_check(h.curriculum.depths[gpu[i].curriculum.level]==96,"legacy semantic frontier did not restore");
            }
            fprintf(stderr,"mode=%s actors=%d FPG=%d practice=%d parity=passed\n",mode,count,fpg_actors,practice_actors);
            smb_cuda_check(cudaGraphExecDestroy(executable));smb_cuda_check(cudaGraphDestroy(graph));puf_close(envs);
            cudaStreamDestroy(stream);cudaFree(obs);cudaFree(actions);cudaFree(rewards);cudaFree(terminals);puf_ini_free(&ini);
        }
        teacher_case(argc,argv,&decisions);teacher_case(argc,argv,&decisions,true);
        printf("{\"decisions\":%u,\"resets\":%u,\"stages\":32,\"modes\":4,\"practice_checkpoint_resume\":true,\"teacher\":true,\"failures\":0}\n",decisions,resets);return 0;
    }catch(const std::exception& e){fprintf(stderr,"unified CUDA test: %s\n",e.what());return 1;}
}
