#include "netcode.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

SwatNetTuning swat_net_tuning_default(void) {
    return (SwatNetTuning){.delta=true,.redundancy=true,.drain=true,.interpolate=true,.predict=true,.lag_comp=true,
        .controller=true,.starve_decay=true,.view_state=true,.lag_doors=true};
}
SwatNetTuning swat_net_tuning_legacy(void) { return (SwatNetTuning){0}; }
SwatNetTuning swat_net_tuning_pass1(void) {
    SwatNetTuning t=swat_net_tuning_default(); t.controller=t.starve_decay=t.view_state=t.lag_doors=false; return t;
}

// ---------------------------------------------------------------------------
// Server command queue
// ---------------------------------------------------------------------------
// Window over which the queue's minimum depth is measured. A minimum above
// SWAT_DRAIN_SLACK means every command in that surplus waited a full window
// for nothing: a delivery burst (lost reliable packet, jitter spike) left the
// player permanently further behind. Merging pairs removes it again.
#define SWAT_DRAIN_WINDOW 60
#define SWAT_DRAIN_SLACK 2
#define STARVE_DECAY_DEFAULT .25f

void swat_remote_reset(SwatRemoteSlot* r,uint32_t now_ms) {
    void* peer=r->peer; memset(r,0,sizeof(*r)); r->peer=peer;
    r->held=swat_neutral_input(); r->last_receive_ms=now_ms; r->window_min=SWAT_INPUT_QUEUE;
}
bool swat_remote_push(SwatRemoteSlot* r,const SwatCommand* c,bool running,uint32_t now_ms) {
    if(c->sequence<=r->received) return true;
    if(c->sequence>r->received+1 && r->received) r->lost+=c->sequence-r->received-1;
    if(!running) {
        // Paused/ended authority acknowledges without executing.
        r->received=r->ack=c->sequence; r->count=0; r->last_receive_ms=now_ms; return true;
    }
    if(r->count==SWAT_INPUT_QUEUE) {
        // A burst larger than the queue (a long outage recovered through
        // redundancy) folds the two oldest commands instead of disconnecting.
        SwatCommand* first=&r->queue[r->head];
        r->head=(r->head+1)%SWAT_INPUT_QUEUE; r->count--;
        SwatCommand* second=&r->queue[r->head];
        second->input=swat_input_merge(&first->input,&second->input);
        r->merged++;
    }
    r->queue[(r->head+r->count)%SWAT_INPUT_QUEUE]=*c; r->count++;
    r->received=c->sequence; r->last_receive_ms=now_ms;
    return true;
}
SwatInput swat_input_merge(const SwatInput* a,const SwatInput* b) {
    SwatInput m=*b;
    // Look deltas are per tick and clamped by the controller; the sum keeps
    // the aim the player actually produced up to that clamp.
    m.yaw_delta=swat_clamp(a->yaw_delta+b->yaw_delta,-.4f,.4f);
    m.pitch_delta=swat_clamp(a->pitch_delta+b->pitch_delta,-.3f,.3f);
    // A press in either tick survives; the later tick then releases it.
    m.jump|=a->jump; m.fire|=a->fire; m.reload|=a->reload; m.interact|=a->interact; m.selector|=a->selector;
    m.inspect|=a->inspect; m.command|=a->command; m.melee|=a->melee; m.taser|=a->taser; m.pepper_spray|=a->pepper_spray;
    m.cancel_reload|=a->cancel_reload; m.squad_queue|=a->squad_queue; m.squad_execute|=a->squad_execute;
    // One-shot requests: keep the earlier one when the later tick has none.
#define KEEP(f) if(!m.f) m.f=a->f
    KEEP(weapon); KEEP(loadout); KEEP(throwable); KEEP(door_tool); KEEP(sniper_order); KEEP(primary_profile);
    KEEP(sight_profile); KEEP(device_deploy);
    if(!b->squad_order && a->squad_order) { m.squad_order=a->squad_order; m.squad_team=a->squad_team; }
#undef KEEP
    return m;
}
SwatInput swat_remote_take(SwatRemoteSlot* r,uint32_t now_ms,bool drain) { return swat_remote_take_policy(r,now_ms,drain,false); }
// Starvation policy (prediction pass, tuning.starve_decay). A starved tick still
// steps every body, so it cannot simply be skipped. Repeating the held input
// at full strength (protocol 53/54) moves the player one extra tick, which
// the client sees as a correction once the late command executes too.
// Instead held movement decays toward neutral on each consecutive starved
// tick. Buttons stay as held: every press in the sim is edge-detected
// (last_jump, last_interact, last_fire...), so a held button never fires
// again, while releasing it would turn the next command into a false press.
// Look deltas and one-shot requests are never repeated.
float swat_starve_decay=STARVE_DECAY_DEFAULT;
SwatInput swat_remote_take_policy(SwatRemoteSlot* r,uint32_t now_ms,bool drain,bool decay) {
    if(drain) {
        if(r->count<r->window_min) r->window_min=r->count;
        if(++r->window_ticks>=SWAT_DRAIN_WINDOW) {
            if(r->window_min>SWAT_DRAIN_SLACK) r->drain+=r->window_min-SWAT_DRAIN_SLACK;
            r->window_ticks=0; r->window_min=SWAT_INPUT_QUEUE;
        }
        if(r->drain>0 && r->count>=2) {
            SwatCommand* first=&r->queue[r->head];
            r->head=(r->head+1)%SWAT_INPUT_QUEUE; r->count--;
            SwatCommand* second=&r->queue[r->head];
            second->input=swat_input_merge(&first->input,&second->input);
            if(!second->view_tick) second->view_tick=first->view_tick;
            r->drain--; r->merged++;
        } else if(r->count<2) r->drain=0;
    }
    if(r->count) {
        SwatCommand* c=&r->queue[r->head]; r->held=c->input; r->ack=c->sequence; r->view_tick=c->view_tick;
        r->head=(r->head+1)%SWAT_INPUT_QUEUE; r->count--; r->starve_run=0;
    } else {
        r->starved++; r->view_tick=0; r->starve_run++;
        if(decay) {
            r->held.forward*=swat_starve_decay; r->held.strafe*=swat_starve_decay;
            r->held.weapon=r->held.loadout=r->held.primary_profile=r->held.sight_profile=0;
        }
        // Expire repeated holds, never an unconsumed command. A slow server
        // frame must not acknowledge input it discards.
        if(now_ms-r->last_receive_ms>250) r->held=swat_neutral_input();
    }
    SwatInput input=r->held;
    // A repeated hold never repeats a one-shot press or look delta.
    r->held.yaw_delta=r->held.pitch_delta=0;
    return input;
}

