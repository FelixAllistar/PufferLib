// Headless weapon-rule checks: staged reload events and their equivalence
// with the pre-event weapon step; optional weapon data loading, validation,
// default equivalence and its binding to journals and the connect handshake.
#include "replay.h"
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
// The one rule added since the frozen step, kept beside it so the frozen copy
// stays exact for every other case. A reload that starts with a seated
// magazine holding rounds and an empty chamber is a charge. The frozen step
// starts it at REMOVE with the whole empty-reload timer, so it would strip and
// re-seat the magazine. The charge starts at CHAMBER with only that stage's
// share of the same timer and moves no magazine, because only the action needs
// racking. A reload that has just started is the only one whose timer is
// still full, and starting changes nothing else in the step, so the departure
// is exactly these two fields on that step. From then on the frozen step runs
// the CHAMBER stage as it always did.
static SwatShot reference_step(SwatArsenal* a,const SwatInput* in,float ads,float speed,bool grounded,bool blocked,long* charges) {
    SwatShot shot=legacy_step(a,in,ads,speed,grounded,blocked);
    SwatWeapon* w=&a->slots[a->active];
    if(w->reload_remaining && w->reload_remaining==w->reload_duration && w->reload_stage==SWAT_RELOAD_REMOVE &&
       !w->chambered && w->magazine>0) {
        w->reload_remaining=w->reload_duration-w->reload_duration*2/3;
        w->reload_stage=SWAT_RELOAD_CHAMBER;
        (*charges)++;
    }
    return shot;
}

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
    // A full reload starts its whole timer at REMOVE or INSERT. A charge starts
    // at CHAMBER with that stage's share of the timer, with the magazine
    // seated and holding rounds and the chamber empty, and has moved nothing.
    if(e&SWAT_WEAPON_EVENT_RELOAD_START) assert(after->reload_remaining>0 &&
        (after->reload_stage==SWAT_RELOAD_CHAMBER ?
            after->reload_remaining==after->reload_duration-swat_reload_stage_end(after->reload_duration,SWAT_RELOAD_INSERT) &&
            after->magazine_seated && !after->chambered && after->magazine>0 && after->magazine==before->magazine &&
            after->magazine_count==before->magazine_count && after->reserve==before->reserve :
            after->reload_remaining==after->reload_duration &&
            (after->reload_stage==SWAT_RELOAD_REMOVE || after->reload_stage==SWAT_RELOAD_INSERT)));
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
    long ticks=0,events=0,charges=0;
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
            SwatShot b=reference_step(&then,&in,ads,speed,grounded,blocked,&charges);
            assert_same_shot(a,b); assert_same_arsenal(&now,&then);
            for(int s=0;s<2;s++) { assert_events_explain(&before[s],&now.slots[s]); events+=now.slots[s].events!=0; }
            // An inactive slot can only report the switch that left it.
            assert(!(now.slots[1-now.active].events&~SWAT_WEAPON_EVENT_RELOAD_SWITCHED));
            ticks++;
        }
    }
    assert(events>1000 && charges>0);
    printf("PASS weapon step: %ld randomized ticks bit-identical to the pre-event step plus the charge rule; %ld ticks reported events, %ld charges\n",ticks,events,charges);
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
    // An interrupt from outside the step (damage, sprint) is held until the
    // next step, which reports it once. An idle weapon reports nothing.
    for(int profile=0;profile<2;profile++) {
        SwatArsenal a=armed(profile,11); SwatWeapon* w=&a.slots[a.active];
        w->magazine=0;
        SwatInput in=swat_neutral_input();
        swat_weapons_interrupt(&a); assert(!w->pending_events);
        swat_weapons_step(&a,&in,0,0,true,false); assert(!w->events);
        in.reload=true; swat_weapons_step(&a,&in,0,0,true,false); in.reload=false;
        assert(w->events==SWAT_WEAPON_EVENT_RELOAD_START);
        int rounds=swat_weapon_rounds(w);
        swat_weapons_interrupt(&a);
        assert(!w->reload_remaining && w->pending_events==SWAT_WEAPON_EVENT_RELOAD_DISTURBED && w->events==SWAT_WEAPON_EVENT_RELOAD_START);
        swat_weapons_interrupt(&a); assert(w->pending_events==SWAT_WEAPON_EVENT_RELOAD_DISTURBED);
        swat_weapons_step(&a,&in,0,0,true,false);
        assert(w->events==SWAT_WEAPON_EVENT_RELOAD_DISTURBED && !w->pending_events && swat_weapon_rounds(w)==rounds);
        swat_weapons_step(&a,&in,0,0,true,false); assert(!w->events);
    }
    puts("PASS reload interrupts: cancel and switch are reported only for active reloads and conserve ammunition; an outside interrupt is reported once by the next step");
}

