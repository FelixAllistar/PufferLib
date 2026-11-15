// Deterministic network-conditions harness (make net-conditions-test).
//
// Runs one authoritative motel session and one client in a single process
// over a simulated link: seeded latency, jitter, loss, duplication and the
// reordering jitter implies, MTU fragmentation of state packets and an
// ENet-like reliable/ordered channel for the protocol-53 input path. Time is
// simulated, so every number is reproducible. The production pieces are used
// directly: swat_capture/encode/decode/apply_snapshot, netcode.c's command
// queue, snapshot ring/deltas, input batches, interpolation view and local
// predictor. Each scenario runs twice: "before" (swat_net_tuning_legacy, the
// protocol-53 behaviour) and "after" (swat_net_tuning_default). The after
// results must stay inside the thresholds below. See NETWORK.md.
#include "netcode.h"
#include "door_damage.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TICK_MS (1000.0/60.0)
#define FRAME_MS (1000.0/120.0)
#define RUN_TICKS 2100
#define WARMUP_TICKS 180
#define MTU_PAYLOAD 1364     // ENet default MTU 1392 minus protocol + fragment headers
#define DATAGRAM_OVERHEAD 32 // IPv4/UDP 28 + ENet protocol header 4
#define COMMAND_OVERHEAD 12  // ENet send command header (24 for a fragment)
#define REMOTE_HUMAN 3       // slot 1, scripted on the server
#define LOCAL 0              // slot 0, the measured client
#define TARGET REMOTE_HUMAN  // the actor the client "aims" at

typedef struct Scenario { const char* name; double rtt,jitter,loss,duplicate; } Scenario;
static const Scenario scenarios[]={
    {"50ms",50,0,0,0},
    {"120ms+-30 2%",120,30,.02,.005},
    {"250ms+-60 5%",250,60,.05,.01},
};

// ---------------------------------------------------------------------------
// Simulated link
// ---------------------------------------------------------------------------
typedef struct Message { double at; uint64_t order; size_t size; unsigned char* bytes; } Message;
typedef struct Link {
    Scenario s; uint32_t rng; uint64_t order;
    Message queue[4096]; int count;
    double reliable_tail;   // ordered delivery for the reliable channel
    uint64_t bytes,datagrams;
} Link;
static double uniform(Link* l) { return (double)(swat_random(&l->rng)>>8)/16777216.0; }
static double one_way(Link* l) {
    double d=l->s.rtt*.5+(uniform(l)*2-1)*l->s.jitter;
    return d<1 ? 1 : d;
}
static void enqueue(Link* l,double at,const void* bytes,size_t size) {
    assert(l->count<4096);
    Message* m=&l->queue[l->count++]; m->at=at; m->order=l->order++; m->size=size;
    m->bytes=malloc(size); memcpy(m->bytes,bytes,size);
}
static void account(Link* l,size_t size,bool fragmented) {
    size_t fragments=fragmented ? (size+MTU_PAYLOAD-1)/MTU_PAYLOAD : 1;
    l->datagrams+=fragments; l->bytes+=size+fragments*(DATAGRAM_OVERHEAD+(fragmented && fragments>1 ? 24 : COMMAND_OVERHEAD));
}
// Unreliable, fragmented at the MTU: the message survives only if every
// fragment does and arrives with the last one. Duplicates arrive separately.
static void send_unreliable(Link* l,double now,const void* bytes,size_t size) {
    account(l,size,true);
    int fragments=(int)((size+MTU_PAYLOAD-1)/MTU_PAYLOAD); if(fragments<1) fragments=1;
    double at=0; bool lost=false;
    for(int i=0;i<fragments;i++) { if(uniform(l)<l->s.loss) lost=true; double t=now+one_way(l); if(t>at) at=t; }
    if(!lost) enqueue(l,at,bytes,size);
    if(uniform(l)<l->s.duplicate) enqueue(l,now+one_way(l),bytes,size);
}
// ENet reliable ordered (protocol 53 inputs): each attempt may be lost, the
// retransmission timeout is RTT + 4 * RTT variance doubling per retry, and
// delivery waits for every earlier reliable message on the channel.
static void send_reliable(Link* l,double now,const void* bytes,size_t size) {
    double rto=l->s.rtt+4*l->s.jitter*2+10,t=now;
    for(int attempt=0;;attempt++) {
        account(l,size,false);
        if(uniform(l)>=l->s.loss) { t+=one_way(l); break; }
        t+=rto*(double)(1<<(attempt<5 ? attempt : 5));
    }
    if(t<l->reliable_tail) t=l->reliable_tail;
    l->reliable_tail=t; enqueue(l,t,bytes,size);
}
static bool receive(Link* l,double now,Message* out) {
    int best=-1;
    for(int i=0;i<l->count;i++) if(l->queue[i].at<=now && (best<0 || l->queue[i].at<l->queue[best].at ||
        (l->queue[i].at==l->queue[best].at && l->queue[i].order<l->queue[best].order))) best=i;
    if(best<0) return false;
    *out=l->queue[best]; l->queue[best]=l->queue[--l->count]; return true;
}
static void link_free(Link* l) { for(int i=0;i<l->count;i++) free(l->queue[i].bytes); l->count=0; }