// ---------------------------------------------------------------------------
// Snapshot ring and per-client state packets
// ---------------------------------------------------------------------------
void swat_ring_store(SwatSnapshotRing* ring,uint32_t epoch,uint32_t revision,const void* bytes,size_t size) {
    if(ring->epoch!=epoch) { for(int i=0;i<SWAT_SNAPSHOT_RING;i++) ring->revision[i]=0; ring->epoch=epoch; }
    int i=(int)(revision%SWAT_SNAPSHOT_RING);
    if(ring->capacity[i]<size) {
        unsigned char* grown=realloc(ring->bytes[i],size);
        if(!grown) { ring->revision[i]=0; return; }
        ring->bytes[i]=grown; ring->capacity[i]=size;
    }
    memcpy(ring->bytes[i],bytes,size); ring->size[i]=size; ring->revision[i]=revision;
}
const unsigned char* swat_ring_find(const SwatSnapshotRing* ring,uint32_t epoch,uint32_t revision,size_t* size) {
    if(!revision || ring->epoch!=epoch) return NULL;
    int i=(int)(revision%SWAT_SNAPSHOT_RING);
    if(ring->revision[i]!=revision) return NULL;
    *size=ring->size[i]; return ring->bytes[i];
}
void swat_ring_free(SwatSnapshotRing* ring) {
    for(int i=0;i<SWAT_SNAPSHOT_RING;i++) free(ring->bytes[i]);
    memset(ring,0,sizeof(*ring));
}
size_t swat_snapshot_packet(void* out,size_t capacity,const SwatSnapshotRing* ring,uint32_t epoch,
    uint32_t revision,uint32_t client_ack,bool delta) {
    size_t full_size,base_size=0; const unsigned char* full=swat_ring_find(ring,epoch,revision,&full_size);
    if(!full || full_size>capacity) return 0;
    // Only baselines well inside the ring: the client keeps the same window.
    const unsigned char* base=delta && client_ack<revision && revision-client_ack<SWAT_SNAPSHOT_RING/2 ?
        swat_ring_find(ring,epoch,client_ack,&base_size) : NULL;
    if(base) {
        size_t size=swat_encode_snapshot_delta(out,capacity,epoch,client_ack,base,base_size,full,full_size);
        if(size && size<full_size) return size;
    }
    memcpy(out,full,full_size); return full_size;
}
size_t swat_snapshot_expand(void* out,size_t capacity,const SwatSnapshotRing* ring,const void* packet,size_t size) {
    SwatMessage type=swat_message_type(packet,size);
    if(type==SWAT_MSG_SNAPSHOT) { if(size>capacity) return 0; memcpy(out,packet,size); return size; }
    if(type!=SWAT_MSG_DELTA) return 0;
    uint32_t epoch,base_revision; size_t base_size;
    if(!swat_decode_snapshot_delta_header(packet,size,&epoch,&base_revision)) return 0;
    const unsigned char* base=swat_ring_find(ring,epoch,base_revision,&base_size);
    if(!base) return 0;
    return swat_expand_snapshot_delta(out,capacity,base,base_size,packet,size);
}

// ---------------------------------------------------------------------------
// Remote actor interpolation
// ---------------------------------------------------------------------------
#define TICK_MS (1000.0/60.0)
// Hard bounds on the adaptive delay. The floor covers one snapshot interval
// (two ticks) plus a small scheduling margin; the ceiling keeps the view
// within 250 ms of the newest data even on the worst links.
#define VIEW_DELAY_MIN 40.0
#define VIEW_DELAY_MAX 250.0
// Extrapolate at most this far past the newest snapshot, then hold.
#define VIEW_EXTRAPOLATE_MS 100.0
// Residual error decay time constant (seconds) and teleport threshold.
#define VIEW_BLEND_TAU .09f
#define VIEW_SNAP_METRES 2.5f

