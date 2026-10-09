// Independent ROM and native rollouts from one initial copy of state/geometry.
// The native engine never consumes later ROM state. Geometry is reconstructed
// separately from conservative, already-parsed columns on a reference traversal.
#include "../retro/retro.h"
#include "fpg_rom.h"
#include "fpg_task.h"
#include "fpg_config.h"
#include "policy.h"
#include "parity_format.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

static void advance(Nes_Emu& emu,int action) {
    retro_bind_pixels(&emu);
    retro_check(emu.emulate_frame(fpg_rom_mask(fpg_buttons(action))));
    if(emu.error_count())throw std::runtime_error("emulator opcode error");
}
static std::vector<int> read_tape(const char* path,bool ml) {
    std::ifstream f(path);if(!f)throw std::runtime_error("missing action tape");
    std::vector<int> out;int a;
    while(f>>a) {
        if(!ml){if(a<0||a>=12)throw std::runtime_error("bad FPG action");out.push_back(a);continue;}
        if(a<0||a>=64)throw std::runtime_error("bad Mario Lab action");
        int buttons=fpg_from_ml(a),converted=-1;
        for(int k=0;k<12;k++)if(fpg_buttons(k)==buttons)converted=k;
        // Prefix replay includes pipe-direction buttons outside this task.
        out.push_back(converted<0?-(buttons+1):converted);
    }
    return out;
}
static void prefix_step(Nes_Emu& e,int a) {
    if(a>=0){advance(e,a);return;}
    retro_bind_pixels(&e);retro_check(e.emulate_frame(fpg_rom_mask(-a-1)));
    if(e.error_count())throw std::runtime_error("emulator opcode error");
}
static bool in_scope(const unsigned char* m) {
    auto b=fpg_rom_body(m);
    return m[0x74e]==1 && m[0x754]==1 && m[0x747]==0 && m[0x772]==3
        && b.routine==8 && b.x>=2912 && b.x<3180 && b.y>=0 && b.y<208;
}
struct Start {
    std::unique_ptr<Nes_State> rom;
    std::vector<int> teacher;
    int frame;
    std::string source;
};
struct Fixture {
    FpgWorld world={};
    int raw[FPG_ROWS*FPG_COLS]={};
    int samples=0,changed_cells=0;
    Fixture(){world.origin_col=168;world.pole_col=198;}
    void capture(const unsigned char* m) {
        if(m[0x74e]!=1)return;
        // Independent of fpg_rom_last_column: always exclude BOTH frontier
        // columns. Every included column is resident at every parser phase.
        int cursor=m[0x725]*16+m[0x726];
        for(int col=cursor-30;col<=cursor-2;col++) {
            int local=col-world.origin_col;
            if(local<0||local>=FPG_COLS)continue;
            for(int row=2;row<15;row++) {
                int value=m[0x500+((col&16)?0xd0:0)+(row-2)*16+(col&15)];
                if(world.known[local]&&raw[row*FPG_COLS+local]!=value)changed_cells++;
                raw[row*FPG_COLS+local]=value;
                fpg_put(&world,col,row,fpg_rom_collision_tile(value));
            }
            world.known[local]=1;samples++;
        }
    }
};
static std::string body_difference(const FpgBody& a,const FpgBody& b) {
    std::ostringstream out;
#define FIELD(n) if(a.n!=b.n)out<<#n<<":"<<a.n<<"/"<<b.n<<" ";
    FIELD(x);FIELD(y);FIELD(vx);FIELD(vy);FIELD(xsub);FIELD(ysub);FIELD(ax);FIELD(vyfrac);
    FIELD(motion);FIELD(facing);FIELD(moving);FIELD(abs_vx);FIELD(running);FIELD(run_timer);
    FIELD(gravity);FIELD(fall_gravity);FIELD(jump_y);FIELD(previous_ab);FIELD(collision);
    FIELD(side_timer);FIELD(routine);FIELD(flag_y);FIELD(flag_fraction);FIELD(grab_y);
#undef FIELD
    return out.str();
}
static FpgState rom_state(Nes_Emu& e,const FpgConfig& cfg,int tick) {
    const auto* m=e.low_mem();FpgState s={};s.body=fpg_rom_body(m);
    fpg_rom_world_version(m,&s.world,cfg.contract_version);
    if(cfg.contract_version>=2){s.camera=fpg_rom_camera(m);s.actors=fpg_rom_actors(m);}
    s.tick=tick;s.status=fpg_outcome(&s.body);
    if(!s.status&&tick>=cfg.max_frames)s.status=FPG_TIMEOUT;
    return s;
}
static void observe(const FpgState& s,const FpgConfig& cfg,float* obs) {
    for(int i=0;i<FPG_OBS;i++)obs[i]=fpg_observation(&s,&cfg,i);
}
static int different(const float* a,const float* b,int n) {
    for(int i=0;i<n;i++)if(a[i]!=b[i])return i;return -1;
}
static int argmax(const float* logits) {
    int best=0;for(int i=1;i<12;i++)if(logits[i]>logits[best])best=i;return best;
}
static std::string observation_difference(int i,const float* a,const float* b,const FpgState& s) {
    std::ostringstream out;out<<"obs["<<i<<"]="<<a[i]<<"/"<<b[i];
    if(i>=FPG_EGO) {
        int cell=(i-FPG_EGO)/2;
        out<<" col="<<fpg_floor16(s.body.x)-4+cell%FPG_GRID_W
           <<" row="<<fpg_floor16(s.body.y+32)-8+cell/FPG_GRID_W;
    }
    return out.str();
}
int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::runtime_error("usage: parity MODEL CONFIG OUTPUT_DIRECTORY");
        Ini ini={};puf_ini_load_file(&ini,argv[2]);FpgConfig cfg=fpg_config(puf_ini_section(&ini,"env",0));
        int hidden=(int)puf_ini_get(&ini,"policy","hidden_size"),layers=(int)puf_ini_get(&ini,"policy","num_layers");
        void* native_policy=fpg_policy_load(argv[1],hidden,layers);
        void* real_policy=fpg_policy_load(argv[1],hidden,layers);
        if(!native_policy||!real_policy)throw std::runtime_error("invalid policy");
        std::filesystem::create_directories(argv[3]);std::string directory=argv[3];
        std::ofstream cases(directory+"/cases.jsonl"),trace(directory+"/rom_traces.bin",std::ios::binary);
        if(!cases||!trace)throw std::runtime_error("cannot open output files");
        // An interrupted, filtered, or failed run must never look like a
        // qualified CUDA input just because its passing prefix was exported.
        FpgParityHeader header={0,1,sizeof(FpgParityCase),sizeof(FpgParityFrame),0,cfg};
        trace.write((char*)&header,sizeof(header));
        RetroRom& rom=retro_rom();retro_load_rom_locked(rom,RETRO_SMB1_ROM_PATH);retro_prepare_start_locked(rom,1,1);
        Nes_Emu emu;retro_check(emu.set_cart(&rom.cart,&rom.seed));
        Fixture fixture;std::vector<Start> starts;
        auto prefix=read_tape("ocean/mario_fpg/reference/learned_pipe_route.actions",true);
        auto suffix=read_tape("ocean/mario_fpg/reference/fpg_suffix.actions",false);
        auto capture=[&](const std::vector<int>& tape,const std::string& name,int start_frame,int stop_frame) {
            emu.load_state(rom.starts[0]->state);
            for(int t=0;t<(int)tape.size();t++) {
                const auto* m=emu.low_mem();auto b=fpg_rom_body(m);
                if(m[0x74e]==1&&b.x>=2688)fixture.capture(m);
                if(t>=start_frame&&t<stop_frame&&in_scope(m)) {
                    Start s;s.rom=std::make_unique<Nes_State>();emu.save_state(s.rom.get());
                    s.frame=t;s.source=name;s.teacher.assign(tape.begin()+t,tape.end());starts.push_back(std::move(s));
                }
                prefix_step(emu,tape[t]);
                FpgBody after=fpg_rom_body(emu.low_mem());
                if(b.routine==8 && fpg_outcome(&after)!=FPG_ACTIVE)break;
            }
        };
        capture(prefix,"learned_pipe_route",0,(int)prefix.size());
        for(int episode:{7,26,41}) {
            std::string path="ocean/mario_fpg/reference/routes/episode_"+std::to_string(episode)+".actions";
            auto tape=read_tape(path.c_str(),true);
            capture(tape,"learned_route_"+std::to_string(episode),0,(int)tape.size());
        }
        std::vector<int> searched(prefix.begin(),prefix.begin()+1510);searched.insert(searched.end(),suffix.begin(),suffix.end());
        capture(searched,"searched_fpg",1510,(int)searched.size());
        if(starts.empty())throw std::runtime_error("no qualified reference starts");
        std::ofstream terrain(directory+"/terrain.txt");
        for(int row=0;row<FPG_ROWS;row++){for(int col=0;col<FPG_COLS;col++)terrain<<(fixture.world.known[col]?".#|"[fixture.world.tiles[row*FPG_COLS+col]]:'?');terrain<<'\n';}
        long frames=0,body_errors=0,obs_errors=0,camera_errors=0,policy_errors=0,geometry_errors=0,actor_errors=0;
        int total=0,failed=0,skipped=0,successes=0,ordinary=0,timeouts=0,boundary_trials=0;
        // Every reference frame branches through all 12 actions. Every 16th
        // frame additionally starts persistent trajectories, with no resync.
        for(size_t si=0;si<starts.size();si++) for(int kind=0;kind<32;kind++) {
            const char* filter=getenv("FPG_PARITY_SOURCE");if(filter&&starts[si].source!=filter)continue;
            if(kind>=12 && si%16 && !(starts[si].source=="searched_fpg"&&starts[si].frame==1510))continue;
            const Start& start=starts[si];emu.load_state(*start.rom);retro_bind_pixels(&emu);
            FpgState native=rom_state(emu,cfg,0);native.world=fixture.world;
            FpgParityCase run={native,0,start.frame,kind};std::vector<FpgParityFrame> recorded;
            fpg_policy_reset(native_policy);fpg_policy_reset(real_policy);
            uint32_t rng=fpg_hash((uint32_t)si*91u+(uint32_t)kind+711u)|1u;
            std::vector<int> actions;std::string error,category;int bad_frame=-1;bool boundary=false;
            int limit=kind<12?1:cfg.max_frames;
            for(int t=0;t<limit;t++) {
                FpgState actual=rom_state(emu,cfg,t);float no[FPG_OBS],ro[FPG_OBS],nl[12],rl[12];
                observe(native,cfg,no);observe(actual,cfg,ro);
                int oi=different(no,ro,FPG_OBS);
                if(oi>=0){error=observation_difference(oi,no,ro,native);category="observation";obs_errors++;bad_frame=t;break;}
                fpg_policy_logits(native_policy,no,nl);fpg_policy_logits(real_policy,ro,rl);
                if(different(nl,rl,12)>=0){error="policy logits disagree";category="policy";policy_errors++;bad_frame=t;break;}
                // Compare independently captured static geometry to live RAM,
                // including every on-screen row, not just collision probes.
                int left=fpg_floor16(actual.camera.left),right=fpg_floor16(actual.camera.left+255);
                if(cfg.contract_version<2){left=fpg_floor16(fpg_rom_camera(emu.low_mem()).left);right=left+15;}
                for(int col=left;col<=right&&error.empty();col++)for(int row=2;row<15;row++) {
                    int raw=fpg_rom_raw_tile(emu.low_mem(),col,row);
                    if(raw<0 || !fpg_column_known(&fixture.world,col)
                        || fpg_tile(&fixture.world,col*16,row*16)!=fpg_rom_collision_tile(raw)) {
                        error="geometry col="+std::to_string(col)+" row="+std::to_string(row);
                        category="geometry";geometry_errors++;bad_frame=t;break;
                    }
                }
                if(!error.empty())break;
                int action;
                if(kind<12)action=kind;
                else if(kind==12) {
                    if(t>=(int)start.teacher.size()||start.teacher[t]<0)break;
                    action=start.teacher[t];
                } else if(kind==13)action=argmax(nl);
                else if(kind<26)action=kind-14;
                else if(kind==26)action=t<100?8:7;
                else if(kind==27)action=t%36<18?10:7;
                else {
                    int hold=kind==28?1:kind==29?4:kind==30?8:16;
                    action=t%hold?actions.back():(int)(fpg_rand(&rng)%12);
                }
                actions.push_back(action);fpg_step_task(&native,&cfg,action);advance(emu,action);frames++;
                actual=rom_state(emu,cfg,t+1);
                boundary=boundary||actual.body.x<=fpg_rom_camera(emu.low_mem()).left+1;
                error=body_difference(native.body,actual.body);
                if(!error.empty()) {
                    error+="at=("+std::to_string(actual.body.x)+","+std::to_string(actual.body.y)+") ";
                    const auto* m=emu.low_mem();
                    for(int k=0;k<5;k++)if(m[0xf + k]) {
                        std::ostringstream e;e<<"enemy["<<k<<"] type="<<(int)m[0x16 + k]<<" state="<<(int)m[0x1e + k]
                            <<" pos="<<m[0x6e + k]*256+m[0x87 + k]<<","<<((int)m[0xb6 + k]-1)*256+m[0xcf + k]
                            <<" speed="<<(int)(int8_t)m[0x58 + k]<<","<<(int)(int8_t)m[0xa0 + k]
                            <<" bbox="<<(int)m[0x4b0+k*4]<<","<<(int)m[0x4b1+k*4]<<","<<(int)m[0x4b2+k*4]<<","<<(int)m[0x4b3+k*4]
                            <<" off="<<(int)m[0x3d8+k]<<" ";error+=e.str();
                    }
                    error+=" left="+std::to_string(actual.camera.left);
                    category="body";body_errors++;bad_frame=t+1;break;
                }
                if(cfg.contract_version>=2&&memcmp(&native.camera,&actual.camera,sizeof(FpgCamera))) {
                    std::ostringstream s;s<<"camera left="<<native.camera.left<<"/"<<actual.camera.left
                        <<" relative="<<native.camera.relative_x<<"/"<<actual.camera.relative_x
                        <<" locked="<<native.camera.locked<<"/"<<actual.camera.locked
                        <<" parsed="<<native.camera.parsed_col<<"/"<<actual.camera.parsed_col
                        <<" task="<<native.camera.parser_task<<"/"<<actual.camera.parser_task
                        <<" scroll32="<<native.camera.scroll32<<"/"<<actual.camera.scroll32;
                    error=s.str();category="camera";camera_errors++;bad_frame=t+1;break;
                }
                if(cfg.contract_version>=2&&memcmp(&native.actors,&actual.actors,sizeof(FpgActors))) {
                    const int* na=(const int*)&native.actors;const int* ra=(const int*)&actual.actors;
                    std::ostringstream s;s<<"actors ";
                    for(int j=0;j<(int)(sizeof(FpgActors)/sizeof(int));j++)if(na[j]!=ra[j])s<<j<<":"<<na[j]<<"/"<<ra[j]<<" ";
                    error=s.str();category="actors";actor_errors++;bad_frame=t+1;break;
                }
                observe(native,cfg,no);observe(actual,cfg,ro);oi=different(no,ro,FPG_OBS);
                if(oi>=0){error=observation_difference(oi,no,ro,native);category="observation";obs_errors++;bad_frame=t+1;break;}
                if(native.status!=actual.status)throw std::runtime_error("outcome mismatch");
                FpgParityFrame f={};f.action=action;f.status=actual.status;f.body=actual.body;f.camera=actual.camera;f.actors=actual.actors;
                memcpy(f.observations,ro,sizeof(ro));recorded.push_back(f);
                if(native.status)break;
            }
            bool skip=error.empty()&&recorded.empty();
            if(skip)skipped++;else total++;
            boundary_trials+=boundary;
            if(!error.empty()){failed++;if(failed<=20)fprintf(stderr,"%s frame=%d kind=%d t=%d %s\n",start.source.c_str(),start.frame,kind,bad_frame,error.c_str());}
            else {
                successes+=native.status==FPG_SUCCESS;ordinary+=native.status==FPG_NORMAL_FLAG;timeouts+=native.status==FPG_TIMEOUT;
                if(!recorded.empty()) {
                    run.frames=(int)recorded.size();trace.write((char*)&run,sizeof(run));
                    trace.write((char*)recorded.data(),recorded.size()*sizeof(FpgParityFrame));header.cases++;
                }
            }
            cases<<"{\"source\":\""<<start.source<<"\",\"start_frame\":"<<start.frame<<",\"kind\":"<<kind
                 <<",\"frames\":"<<actions.size()<<",\"status\":"<<native.status<<",\"skipped\":"<<(skip?"true":"false")
                 <<",\"passed\":"<<(skip?"null":error.empty()?"true":"false")
                 <<",\"first_mismatch_frame\":"<<bad_frame<<",\"category\":\""<<category<<"\",\"detail\":\""<<error<<"\",\"actions\":[";
            for(size_t i=0;i<actions.size();i++)cases<<(i?",":"")<<actions[i];cases<<"]}\n";
        }
        header.magic=!failed&&!getenv("FPG_PARITY_SOURCE")?FPG_PARITY_MAGIC:0;
        trace.seekp(0);trace.write((char*)&header,sizeof(header));trace.close();
        std::ostringstream summary;summary<<"{\"contract_version\":"<<cfg.contract_version<<",\"reference_starts\":"<<starts.size()
            <<",\"trials\":"<<total<<",\"skipped_empty_reference_trials\":"<<skipped
            <<",\"frames\":"<<frames<<",\"failed_trials\":"<<failed
            <<",\"body_mismatches\":"<<body_errors<<",\"observation_mismatches\":"<<obs_errors
            <<",\"camera_mismatches\":"<<camera_errors<<",\"policy_mismatches\":"<<policy_errors
            <<",\"actor_mismatches\":"<<actor_errors
            <<",\"geometry_mismatches\":"<<geometry_errors<<",\"fpg_successes\":"<<successes
            <<",\"ordinary_flags\":"<<ordinary<<",\"timeouts\":"<<timeouts
            <<",\"camera_boundary_trials\":"<<boundary_trials<<",\"exported_cases\":"<<header.cases
            <<",\"fixture_column_samples\":"<<fixture.samples<<",\"fixture_changed_cells\":"<<fixture.changed_cells<<"}";
        std::ofstream(directory+"/summary.json")<<summary.str()<<'\n';puts(summary.str().c_str());
        fpg_policy_free(native_policy);fpg_policy_free(real_policy);puf_ini_free(&ini);
        return failed?1:0;
    } catch(const std::exception& e){fprintf(stderr,"fpg parity: %s\n",e.what());return 2;}
}
