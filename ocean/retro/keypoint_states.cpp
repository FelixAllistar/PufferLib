// Key-point practice starts (2026-10-31) for env.archive_practice: a few safe states shortly before each hard section.
// SPEC lines are "LEVEL X_LO X_HI [clean]" (e.g. "7-4 820 960 clean"), one key point each, x in the level's own
// pixels. A policy plays each level from its start; every decision where Mario stands on the ground, alive and on
// screen, with x in a window becomes a candidate (with "clean", only if the maze counters show no failed check so far:
// MultiLoopCorrectCntr $06D9 == MultiLoopPassCntr $06DA). Candidates must survive 60 frames of no input. Per key point
// the kept states are spread over the window (one per 4 px bucket, closest to the bucket's first sighting), at most
// PER_POINT. Writes them as an archive for env.archive_init_path; key bits 52-59 hold the key point.
// Usage: keypoint_states CHECKPOINT SPEC OUT.archive.gz [ATTEMPTS=64] [PER_POINT=16] [SEED] [FRAMES=20000]
#define PUFFERLIB_BUILD_MAIN
#include "retro.h"
#undef PUFFERLIB_BUILD_MAIN
#include "retro_policy_shape.h"
typedef void RetroPolicy;
extern "C" {
RetroPolicy* retro_cpu_load(const char* path, int hidden, int layers);
RetroPolicy* retro_cpu_clone(RetroPolicy* source, int hidden, int layers);
const float* retro_cpu_logits(RetroPolicy* policy, const float* obs);
void retro_cpu_free(RetroPolicy* policy);
}
#include "retro_sweep_config.h"
#include <fstream>
#include <map>
#include <mutex>
#include <omp.h>
#include <sstream>

struct KeyPoint { std::string level; int id, lo, hi; bool clean; };
struct Candidate { std::shared_ptr<RetroArchiveSnapshot> state; int point, x; };

static std::shared_ptr<RetroArchiveSnapshot> snapshot(Env* e) {
    auto s=std::make_shared<RetroArchiveSnapshot>();auto& st=s->start;auto& l=s->ledger;
    const auto* m=e->emu->low_mem();
    e->emu->save_state(&st.state);const auto& f=e->emu->frame();
    for(int y=0;y<240;y++)memcpy(st.pixels+y*256,f.pixels+y*f.pitch,256);
    memcpy(st.palette,f.palette,sizeof(st.palette));
    st.world=e->world;st.stage=e->stage;st.area=e->area;st.data=m[0xe7]+256*m[0xe8];st.x=e->x_pos;st.rta_offset_frames=0;
    memcpy(l.frontiers,e->frontiers,sizeof(l.frontiers));l.frontier_count=e->frontier_count;
    memcpy(l.areas,e->area_transition_keys,sizeof(l.areas));l.area_count=e->area_transition_key_count;
    l.rewarded=e->rewarded_levels;l.last_area=e->last_area_key;l.pipe_phase=e->pipe_phase;
    return s;
}