// ---------------------------------------------------------------------------
// Optional weapon data files.
static const char* scratch_base="weapons-test";
static void scratch(char* out,size_t capacity,const char* suffix) {
    int n=snprintf(out,capacity,"%s%s",scratch_base,suffix); assert(n>0 && (size_t)n<capacity);
}
static void write_file(const char* path,const char* text) {
    FILE* file=fopen(path,"w"); assert(file);
    assert(fputs(text,file)>=0); assert(fclose(file)==0);
}
static void assert_same_def(const SwatWeaponDef* a,const SwatWeaponDef* b) {
    assert(!strcmp(a->name,b->name));
    assert(a->capacity==b->capacity && a->shot_ticks==b->shot_ticks && a->reload_ticks==b->reload_ticks);
    assert(a->empty_reload_ticks==b->empty_reload_ticks && a->equip_ticks==b->equip_ticks && a->automatic==b->automatic);
    assert(same_bits(a->damage,b->damage) && same_bits(a->range,b->range) && same_bits(a->energy,b->energy));
    assert(same_bits(a->hip_spread,b->hip_spread) && same_bits(a->ads_spread,b->ads_spread) && same_bits(a->recoil,b->recoil));
    assert(same_bits(a->barrel,b->barrel) && same_bits(a->sight_height,b->sight_height));
}
static void assert_compiled_active(void) {
    assert(swat_weapons_table_id()==0);
    // Pointer identity: the compiled table itself is the authority, not a copy.
    for(int i=0;i<SWAT_WEAPON_PROFILES;i++) assert(swat_weapon_def(i)==swat_weapon_builtin_def(i));
}

static void data_default_equivalence(void) {
    char path[1024]; char error[512];
    swat_weapons_reset(); assert_compiled_active();
    // Exporting the compiled table and loading it back changes nothing.
    scratch(path,sizeof(path),"-defaults.ini");
    assert(swat_weapons_save(path));
    assert(swat_weapons_load(path,error,sizeof(error)) && !error[0]);
    assert_compiled_active();
    // A bare file, or one that restates compiled values, is also the default.
    scratch(path,sizeof(path),"-restated.ini");
    write_file(path,"# restated\nversion = 1\n\n[profile 3]\ncapacity = 4\nname = GE PRECISION ; same name\n[profile 0]\nbarrel = .89999995\n");
    assert(swat_weapons_load(path,error,sizeof(error))); assert_compiled_active();
    write_file(path,"version=1\n");
    assert(swat_weapons_load(path,error,sizeof(error))); assert_compiled_active();
    // Negative zero is folded into zero rather than producing a new table.
    write_file(path,"version = 1\n[profile 8]\ndamage = -0\n");
    assert(swat_weapons_load(path,error,sizeof(error))); assert_compiled_active();
    puts("PASS weapon data defaults: no file, an exported default table and restated values keep the compiled table and id 0");
}

