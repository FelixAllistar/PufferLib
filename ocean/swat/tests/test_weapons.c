// Headless weapon-rule checks: staged reload events and their equivalence
// with the pre-event weapon step.
#include "weapons.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Frozen copy of swat_weapons_step before reload events were added. Events
// only report the stage machine, so every other field must stay bit-identical.
static void legacy_remove(SwatWeapon* w) {
    if(!w->magazine_seated) return;
    if(w->use_magazines && w->magazine_count<SWAT_MAGAZINES) w->magazines[w->magazine_count++]=w->magazine;
    else w->reserve+=w->magazine;
    w->magazine=0; w->magazine_seated=false;
}
static void legacy_insert(SwatWeapon* w,int capacity) {
    if(w->magazine_seated) return;
    if(w->use_magazines && w->magazine_count) {
        int best=0;
        for(int i=1;i<w->magazine_count;i++) if(w->magazines[i]>w->magazines[best]) best=i;
        w->magazine=w->magazines[best];
        w->magazines[best]=w->magazines[--w->magazine_count]; w->magazines[w->magazine_count]=0;
    } else {
        int rounds=w->reserve<capacity ? w->reserve : capacity;
        w->magazine=rounds; w->reserve-=rounds;
    }
    w->magazine_seated=true;
}
static SwatShot legacy_step(SwatArsenal* a,const SwatInput* in,float ads,float speed,bool grounded,bool blocked) {
    SwatShot shot={0};
    bool fire_edge=in->fire && !a->last_fire;
    bool reload_edge=in->reload && !a->last_reload;
    bool selector_edge=in->selector && !a->last_selector;
    a->last_fire=in->fire; a->last_reload=in->reload; a->last_selector=in->selector;
    for(int i=0;i<2;i++) if(a->slots[i].cooldown>0) a->slots[i].cooldown--;
    if(a->equip_remaining>0) a->equip_remaining--;
    int requested=in->weapon==1 ? 0 : (in->weapon==2 ? 1 : a->active);
    if(requested!=a->active) {
        swat_weapons_cancel_reload(&a->slots[a->active]);
        a->fire_buffer_ticks=a->reload_buffer_ticks=0;
        a->active=requested;
        a->equip_remaining=swat_arsenal_def(a,requested)->equip_ticks;
    }
    SwatWeapon* w=&a->slots[a->active];
    const SwatWeaponDef* def=swat_arsenal_def(a,a->active);
    if(selector_edge) w->mode=(SwatFireMode)(((int)w->mode+1)%(def->automatic ? 3 : 2));
    if(in->cancel_reload) swat_weapons_cancel_reload(w);
    bool was_reloading=w->reload_remaining>0;
    if(was_reloading) {
        w->reload_remaining--;
        int elapsed=w->reload_duration-w->reload_remaining;
        if(elapsed>=w->reload_duration/4 && w->reload_stage==SWAT_RELOAD_REMOVE) {
            legacy_remove(w); w->reload_stage=SWAT_RELOAD_INSERT;
        }
        if(elapsed>=w->reload_duration*2/3 && w->reload_stage==SWAT_RELOAD_INSERT) {
            legacy_insert(w,def->capacity); w->reload_stage=SWAT_RELOAD_CHAMBER;
        }
        if(!w->reload_remaining) {
            if(!w->chambered && w->magazine>0) { w->magazine--; w->chambered=true; }
            swat_weapons_cancel_reload(w);
        }
    }
    if(reload_edge && !was_reloading) a->reload_buffer_ticks=12;
    bool spare=w->reserve>0 || w->magazine_count>0;
    if(a->reload_buffer_ticks>0 && !was_reloading && !a->equip_remaining &&
       ((spare && w->magazine<def->capacity) || (!w->chambered && w->magazine>0) || !w->magazine_seated)) {
        w->reload_duration=w->chambered ? def->reload_ticks : def->empty_reload_ticks;
        w->reload_remaining=w->reload_duration;
        w->reload_stage=w->magazine_seated ? SWAT_RELOAD_REMOVE : SWAT_RELOAD_INSERT;
        a->reload_buffer_ticks=0;
    }
    if(a->reload_buffer_ticks>0) a->reload_buffer_ticks--;
    if(fire_edge && !blocked && !was_reloading && !swat_weapons_busy(a) && w->mode!=SWAT_SAFE) a->fire_buffer_ticks=8;
    bool fire_request=def->automatic && w->mode==SWAT_AUTO ? in->fire : a->fire_buffer_ticks>0;
    if(a->fire_buffer_ticks>0) a->fire_buffer_ticks--;
    if(blocked || w->mode==SWAT_SAFE || in->cancel_reload) { a->fire_buffer_ticks=0; return shot; }
    if(was_reloading || swat_weapons_busy(a) || w->cooldown>0 || !w->chambered || !fire_request) return shot;
    a->fire_buffer_ticks=0;
    w->chambered=false;
    if(w->magazine>0) { w->magazine--; w->chambered=true; }
    w->cooldown=def->shot_ticks;
    a->shots++;
    shot.fired=true; shot.damage=def->damage; shot.range=def->range; shot.energy=def->energy;
    float spread=def->hip_spread+(def->ads_spread-def->hip_spread)*swat_clamp(ads,0,1);
    if(a->sight==SWAT_RED_DOT) spread*=1-.12f*ads;
    else if(a->sight==SWAT_OPTIC) spread*=1-.2f*ads;
    spread+=fminf(speed,5.0f)*0.18f+(grounded ? 0.0f : 3.0f);
    float angle=swat_rand01(&a->rng)*2.0f*SWAT_PI;
    float radius=sqrtf(swat_rand01(&a->rng))*spread*SWAT_RAD;
    b3CosSin spread_angle=swat_sincos(angle);
    shot.pitch_offset=spread_angle.sine*radius;
    shot.yaw_offset=spread_angle.cosine*radius;
    shot.recoil_yaw=(swat_rand01(&a->rng)-0.5f)*def->recoil*0.5f*SWAT_RAD;
    return shot;
}
// ---------------------------------------------------------------------------

