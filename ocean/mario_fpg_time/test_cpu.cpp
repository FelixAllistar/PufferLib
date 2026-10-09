#define SMB_HEADLESS
#include "mario_fpg_time.h"
#include "test_common.h"
#include <functional>

static void reward_controls() {
    FpgTimeConfig c={0.5f,1,8,1,2.5f,24,FPT_PIPE_POLE_X};
    fpt_check(fpg_time_score(50,100,&c)==1&&fpg_time_score(100,100,&c)==1,"fast score must saturate at one");
    fpt_check(fpg_time_score(200,100,&c)==0.5f&&fpg_time_score(400,100,&c)==0.25f,"slower finishes must lose bonus");
    c.power=2;fpt_check(fpg_time_score(200,100,&c)==0.25f,"power sweep control ignored");c.power=1;
    SmbLogic s={};SmbEpisode e={};SmbTaskConfig task={73,15,1800,SMB_TASK_FPG,-1,16};
    e.frames=239;fpt_check(fpg_time_after_frame(&s,&e,&task,100,&c)==0&&e.status==SMB_EPISODE_ACTIVE,"old 240-frame timeout remains");
    e.frames=1799;fpt_check(fpg_time_after_frame(&s,&e,&task,100,&c)==0&&e.status==SMB_EPISODE_TIMEOUT,"1800-frame timeout missing");
    s.ram[0xe]=5;s.ram[0x70f]=164;s.ram[0xd4]=48;e={};e.frames=199;
    fpt_check(fpg_time_after_frame(&s,&e,&task,100,&c)==1.25f&&e.episode_return==1.25f,"success reward formula wrong");
    c.bonus=0;e={};e.frames=199;fpt_check(fpg_time_after_frame(&s,&e,&task,100,&c)==1&&e.episode_return==1,"zero-bonus ablation failed");
    c.bonus=0.5f;s.ram[0x70f]=80;e={};fpt_check(fpg_time_after_frame(&s,&e,&task,100,&c)==0&&e.status==SMB_EPISODE_NORMAL_FLAG,"normal flag received reward");
    s.ram[0xe]=11;e={};fpt_check(fpg_time_after_frame(&s,&e,&task,100,&c)==0&&e.status==SMB_EPISODE_DEAD,"death received reward");
    s={};s.ram[0x6d]=10;s.ram[0x86]=0;s.ram[0xf]=1;s.ram[0x16]=0x30;s.ram[0x6e]=10;s.ram[0x87]=100;
    fpt_check(fpg_time_target(&s,0,nullptr,&c)==72,"distance fallback incorrect");
    s={};s.ram[0x6d]=10;s.ram[0x86]=56;
    fpt_check(fpt_near(fpg_time_target(&s,0,nullptr,&c),252.8f),"offscreen 1-1 pole fallback incorrect");
}
static unsigned table_controls(const SmbBank& bank) {
    const char* source="build/mario_fpg_time/pipe_targets.bin",*cpu="build/mario_sim/runtime/logic_cpu.a",*gpu="build/mario_sim/runtime/cuda_replay.cubin";
    FpgTimeTable valid(source,bank,cpu,gpu);std::ifstream in(source,std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    auto reject=[&](const std::function<void(std::vector<char>&)>& mutate,int id) {
        auto data=bytes;mutate(data);std::string path="build/mario_fpg_time/bad_table_"+std::to_string(id)+".bin";
        std::ofstream out(path,std::ios::binary);out.write(data.data(),data.size());out.close();bool caught=false;
        try{FpgTimeTable bad(path.c_str(),bank,cpu,gpu);}catch(const std::exception&){caught=true;}
        fpt_check(caught,"invalid timing table accepted");
    };
    reject([](auto& b){b.pop_back();},0);
    reject([](auto& b){b.push_back(0);},1);
    reject([](auto& b){b.back()^=1;},2);
    reject([](auto& b){b[offsetof(FpgTimeHeader,bank_hash)]^=1;},3);
    reject([](auto& b){b[offsetof(FpgTimeHeader,cuda_hash)]^=1;},4);
    reject([](auto& b){b[offsetof(FpgTimeHeader,version)]^=1;},5);
    return 6;
}
int main() {
    try {
        reward_controls();Dict options={};
        Env env={};float observation[FPT_OBS],action=0,reward=0,terminal=0;
        env.agents[0].observations=observation;env.agents[0].actions=&action;env.agents[0].rewards=&reward;env.agents[0].terminals=&terminal;
        puf_init(&env,&options);fpt_check(fpt_host->task.max_frames==1800,"wrong default episode budget");
        unsigned rejected=table_controls(*fpt_host->bank);auto teachers=fpt_test_teachers();unsigned successes=0,frames=0;
        for(float bonus:{0.0f,0.5f}) {
            fpt_host->time.bonus=bonus;
            for(const auto& t:teachers) {
                fpt_check(!smb_scene_reset(env.state,&t.item.scene),"CPU teacher reset failed");env.episode={};
                env.episode.rng=73;env.episode.scene=t.item.seed;env.table_hit=1;
                env.target=fpg_time_target(env.state,env.episode.scene,fpt_host->table.data(),&fpt_host->time);float target=env.target;
                for(size_t i=0;i<t.frames.size();i++) {
                    int buttons=t.frames[i].buttons;action=(float)((buttons&3)|((buttons>>2)&60));puf_step(&env);frames++;
                    fpt_check(terminal==(float)(i+1==t.frames.size()),"CPU adapter ended teacher early");
                    if(terminal) {
                        float expected=1+bonus*powf(fminf(1,target/(float)t.frames.size()),fpt_host->time.power);
                        fpt_check(fpt_near(reward,expected),"CPU adapter timing reward wrong");successes++;
                    }else fpt_check(reward==0,"CPU adapter paid nonterminal reward");
                    float expected_obs[FPT_OBS];auto history=env.observation_history;
                    fpt_observe(env.state,fpt_host->bank->worlds.data(),&history,expected_obs);
                    fpt_check(!memcmp(observation,expected_obs,sizeof(observation)),"CPU semantic observation mismatch");
                }
            }
        }
        // Compare reset streams directly: enabling the estimate consumes no RNG
        // and selects exactly the same worlds/templates/generated native states.
        SmbLogic baseline;SmbEpisode episode={};episode.rng=env.episode.rng;unsigned reset_checks=4096;
        for(unsigned i=0;i<reset_checks;i++) {
            smb_task_reset(&baseline,&episode,&fpt_host->task,fpt_host->bank->entries.data(),fpt_host->eligible.data(),(int)fpt_host->eligible.size());
            puf_reset(&env);fpt_same_state(baseline,*env.state);
            fpt_check(fpt_is_pipe_start(env.state),"near-flag, airborne or invalid pipe reset selected");
            fpt_check(episode.scene==env.episode.scene&&episode.world==env.episode.world&&episode.rng==env.episode.rng,"timing estimate changed reset randomization");
        }
        puf_close(&env);dict_clear(&options);
        // The old bank and synthetic reset mutations cannot silently reintroduce
        // the near-flag starts that motivated this experiment.
        unsigned start_guards=0;
        for(int bad=0;bad<2;bad++) {
            Dict invalid={};if(!bad)dict_set_str(&invalid,"reset_bank","build/mario_sim/runtime/generated/bank.bin");
            else dict_set(&invalid,"generation_knobs",15);
            bool rejected=false;try{fpt_setup(&invalid);}catch(const std::exception&){rejected=true;}
            fpt_check(rejected,"out-of-scope reset configuration accepted");dict_clear(&invalid);start_guards++;
        }
        SmbBank altered("build/mario_fpg_time/pipe/bank.bin");altered.worlds[0]^=1;
        bool world_rejected=false;try{fpt_require_pipe_bank(altered);}catch(const std::exception&){world_rejected=true;}
        fpt_check(world_rejected,"modified ROM world data accepted");start_guards++;
        // Cache-free operation must remain a supported, complete environment.
        Dict fallback={};dict_set_str(&fallback,"time_table","None");env={};puf_init(&env,&fallback);puf_reset(&env);
        fpt_check(env.table_hit==0&&env.target>=1,"cache-free fallback failed");puf_close(&env);dict_clear(&fallback);
        printf("{\"teacher_successes\":%u,\"teacher_frames\":%u,\"reset_stream_comparisons\":%u,\"corrupt_tables_rejected\":%u,\"start_guards\":%u,\"timeout_frames\":1800,\"zero_bonus_ablation\":true,\"cache_free_fallback\":true,\"failures\":0}\n",
            successes,frames,reset_checks,rejected,start_guards);return 0;
    }catch(const std::exception& e){fprintf(stderr,"FPG timing CPU test: %s\n",e.what());return 1;}
}