static void data_overlay(void) {
    char path[1024],copy[1024],error[512];
    scratch(path,sizeof(path),"-overlay.ini");
    write_file(path,
        "version = 1\n"
        "[profile 7]\n"
        "name = GE COMPACT MK2   # renamed\n"
        "capacity = 20\n"
        "automatic = 0\n"
        "damage = 26.5\n"
        "[profile 1]\n"
        "reload_ticks=60\n");
    assert(swat_weapons_load(path,error,sizeof(error)));
    uint32_t id=swat_weapons_table_id();
    assert(id && (id&0x80000000u));
    const SwatWeaponDef* compact=swat_weapon_def(7); const SwatWeaponDef* sidearm=swat_weapon_def(1);
    assert(!strcmp(compact->name,"GE COMPACT MK2") && compact->capacity==20 && !compact->automatic && compact->damage==26.5f);
    assert(sidearm->reload_ticks==60 && sidearm->empty_reload_ticks==swat_weapon_builtin_def(1)->empty_reload_ticks);
    for(int i=0;i<SWAT_WEAPON_PROFILES;i++) if(i!=1 && i!=7) assert_same_def(swat_weapon_def(i),swat_weapon_builtin_def(i));
    // Arsenals and the staged reload read the loaded table.
    SwatArsenal a; swat_weapons_init(&a,3);
    assert(a.slots[1].magazine==15);
    swat_weapons_primary(&a,7); assert(a.slots[0].magazine==20 && a.slots[0].reserve==60);
    a=armed(1,3); a.slots[1].magazine=2;
    SwatInput in=swat_neutral_input(); in.reload=true;
    swat_weapons_step(&a,&in,0,0,true,false); in.reload=false;
    assert(a.slots[1].reload_duration==60);
    int out=0,inserted=0;
    for(int t=1;t<=60;t++) {
        swat_weapons_step(&a,&in,0,0,true,false);
        if(a.slots[1].events&SWAT_WEAPON_EVENT_MAGAZINE_OUT) out=t;
        if(a.slots[1].events&SWAT_WEAPON_EVENT_MAGAZINE_IN) inserted=t;
    }
    assert(out==15 && inserted==40 && a.slots[1].magazine==15 && !a.slots[1].reload_remaining);
    // Saving a loaded table round-trips exactly, including its id.
    SwatWeaponDef before[SWAT_WEAPON_PROFILES]; char names[SWAT_WEAPON_PROFILES][SWAT_WEAPON_NAME_SIZE];
    for(int i=0;i<SWAT_WEAPON_PROFILES;i++) {
        before[i]=*swat_weapon_def(i); snprintf(names[i],sizeof(names[i]),"%s",before[i].name); before[i].name=names[i];
    }
    scratch(copy,sizeof(copy),"-overlay-saved.ini");
    assert(swat_weapons_save(copy));
    swat_weapons_reset(); assert_compiled_active();
    assert(swat_weapons_load(copy,error,sizeof(error)) && swat_weapons_table_id()==id);
    for(int i=0;i<SWAT_WEAPON_PROFILES;i++) assert_same_def(swat_weapon_def(i),&before[i]);
    // Any field change gives a different id.
    write_file(copy,"version = 1\n[profile 7]\nname = GE COMPACT MK2\ncapacity = 20\nautomatic = 0\ndamage = 26.500002\n[profile 1]\nreload_ticks = 60\n");
    assert(swat_weapons_load(copy,error,sizeof(error)) && swat_weapons_table_id()!=id && swat_weapons_table_id());
    assert(swat_weapons_load(path,error,sizeof(error)) && swat_weapons_table_id()==id);
    swat_weapons_reset(); assert_compiled_active();
    puts("PASS weapon data overlay: partial profiles, arsenal/reload use, exact save round trip and content-bound ids");
}