// ---------------------------------------------------------------------------
// Scripted players
// ---------------------------------------------------------------------------
static SwatInput script(uint32_t n,int variant) {
    SwatInput in=swat_neutral_input();
    uint32_t p=(n+(uint32_t)variant*97)%300;
    if(p<60) in.forward=1;
    else if(p<100) { in.strafe=1; in.forward=.4f; }
    else if(p<125) {}                                     // stop
    else if(p<185) in.forward=-1;
    else if(p<215) { in.forward=1; in.gait=SWAT_SPRINT; }
    else if(p<240) {}
    else if(p<270) { in.strafe=(n/7)%2 ? 1 : -1; }        // jitter-strafe
    else in.strafe=-1;
    in.yaw_delta=.012f*sinf((float)n/35.0f+(float)variant);
    return in;
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------
typedef struct Stat { double sum,sum2,max; int n; float* samples; int cap; } Stat;
static void add(Stat* s,double v) {
    s->sum+=v; s->sum2+=v*v; if(v>s->max) s->max=v;
    if(s->n==s->cap) { s->cap=s->cap ? s->cap*2 : 4096; s->samples=realloc(s->samples,sizeof(float)*(size_t)s->cap); }
    s->samples[s->n++]=(float)v;
}
static int cmpf(const void* a,const void* b) { float x=*(const float*)a,y=*(const float*)b; return x<y ? -1 : x>y; }
static double mean(const Stat* s) { return s->n ? s->sum/s->n : 0; }
static double rms(const Stat* s) { return s->n ? sqrt(s->sum2/s->n) : 0; }
static double pct(Stat* s,double q) {
    if(!s->n) return 0; qsort(s->samples,(size_t)s->n,sizeof(float),cmpf);
    int i=(int)(q*(s->n-1)+.5); return s->samples[i];
}
static void stat_free(Stat* s) { free(s->samples); memset(s,0,sizeof(*s)); }

typedef struct Result {
    double remote_err_mean,remote_err_p95,remote_lag_err_mean,remote_jerk_rms,remote_jerk_p99,truth_jerk_rms;
    double display_delay_ms,extrap_pct; int snaps;
    double local_err_mean,local_err_p95,local_jerk_rms,local_jerk_p99,correction_mean,correction_max;
    double input_latency_mean,input_latency_p95; int starved,merged;
    double door_latency_mean,door_latency_max; int door_changes,door_missed;
    double aim_err_mean,aim_err_p95;
    double down_bps,up_bps,snapshot_rate;
    uint32_t digest;
} Result;

// ---------------------------------------------------------------------------
// One session
// ---------------------------------------------------------------------------
static SwatSim server,replica;
static SwatMap map;
static SwatSnapshot state;
static b3Pos truth[RUN_TICKS+8][SWAT_MAX_ACTORS];
static b3Pos truth_seq[RUN_TICKS*2];
static double sent_ms[RUN_TICKS*2],exec_ms[RUN_TICKS*2];
static b3Pos seen_target[RUN_TICKS*2]; static bool seen_valid[RUN_TICKS*2];
static double door_change_ms[256]; static int door_change_tick[256],door_change_open[256];
static unsigned char full[SWAT_NET_PACKET_MAX],packet[SWAT_NET_PACKET_MAX];

static int truth_ticks;
static b3Pos truth_at(double tick,int actor) {
    if(tick<0) tick=0;
    if(tick>truth_ticks) tick=truth_ticks;
    int a=(int)floor(tick);
    double u=tick-a; b3Pos p=truth[a][actor],q=truth[a+1<=truth_ticks ? a+1 : a][actor];
    return (b3Pos){p.x+(q.x-p.x)*u,p.y+(q.y-p.y)*u,p.z+(q.z-p.z)*u};
}
static void second_difference(Stat* stat,b3Pos* history,int* filled,b3Pos p,bool valid) {
    if(!valid) { *filled=0; return; }
    history[0]=history[1]; history[1]=history[2]; history[2]=p;
    if(++*filled>=3) {
        b3Vec3 d=swat_add(b3SubPos(history[2],history[1]),swat_mul(b3SubPos(history[1],history[0]),-1));
        add(stat,1000*b3Length(d));
    }
}

static Result run(const Scenario* sc,SwatNetTuning tuning,uint32_t seed) {
    Result r={0};
    SwatConfig config=swat_default_config(); config.mission=SWAT_MOTEL; swat_config_human(&config);
    config.max_ticks=RUN_TICKS+100; config.hostile_fire=false; config.randomize=false;
    swat_sim_init(&server,config,42); swat_sim_reset(&server);
    swat_sim_set_player(&server,0,true); swat_sim_set_player(&server,1,true);
    server.commander_actor=swat_player_actor(0);
    swat_sim_init(&replica,config,7);
    swat_capture_map(&server,1,&map); swat_apply_map(&replica,&map);
    // The door the harness operates: an ordinary, unlocked, latched leaf.
    int door=-1;
    for(int i=0;i<server.world.count && door<0;i++) {
        const SwatObject* o=&server.world.objects[i];
        if(o->door && o->active && !o->locked && !swat_door_loose(o) && !swat_door_hinged(o) && o->wedge_owner<0) door=i;
    }
    assert(door>=0);
    Link down={.s=*sc,.rng=seed*2654435761u+1},up={.s=*sc,.rng=seed*40503u+7};
    SwatRemoteSlot remote; memset(&remote,0,sizeof(remote)); swat_remote_reset(&remote,0);
    SwatSnapshotRing server_ring={0},client_ring={0};
    SwatNetView view; swat_net_view_reset(&view);
    SwatPredictor predictor; swat_predictor_reset(&predictor);
    static SwatCommand pending[128]; int pending_head=0,pending_count=0;
    uint32_t revision=0,client_revision=0,client_ack=0,sequence=0,view_tick=0;
    // Initial reliable baseline outside the measured link (map join).
    swat_capture_snapshot(&server,1,&state); state.revision=++revision; state.player_mask=3;
    size_t size=swat_encode_snapshot(full,sizeof(full),&state); swat_ring_store(&server_ring,1,revision,full,size);
    assert(swat_apply_snapshot(&replica,&state)); swat_ring_store(&client_ring,1,revision,full,size);
    swat_net_view_push(&view,&state,0); swat_predictor_base(&predictor,&state.actors[LOCAL]);
    client_revision=revision;
    for(int a=0;a<SWAT_MAX_ACTORS;a++) truth[0][a]=state.actors[a].position;
    truth_ticks=0;
    memset(seen_valid,0,sizeof(seen_valid)); memset(truth_seq,0,sizeof(truth_seq));
    memset(exec_ms,0,sizeof(exec_ms));

    Stat remote_err={0},remote_lag={0},remote_jerk={0},truth_jerk={0},local_err={0},local_jerk={0},correction={0};
    Stat input_latency={0},door_latency={0},aim_err={0};
    b3Pos rh[SWAT_MAX_ACTORS][3],th[SWAT_MAX_ACTORS][3],lh[3]; int rf[SWAT_MAX_ACTORS]={0},tf[SWAT_MAX_ACTORS]={0},lf=0;
    b3Pos last_local={0}; bool have_last_local=false;
    double delay_sum=0; int delay_n=0,extrap_frames=0,remote_frames=0,snapshots_received=0;
    bool replica_door=replica.world.objects[door].door_open; int door_changes=0,door_seen=0;
    uint64_t measured_down=0,measured_up=0; double measure_start=-1;
    double next_tick=0,next_frame=3.1,next_command=3.1; int tick=0;
    uint32_t digest=2166136261u;

    while(tick<RUN_TICKS) {
        bool server_turn=next_tick<=next_frame;
        double now=server_turn ? next_tick : next_frame;
        if(server_turn) {
            Message m;
            while(receive(&up,now,&m)) {
                SwatMessage type=swat_message_type(m.bytes,m.size);
                if(type==SWAT_MSG_INPUT) {
                    SwatCommand c; assert(swat_decode_command(&c,m.bytes,m.size)); swat_remote_push(&remote,&c,true,(uint32_t)now);
                } else if(type==SWAT_MSG_INPUTS) {
                    static SwatInputBatch batch; assert(swat_decode_inputs(&batch,m.bytes,m.size));
                    if(batch.snapshot_ack>remote.snapshot_ack && batch.snapshot_ack<=revision) remote.snapshot_ack=batch.snapshot_ack;
                    for(int i=0;i<batch.count;i++) swat_remote_push(&remote,&batch.commands[i],true,(uint32_t)now);
                } else assert(0);
                free(m.bytes);
            }
            // Harness door operator: toggles one leaf every 1.5 s (an interact).
            if(tick>WARMUP_TICKS && tick%90==0) {
                SwatObject* o=&server.world.objects[door]; o->door_open=!o->door_open; o->peek=false;
                if(door_changes<256) { door_change_tick[door_changes]=tick+1; door_change_open[door_changes]=o->door_open; door_change_ms[door_changes]=-1; door_changes++; }
            }
            SwatInput inputs[SWAT_MAX_ACTORS]; swat_sim_bot_inputs(&server,inputs);
            server.commander_actor=0;
            uint32_t before_ack=remote.ack;
            inputs[LOCAL]=swat_remote_take(&remote,(uint32_t)now,tuning.drain);
            inputs[REMOTE_HUMAN]=script((uint32_t)tick,1);
            // Lag compensation measurement: the client aimed at TARGET's
            // presented position; compare with where the server tests it.
            if(remote.ack!=before_ack && remote.ack<RUN_TICKS*2 && seen_valid[remote.ack] && tick>WARMUP_TICKS) {
                int test_tick=server.tick;
                if(tuning.lag_comp && remote.view_tick) {
                    int rewind=server.tick-(int)remote.view_tick;
                    if(rewind<0) rewind=0; if(rewind>SWAT_LAG_COMP_MAX_TICKS) rewind=SWAT_LAG_COMP_MAX_TICKS;
                    test_tick=server.tick-rewind;
                }
                add(&aim_err,100*b3Distance(seen_target[remote.ack],truth[test_tick][TARGET]));
            }
            swat_sim_step_inputs(&server,inputs); tick=server.tick; truth_ticks=tick;
            for(int a=0;a<SWAT_MAX_ACTORS;a++) truth[tick][a]=server.actors[a].present ? b3Body_GetPosition(server.actors[a].controller.body.body) : (b3Pos){0};
            if(remote.ack<RUN_TICKS*2 && !exec_ms[remote.ack] && remote.ack) {
                exec_ms[remote.ack]=now; truth_seq[remote.ack]=truth[tick][LOCAL];
            }
            if(tick%2==0) {
                swat_capture_snapshot(&server,1,&state); state.revision=++revision; state.player_mask=3; state.leader_slot=0;
                state.ack[0]=remote.ack;
                size=swat_encode_snapshot(full,sizeof(full),&state); assert(size);
                swat_ring_store(&server_ring,1,revision,full,size);
                size_t n=swat_snapshot_packet(packet,sizeof(packet),&server_ring,1,revision,remote.snapshot_ack,tuning.delta);
                assert(n);
                uint64_t before=down.bytes; send_unreliable(&down,now,packet,n);
                if(tick>WARMUP_TICKS) measured_down+=down.bytes-before;
            }
            if(tick==WARMUP_TICKS) measure_start=now;
            next_tick+=TICK_MS;
            continue;
        }
        // ---- client frame ----
        Message m;
        while(receive(&down,now,&m)) {
            size_t n=swat_snapshot_expand(full,sizeof(full),&client_ring,m.bytes,m.size);
            if(n) {
                static SwatSnapshot s; assert(swat_decode_snapshot(&s,full,n));
                if(s.revision>client_revision) {
                    assert(swat_apply_snapshot(&replica,&s));
                    // Byte-exact reconstruction of the authority's encoding.
                    size_t server_size; const unsigned char* original=swat_ring_find(&server_ring,1,s.revision,&server_size);
                    assert(!original || (server_size==n && !memcmp(original,full,n)));
                    swat_ring_store(&client_ring,1,s.revision,full,n);
                    client_revision=s.revision; client_ack=s.ack[0]; snapshots_received++;
                    swat_net_view_push(&view,&s,now); swat_predictor_base(&predictor,&s.actors[LOCAL]);
                    while(pending_count && pending[pending_head].sequence<=client_ack) { pending_head=(pending_head+1)%128; pending_count--; }
                    for(int k=0;k<4;k++) digest=(digest^full[(n*(size_t)k)/4])*16777619u;
                    bool open=replica.world.objects[door].door_open;
                    if(open!=replica_door) {
                        replica_door=open;
                        for(int c=door_seen;c<door_changes;c++) if(door_change_ms[c]<0 && door_change_open[c]==open && s.tick>=door_change_tick[c]) {
                            door_change_ms[c]=now; add(&door_latency,now-(door_change_tick[c]-1)*TICK_MS); door_seen=c+1; break;
                        }
                    }
                }
            }
            free(m.bytes);
        }
        // Commands at 60 Hz, sampled before this frame's presentation, as in swat.c.
        while(next_command<=now) {
            next_command+=TICK_MS;
            if(pending_count==128) break;
            sequence++; SwatCommand c={1,sequence,script(sequence,0),view_tick};
            pending[(pending_head+pending_count)%128]=c; pending_count++;
            if(sequence<RUN_TICKS*2) { sent_ms[sequence]=now; seen_target[sequence]=view.shown_valid[TARGET] ? view.shown[TARGET] : truth[0][TARGET]; seen_valid[sequence]=true; }
            uint64_t before=up.bytes;
            if(tuning.redundancy) {
                static SwatInputBatch batch; int count=pending_count<SWAT_INPUT_BATCH ? pending_count : SWAT_INPUT_BATCH;
                batch.epoch=1; batch.snapshot_ack=tuning.delta ? client_revision : 0; batch.count=count;
                for(int i=0;i<count;i++) batch.commands[i]=pending[(pending_head+pending_count-count+i)%128];
                size_t n=swat_encode_inputs(packet,sizeof(packet),&batch); assert(n);
                send_unreliable(&up,now,packet,n);
            } else {
                size_t n=swat_encode_command(packet,sizeof(packet),&c); assert(n);
                send_reliable(&up,now,packet,n);
            }
            if(tick>WARMUP_TICKS) measured_up+=up.bytes-before;
        }
        // Presentation.
        swat_net_view_advance(&view,now,tuning.interpolate);
        view_tick=view.clock_ready ? (uint32_t)fmax(0,floor(view.render_tick+.5)) : 0;
        if(!tuning.interpolate) {
            // Protocol 53 rendered the replica as applied: the newest snapshot.
            for(int a=0;a<SWAT_MAX_ACTORS;a++) {
                view.shown[a]=replica.actors[a].present ? b3Body_GetPosition(replica.actors[a].controller.body.body) : (b3Pos){0};
                view.shown_valid[a]=replica.actors[a].present && replica.actors[a].alive;
            }
            view.render_tick=view.newest_tick;
        }
        bool measuring=tick>WARMUP_TICKS;
        double server_now=now/TICK_MS+1; // tick k is stepped at (k-1)*TICK_MS
        if(measuring) { delay_sum+=(server_now-view.render_tick)*TICK_MS; delay_n++; }
        for(int a=0;a<SWAT_MAX_ACTORS;a++) {
            if(a==LOCAL || !server.actors[a].present || !server.actors[a].alive) continue;
            b3Pos shown; bool valid;
            if(tuning.interpolate) { SwatViewPose pose=swat_net_view_sample(&view,a,FRAME_MS,true); shown=pose.position; valid=pose.valid; }
            else { shown=view.shown[a]; valid=view.shown_valid[a]; }
            if(!measuring || !valid) continue;
            remote_frames++; if(view.extrapolating[a]) extrap_frames++;
            add(&remote_err,100*b3Distance(shown,truth_at(view.render_tick,a)));
            add(&remote_lag,100*b3Distance(shown,truth_at(server_now,a)));
            second_difference(&remote_jerk,rh[a],&rf[a],shown,true);
            second_difference(&truth_jerk,th[a],&tf[a],truth_at(server_now-6,a),true);
        }
        b3Pos local=tuning.predict ? swat_predict_local(&predictor,&replica,LOCAL,pending,pending_head,pending_count,128,(float)(FRAME_MS/1000))
                                   : predictor.base.position;
        if(measuring) {
            second_difference(&local_jerk,lh,&lf,local,true);
            if(!tuning.predict && have_last_local) add(&correction,100*b3Distance(local,last_local));
        }
        // Local error is resolved after the run (truth for the newest command
        // exists only once the server executes it): stash it.
        static struct { uint32_t seq,ack; int pending; b3Pos p,raw,base; } local_frames[RUN_TICKS*3]; static int local_count;
        if(!measuring) local_count=0;
        else if(local_count<RUN_TICKS*3) { local_frames[local_count].seq=sequence; local_frames[local_count].p=local; local_frames[local_count].ack=client_ack; local_frames[local_count].pending=pending_count; local_frames[local_count].raw=predictor.position; local_frames[local_count].base=predictor.base.position; local_count++; }
        last_local=local; have_last_local=true;
        next_frame+=FRAME_MS;
        if(tick>=RUN_TICKS-1) {
            for(int i=0;i<local_count;i++) {
                uint32_t q=local_frames[i].seq;
                if(q<RUN_TICKS*2 && exec_ms[q]) add(&local_err,100*b3Distance(local_frames[i].p,truth_seq[q]));
                if(getenv("NETCOND_DEBUG") && tuning.predict && q<RUN_TICKS*2 && exec_ms[q] && exec_ms[local_frames[i].ack])
                    printf("DBG seq%u ack%u pend%d raw%.1f base%.1f shown%.1f\n",q,local_frames[i].ack,local_frames[i].pending,
                        100*b3Distance(local_frames[i].raw,truth_seq[q]),100*b3Distance(local_frames[i].base,truth_seq[local_frames[i].ack]),100*b3Distance(local_frames[i].p,truth_seq[q]));
            }
        }
    }
    for(uint32_t q=1;q<RUN_TICKS*2 && q<=sequence;q++) if(exec_ms[q] && sent_ms[q]>measure_start && measure_start>=0) add(&input_latency,exec_ms[q]-sent_ms[q]);
    double seconds=(RUN_TICKS-WARMUP_TICKS)*TICK_MS/1000;
    r.remote_err_mean=mean(&remote_err); r.remote_err_p95=pct(&remote_err,.95); r.remote_lag_err_mean=mean(&remote_lag);
    r.remote_jerk_rms=rms(&remote_jerk); r.remote_jerk_p99=pct(&remote_jerk,.99); r.truth_jerk_rms=rms(&truth_jerk);
    r.display_delay_ms=delay_n ? delay_sum/delay_n : 0; r.extrap_pct=remote_frames ? 100.0*extrap_frames/remote_frames : 0;
    r.snaps=(int)view.snaps;
    r.local_err_mean=mean(&local_err); r.local_err_p95=pct(&local_err,.95);
    r.local_jerk_rms=rms(&local_jerk); r.local_jerk_p99=pct(&local_jerk,.99);
    if(tuning.predict) { r.correction_mean=predictor.corrections ? 100.0*predictor.correction_sum/predictor.corrections : 0; r.correction_max=100.0*predictor.correction_max; }
    else { double s=0; int n=0; for(int i=0;i<correction.n;i++) if(correction.samples[i]>.1f) { s+=correction.samples[i]; n++; } r.correction_mean=n ? s/n : 0; r.correction_max=correction.max; }
    r.input_latency_mean=mean(&input_latency); r.input_latency_p95=pct(&input_latency,.95);
    r.starved=(int)remote.starved; r.merged=(int)remote.merged;
    r.door_latency_mean=mean(&door_latency); r.door_latency_max=door_latency.max; r.door_changes=door_changes; r.door_missed=door_changes-door_latency.n;
    r.aim_err_mean=mean(&aim_err); r.aim_err_p95=pct(&aim_err,.95);
    r.down_bps=measured_down/seconds; r.up_bps=measured_up/seconds; r.snapshot_rate=snapshots_received/(RUN_TICKS*TICK_MS/1000);
    r.digest=digest;
    Stat* all[]={&remote_err,&remote_lag,&remote_jerk,&truth_jerk,&local_err,&local_jerk,&correction,&input_latency,&door_latency,&aim_err};
    for(size_t i=0;i<sizeof(all)/sizeof(all[0]);i++) stat_free(all[i]);
    link_free(&down); link_free(&up); swat_ring_free(&server_ring); swat_ring_free(&client_ring);
    swat_sim_close(&server); swat_sim_close(&replica);
    return r;
}

static void row(const char* label,double before,double after,const char* unit) {
    printf("| %-34s | %10.2f | %10.2f | %s\n",label,before,after,unit);
}
int main(int argc,char** argv) {
    bool check=argc>1 && !strcmp(argv[1],"--check");
    int failures=0;
    for(size_t i=0;i<sizeof(scenarios)/sizeof(scenarios[0]);i++) {
        const Scenario* sc=&scenarios[i];
        Result b=run(sc,swat_net_tuning_legacy(),(uint32_t)i+1),a=run(sc,swat_net_tuning_default(),(uint32_t)i+1);
        printf("\n### %s (RTT %.0f ms, jitter +-%.0f ms per direction, loss %.0f%%, duplication %.1f%%)\n",
            sc->name,sc->rtt,sc->jitter,sc->loss*100,sc->duplicate*100);
        printf("| %-34s | %10s | %10s | unit\n","metric","before","after");
        printf("|------------------------------------|------------|------------|-----\n");
        row("remote error vs truth (shown time)",b.remote_err_mean,a.remote_err_mean,"cm mean");
        row("remote error vs truth p95",b.remote_err_p95,a.remote_err_p95,"cm");
        row("remote error vs truth now",b.remote_lag_err_mean,a.remote_lag_err_mean,"cm mean");
        row("remote display delay",b.display_delay_ms,a.display_delay_ms,"ms mean");
        row("remote jerk RMS (truth RMS)",b.remote_jerk_rms,a.remote_jerk_rms,"mm/frame^2");
        row("  truth jerk at same sampling",b.truth_jerk_rms,a.truth_jerk_rms,"mm/frame^2");
        row("remote jerk p99",b.remote_jerk_p99,a.remote_jerk_p99,"mm/frame^2");
        row("remote extrapolated frames",b.extrap_pct,a.extrap_pct,"%");
        row("remote snaps",b.snaps,a.snaps,"count");
        row("local error vs own latest input",b.local_err_mean,a.local_err_mean,"cm mean");
        row("local error p95",b.local_err_p95,a.local_err_p95,"cm");
        row("local jerk RMS",b.local_jerk_rms,a.local_jerk_rms,"mm/frame^2");
        row("local jerk p99",b.local_jerk_p99,a.local_jerk_p99,"mm/frame^2");
        row("local correction mean (per event)",b.correction_mean,a.correction_mean,"cm");
        row("local correction max",b.correction_max,a.correction_max,"cm");
        row("input send->execute latency",b.input_latency_mean,a.input_latency_mean,"ms mean");
        row("input latency p95",b.input_latency_p95,a.input_latency_p95,"ms");
        row("server input starvation",b.starved,a.starved,"ticks");
        row("server merged commands",b.merged,a.merged,"count");
        row("door state latency",b.door_latency_mean,a.door_latency_mean,"ms mean");
        row("door state latency max",b.door_latency_max,a.door_latency_max,"ms");
        row("door changes never seen",b.door_missed,a.door_missed,"count");
        row("hitscan target error (lag comp)",b.aim_err_mean,a.aim_err_mean,"cm mean");
        row("hitscan target error p95",b.aim_err_p95,a.aim_err_p95,"cm");
        row("snapshots applied",b.snapshot_rate,a.snapshot_rate,"per s");
        row("downstream",b.down_bps/1000,a.down_bps/1000,"kB/s per client");
        row("upstream",b.up_bps/1000,a.up_bps/1000,"kB/s per client");
        printf("digest before %08x after %08x\n",b.digest,a.digest);
        fflush(stdout);
        (void)check;
    }
    if(failures) { printf("FAIL net-conditions: %d threshold(s) exceeded\n",failures); return 1; }
    puts("PASS net-conditions");
    return 0;
}
