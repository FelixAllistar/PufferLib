// Weapon test range (SWAT_WEAPON_RANGE): controller and weapon determinism.
//  1. A recorded human input stream replayed through the actor input path
//     (swat_sim_step_inputs) gives the same authoritative state every tick,
//     and so does a stream produced by the policy action decoder.
//  2. The same human timeline paced at different frame rates (pacing.h), with
//     read-only view queries between ticks, gives the same state.
//  3. Reload interruptions at every stage boundary, for every cause, conserve
//     ammunition and keep magazine/reserve counts legal, also on replicas.
//  4. Shots from a muzzle inside or just behind tight cover resolve the same
//     for the shooter, the target replica and the replay: no hits through
//     walls.
#include "replay.h"
#include "pacing.h"
#include "pose.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* scratch_base="weapon-range-test";
static void scratch(char* out,size_t capacity,const char* suffix) {
    snprintf(out,capacity,"%s-%s.sgrp",scratch_base,suffix);
}
static SwatConfig range_config(void) {
    SwatConfig config=swat_default_config();
    config.mission=SWAT_WEAPON_RANGE; config.randomize=false; config.max_ticks=100000;
    return config;
}
static bool same_bits(const void* a,const void* b,size_t size) { return !memcmp(a,b,size); }
#define SAME(a,b) assert(same_bits(&(a),&(b),sizeof(a)))

static void assert_same_weapon(const SwatWeapon* a,const SwatWeapon* b) {
    assert(a->magazine==b->magazine && a->reserve==b->reserve && a->cooldown==b->cooldown);
    assert(a->reload_remaining==b->reload_remaining && a->reload_duration==b->reload_duration);
    assert(a->chambered==b->chambered && a->mode==b->mode && a->reload_stage==b->reload_stage);
    assert(a->magazine_seated==b->magazine_seated && a->use_magazines==b->use_magazines);
    assert(a->magazine_count==b->magazine_count && !memcmp(a->magazines,b->magazines,sizeof(a->magazines)));
    assert(a->events==b->events);
}
// Authoritative state that matters to rules: bodies, aim, ammunition, hits and
// damage. Compared bit for bit; the replay digest covers the replicated rest.
static void assert_same_state(const SwatSim* a,const SwatSim* b) {
    assert(a->tick==b->tick && a->end==b->end && a->actor_count==b->actor_count && a->rng==b->rng);
    SAME(a->totals.hostile_damage,b->totals.hostile_damage); SAME(a->totals.officer_damage,b->totals.officer_damage);
    assert(a->totals.shots==b->totals.shots && a->totals.hostile_down==b->totals.hostile_down && a->totals.destroyed==b->totals.destroyed);
    for(int i=0;i<a->actor_count;i++) {
        const SwatActor* x=&a->actors[i]; const SwatActor* y=&b->actors[i];
        assert(x->present==y->present && x->alive==y->alive); SAME(x->health,y->health);
        if(!x->present) continue;
        b3Pos px=b3Body_GetPosition(x->controller.body.body),py=b3Body_GetPosition(y->controller.body.body);
        b3Vec3 vx=b3Body_GetLinearVelocity(x->controller.body.body),vy=b3Body_GetLinearVelocity(y->controller.body.body);
        SAME(px,py); SAME(vx,vy);
        const SwatController* c=&x->controller; const SwatController* d=&y->controller;
        SAME(c->yaw,d->yaw); SAME(c->pitch,d->pitch); SAME(c->ads,d->ads); SAME(c->stamina,d->stamina);
        SAME(c->lean,d->lean); SAME(c->recoil_pitch,d->recoil_pitch); SAME(c->recoil_yaw,d->recoil_yaw);
        SAME(c->ready_blend,d->ready_blend); assert(c->muzzle_blocked==d->muzzle_blocked && c->sprinting==d->sprinting);
        assert(c->body.crouched==d->body.crouched && c->body.onGround==d->body.onGround);
        const SwatArsenal* r=&x->arsenal; const SwatArsenal* s=&y->arsenal;
        assert(r->active==s->active && r->equip_remaining==s->equip_remaining && r->primary==s->primary);
        assert(r->shots==s->shots && r->rng==s->rng && r->fire_buffer_ticks==s->fire_buffer_ticks && r->reload_buffer_ticks==s->reload_buffer_ticks);
        for(int k=0;k<2;k++) assert_same_weapon(&r->slots[k],&s->slots[k]);
        SAME(x->gear.wounds,y->gear.wounds);
        SAME(x->tracer_start,y->tracer_start); SAME(x->tracer_end,y->tracer_end);
        assert(x->last_shot_tick==y->last_shot_tick);
    }
    for(int i=0;i<a->world.count;i++) {
        assert(a->world.objects[i].active==b->world.objects[i].active && a->world.objects[i].door_open==b->world.objects[i].door_open);
        SAME(a->world.objects[i].health,b->world.objects[i].health); SAME(a->world.objects[i].door_angle,b->world.objects[i].door_angle);
    }
}