void swat_net_view_reset(SwatNetView* v) {
    int doors=v->door_count; int objects[SWAT_VIEW_DOORS]; memcpy(objects,v->door_object,sizeof(objects));
    memset(v,0,sizeof(*v));
    v->door_count=doors; memcpy(v->door_object,objects,sizeof(objects));
}
void swat_net_view_bind(SwatNetView* v,const SwatSim* replica) {
    v->door_count=0;
    for(int i=0;i<replica->world.count && v->door_count<SWAT_VIEW_DOORS;i++)
        if(replica->world.objects[i].door) v->door_object[v->door_count++]=i;
}
static SwatViewFrame* view_frame(SwatNetView* v,int tick) {
    for(int i=0;i<v->count;i++) if(v->frames[i].tick==tick) return &v->frames[i];
    return NULL;
}
void swat_net_view_push(SwatNetView* v,const SwatSnapshot* s,double now_ms) {
    if(v->epoch!=s->epoch) { swat_net_view_reset(v); v->epoch=s->epoch; }
    if(view_frame(v,s->tick)) return;
    // Snapshots are kept sorted by tick; the oldest is dropped when full.
    if(v->count==SWAT_VIEW_FRAMES) {
        if(s->tick<v->frames[0].tick) return;
        memmove(v->frames,v->frames+1,sizeof(v->frames[0])*(SWAT_VIEW_FRAMES-1)); v->count--;
    }
    int at=v->count; while(at>0 && v->frames[at-1].tick>s->tick) at--;
    memmove(v->frames+at+1,v->frames+at,sizeof(v->frames[0])*(size_t)(v->count-at)); v->count++;
    SwatViewFrame* f=&v->frames[at]; f->tick=s->tick;
    for(int i=0;i<SWAT_MAX_ACTORS;i++) {
        const SwatActorState* a=&s->actors[i];
        f->actors[i]=(SwatViewActor){a->position,a->velocity,a->yaw,a->pitch,a->present,a->alive,
            a->crouched,a->grounded,a->sprinting,a->upper_offset,a->eye_height,a->lean,a->ads,a->ready_blend,a->mantle};
    }
    for(int k=0;k<v->door_count;k++) {
        int o=v->door_object[k];
        if(o>=s->object_count) { f->doors[k].active=false; continue; }
        const SwatObjectState* d=&s->objects[o];
        f->doors[k]=(SwatViewDoor){d->door_angle,d->door.position,d->door.rotation,d->door.broken_hinges || d->door.hinged,d->active};
    }
    if(s->revision>v->newest_revision) {
        if(v->newest_revision) {
            double missing=(double)(s->revision-v->newest_revision-1);
            v->loss+=.05*(fmin(missing,4)-v->loss);
        }
        v->newest_revision=s->revision;
    }
    if(s->tick>v->newest_tick) v->newest_tick=s->tick;
    // Clock: arrival minus server time. The minimum tracks the least-delayed
    // path; lateness above it is jitter the buffer must absorb.
    double sample=now_ms-s->tick*TICK_MS;
    if(!v->clock_ready) {
        v->offset_ms=sample; v->jitter_ms=0; v->clock_ready=true;
        v->delay_ms=VIEW_DELAY_MIN; v->render_tick=s->tick-v->delay_ms/TICK_MS; v->last_ms=now_ms;
        return;
    }
    if(sample<v->offset_ms) v->offset_ms=sample;
    else v->offset_ms+=.002*(sample-v->offset_ms); // slow upward drift tracking
    // jitter_ms tracks the 95th percentile of lateness (stochastic quantile
    // estimate: up 0.95 ms per late sample, down 0.05 ms otherwise).
    double late=sample-v->offset_ms;
    v->jitter_ms+=late>v->jitter_ms ? .95 : -.05;
    if(v->jitter_ms<0) v->jitter_ms=0;
}
void swat_net_view_advance(SwatNetView* v,double now_ms,double frame_ms,bool interpolate) {
    if(!v->clock_ready) return;
    // Presentation time advances by the smooth frame duration; the (possibly
    // coarse) clock only steers it through the bounded time warp below.
    double dt=frame_ms<0 ? 0 : frame_ms; v->last_ms=now_ms;
    if(!interpolate) { v->render_tick=v->newest_tick; return; }
    // One snapshot interval plus the 95th-percentile lateness, plus up to
    // another interval as loss rises so a single drop rarely extrapolates.
    double target=2*TICK_MS+v->jitter_ms+2*TICK_MS*fmin(1,v->loss*10)+4;
    target=fmax(VIEW_DELAY_MIN,fmin(VIEW_DELAY_MAX,target));
    v->delay_ms+=(target>v->delay_ms ? .05 : .01)*(target-v->delay_ms);
    double goal=(now_ms-v->offset_ms-v->delay_ms)/TICK_MS;
    double next=v->render_tick+dt/TICK_MS;
    double error=goal-next;
    if(fabs(error)>15) next=goal; // >250 ms: resynchronise outright
    else next+=fmax(-.08,fmin(.08,error*.1))*dt/TICK_MS; // +-8% time warp
    if(next>v->render_tick) v->render_tick=next;
}
static float lerp_angle(float a,float b,float t) {
    float d=fmodf(b-a+3*SWAT_PI,2*SWAT_PI)-SWAT_PI; return swat_angle(a+d*t);
}
static b3Pos hermite(b3Pos p0,b3Vec3 v0,b3Pos p1,b3Vec3 v1,float t,float span,b3Vec3* velocity) {
    float t2=t*t,t3=t2*t;
    float h00=2*t3-3*t2+1,h10=t3-2*t2+t,h01=-2*t3+3*t2,h11=t3-t2;
    b3Vec3 d=b3SubPos(p1,p0);
    b3Vec3 offset=swat_add(swat_add(swat_mul(v0,h10*span),swat_mul(d,h01)),swat_mul(v1,h11*span));
    (void)h00;
    float g00=6*t2-6*t,g10=3*t2-4*t+1,g01=-6*t2+6*t,g11=3*t2-2*t;
    *velocity=swat_add(swat_add(swat_mul(v0,g10),swat_mul(d,g01/span)),swat_mul(v1,g11));
    (void)g00;
    return b3OffsetPos(p0,offset);
}
SwatViewPose swat_net_view_sample(SwatNetView* v,int actor,double frame_ms,bool interpolate) {
    SwatViewPose out={0};
    if(!v->count) return out;
    const SwatViewFrame* newest=&v->frames[v->count-1];
    const SwatViewActor* last=&newest->actors[actor];
    SwatViewPose raw={.position=last->position,.velocity=last->velocity,.yaw=last->yaw,.pitch=last->pitch,.valid=last->present};
    bool extrapolating=false;
    if(interpolate) {
        double t=v->render_tick; int i=v->count-1;
        while(i>0 && v->frames[i-1].tick>t) i--;
        if(t>=newest->tick) {
            double ahead=fmin((t-newest->tick)*TICK_MS,VIEW_EXTRAPOLATE_MS)/1000.0;
            extrapolating=t>newest->tick && last->alive;
            if(last->alive) raw.position=b3OffsetPos(last->position,swat_mul(swat_v(last->velocity.x,0,last->velocity.z),(float)ahead));
        } else if(i==0) {
            const SwatViewActor* a=&v->frames[0].actors[actor];
            raw=(SwatViewPose){.position=a->position,.velocity=a->velocity,.yaw=a->yaw,.pitch=a->pitch,.valid=a->present};
        } else {
            const SwatViewFrame* f0=&v->frames[i-1]; const SwatViewFrame* f1=&v->frames[i];
            const SwatViewActor* a=&f0->actors[actor]; const SwatViewActor* b=&f1->actors[actor];
            float span=(float)((f1->tick-f0->tick)*TICK_MS/1000.0);
            float u=(float)((t-f0->tick)/(f1->tick-f0->tick));
            if(!a->present || !b->present || !a->alive || b3Distance(a->position,b->position)>VIEW_SNAP_METRES) {
                const SwatViewActor* c=u<1 ? a : b; if(!a->present) c=b;
                raw=(SwatViewPose){.position=c->position,.velocity=c->velocity,.yaw=c->yaw,.pitch=c->pitch,.valid=c->present};
            } else {
                raw.position=hermite(a->position,a->velocity,b->position,b->velocity,u,span,&raw.velocity);
                raw.yaw=lerp_angle(a->yaw,b->yaw,u); raw.pitch=a->pitch+(b->pitch-a->pitch)*u;
                raw.valid=true;
            }
        }
    }
    out=raw;
    if(!raw.valid) { v->shown_valid[actor]=false; v->error[actor]=swat_v(0,0,0); return out; }
    // Continuity: when fresh data replaces an extrapolation, the difference
    // between where the actor was heading and the new curve becomes an offset
    // that decays smoothly. Teleports and respawns snap.
    float dt=(float)fmax(0,frame_ms)/1000.0f;
    if(interpolate && v->shown_valid[actor]) {
        b3Pos continued=b3OffsetPos(v->shown[actor],swat_mul(raw.velocity,dt));
        b3Vec3 discontinuity=b3SubPos(continued,raw.position);
        if(b3Length(b3SubPos(v->shown[actor],raw.position))>VIEW_SNAP_METRES) { v->error[actor]=swat_v(0,0,0); v->snaps++; }
        else if(v->extrapolating[actor] && !extrapolating) v->error[actor]=discontinuity;
        else v->error[actor]=swat_mul(v->error[actor],expf(-dt/VIEW_BLEND_TAU));
        if(b3Length(v->error[actor])<1e-4f) v->error[actor]=swat_v(0,0,0);
    } else v->error[actor]=swat_v(0,0,0);
    if(extrapolating) v->extrapolated_frames++;
    v->extrapolating[actor]=extrapolating;
    out.position=b3OffsetPos(raw.position,v->error[actor]); out.valid=true;
    v->shown[actor]=out.position; v->shown_valid[actor]=true;
    return out;
}

