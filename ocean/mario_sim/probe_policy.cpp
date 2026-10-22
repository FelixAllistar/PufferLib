// Bounded input-sensitivity diagnostic. Natural reset states and actual policy
// rollouts supply context; counterfactual observations never alter the world.
// Sensitivity is not an assertion that an action is correct or a level solved.
#define SMB_HEADLESS
#include "mario_sim.h"
#include "policy.h"
#include <array>
#include <iomanip>

using Observation=std::array<float,FPT_OBS>;
using Probabilities=std::array<double,64>;
static void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static Probabilities probabilities(const float* logits) {
    Probabilities p;double maximum=logits[0],sum=0;
    for(int a=0;a<64;a++){require(std::isfinite(logits[a]),"nonfinite policy logits");maximum=std::max(maximum,(double)logits[a]);}
    for(int a=0;a<64;a++){p[a]=exp(logits[a]-maximum);sum+=p[a];}
    for(auto& v:p)v/=sum;return p;
}
static double button(const Probabilities& p,int bit) {
    double sum=0;for(int a=0;a<64;a++)if(smb_action_buttons(a)&bit)sum+=p[a];return sum;
}
static int greedy(const Probabilities& p) {return std::max_element(p.begin(),p.end())-p.begin();}
static int sample(const Probabilities& p,uint32_t* rng) {
    double draw=((double)smb_random(rng)+.5)/4294967296.0;
    for(int a=0;a<64;a++){draw-=p[a];if(draw<=0)return a;}return 63;
}
static const char* probes[]={"unchanged_control","nearby_solids_removed","entities_hidden",
    "entity_motion_hidden","water_cues_flipped","player_velocity_zero","entities_shifted_16px",
    "platform_motion_reversed"};