// A human-like stream: continuous non-dyadic mouse look, held movement, ADS,
// taps on the open and waist-cover lanes, a reload, a reload interrupted by a
// weapon switch, sprint, crouch, lean, jump, and opening the door behind.
static SwatInput human_input(int t) {
    SwatInput in=swat_neutral_input();
    float wobble=.0011f*sinf(t*.37f)+.0007f*cosf(t*.11f);
    in.yaw_delta=wobble; in.pitch_delta=-.6f*wobble;
    in.aim=t>=10 && t<330;
    in.fire=t==60 || t==75 || t==90 || t==270 || t==285;
    in.reload=t==100 || t==300;
    // Lane C, 4 m to the right of lane A at 10 m: turn over 30 ticks.
    if(t>=230 && t<260) in.yaw_delta+=atan2f(4,10)/30;
    in.weapon=t==320 ? 2 : (t==420 ? 1 : 0);
    if(t>=340 && t<380) { in.forward=1; in.gait=SWAT_SPRINT; }
    if(t>=380 && t<400) { in.strafe=-1; in.crouch=true; }
    in.lean=t>=400 && t<430 ? 1 : 0;
    in.jump=t==440;
    // Turn round to the door and walk back to it, then use it. A full-speed
    // walk is needed to come within reach of the leaf before t=580.
    if(t>=460 && t<500) in.yaw_delta+=SWAT_PI/40;
    if(t>=500 && t<570) in.forward=1;
    in.interact=t==580;
    in.selector=t==600;
    return in;
}
#define HUMAN_TICKS 640