static bool same_bits(float a,float b) { return !memcmp(&a,&b,sizeof(a)); }
static void assert_same_weapon(const SwatWeapon* a,const SwatWeapon* b) {
    assert(a->magazine==b->magazine && a->reserve==b->reserve && a->cooldown==b->cooldown);
    assert(a->reload_remaining==b->reload_remaining && a->reload_duration==b->reload_duration);
    assert(a->chambered==b->chambered && a->mode==b->mode && a->reload_stage==b->reload_stage);
    assert(a->magazine_seated==b->magazine_seated && a->use_magazines==b->use_magazines);
    assert(a->magazine_count==b->magazine_count && !memcmp(a->magazines,b->magazines,sizeof(a->magazines)));
}
static void assert_same_arsenal(const SwatArsenal* a,const SwatArsenal* b) {
    for(int i=0;i<2;i++) assert_same_weapon(&a->slots[i],&b->slots[i]);
    assert(a->active==b->active && a->equip_remaining==b->equip_remaining && a->primary==b->primary);
    assert(a->last_fire==b->last_fire && a->last_reload==b->last_reload && a->last_selector==b->last_selector);
    assert(a->shots==b->shots && a->rng==b->rng && a->sight==b->sight);
    assert(a->fire_buffer_ticks==b->fire_buffer_ticks && a->reload_buffer_ticks==b->reload_buffer_ticks);
}
static void assert_same_shot(SwatShot a,SwatShot b) {
    assert(a.fired==b.fired && same_bits(a.damage,b.damage) && same_bits(a.range,b.range) && same_bits(a.energy,b.energy));
    assert(same_bits(a.pitch_offset,b.pitch_offset) && same_bits(a.yaw_offset,b.yaw_offset) && same_bits(a.recoil_yaw,b.recoil_yaw));
}

static SwatArsenal armed(int profile,uint32_t seed) {
    SwatArsenal a; swat_weapons_init(&a,seed);
    if(profile!=1) swat_weapons_primary(&a,profile); else a.active=1;
    a.equip_remaining=0;
    return a;
}