static void data_validation(void) {
    static const struct { const char* text; const char* reason; } bad[]={
        {"","missing \"version = 1\""},
        {"[profile 0]\ncapacity = 30\n","must come before the first section"},
        {"version = 2\n","unsupported version"},
        {"version = one\n","unsupported version"},
        {"version = 1\nversion = 1\n","version must appear once"},
        {"version = 1\ncapacity = 3\n","outside a [profile N] section"},
        {"version = 1\n[weapon 0]\n","unknown section"},
        {"version = 1\n[profile]\n","unknown section"},
        {"version = 1\n[profile 12]\n","is not 0..11"},
        {"version = 1\n[profile -1]\n","is not 0..11"},
        {"version = 1\n[profile x]\n","is not 0..11"},
        {"version = 1\n[profile 2\n","malformed section header"},
        {"version = 1\n[profile 2] extra\n","malformed section header"},
        {"version = 1\n[profile 2]\n[profile 2]\n","appears twice"},
        {"version = 1\n[profile 2]\nspeed = 3\n","unknown key \"speed\""},
        {"version = 1\n[profile 2]\ncapacity = 3\ncapacity = 4\n","appears twice in [profile 2]"},
        {"version = 1\n[profile 2]\ncapacity\n","expected \"key = value\""},
        {"version = 1\n[profile 2]\ncapacity = 0\n","outside 1..100"},
        {"version = 1\n[profile 2]\ncapacity = 101\n","outside 1..100"},
        {"version = 1\n[profile 2]\ncapacity = 3.5\n","needs an integer"},
        {"version = 1\n[profile 2]\ncapacity = 3 rounds\n","needs an integer"},
        {"version = 1\n[profile 2]\ncapacity =\n","needs an integer"},
        {"version = 1\n[profile 2]\ncapacity = 99999999999999999999\n","needs an integer"},
        {"version = 1\n[profile 2]\nshot_ticks = 0\n","outside 1..600"},
        {"version = 1\n[profile 2]\nreload_ticks = 3601\n","outside 1..3600"},
        {"version = 1\n[profile 2]\nempty_reload_ticks = 0\n","outside 1..3600"},
        {"version = 1\n[profile 2]\nequip_ticks = -5\n","outside 1..600"},
        {"version = 1\n[profile 2]\ndamage = nan\n","finite number"},
        {"version = 1\n[profile 2]\ndamage = inf\n","finite number"},
        {"version = 1\n[profile 2]\ndamage = -1\n","outside 0..1000"},
        {"version = 1\n[profile 2]\nenergy = 0\n","outside 0.001..10"},
        {"version = 1\n[profile 2]\nrange = 1e9\n","outside 0.1..500"},
        {"version = 1\n[profile 2]\nhip_spread = 1,5\n","finite number"},
        {"version = 1\n[profile 2]\nrecoil = 21\n","outside 0..20"},
        {"version = 1\n[profile 2]\nbarrel = 2\n","outside 0.1..1.5"},
        {"version = 1\n[profile 2]\nautomatic = 2\n","outside 0..1"},
        {"version = 1\n[profile 2]\nautomatic = yes\n","needs an integer"},
        {"version = 1\n[profile 2]\nname =\n","printable ASCII"},
        {"version = 1\n[profile 2]\nname = 0123456789012345678901234567890123\n","printable ASCII"},
        {"version = 1\n[profile 0]\nbarrel = .5\n","fixed by the measured carbine"},
        {"version = 1\n[profile 0]\nsight_height = .1\n","fixed by the measured carbine"},
    };
    char path[1024],good[1024],error[512];
    scratch(good,sizeof(good),"-good.ini"); scratch(path,sizeof(path),"-bad.ini");
    write_file(good,"version = 1\n[profile 7]\ncapacity = 20\n");
    assert(swat_weapons_load(good,error,sizeof(error)));
    uint32_t id=swat_weapons_table_id(); assert(id);
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        write_file(path,bad[i].text);
        error[0]='\0';
        bool loaded=swat_weapons_load(path,error,sizeof(error));
        if(loaded || !strstr(error,bad[i].reason) || strncmp(error,path,strlen(path))) {
            fprintf(stderr,"case %zu accepted or misreported: %s\n",i,error); assert(false);
        }
        // All or nothing: the previously loaded table is untouched.
        assert(swat_weapons_table_id()==id && swat_weapon_def(7)->capacity==20 && swat_weapon_def(2)->capacity==5);
    }
    // Errors name the line.
    write_file(path,"version = 1\n\n[profile 2]\nspeed = 3\n");
    assert(!swat_weapons_load(path,error,sizeof(error)) && strstr(error,"-bad.ini:4: unknown key"));
    // Over-long lines are rejected even when they are comments.
    char text[600]="version = 1\n#"; size_t n=strlen(text);
    while(n<sizeof(text)-3) text[n++]='x';
    text[n++]='\n'; text[n]='\0';
    write_file(path,text);
    assert(!swat_weapons_load(path,error,sizeof(error)) && strstr(error,":2: line is longer"));
    // Missing files, missing paths and absent error buffers.
    scratch(path,sizeof(path),"-missing.ini"); remove(path);
    assert(!swat_weapons_load(path,error,sizeof(error)) && strstr(error,"cannot open"));
    assert(!swat_weapons_load(NULL,error,sizeof(error)) && !swat_weapons_load("",NULL,0));
    assert(!swat_weapons_load(path,NULL,0));
    assert(swat_weapons_table_id()==id);
    swat_weapons_reset(); assert_compiled_active();
    printf("PASS weapon data validation: %zu malformed or out-of-range files rejected with path:line reasons, table unchanged\n",
        sizeof(bad)/sizeof(bad[0]));
}