static void human_and_actor(const char* path) {
    static SwatSim human,actor,policy;
    swat_sim_init(&human,range_config(),71);
    assert(human.actor_count==1+SWAT_WEAPON_RANGE_TARGETS && !human.config.hostile_fire && !human.config.tactical_rules);
    SwatReplay writer; assert(swat_replay_record(&writer,path,&human));
    static uint32_t digests[HUMAN_TICKS];
    for(int t=0;t<HUMAN_TICKS;t++) {
        SwatInput in=human_input(t);
        swat_sim_step(&human,&in);
        assert(swat_replay_append(&writer,&in,&human));
        digests[t]=swat_replay_digest(&human);
    }
    assert(swat_replay_close(&writer));
    assert(human.end==SWAT_RUNNING && human.totals.shots>=3 && human.totals.hostile_damage>0);
    // The stream really reached and opened the door behind the spawn.
    int door=-1;
    for(int i=0;i<human.world.count;i++) if(human.world.objects[i].door) door=i;
    assert(door>=0 && human.world.objects[door].door_open);

    // Replay the journal through the actor path: no player wrapper and no bot
    // input selection, as a policy or squad actor would drive the officer.
    // Range targets receive neutral input in both paths: without hostile fire
    // or tactical rules swat_sim_bot_inputs returns neutral input.
    SwatReplay reader; assert(swat_replay_open(&reader,path));
    swat_sim_init(&actor,reader.config,reader.seed);
    static SwatSim shadow; swat_sim_init(&shadow,range_config(),71);
    SwatInput in,inputs[SWAT_MAX_ACTORS]; uint32_t expected; int status,frame=0;
    while((status=swat_replay_next(&reader,&in,&expected))==1) {
        for(int i=0;i<SWAT_MAX_ACTORS;i++) inputs[i]=swat_neutral_input();
        inputs[0]=in;
        swat_sim_step_inputs(&actor,inputs);
        assert(swat_replay_digest(&actor)==expected && expected==digests[frame]);
        // Rebuild the human run tick by tick for the field-level comparison.
        SwatInput original=human_input(frame); swat_sim_step(&shadow,&original);
        assert_same_state(&actor,&shadow);
        frame++;
    }
    assert(status==0 && frame==HUMAN_TICKS && swat_replay_close(&reader));
    assert_same_state(&actor,&human);

    // Policy path: decoded discrete actions drive the human wrapper, are
    // recorded, and replay identically through the actor path.
    static SwatSim recorded; swat_sim_init(&recorded,range_config(),72); swat_sim_init(&policy,range_config(),72);
    uint32_t lcg=12345;
    for(int t=0;t<400;t++) {
        static const int sizes[SWAT_ACTION_HEADS]=SWAT_ACTION_SIZES;
        float action[SWAT_ACTION_HEADS];
        for(int h=0;h<SWAT_ACTION_HEADS;h++) { lcg=lcg*1664525u+1013904223u; action[h]=(float)((lcg>>16)%(uint32_t)sizes[h]); }
        SwatInput decoded=swat_decode_action(action);
        swat_sim_step(&recorded,&decoded);
        for(int i=0;i<SWAT_MAX_ACTORS;i++) inputs[i]=swat_neutral_input();
        // Through the wire format, as journals and the server receive it.
        unsigned char bytes[256]; SwatCommand command={1,(uint32_t)t+1,decoded},received;
        size_t size=swat_encode_command(bytes,sizeof(bytes),&command);
        assert(size && swat_decode_command(&received,bytes,size));
        inputs[0]=received.input;
        swat_sim_step_inputs(&policy,inputs);
        assert_same_state(&policy,&recorded);
        assert(swat_replay_digest(&policy)==swat_replay_digest(&recorded));
    }
    int shots=actor.totals.shots; double damage=actor.totals.hostile_damage;
    swat_sim_close(&recorded); swat_sim_close(&policy); swat_sim_close(&shadow);
    swat_sim_close(&actor); swat_sim_close(&human);
    printf("PASS weapon range replay: %d human ticks (%d shots, %.0f damage) identical through the journal and actor path; 400 decoded policy ticks identical over the wire\n",
        HUMAN_TICKS,shots,damage);
}

// The human timeline as 1/30 s segments of held input with a constant look
// rate. Look rates are multiples of 2^-20 rad per pace unit, so the paced
// tick inputs are exact at every rate tested (tests/test_pacing.c).
#define SEGMENT 480
#define SEGMENTS 150
static SwatInput segment_sample(int start,int units) {
    int m=start/SEGMENT,t=m*2;
    SwatInput in=human_input(t);
    int yaw=(int)lrintf(in.yaw_delta*1048576.0f/SWAT_PACE_TICK),pitch=(int)lrintf(in.pitch_delta*1048576.0f/SWAT_PACE_TICK);
    in.yaw_delta=yaw*units/1048576.0f; in.pitch_delta=pitch*units/1048576.0f;
    // Momentary presses in the source fall on single ticks; widen them to the
    // segment so every rate sees them.
    SwatInput next=human_input(t+1);
    in.fire|=next.fire; in.reload|=next.reload; in.jump|=next.jump; in.interact|=next.interact; in.selector|=next.selector;
    if(!in.weapon) in.weapon=next.weapon;
    return in;
}
static void frame_rates(void) {
    static SwatSim reference,paced;
    static uint32_t digests[SEGMENTS*SEGMENT/SWAT_PACE_TICK];
    const int rates[]={240,480,120,60,0};
    const char* names[]={"60","30","120","240","uneven"};
    float observation[SWAT_OBS_SIZE];
    for(int r=0;r<5;r++) {
        SwatSim* s=r ? &paced : &reference;
        swat_sim_init(s,range_config(),73);
        SwatPacer pacer; swat_pacer_reset(&pacer);
        uint32_t lcg=99; int time=0,ticks=0;
        while(time<SEGMENTS*SEGMENT) {
            int units=rates[r];
            if(!units) {
                // 1/240..1/60 s frames that never cross a tick boundary.
                lcg=lcg*1664525u+1013904223u; units=60*(1+(int)((lcg>>16)%4));
                int boundary=(time/SWAT_PACE_TICK+1)*SWAT_PACE_TICK; if(time+units>boundary) units=boundary-time;
            }
            SwatInput sample=segment_sample(time,units),out[SWAT_PACE_MAX_TICKS];
            int n=swat_pacer_frame(&pacer,units,&sample,out);
            for(int i=0;i<n;i++) {
                swat_sim_step(s,&out[i]);
                uint32_t digest=swat_replay_digest(s);
                if(!r) digests[ticks]=digest; else assert(digest==digests[ticks]);
                ticks++;
            }
            // Presentation reads between ticks must not change the authority.
            SwatPose pose=swat_pose(&s->actors[0].controller,&s->actors[0].arsenal); (void)pose;
            swat_sim_observe(s,0,observation);
            (void)swat_sim_progress(s);
            time+=units;
        }
        assert(ticks==SEGMENTS*SEGMENT/SWAT_PACE_TICK);
        if(r) { assert_same_state(&paced,&reference); swat_sim_close(&paced); }
        printf("  %s Hz frames: %d ticks identical\n",names[r],ticks);
    }
    assert(reference.totals.shots>0);
    swat_sim_close(&reference);
    puts("PASS weapon range frame rates: 30/60/120/240 Hz and uneven frames give identical authoritative state");
}