// Bracketing frames for the presented time: f0 at or before it, f1 after
// (equal past either end), u the share of the interval elapsed.
static void view_bracket(const SwatNetView* v,bool interpolate,int* i0,int* i1,float* u) {
    *i0=*i1=v->count-1; *u=0;
    if(!interpolate || v->count<2) return;
    double t=v->render_tick;
    if(t>=v->frames[v->count-1].tick) return;
    if(t<=v->frames[0].tick) { *i0=*i1=0; return; }
    int i=v->count-1; while(i>0 && v->frames[i-1].tick>t) i--;
    *i0=i-1; *i1=i; *u=(float)((t-v->frames[i-1].tick)/(v->frames[i].tick-v->frames[i-1].tick));
}
static float lerpf(float a,float b,float t) { return a+(b-a)*t; }
void swat_net_view_sample_state(const SwatNetView* v,int actor,SwatViewPose* pose) {
    if(!v->count) return;
    int i0,i1; float u; view_bracket(v,true,&i0,&i1,&u);
    const SwatViewActor* a=&v->frames[i0].actors[actor]; const SwatViewActor* b=&v->frames[i1].actors[actor];
    if(!b->present || !a->present) a=b;
    pose->state=true;
    // Discrete state switches when the presented time reaches the frame that
    // carries it, not when the packet arrives.
    pose->crouched=a->crouched; pose->grounded=a->grounded; pose->sprinting=a->sprinting; pose->mantle=a->mantle;
    pose->height_blend=lerpf(a->crouched ? 1.0f : 0.0f,b->crouched ? 1.0f : 0.0f,u);
    pose->upper_offset=swat_add(a->upper_offset,swat_mul(swat_add(b->upper_offset,swat_mul(a->upper_offset,-1)),u));
    pose->eye_height=lerpf(a->eye_height,b->eye_height,u); pose->lean=lerpf(a->lean,b->lean,u);
    pose->ads=lerpf(a->ads,b->ads,u); pose->ready_blend=lerpf(a->ready_blend,b->ready_blend,u);
    if(a->mantle.active && b->mantle.active && a->mantle.total==b->mantle.total)
        pose->mantle.tick=(int)lrintf(lerpf((float)a->mantle.tick,(float)b->mantle.tick,u));
}
void swat_net_view_present_actor(SwatSim* sim,int actor,const SwatViewPose* pose) {
    SwatActor* a=&sim->actors[actor]; SwatController* c=&a->controller;
    b3Body_SetTransform(c->body.body,pose->position,b3Quat_identity);
    b3Body_SetLinearVelocity(c->body.body,pose->velocity);
    c->yaw=pose->yaw; c->pitch=pose->pitch;
    if(!pose->state) return;
    // Presentation fields only: the replica's shapes keep the applied
    // stance, and the next snapshot's swat_body_replica_pose rebuilds them
    // whenever these differ.
    c->body.totalHeight=c->body.standHeight+(c->body.crouchHeight-c->body.standHeight)*swat_clamp(pose->height_blend,0,1);
    c->body.crouched=c->body.crouchWish=pose->height_blend>=.5f;
    c->body.upperOffset=pose->upper_offset; c->body.onGround=pose->grounded;
    c->eye_height=pose->eye_height; c->lean=pose->lean; c->ads=pose->ads; c->ready_blend=pose->ready_blend;
    c->sprinting=pose->sprinting; c->mantle=pose->mantle;
}
static b3Quat nlerp(b3Quat a,b3Quat b,float t) {
    float dot=a.v.x*b.v.x+a.v.y*b.v.y+a.v.z*b.v.z+a.s*b.s; float sign=dot<0 ? -1.0f : 1.0f;
    b3Quat q={{a.v.x+(sign*b.v.x-a.v.x)*t,a.v.y+(sign*b.v.y-a.v.y)*t,a.v.z+(sign*b.v.z-a.v.z)*t},a.s+(sign*b.s-a.s)*t};
    float n=sqrtf(q.v.x*q.v.x+q.v.y*q.v.y+q.v.z*q.v.z+q.s*q.s); if(n<1e-6f) return b;
    q.v.x/=n; q.v.y/=n; q.v.z/=n; q.s/=n; return q;
}
void swat_net_view_present_doors(SwatNetView* v,SwatSim* sim) {
    if(v->count<2) return;
    int i0,i1; float u; view_bracket(v,true,&i0,&i1,&u);
    const SwatViewFrame* f0=&v->frames[i0]; const SwatViewFrame* f1=&v->frames[i1];
    for(int k=0;k<v->door_count;k++) {
        int index=v->door_object[k]; if(index>=sim->world.count) continue;
        SwatObject* o=&sim->world.objects[index];
        const SwatViewDoor* a=&f0->doors[k]; const SwatViewDoor* b=&f1->doors[k];
        if(!o->active || !o->door || !a->active || !b->active || B3_IS_NULL(o->body)) continue;
        if(!a->moving && !b->moving && !o->motion) {
            // Ordinary leaf: the same pose math the snapshot applies. Past
            // the newest snapshot it keeps swinging at its last rate for at
            // most VIEW_EXTRAPOLATE_MS (as actors do), so the next arrival
            // does not pop it.
            float angle=lerpf(a->angle,b->angle,u);
            if(i0==i1 && i1==v->count-1 && v->render_tick>f1->tick) {
                const SwatViewFrame* f=&v->frames[v->count-2]; const SwatViewDoor* p=&f->doors[k];
                double ahead=fmin(v->render_tick-f1->tick,VIEW_EXTRAPOLATE_MS/TICK_MS);
                if(p->active && !p->moving && f1->tick>f->tick)
                    angle=swat_clamp(b->angle+(b->angle-p->angle)*(float)(ahead/(f1->tick-f->tick)),0,SWAT_PI*.5f);
            }
            if(fabsf(angle-o->door_angle)<1e-6f) continue;
            o->door_angle=angle; o->yaw=o->closed_yaw+angle; o->center=swat_door_center(o,o->yaw);
            swat_world_tilt(o,o->pitch);
        } else if(a->moving && b->moving && o->motion) {
            if(b3Distance(a->position,b->position)>1.0f) continue; // knocked off: no invented sweep
            b3Pos p={a->position.x+(b->position.x-a->position.x)*u,a->position.y+(b->position.y-a->position.y)*u,
                a->position.z+(b->position.z-a->position.z)*u};
            b3Quat q=nlerp(a->rotation,b->rotation,u);
            o->center=p; o->motion->state.position=p; o->motion->state.rotation=q;
            b3Vec3 normal=b3RotateVector(q,swat_v(1,0,0));
            if(normal.x*normal.x+normal.z*normal.z>1e-8f) o->yaw=b3Atan2(-normal.z,normal.x);
            b3Body_SetTransform(o->body,p,q);
        }
    }
}