static bool intervene(Observation& o,int probe) {
    auto before=o;
    if(probe==1)for(int cell=0;cell<FPT_GRID_CELLS;cell++) {
        float dx=o[FPT_P_GRID_DX]+(cell%FPT_GRID_W*16+8)/256.f;
        if(fabsf(dx)<.25f&&fpt_terrain_feature((int)o[FPT_TERRAIN_OFFSET+cell],1,cell,0,0,0)>0)
            o[FPT_TERRAIN_OFFSET+cell]=0;
    }
    if(probe==2)std::fill(o.begin()+FPT_ENTITY_OFFSET,o.end(),0);
    if(probe==3||probe==6||probe==7)for(int k=0;k<FPT_ENTITY_COUNT;k++) {
        float* e=o.data()+FPT_ENTITY_OFFSET+k*FPT_ENTITY_FEATURES;if(!e[FPT_E_TYPE])continue;
        if(probe==3)e[FPT_E_VX]=e[FPT_E_VY]=e[FPT_E_MOTION_KNOWN]=0;
        if(probe==6)e[FPT_E_DX]+=16.f/256;
        int id=(int)e[FPT_E_TYPE]-1;
        if(probe==7&&id>=0x24&&id<=0x2c){e[FPT_E_VX]*=-1;e[FPT_E_VY]*=-1;}
    }
    if(probe==4){o[24]=1-o[24];o[61]=1-o[61];}
    if(probe==5)o[0]=o[1]=0;
    return probe==0||before!=o;
}
int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::runtime_error("usage: probe_policy CONFIG MODEL OUTPUT.csv");
        require(!std::filesystem::exists(argv[3]),"probe output already exists");
        Ini ini={};puf_ini_load_file(&ini,argv[1]);
        int hidden=(int)puf_ini_get(&ini,"policy","hidden_size"),layers=(int)puf_ini_get(&ini,"policy","num_layers");
        void* policy=fpt_policy_load(argv[2],hidden,layers);
        void* anchor=fpt_policy_load(argv[2],hidden,layers);void* branch=fpt_policy_load(argv[2],hidden,layers);
        require(policy&&anchor&&branch,"cannot load semantic policy");
        std::ofstream out(argv[3]);out<<std::setprecision(9);
        out<<"source,scene,stage,frame,x,y,water,swimming,entities,moving_platforms,loop_command,loop_correct,loop_pass,probe,tv,greedy_changed,jump_probability,jump_delta,right_probability,right_delta\n";
        unsigned frames=0,rows=0,episodes=0,controls=0;
        for(const char* name:{"roots","practice"}) {
            bool roots=!strcmp(name,"roots");
            SmbBank bank(roots?"build/mario_sim/runtime/generated/bank.bin":"build/mario_sim/practice/bank.bin");
            bool seen[32]={};
            for(size_t index=0;index<bank.entries.size();index++) {
                const auto& entry=bank.entries[index];
                if(roots&&(!(entry.flags&SMB_BANK_STAGE_START)||(entry.flags&SMB_BANK_CONSTRUCTED)||seen[entry.stage]))continue;
                seen[entry.stage]=true;SmbLogic state;require(!smb_scene_reset(&state,&entry.scene),"reset failed");
                FptObservationHistory history={};fpt_policy_reset(policy);uint32_t rng=smb_seed(137,index+1);episodes++;
                for(int frame=0;frame<(roots?256:128);frame++) {
                    const auto* m=state.ram;
                    if(m[0x770]!=1||m[0xe]==11||m[0x75f]*4+m[0x75c]!=(int)entry.stage)break;
                    Observation obs;fpt_observe(&state,bank.worlds.data(),&history,obs.data());
                    require(fpt_policy_copy_state(anchor,policy),"cannot preserve recurrent carry");
                    auto baseline=probabilities(fpt_policy_logits(policy,obs.data()));
                    if(frame%16==0&&m[0xe]==8) {
                        int entities=0,platforms=0;
                        for(int k=0;k<FPT_ENTITY_COUNT;k++) {
                            auto* e=obs.data()+FPT_ENTITY_OFFSET+k*FPT_ENTITY_FEATURES;
                            entities+=e[0]!=0;int id=(int)e[0]-1;
                            platforms+=id>=0x24&&id<=0x2c&&e[FPT_E_MOTION_KNOWN]
                                &&(e[FPT_E_VX]!=0||e[FPT_E_VY]!=0);
                        }
                        for(int probe=0;probe<8;probe++) {
                            auto changed=obs;if(!intervene(changed,probe))continue;
                            require(fpt_policy_copy_state(branch,anchor),"counterfactual carry mismatch");
                            auto altered=probabilities(fpt_policy_logits(branch,changed.data()));double tv=0;
                            for(int a=0;a<64;a++)tv+=.5*fabs(baseline[a]-altered[a]);
                            if(probe==0){require(tv==0,"unchanged input with copied carry changed policy");controls++;}
                            out<<name<<','<<index<<','<<entry.stage<<','<<frame<<','<<fpt_x(m,0)<<','<<fpt_y(m,0)
                                <<','<<obs[61]<<','<<obs[24]<<','<<entities<<','<<platforms<<','<<(int)m[0x745]
                                <<','<<(int)m[0x6d9]<<','<<(int)m[0x6da]<<','<<probes[probe]<<','<<tv
                                <<','<<(greedy(baseline)!=greedy(altered))<<','<<button(baseline,128)
                                <<','<<button(altered,128)-button(baseline,128)<<','<<button(baseline,1)
                                <<','<<button(altered,1)-button(baseline,1)<<'\n';rows++;
                        }
                    }
                    require(!smb_native_frame(&state,bank.worlds.data(),smb_action_buttons(sample(baseline,&rng))),"native frame failed");frames++;
                }
            }
        }
        out.close();require(bool(out),"cannot write probe output");
        fpt_policy_free(policy);fpt_policy_free(anchor);fpt_policy_free(branch);puf_ini_free(&ini);
        printf("{\"natural_starts\":%u,\"rollout_frames\":%u,\"rows\":%u,\"identical_carry_controls\":%u,\"precision\":\"fp32\",\"causal_correctness_test\":false}\n",episodes,frames,rows,controls);return 0;
    }catch(const std::exception& e){fprintf(stderr,"policy probe: %s\n",e.what());return 1;}
}