// Records a short solo range round and returns the first frame's digest.
static uint32_t record_round(const char* path) {
    static SwatSim sim;
    SwatConfig config=swat_default_config(); config.mission=SWAT_RANGE; config.randomize=false; config.hostile_fire=false;
    config.max_ticks=1200; swat_sim_init(&sim,config,91);
    SwatReplay writer; assert(swat_replay_record(&writer,path,&sim));
    assert(writer.weapon_table==swat_weapons_table_id());
    uint32_t first=0;
    for(int t=0;t<160;t++) {
        SwatInput input=swat_neutral_input();
        input.fire=t==20; input.reload=t==40; input.weapon=t==150 ? 2 : 0;
        swat_sim_step(&sim,&input); assert(swat_replay_append(&writer,&input,&sim));
        if(!t) first=swat_replay_digest(&sim);
    }
    assert(swat_replay_close(&writer)); swat_sim_close(&sim);
    return first;
}
static bool verify_round(const char* path,SwatReplay* reader) {
    static SwatSim sim;
    if(!swat_replay_open(reader,path)) return false;
    swat_sim_init(&sim,reader->config,reader->seed);
    SwatInput input; uint32_t expected; int status;
    while((status=swat_replay_next(reader,&input,&expected))==1) {
        swat_sim_step(&sim,&input); assert(swat_replay_digest(&sim)==expected);
    }
    assert(status==0 && reader->frame==160 && swat_replay_close(reader));
    swat_sim_close(&sim); return true;
}
static uint32_t header_word(const char* path,int index) {
    FILE* file=fopen(path,"rb"); assert(file);
    unsigned char b[4]; assert(fseek(file,index*4,SEEK_SET)==0 && fread(b,1,4,file)==4); fclose(file);
    return ((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3];
}

static void data_binding(void) {
    char compiled[1024],custom[1024],data[1024],other[1024],error[512];
    scratch(compiled,sizeof(compiled),"-compiled.sgrp"); scratch(custom,sizeof(custom),"-custom.sgrp");
    scratch(data,sizeof(data),"-bound.ini"); scratch(other,sizeof(other),"-other.ini");
    write_file(data,"version = 1\n[profile 0]\nreload_ticks = 100\n[profile 1]\ncapacity = 14\n");
    write_file(other,"version = 1\n[profile 0]\nreload_ticks = 101\n[profile 1]\ncapacity = 14\n");
    // Compiled table: the header and the handshake are what they were before
    // weapon data existed.
    swat_weapons_reset();
    assert(swat_net_handshake()==SWAT_NET_MAGIC+SWAT_NET_VERSION);
    uint32_t compiled_first=record_round(compiled);
    assert(header_word(compiled,1)==1 && header_word(compiled,2)==SWAT_NET_VERSION);
    SwatReplay reader; assert(verify_round(compiled,&reader) && reader.weapon_table==0 && !reader.weapon_mismatch);
    // A loaded table changes the handshake and writes a version 2 journal.
    assert(swat_weapons_load(data,error,sizeof(error)));
    uint32_t id=swat_weapons_table_id(); assert(id&0x80000000u);
    assert(swat_net_handshake()!=SWAT_NET_MAGIC+SWAT_NET_VERSION && swat_net_handshake()-SWAT_NET_MAGIC-SWAT_NET_VERSION==id);
    assert(!verify_round(compiled,&reader) && reader.weapon_mismatch && reader.weapon_table==0);
    uint32_t custom_first=record_round(custom);
    assert(custom_first!=compiled_first); // the data reaches authoritative state from the first tick
    assert(header_word(custom,1)==2 && header_word(custom,2)==SWAT_NET_VERSION && header_word(custom,8)==id);
    assert(verify_round(custom,&reader) && reader.weapon_table==id);
    // A different table or the compiled one refuses it with a clear reason.
    assert(swat_weapons_load(other,error,sizeof(error)) && swat_weapons_table_id()!=id);
    assert(!verify_round(custom,&reader) && reader.weapon_mismatch && reader.weapon_table==id);
    swat_weapons_reset();
    assert(!verify_round(custom,&reader) && reader.weapon_mismatch);
    static SwatSim restored;
    SwatConfig config=swat_default_config(); config.mission=SWAT_RANGE; swat_sim_init(&restored,config,5);
    assert(!swat_replay_restore(&restored,custom,error,sizeof(error)) && strstr(error,"different weapon data"));
    swat_sim_close(&restored);
    // Loading the recording's file again makes it playable again.
    assert(swat_weapons_load(data,error,sizeof(error)) && swat_weapons_table_id()==id);
    assert(verify_round(custom,&reader));
    swat_weapons_reset();
    remove(compiled); remove(custom);
    puts("PASS weapon data binding: compiled-table journals and handshake unchanged; loaded tables write v2 journals checked on open");
}

int main(int argc,char** argv) {
    if(argc>1) scratch_base=argv[1]; // test-owned path prefix for data files
    staged_timeline();
    interruptions();
    legacy_equivalence();
    data_default_equivalence();
    data_overlay();
    data_validation();
    data_binding();
    return 0;
}