// ---------------------------------------------------------------------------
// Local prediction
// ---------------------------------------------------------------------------
void swat_predictor_reset(SwatPredictor* p) {
    if(p->world) { swat_sim_close(p->world); free(p->world); }
    free(p->map);
    bool shared=p->shared; memset(p,0,sizeof(*p)); p->shared=shared;
}
void swat_predictor_base(SwatPredictor* p,const SwatActorState* state) { p->base=*state; p->has_base=true; }
static bool predictor_build(SwatPredictor* p) {
    if(!p->map) return false;
    if(!p->world && !(p->world=calloc(1,sizeof(SwatSim)))) return false;
    swat_apply_map(p->world,p->map);
    p->rebuilds++; p->valid=false;
    return true;
}
bool swat_predictor_map(SwatPredictor* p,const SwatMap* map) {
    if(!p->map && !(p->map=malloc(sizeof(SwatMap)))) return false;
    memcpy(p->map,map,sizeof(*map));
    for(int i=0;i<SWAT_PREDICT_RING;i++) p->ring_seq[i]=0;
    p->has_base=false; p->valid=false;
    return predictor_build(p);
}
void swat_predictor_snapshot(SwatPredictor* p,const SwatSnapshot* state,int actor,uint32_t ack) {
    if(actor<0 || actor>=SWAT_MAX_ACTORS) return;
    swat_predictor_base(p,&state->actors[actor]);
    p->actor=actor; p->ack=ack;
    if(!p->world) return;
    // The private world follows authority exactly. A snapshot the predicted
    // world refuses (prediction moved a leaf the authority never moved, say)
    // is applied to a freshly built world instead.
    if(!swat_apply_snapshot(p->world,state) && (!predictor_build(p) || !swat_apply_snapshot(p->world,state))) {
        swat_sim_close(p->world); free(p->world); p->world=NULL; p->valid=false; return;
    }
    p->world->tick=state->tick;
    p->rebased=true;
}
const SwatController* swat_predictor_controller(const SwatPredictor* p) {
    return p->shared && p->world && p->valid ? &p->world->actors[p->actor].controller : NULL;
}
typedef struct PredictCast { b3BodyId self; float fraction; bool hit; } PredictCast;
static float predict_cast(b3ShapeId shape,b3Pos point,b3Vec3 normal,float fraction,uint64_t material,int triangle,int child,void* context) {
    PredictCast* c=context;
    if(B3_ID_EQUALS(b3Shape_GetBody(shape),c->self) || b3Shape_IsSensor(shape)) return -1;
    if(fraction<c->fraction) { c->fraction=fraction; c->hit=true; }
    return fraction;
}
static b3Vec3 predict_move(const SwatBody* body,b3Pos from,b3Vec3 delta) {
    float length=b3Length(delta);
    if(length<1e-6f || B3_IS_NULL(body->capsuleId)) return delta;
    b3Capsule capsule=b3Shape_GetCapsule(body->capsuleId);
    b3Vec3 points[2]={capsule.center1,capsule.center2};
    b3ShapeProxy proxy={points,2,capsule.radius};
    for(int slide=0;slide<2;slide++) {
        PredictCast cast={body->body,1,false};
        b3World_CastShape(body->world,from,&proxy,delta,(b3QueryFilter){.categoryBits=UINT64_MAX,.maskBits=body->queryMask},predict_cast,&cast);
        if(!cast.hit) return delta;
        // Stop at the contact and drop the blocked part (one slide attempt
        // along the axis that remains free).
        b3Vec3 x=swat_v(delta.x,0,0),z=swat_v(0,0,delta.z);
        delta=fabsf(delta.x)>fabsf(delta.z) ? z : x;
        if(slide==1) return swat_v(0,0,0);
    }
    return swat_v(0,0,0);
}
static void predict_step(SwatPredictState* s,const SwatController* c,const SwatInput* in) {
    s->yaw=swat_angle(s->yaw+swat_clamp(in->yaw_delta,-.4f,.4f));
    // Crouching is immediate; standing needs clearance the authority checks.
    s->crouched=in->crouch || (s->crouched && !swat_body_clear_at(&c->body,swat_body_feet_position(&c->body)));
    bool sprint=in->gait==SWAT_SPRINT && in->forward>.1f && !s->crouched && !in->aim && !in->fire && !in->reload &&
        fabsf(in->lean)<.01f && s->stamina>.05f;
    s->stamina=swat_clamp(s->stamina+(sprint ? -.16f : .10f)*SWAT_DT,0,1);
    float speed=s->crouched ? 1.25f : sprint ? 4.6f : in->gait==SWAT_SLOW ? 1.2f : in->aim ? 1.7f : 2.8f;
    speed*=c->mobility;
    b3CosSin h=b3ComputeCosSin(s->yaw);
    b3Vec3 forward=swat_v(h.cosine,0,h.sine),right=swat_v(-h.sine,0,h.cosine);
    float f=swat_clamp(in->forward,-1,1),r=swat_clamp(in->strafe,-1,1);
    b3Vec3 wish=swat_add(swat_mul(forward,speed*f),swat_mul(right,speed*r));
    float wl=b3Length(wish); if(wl>speed) wish=swat_mul(wish,speed/wl);
    float accel=s->ground ? (b3Length(wish)<.001f ? 30.0f : 22.0f) : 4.5f;
    b3Vec3 change=swat_add(wish,swat_mul(s->velocity,-1)); change.y=0;
    float cl=b3Length(change),limit=accel/60.0f;
    if(cl>limit) change=swat_mul(change,limit/cl);
    s->velocity=swat_add(s->velocity,change);
    b3Vec3 want=swat_mul(s->velocity,SWAT_DT);
    b3Vec3 step=predict_move(&c->body,s->position,want);
    if(b3Length(step)+1e-5f<b3Length(want)) s->velocity=swat_mul(step,1/SWAT_DT);
    s->position=b3OffsetPos(s->position,step);
}
// Inputs for the other actors while the private world replays: each keeps
// walking the way the newest snapshot shows it moving (dead reckoning through
// the same controller), so a shove meets a body with the right momentum.
static SwatInput predict_other_input(const SwatActor* a) {
    SwatInput in=swat_neutral_input();
    b3Vec3 v=b3Body_GetLinearVelocity(a->controller.body.body);
    float speed=sqrtf(v.x*v.x+v.z*v.z);
    if(speed<.15f || !a->controller.body.onGround) return in;
    b3CosSin h=b3ComputeCosSin(a->controller.yaw);
    float top=a->controller.body.crouched ? 1.25f : speed>3.2f ? 4.6f : 2.8f;
    in.forward=swat_clamp((v.x*h.cosine+v.z*h.sine)/top,-1,1);
    in.strafe=swat_clamp((-v.x*h.sine+v.z*h.cosine)/top,-1,1);
    in.crouch=a->controller.body.crouched;
    if(speed>3.2f && in.forward>.1f) in.gait=SWAT_SPRINT;
    return in;
}
// Fastest presented local motion (m/s): sprint plus a jump's rise.
#define PREDICT_PRESENT_SPEED 6.5f
static void predict_world_step(SwatPredictor* p,const SwatCommand* cmd) {
    SwatSim* w=p->world; SwatInput inputs[SWAT_MAX_ACTORS];
    for(int i=0;i<SWAT_MAX_ACTORS;i++) inputs[i]=i<w->actor_count && i!=p->actor && w->actors[i].alive ?
        predict_other_input(&w->actors[i]) : swat_neutral_input();
    inputs[p->actor]=cmd->input;
    swat_sim_step_movement(w,inputs);
    int k=(int)(cmd->sequence%SWAT_PREDICT_RING);
    p->ring_seq[k]=cmd->sequence; p->ring_input[k]=cmd->input;
    p->ring_pos[k]=b3Body_GetPosition(w->actors[p->actor].controller.body.body);
    p->steps++;
}
static bool ring_get(const SwatPredictor* p,uint32_t seq,b3Pos* out) {
    int k=(int)(seq%SWAT_PREDICT_RING);
    if(!seq || p->ring_seq[k]!=seq) return false;
    *out=p->ring_pos[k]; return true;
}
static b3Pos predict_world(SwatPredictor* p,const SwatCommand* pending,int head,int count,int ring,float frame_dt,float alpha) {
    SwatSim* w=p->world; SwatActor* a=&w->actors[p->actor];
    if(!p->has_base || !p->base.present || !p->base.alive || !a->present || !a->alive) {
        p->valid=false; p->last_raw_valid=false; p->offset=swat_v(0,0,0); return p->base.position;
    }
    if(p->rebased) {
        // Rewind: the world now holds the authority after command `ack`.
        // Edge state the snapshot does not carry comes from that command, so
        // a held jump or use is not mistaken for a fresh press.
        int k=(int)(p->ack%SWAT_PREDICT_RING);
        if(p->ack && p->ring_seq[k]==p->ack) {
            a->controller.last_jump=p->ring_input[k].jump; a->last_interact=p->ring_input[k].interact;
        }
        b3Pos before; bool had=p->valid && ring_get(p,p->sequence,&before);
        uint32_t old=p->sequence;
        p->ring_seq[k]=p->ack; p->ring_pos[k]=b3Body_GetPosition(a->controller.body.body);
        if(!p->ack) p->ring_seq[k]=0;
        p->sequence=p->ack;
        for(int i=0;i<count;i++) {
            const SwatCommand* cmd=&pending[(head+i)%ring];
            if(cmd->sequence<=p->ack) continue;
            predict_world_step(p,cmd); p->sequence=cmd->sequence;
        }
        p->replays++; p->rebased=false;
        b3Pos after; bool have_after=false;
        if(had && old==p->ack) { after=p->ring_pos[k]; have_after=true; }
        else if(had && old>p->ack && old<=p->sequence) have_after=ring_get(p,old,&after);
        if(have_after) {
            b3Vec3 correction=b3SubPos(before,after);
            float jump=b3Length(correction);
            if(jump>1.0f) p->offset=swat_v(0,0,0); // teleport/respawn: snap
            else {
                p->offset=swat_add(p->offset,correction);
                if(jump>.001f) { p->corrections++; p->correction_sum+=jump; if(jump>p->correction_max) p->correction_max=jump; }
            }
            p->correction_last=jump;
        } else p->offset=swat_v(0,0,0);
    } else {
        // Continue: only the commands issued since the last frame.
        for(int i=0;i<count;i++) {
            const SwatCommand* cmd=&pending[(head+i)%ring];
            if(cmd->sequence<=p->sequence) continue;
            predict_world_step(p,cmd); p->sequence=cmd->sequence;
        }
    }
    if(p->valid) {
        p->offset=swat_mul(p->offset,expf(-frame_dt/.1f));
        if(b3Length(p->offset)<1e-4f) p->offset=swat_v(0,0,0);
    }
    b3Pos newest=b3Body_GetPosition(a->controller.body.body),previous=newest;
    if(p->sequence>p->ack) ring_get(p,p->sequence-1,&previous);
    p->position=newest; p->valid=true; p->base_tick=(uint32_t)w->tick;
    alpha=swat_clamp(alpha,0,1);
    b3Pos shown={previous.x+(newest.x-previous.x)*alpha,previous.y+(newest.y-previous.y)*alpha,
        previous.z+(newest.z-previous.z)*alpha};
    // Stair smoothing: the controller's step-up lifts (and Box3D's
    // depenetration can push) the body up to ~0.3 m in one tick. The
    // prediction keeps that exact; only the presented pose spreads anything
    // faster than a sprint over the offset's decay, as for a correction.
    if(p->last_raw_valid && frame_dt>0) {
        b3Vec3 move=b3SubPos(shown,p->last_raw);
        float length=b3Length(move),allow=PREDICT_PRESENT_SPEED*frame_dt;
        if(length>allow && length<1.0f) {
            p->offset=swat_add(p->offset,swat_mul(move,-(1-allow/length))); p->smoothed++;
        }
    }
    p->last_raw=shown; p->last_raw_valid=true;
    return b3OffsetPos(shown,p->offset);
}
b3Pos swat_predict_local(SwatPredictor* p,const SwatSim* sim,int actor,const SwatCommand* pending,int head,int count,int ring,float frame_dt,float alpha) {
    if(p->shared && p->world && actor==p->actor) return predict_world(p,pending,head,count,ring,frame_dt,alpha);
    const SwatActor* a=&sim->actors[actor];
    const SwatController* c=&a->controller;
    const SwatActorState* b=&p->base;
    if(!p->has_base || !b->present || !b->alive || b->mantle.active || !a->present) {
        p->valid=false; p->offset=swat_v(0,0,0); return b->position;
    }
    // Full re-simulation from the newest authority over every pending command.
    SwatPredictState full={b->position,swat_v(b->velocity.x,0,b->velocity.z),b->yaw,b->stamina,b->crouched,b->grounded};
    b3Pos previous=full.position;
    for(int i=0;i<count;i++) { previous=full.position; predict_step(&full,c,&pending[(head+i)%ring].input); }
    // With nothing pending the state is the base itself: sequence 0 makes the
    // next continuation step every later command from it.
    uint32_t newest=count ? pending[(head+count-1)%ring].sequence : 0;
    // Reconciliation: continue last frame's prediction through the same
    // commands; any difference to the re-simulation is a correction. It
    // joins a decaying offset instead of moving the camera at once.
    if(p->valid) {
        SwatPredictState continued=p->state;
        for(int i=0;i<count;i++) {
            const SwatCommand* cmd=&pending[(head+i)%ring];
            if(cmd->sequence>p->sequence) predict_step(&continued,c,&cmd->input);
        }
        b3Vec3 correction=b3SubPos(continued.position,full.position);
        float jump=b3Length(correction);
        if(jump>1.0f) p->offset=swat_v(0,0,0); // teleport/respawn: snap
        else {
            p->offset=swat_add(p->offset,correction);
            if(jump>.001f) { p->corrections++; p->correction_sum+=jump; if(jump>p->correction_max) p->correction_max=jump; }
        }
        p->correction_last=jump;
        p->offset=swat_mul(p->offset,expf(-frame_dt/.1f));
        if(b3Length(p->offset)<1e-4f) p->offset=swat_v(0,0,0);
    } else p->offset=swat_v(0,0,0);
    p->state=full; p->sequence=newest; p->position=full.position; p->valid=true; p->base_tick=(uint32_t)sim->tick;
    // Present between the last two predicted ticks by the elapsed fraction of
    // the current tick, so 60 Hz prediction moves smoothly at any frame rate.
    alpha=swat_clamp(alpha,0,1);
    b3Pos shown={previous.x+(full.position.x-previous.x)*alpha,previous.y+(full.position.y-previous.y)*alpha,
        previous.z+(full.position.z-previous.z)*alpha};
    return b3OffsetPos(shown,p->offset);
}