// Every event bit agrees with the observed transition of the stage machine.
static void assert_events_explain(const SwatWeapon* before,const SwatWeapon* after) {
    unsigned int e=after->events;
    if(e&SWAT_WEAPON_EVENT_INTERRUPTED) assert(before->reload_remaining>0);
    if(e&SWAT_WEAPON_EVENT_RELOAD_START) assert(after->reload_remaining==after->reload_duration && after->reload_remaining>0 &&
        (after->reload_stage==SWAT_RELOAD_REMOVE || after->reload_stage==SWAT_RELOAD_INSERT));
    // A start needs an idle weapon and completion needs a running one, so the
    // two never share a step.
    if(e&SWAT_WEAPON_EVENT_RELOAD_DONE) assert(before->reload_remaining>0 && !after->reload_remaining &&
        after->reload_stage==SWAT_RELOAD_IDLE && !(e&SWAT_WEAPON_EVENT_RELOAD_START));
    if(e&SWAT_WEAPON_EVENT_ROUND_CHAMBERED) assert((e&SWAT_WEAPON_EVENT_RELOAD_DONE) && !before->chambered && after->chambered);
    if(e&SWAT_WEAPON_EVENT_MAGAZINE_OUT) assert(before->reload_stage==SWAT_RELOAD_REMOVE && before->reload_remaining>0);
    if(e&SWAT_WEAPON_EVENT_MAGAZINE_IN) assert(before->reload_stage!=SWAT_RELOAD_IDLE && before->reload_remaining>0);
    if(!e) assert(before->reload_stage==after->reload_stage);
}

static void legacy_equivalence(void) {
    uint32_t rng=0x5eed;
    long ticks=0,events=0;
    for(int inventory=0;inventory<2;inventory++) for(int profile=0;profile<SWAT_WEAPON_PROFILES;profile++) for(int run=0;run<6;run++) {
        SwatArsenal now=armed(profile,1000+run),then=now;
        int capacity=swat_arsenal_def(&now,now.active)->capacity;
        swat_weapons_magazines(&now.slots[now.active],inventory!=0,capacity);
        swat_weapons_magazines(&then.slots[then.active],inventory!=0,capacity);
        for(int t=0;t<1500;t++) {
            SwatInput in=swat_neutral_input();
            float r=swat_rand01(&rng);
            in.fire=r<.35f; in.reload=swat_rand01(&rng)<.08f; in.selector=swat_rand01(&rng)<.02f;
            in.cancel_reload=swat_rand01(&rng)<.015f;
            float w=swat_rand01(&rng); in.weapon=w<.01f ? 1 : (w<.02f ? 2 : 0);
            float ads=swat_rand01(&rng),speed=6*swat_rand01(&rng);
            bool grounded=swat_rand01(&rng)<.9f,blocked=swat_rand01(&rng)<.05f;
            SwatWeapon before[2]={now.slots[0],now.slots[1]};
            SwatShot a=swat_weapons_step(&now,&in,ads,speed,grounded,blocked);
            SwatShot b=legacy_step(&then,&in,ads,speed,grounded,blocked);
            assert_same_shot(a,b); assert_same_arsenal(&now,&then);
            for(int s=0;s<2;s++) { assert_events_explain(&before[s],&now.slots[s]); events+=now.slots[s].events!=0; }
            // An inactive slot can only report the switch that left it.
            assert(!(now.slots[1-now.active].events&~SWAT_WEAPON_EVENT_RELOAD_SWITCHED));
            ticks++;
        }
    }
    assert(events>1000);
    printf("PASS weapon step: %ld randomized ticks bit-identical to the pre-event step; %ld ticks reported events\n",ticks,events);
}

static void staged_timeline(void) {
    for(int profile=0;profile<SWAT_WEAPON_PROFILES;profile++) for(int empty=0;empty<2;empty++) {
        SwatArsenal a=armed(profile,7);
        SwatWeapon* w=&a.slots[a.active];
        w->magazine=0; w->chambered=!empty;
        if(profile==10) w->reserve=5; // The probe launcher starts with no reserve.
        const SwatWeaponDef* def=swat_arsenal_def(&a,a.active);
        int duration=empty ? def->empty_reload_ticks : def->reload_ticks;
        SwatInput in=swat_neutral_input(); in.reload=true;
        swat_weapons_step(&a,&in,0,0,true,false); in.reload=false;
        assert(w->events==SWAT_WEAPON_EVENT_RELOAD_START && w->reload_stage==SWAT_RELOAD_REMOVE && w->reload_duration==duration);
        int out=-1,inserted=-1,done=-1;
        for(int elapsed=1;elapsed<=duration;elapsed++) {
            swat_weapons_step(&a,&in,0,0,true,false);
            unsigned int e=w->events;
            if(e&SWAT_WEAPON_EVENT_MAGAZINE_OUT) { assert(out<0); out=elapsed; }
            if(e&SWAT_WEAPON_EVENT_MAGAZINE_IN) { assert(inserted<0); inserted=elapsed; }
            if(e&SWAT_WEAPON_EVENT_RELOAD_DONE) { assert(done<0); done=elapsed; }
            assert(!(e&~(SWAT_WEAPON_EVENT_MAGAZINE_OUT|SWAT_WEAPON_EVENT_MAGAZINE_IN|SWAT_WEAPON_EVENT_RELOAD_DONE|SWAT_WEAPON_EVENT_ROUND_CHAMBERED)));
            assert(!(e&SWAT_WEAPON_EVENT_ROUND_CHAMBERED) || (empty && elapsed==duration));
        }
        assert(out==swat_reload_stage_end(duration,SWAT_RELOAD_REMOVE) && inserted==swat_reload_stage_end(duration,SWAT_RELOAD_INSERT));
        assert(done==swat_reload_stage_end(duration,SWAT_RELOAD_CHAMBER) && done==duration && !w->reload_remaining && w->chambered);
        assert(swat_reload_stage_end(duration,SWAT_RELOAD_IDLE)==0);
        swat_weapons_step(&a,&in,0,0,true,false); assert(!w->events);
    }
    puts("PASS reload timeline: start/out/in/done events land exactly on the shared stage boundaries for every profile");
}

