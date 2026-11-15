#include "netcode.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

SwatNetTuning swat_net_tuning_default(void) {
    return (SwatNetTuning){.delta=true,.redundancy=true,.drain=true,.interpolate=true,.predict=true,.lag_comp=true};
}
SwatNetTuning swat_net_tuning_legacy(void) { return (SwatNetTuning){0}; }

// ---------------------------------------------------------------------------
// Server command queue
// ---------------------------------------------------------------------------
// Window over which the queue's minimum depth is measured. A minimum above
// SWAT_DRAIN_SLACK means every command in that surplus waited a full window
// for nothing: a delivery burst (lost reliable packet, jitter spike) left the
// player permanently further behind. Merging pairs removes it again.
#define SWAT_DRAIN_WINDOW 60
#define SWAT_DRAIN_SLACK 2

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
SwatInput swat_remote_take(SwatRemoteSlot* r,uint32_t now_ms,bool drain) {
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
        r->head=(r->head+1)%SWAT_INPUT_QUEUE; r->count--;
    } else {
        r->starved++; r->view_tick=0;
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

void swat_net_view_reset(SwatNetView* v) { memset(v,0,sizeof(*v)); }
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
        f->actors[i]=(SwatViewActor){a->position,a->velocity,a->yaw,a->pitch,a->present,a->alive};
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
    double late=sample-v->offset_ms;
    v->jitter_ms+=(late>v->jitter_ms ? .1 : .01)*(late-v->jitter_ms);
}
void swat_net_view_advance(SwatNetView* v,double now_ms,bool interpolate) {
    if(!v->clock_ready) return;
    double dt=now_ms-v->last_ms; v->last_ms=now_ms; if(dt<0) dt=0;
    if(!interpolate) { v->render_tick=v->newest_tick; return; }
    // Two snapshot intervals plus the observed lateness, plus one more interval
    // per lost snapshot share so a single drop still interpolates.
    double target=2*2*TICK_MS*.5+2.2*v->jitter_ms+2*2*TICK_MS*fmin(1,v->loss*4)+4;
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
    SwatViewPose raw={last->position,last->velocity,last->yaw,last->pitch,last->present};
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
            raw=(SwatViewPose){a->position,a->velocity,a->yaw,a->pitch,a->present};
        } else {
            const SwatViewFrame* f0=&v->frames[i-1]; const SwatViewFrame* f1=&v->frames[i];
            const SwatViewActor* a=&f0->actors[actor]; const SwatViewActor* b=&f1->actors[actor];
            float span=(float)((f1->tick-f0->tick)*TICK_MS/1000.0);
            float u=(float)((t-f0->tick)/(f1->tick-f0->tick));
            if(!a->present || !b->present || !a->alive || b3Distance(a->position,b->position)>VIEW_SNAP_METRES) {
                const SwatViewActor* c=u<1 ? a : b; if(!a->present) c=b;
                raw=(SwatViewPose){c->position,c->velocity,c->yaw,c->pitch,c->present};
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

// ---------------------------------------------------------------------------
// Local prediction
// ---------------------------------------------------------------------------
void swat_predictor_reset(SwatPredictor* p) { memset(p,0,sizeof(*p)); }
void swat_predictor_base(SwatPredictor* p,const SwatActorState* state) { p->base=*state; p->has_base=true; }
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
b3Pos swat_predict_local(SwatPredictor* p,const SwatSim* sim,int actor,const SwatCommand* pending,int head,int count,int ring,float frame_dt) {
    const SwatActor* a=&sim->actors[actor];
    const SwatController* c=&a->controller;
    const SwatActorState* b=&p->base;
    b3Pos position=b->position;
    if(!p->has_base || !b->present || !b->alive || b->mantle.active || !a->present) { p->valid=false; p->offset=swat_v(0,0,0); return position; }
    b3Vec3 v=b->velocity; v.y=0;
    float yaw=b->yaw,stamina=b->stamina;
    bool crouched=b->crouched,ground=b->grounded;
    for(int i=0;i<count;i++) {
        const SwatInput* in=&pending[(head+i)%ring].input;
        yaw=swat_angle(yaw+swat_clamp(in->yaw_delta,-.4f,.4f));
        if(in->crouch) crouched=true; // standing up needs clearance: wait for authority
        bool sprint=in->gait==SWAT_SPRINT && in->forward>.1f && !crouched && !in->aim && !in->fire && !in->reload &&
            fabsf(in->lean)<.01f && stamina>.05f;
        stamina=swat_clamp(stamina+(sprint ? -.16f : .10f)*SWAT_DT,0,1);
        float speed=crouched ? 1.25f : sprint ? 4.6f : in->gait==SWAT_SLOW ? 1.2f : in->aim ? 1.7f : 2.8f;
        speed*=c->mobility;
        b3CosSin h=b3ComputeCosSin(yaw);
        b3Vec3 forward=swat_v(h.cosine,0,h.sine),right=swat_v(-h.sine,0,h.cosine);
        float f=swat_clamp(in->forward,-1,1),s=swat_clamp(in->strafe,-1,1);
        b3Vec3 wish=swat_add(swat_mul(forward,speed*f),swat_mul(right,speed*s));
        float wl=b3Length(wish); if(wl>speed) wish=swat_mul(wish,speed/wl);
        float accel=ground ? (b3Length(wish)<.001f ? 30.0f : 22.0f) : 4.5f;
        b3Vec3 change=swat_add(wish,swat_mul(v,-1)); change.y=0;
        float cl=b3Length(change),limit=accel/60.0f;
        if(cl>limit) change=swat_mul(change,limit/cl);
        v=swat_add(v,change);
        b3Vec3 step=predict_move(&c->body,position,swat_mul(v,SWAT_DT));
        if(b3Length(step)+1e-5f<b3Length(swat_mul(v,SWAT_DT))) v=swat_mul(step,1/SWAT_DT);
        position=b3OffsetPos(position,step);
    }
    // Reconciliation: the new prediction differs from what was shown last
    // frame by (old position + offset) - new position. Fold it into the
    // offset and decay it, instead of snapping the camera.
    if(p->valid) {
        b3Pos shown=b3OffsetPos(p->position,p->offset);
        b3Vec3 offset=b3SubPos(shown,position);
        float jump=b3Length(swat_add(offset,swat_mul(p->offset,-1)));
        if(b3Length(offset)>1.0f) offset=swat_v(0,0,0); // teleport/respawn: snap
        p->correction_last=jump; p->correction_sum+=jump; if(jump>p->correction_max) p->correction_max=jump;
        if(jump>.001f) p->corrections++;
        p->offset=swat_mul(offset,expf(-frame_dt/.1f));
    } else p->offset=swat_v(0,0,0);
    p->position=position; p->valid=true; p->base_tick=(uint32_t)sim->tick;

    return b3OffsetPos(position,p->offset);
}