typedef enum Cause { CAUSE_CANCEL,CAUSE_SPRINT,CAUSE_SWITCH,CAUSE_INTERACT,CAUSE_DOOR,CAUSE_DAMAGE,CAUSE_DEATH,CAUSES } Cause;
static const char* cause_names[CAUSES]={"cancel","sprint","switch","interact","door","damage","death"};

static void assert_legal(const SwatArsenal* a,int slot) {
    const SwatWeapon* w=&a->slots[slot];
    int capacity=swat_arsenal_def(a,slot)->capacity;
    assert(w->magazine>=0 && w->magazine<=capacity && w->reserve>=0);
    assert(w->magazine_count>=0 && w->magazine_count<=SWAT_MAGAZINES);
    assert(w->use_magazines || !w->magazine_count);
    for(int i=0;i<SWAT_MAGAZINES;i++) assert(i<w->magazine_count ? w->magazines[i]>=0 && w->magazines[i]<=capacity : !w->magazines[i]);
    assert(w->magazine_seated || !w->magazine);
    assert((w->reload_stage==SWAT_RELOAD_IDLE)==(w->reload_remaining==0));
    assert(w->reload_remaining>=0 && w->reload_remaining<=w->reload_duration);
}
static int rounds(const SwatSim* s) {
    return swat_weapon_rounds(&s->actors[0].arsenal.slots[0])+swat_weapon_rounds(&s->actors[0].arsenal.slots[1]);
}
static void step_checked(SwatSim* s,SwatInput* in,int total) {
    swat_sim_step(s,in);
    assert(rounds(s)==total);
    assert_legal(&s->actors[0].arsenal,0); assert_legal(&s->actors[0].arsenal,1);
}
// The snapshot validator accepts the state and a replica holds the same rounds.
static void assert_replica(const SwatSim* s) {
    static SwatSim replica; static SwatMap map; static SwatSnapshot state,decoded;
    static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(s,1,&map); swat_apply_map(&replica,&map);
    swat_capture_snapshot(s,1,&state);
    size_t n=swat_encode_snapshot(bytes,sizeof(bytes),&state);
    assert(n && swat_decode_snapshot(&decoded,bytes,n) && swat_apply_snapshot(&replica,&decoded));
    for(int k=0;k<2;k++) {
        const SwatWeapon* a=&s->actors[0].arsenal.slots[k]; const SwatWeapon* b=&replica.actors[0].arsenal.slots[k];
        assert(swat_weapon_rounds(a)==swat_weapon_rounds(b) && a->magazine==b->magazine && a->reload_stage==b->reload_stage);
    }
    swat_sim_close(&replica);
}
static int prepare(SwatSim* s,int slot,bool inventory,float yaw) {
    swat_sim_init(s,range_config(),74);
    s->actors[0].controller.yaw=yaw;
    SwatInput in=swat_neutral_input();
    if(slot) {
        in.weapon=2; swat_sim_step(s,&in); in.weapon=0;
        for(int t=0;t<40;t++) swat_sim_step(s,&in);
    }
    // A part-used magazine, so both the seated rounds and the spares matter.
    s->actors[0].arsenal.slots[slot].magazine=swat_arsenal_def(&s->actors[0].arsenal,slot)->capacity/3;
    // Magazine inventory is a staging choice: the officer is still at staging.
    in.magazine_inventory=inventory; swat_sim_step(s,&in);
    assert(s->actors[0].arsenal.slots[slot].use_magazines==inventory);
    assert(!inventory || s->actors[0].arsenal.slots[slot].magazine_count>0);
    in.magazine_inventory=false;
    for(int t=0;t<5;t++) swat_sim_step(s,&in);
    assert(!swat_weapons_busy(&s->actors[0].arsenal));
    return rounds(s);
}
// Elapsed ticks, from the reload press, at which the reload starts, each
// stage ends and the reload completes, measured on an uninterrupted run.
static void reload_marks(int slot,bool inventory,int marks[4]) {
    static SwatSim s; int total=prepare(&s,slot,inventory,0);
    SwatInput in=swat_neutral_input(); marks[0]=marks[1]=marks[2]=marks[3]=-1;
    for(int t=0;t<300 && marks[3]<0;t++) {
        in.reload=t==0;
        step_checked(&s,&in,total);
        unsigned int events=s.actors[0].arsenal.slots[slot].events;
        if(events&SWAT_WEAPON_EVENT_RELOAD_START) marks[0]=t;
        if(events&SWAT_WEAPON_EVENT_MAGAZINE_OUT) marks[1]=t;
        if(events&SWAT_WEAPON_EVENT_MAGAZINE_IN) marks[2]=t;
        if(events&SWAT_WEAPON_EVENT_RELOAD_DONE) marks[3]=t;
    }
    assert(marks[0]==0 && marks[0]<marks[1] && marks[1]<marks[2] && marks[2]<marks[3]);
    const SwatWeapon* w=&s.actors[0].arsenal.slots[slot];
    assert(w->chambered && w->magazine_seated && w->magazine>swat_arsenal_def(&s.actors[0].arsenal,slot)->capacity/3);
    swat_sim_close(&s);
}