static void interruptions(void) {
    for(int profile=0;profile<SWAT_WEAPON_PROFILES;profile++) {
        if(profile==1) continue;
        SwatArsenal a=armed(profile,9); SwatWeapon* w=&a.slots[0];
        w->magazine=0; if(!w->reserve) w->reserve=3;
        SwatInput in=swat_neutral_input();
        // Cancelling an idle weapon is not an interruption.
        in.cancel_reload=true; swat_weapons_step(&a,&in,0,0,true,false); assert(!w->events);
        in.cancel_reload=false; in.reload=true; swat_weapons_step(&a,&in,0,0,true,false); in.reload=false;
        assert(w->events&SWAT_WEAPON_EVENT_RELOAD_START);
        while(w->reload_stage==SWAT_RELOAD_REMOVE) swat_weapons_step(&a,&in,0,0,true,false);
        int rounds=swat_weapon_rounds(w);
        in.cancel_reload=true; swat_weapons_step(&a,&in,0,0,true,false); in.cancel_reload=false;
        assert(w->events==SWAT_WEAPON_EVENT_RELOAD_CANCELLED && w->reload_stage==SWAT_RELOAD_IDLE && swat_weapon_rounds(w)==rounds);
        // Restarting from an unseated magazine begins at INSERT.
        in.reload=true; swat_weapons_step(&a,&in,0,0,true,false); in.reload=false;
        assert(w->events==SWAT_WEAPON_EVENT_RELOAD_START && w->reload_stage==SWAT_RELOAD_INSERT);
        in.weapon=2; swat_weapons_step(&a,&in,0,0,true,false);
        assert(a.active==1 && w->events==SWAT_WEAPON_EVENT_RELOAD_SWITCHED && !a.slots[1].events && !w->reload_remaining);
        assert(swat_weapon_rounds(w)==rounds);
        swat_weapons_step(&a,&in,0,0,true,false); assert(!w->events);
        // Switching away from an idle weapon reports nothing.
        in.weapon=1; swat_weapons_step(&a,&in,0,0,true,false); assert(!a.slots[1].events);
        // Cancel and a new press on the same tick: the interrupt and the
        // restart both appear, in the order the step applied them.
        while(a.equip_remaining) swat_weapons_step(&a,&in,0,0,true,false);
        in.weapon=0; in.reload=true; swat_weapons_step(&a,&in,0,0,true,false); in.reload=false;
        assert(w->events&SWAT_WEAPON_EVENT_RELOAD_START);
        swat_weapons_step(&a,&in,0,0,true,false);
        in.cancel_reload=true; in.reload=true; swat_weapons_step(&a,&in,0,0,true,false);
        assert(w->events==(SWAT_WEAPON_EVENT_RELOAD_CANCELLED|SWAT_WEAPON_EVENT_RELOAD_START) && w->reload_remaining==w->reload_duration);
    }
    puts("PASS reload interrupts: cancel and switch are reported only for active reloads and conserve ammunition");
}

int main(int argc,char** argv) {
    (void)argc; (void)argv;
    staged_timeline();
    interruptions();
    legacy_equivalence();
    return 0;
}