int main(int argc,char** argv) {
    try {
        if(argc<4) throw std::runtime_error("usage: keypoint_states CHECKPOINT SPEC OUT.archive.gz [ATTEMPTS] [PER_POINT] [SEED] [FRAMES]");
        int attempts=argc>4?atoi(argv[4]):64, per_point=argc>5?atoi(argv[5]):16;
        unsigned seed=argc>6?strtoul(argv[6],nullptr,10):20261031; int frames=argc>7?atoi(argv[7]):20000;
        std::vector<KeyPoint> points; std::vector<std::string> levels;
        std::ifstream spec(argv[2]); std::string line;
        while(std::getline(spec,line)) {
            if(line.empty()||line[0]=='#') continue;
            std::istringstream in(line); KeyPoint k; std::string flag;
            if(!(in>>k.level>>k.lo>>k.hi)) throw std::runtime_error("bad spec line: "+line);
            in>>flag; k.clean=flag=="clean"; k.id=(int)points.size(); points.push_back(k);
            if(std::find(levels.begin(),levels.end(),k.level)==levels.end()) levels.push_back(k.level);
        }
        if(points.size()>255) throw std::runtime_error("at most 255 key points");
        std::string config=std::string(argv[1])+".ini";
        std::vector<Candidate> kept;
        for(auto& level:levels) {
            Ini ini={}; puf_ini_load_env(&ini,"retro",0,nullptr);
            puf_ini_set(puf_ini_section(&ini,"env",0),"practice_replay","None");
            puf_ini_load_file(&ini,config.c_str());
            puf_ini_put(&ini,"env.archive_enabled","0");
            puf_ini_set(puf_ini_section(&ini,"env",0),"practice_replay","None");
            puf_ini_put(&ini,"env.spawn_levels",level.c_str());
            puf_ini_put(&ini,"env.full_render","0");
            puf_ini_put(&ini,"env.max_frames",std::to_string(frames).c_str());
            puf_ini_put(&ini,"env.terminate_on_clear","1");
            int hidden=puf_ini_get(&ini,"policy","hidden_size"),layers=puf_ini_get(&ini,"policy","num_layers");
            RetroPolicy* first=retro_cpu_load(argv[1],hidden,layers);
            if(!first) throw std::runtime_error("checkpoint read failed");
            int n=attempts;
            Dict vec={}; dict_set(&vec,"total_agents",n); dict_set(&vec,"num_buffers",1);
            int count,begin[1],sizes[1]; Env* envs=my_vec_init(&count,begin,sizes,&vec,puf_ini_section(&ini,"env",0));
            std::vector<float> obs(n*OBS_SIZE),actions(n),rewards(n),terminals(n);
            for(int i=0;i<n;i++){Env* e=&envs[i];e->spawn_pin=1;e->cur_spawn=0;
                e->agents[0]={obs.data()+i*OBS_SIZE,&actions[i],&rewards[i],&terminals[i],nullptr,0};puf_reset(e);}
            // Per (key point, 4 px bucket): the first sighting over all attempts.
            std::map<int,Candidate> found; std::mutex found_mutex;
            #pragma omp parallel for schedule(dynamic,1)
            for(int i=0;i<n;i++) {
                Env* e=&envs[i]; RetroPolicy* net=i?retro_cpu_clone(first,hidden,layers):first;
                unsigned rng=seed^((unsigned)(i+1)*2654435761u); int elapsed=0, life=robs_life(e->emu->low_mem());
                while(elapsed<frames) {
                    const float* logits=retro_cpu_logits(net,obs.data()+i*OBS_SIZE);
                    actions[i]=retro_panel_action(logits,retro_random(&rng),false);
                    puf_step(e); elapsed+=e->last_frames;
                    const auto* m=e->emu->low_mem();
                    // First life only: after a death the level restarts and x no longer means the same route.
                    if(terminals[i]||robs_life(m)<life||robs_dead(m)) break;
                    if(!retro_playable_area_state(m)||m[0x1d]!=0||m[0xb5]!=1) continue;
                    for(auto& k:points) {
                        if(k.level!=level||e->x_pos<k.lo||e->x_pos>k.hi) continue;
                        if(k.clean&&m[0x6d9]!=m[0x6da]) continue;
                        int bucket=k.id*4096+e->x_pos/4;
                        std::lock_guard<std::mutex> lock(found_mutex);
                        if(!found.count(bucket)) found[bucket]={snapshot(e),k.id,e->x_pos};
                    }
                }
                if(i) retro_cpu_free(net);
            }
            // Safety: 60 frames without input from the state must not lose a life or reach a dying state.
            Env* probe=&envs[0];
            std::map<int,Candidate> best;
            for(auto& [bucket,c]:found) {
                probe->emu->load_state(c.state->start.state); retro_bind_pixels(probe->emu);
                if(probe->rom_blocks&&!probe->emu->set_rom_blocks(true)) throw std::runtime_error("rom blocks");
                int life=robs_life(probe->emu->low_mem()); bool safe=true;
                for(int f=0;f<60&&safe;f++) {
                    retro_check(probe->emu->emulate_frame(0,0));
                    const auto* m=probe->emu->low_mem();
                    safe=!robs_dead(m)&&!robs_dying(m)&&robs_life(m)>=life&&m[0xb5]<=1;
                }
                if(safe) best[bucket]=c;
            }
            for(auto& k:points) if(k.level==level) {
                std::vector<Candidate> mine;
                for(auto& [bucket,c]:best) if(c.point==k.id) mine.push_back(c);
                // Spread over the window: every len/per_point-th bucket.
                int take=std::min((int)mine.size(),per_point);
                for(int j=0;j<take;j++) kept.push_back(mine[(size_t)j*mine.size()/take]);
                printf("key point %d %s x %d-%d%s: %zu safe buckets, kept %d\n",k.id,level.c_str(),k.lo,k.hi,
                    k.clean?" clean":"",mine.size(),take);
            }
        }
        auto& b=retro_archive_bank();
        b.capacity=kept.size()+1; b.cells.clear(); b.index.clear();
        for(auto& c:kept) {
            RetroArchiveCell cell; cell.state=c.state; cell.level=retro_level_id(c.state->start.world,c.state->start.stage);
            cell.key=(uint64_t(c.point)<<52)|(uint64_t(c.x)<<16)|(uint64_t)b.cells.size();
            cell.depth=retro_archive_depth(*c.state,cell.level);
            b.index[cell.key]=b.cells.size(); b.cells.push_back(cell);
        }
        retro_archive_file(argv[3],true,0);
        printf("wrote %zu states for %zu key points -> %s\n",b.cells.size(),points.size(),argv[3]);
        return 0;
    } catch(const std::exception& ex){fprintf(stderr,"keypoint_states: %s\n",ex.what());return 1;}
}