static void reload_interruptions(void) {
    static SwatSim s;
    int runs=0;
    for(int slot=0;slot<2;slot++) for(int inventory=0;inventory<2;inventory++) {
        int marks[4]; reload_marks(slot,inventory!=0,marks);
        for(int mark=0;mark<4;mark++) for(int offset=-1;offset<=1;offset++) for(int cause=0;cause<CAUSES;cause++) {
            int at=marks[mark]+offset; if(at<0) continue;
            // The door is behind the spawn; everything else faces the range.
            int total=prepare(&s,slot,inventory!=0,cause==CAUSE_DOOR ? SWAT_PI : 0);
            int door=-1;
            for(int i=0;i<s.world.count;i++) if(s.world.objects[i].door) door=i;
            assert(door>=0 && !s.world.objects[door].door_open);
            SwatInput in=swat_neutral_input();
            for(int t=0;t<=at+40;t++) {
                in=swat_neutral_input(); in.reload=t==0;
                if(t==at) {
                    if(cause==CAUSE_CANCEL) in.cancel_reload=true;
                    if(cause==CAUSE_SWITCH) in.weapon=slot ? 1 : 2;
                    if(cause==CAUSE_INTERACT || cause==CAUSE_DOOR) in.interact=true;
                    if(cause==CAUSE_DAMAGE) swat_sim_damage_actor(&s,0,1,10);
                    if(cause==CAUSE_DEATH) swat_sim_damage_actor(&s,0,1,1000);
                }
                if(cause==CAUSE_SPRINT && t>=at && t<at+30) { in.forward=1; in.gait=SWAT_SPRINT; }
                step_checked(&s,&in,total);
            }
            if(cause==CAUSE_DOOR) assert(s.world.objects[door].door_open);
            if(cause==CAUSE_DEATH) assert(!s.actors[0].alive && s.end==SWAT_OFFICER_DOWN);
            assert_replica(&s);
            if(cause!=CAUSE_DEATH) {
                // Back to the interrupted weapon, then finish with a fresh reload.
                in=swat_neutral_input(); in.weapon=slot+1; step_checked(&s,&in,total);
                in.weapon=0; for(int t=0;t<45;t++) step_checked(&s,&in,total);
                in.reload=true; step_checked(&s,&in,total); in.reload=false;
                for(int t=0;t<260;t++) step_checked(&s,&in,total);
                const SwatWeapon* w=&s.actors[0].arsenal.slots[slot];
                assert(w->chambered && w->magazine_seated && !w->reload_remaining);
                assert_replica(&s);
            }
            swat_sim_close(&s); runs++;
        }
        printf("  %s, %s magazines: start %d, out %d, in %d, done %d\n",slot ? "sidearm" : "carbine",inventory ? "kept" : "pooled",
            marks[0],marks[1],marks[2],marks[3]);
    }
    printf("PASS weapon range reload interruptions: %d runs over every stage boundary (+/-1 tick) and %d causes (",runs,CAUSES);
    for(int c=0;c<CAUSES;c++) printf("%s%s",c ? ", " : "",cause_names[c]);
    puts("); ammunition conserved, counts legal, replicas agree");
}

// Lane B's tall slab, found by its authored centre.
static int slab_index(const SwatSim* s) {
    for(int i=0;i<s->world.count;i++) {
        const SwatObject* o=&s->world.objects[i];
        if(fabsf((float)o->center.x-SWAT_WEAPON_RANGE_TALL_COVER_X)<1e-4f && fabsf((float)o->center.z+4)<1e-4f) return i;
    }
    return -1;
}
static bool inside_box(const SwatObject* o,b3Pos p,float margin) {
    return fabsf((float)(p.x-o->center.x))<o->half.x+margin && fabsf((float)(p.y-o->center.y))<o->half.y+margin &&
        fabsf((float)(p.z-o->center.z))<o->half.z+margin;
}
static void place(SwatSim* s,float x,float z,float yaw) {
    SwatController* c=&s->actors[0].controller;
    b3Body_SetTransform(c->body.body,(b3Pos){x,c->body.totalHeight*.5f+.02f,z},b3Quat_identity);
    b3Body_SetLinearVelocity(c->body.body,swat_v(0,0,0));
    c->yaw=yaw; c->pitch=0;
}
static void tight_cover(const char* path) {
    static SwatSim s;
    int fired=0,blocked=0,hits=0,cases=0;
    const b3Pos target=swat_weapon_range_targets[1];
    for(int lean=-1;lean<=1;lean++) for(int lane=0;lane<3;lane++) for(int step=0;step<25;step++) {
        swat_sim_init(&s,range_config(),75);
        int slab=slab_index(&s); assert(slab>=0);
        float z=-4+.6f*lane,x=2.3f+.05f*step;
        place(&s,x,z,atan2f((float)target.z-z,(float)target.x-x));
        SwatInput in=swat_neutral_input(); in.aim=true; in.lean=(float)lean;
        for(int t=0;t<40;t++) swat_sim_step(&s,&in);
        SwatController* c=&s.actors[0].controller;
        SwatPose pose=swat_pose(c,&s.actors[0].arsenal);
        bool muzzle_inside=inside_box(&s.world.objects[slab],pose.muzzle,0);
        int shots=s.actors[0].arsenal.shots; float health=s.actors[2].health;
        // Hitscan resolves on the firing tick; check it before anything moves.
        in.fire=true; swat_sim_step(&s,&in); in.fire=false;
        bool shot=s.actors[0].arsenal.shots>shots;
        // The shooter cannot fire with the muzzle inside cover.
        if(muzzle_inside) assert(!shot);
        if(c->muzzle_blocked) blocked++;
        if(shot) {
            fired++;
            const SwatActor* a=&s.actors[0];
            b3Vec3 travel=b3SubPos(a->tracer_end,a->tracer_start); float length=b3Length(travel);
            // The shot left a muzzle that the eye could reach, outside the slab.
            assert(!inside_box(&s.world.objects[slab],a->tracer_start,0));
            assert(!swat_world_sphere_cast(&s.world,swat_controller_eye(c),b3SubPos(a->tracer_start,swat_controller_eye(c)),.035f,c->body.body).hit);
            if(s.actors[2].health<health) {
                hits++;
                // A hit needs a clear physical path from the muzzle to the impact.
                // The tracer ends on the target's surface, so a ray of exactly
                // that length can stop short of it by rounding; cast 5 cm past.
                SwatHit first=swat_world_ray(&s.world,a->tracer_start,swat_mul(travel,1/length),length+.05f,c->body.body);
                assert(first.kind==SWAT_HIT_ACTOR && first.index==2);
            }
        }
        // A blocked press is dropped, not buffered into a later shot.
        int after=s.actors[0].arsenal.shots;
        for(int t=0;t<12;t++) swat_sim_step(&s,&in);
        assert(s.actors[0].arsenal.shots==after);
        // Pressed square behind the slab (no lean, centre lane) the target is
        // fully covered: never hit, whatever the muzzle does.
        if(lane==0 && lean==0) assert(s.actors[2].health==health);
        // The target's replica sees the same outcome as the authority.
        static SwatSim replica; static SwatMap map; static SwatSnapshot state;
        swat_capture_map(&s,1,&map); swat_apply_map(&replica,&map);
        swat_capture_snapshot(&s,1,&state); assert(swat_apply_snapshot(&replica,&state));
        SAME(replica.actors[2].health,s.actors[2].health); assert(replica.actors[2].alive==s.actors[2].alive);
        assert(replica.actors[0].arsenal.shots==s.actors[0].arsenal.shots);
        swat_sim_close(&replica); swat_sim_close(&s); cases++;
    }

    // A recorded approach: walk into lane B, press against the slab with the
    // weapon raised and press fire repeatedly. Only the first press, before
    // the muzzle reaches the slab, fires; the rest are blocked. The replay
    // matches tick by tick and the covered target is never hurt.
    swat_sim_init(&s,range_config(),76);
    SwatReplay writer; assert(swat_replay_record(&writer,path,&s));
    float heading=atan2f(-4,3.0f);
    int recorded_shots=0;
    for(int t=0;t<420;t++) {
        SwatInput in=swat_neutral_input();
        SwatController* c=&s.actors[0].controller;
        b3Pos feet=swat_body_feet_position(&c->body);
        if(t<20) in.yaw_delta=heading/20;
        else if(t<200 && feet.x<3.0f && feet.z>-3.9f) in.forward=1;
        else if(t<240) in.yaw_delta=-c->yaw/8;
        else { in.forward=.4f; in.aim=true; in.fire=t%20==0; in.lean=t>=330 ? -1.0f : 0; }
        swat_sim_step(&s,&in); assert(swat_replay_append(&writer,&in,&s));
        assert(s.actors[2].health==100);
    }
    recorded_shots=s.actors[0].arsenal.shots;
    assert(swat_replay_close(&writer));
    SwatReplay reader; assert(swat_replay_open(&reader,path));
    static SwatSim replay; swat_sim_init(&replay,reader.config,reader.seed);
    SwatInput in; uint32_t expected; int status;
    while((status=swat_replay_next(&reader,&in,&expected))==1) {
        swat_sim_step(&replay,&in); assert(swat_replay_digest(&replay)==expected);
    }
    assert(status==0 && swat_replay_close(&reader));
    assert_same_state(&replay,&s);
    swat_sim_close(&replay); swat_sim_close(&s);
    printf("PASS weapon range tight cover: %d placements (%d blocked muzzles, %d shots, %d clear-path hits), no shot from inside cover, replicas agree; recorded approach fired %d times, covered target unhurt, replay identical\n",
        cases,blocked,fired,hits,recorded_shots);
}

int main(int argc,char** argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    if(argc>1) scratch_base=argv[1];
    char human_path[1024],cover_path[1024];
    scratch(human_path,sizeof(human_path),"human"); scratch(cover_path,sizeof(cover_path),"cover");
    human_and_actor(human_path);
    frame_rates();
    reload_interruptions();
    tight_cover(cover_path);
    remove(human_path); remove(cover_path);
    return 0;
}
