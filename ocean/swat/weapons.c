#include "weapons.h"
#include "rifle_geometry.h"
#include <string.h>

// Fictional game profiles. Reload events and inventory are authority-owned.
static const SwatWeaponDef swat_weapon_defs[SWAT_WEAPON_PROFILES] = {
    {"GE CARBINE",30,6,120,156,24,34.0f,60.0f,1.0f,2.0f,0.08f,1.2f,true,SWAT_CARBINE_REACH,SWAT_CARBINE_SIGHT_HEIGHT},
    {"P9 SIDEARM",15,10,90,120,18,26.0f,40.0f,0.45f,2.5f,0.15f,1.7f,false,.36f,.03f},
    {"LL IMPACT LAUNCHER",5,48,150,180,30,6.0f,18.0f,0.14f,3.0f,0.5f,1.8f,false,.62f,.055f},
    {"GE PRECISION",4,90,180,210,36,82.0f,150.0f,2.6f,3.0f,.025f,2.5f,false,.84f,.06f},
    {"GE MARKSMAN",10,24,150,180,30,48.0f,100.0f,1.6f,2.5f,.065f,1.5f,false,.72f,.06f},
    {"PEPPER LAUNCHER",20,12,138,168,28,1.0f,22.0f,.08f,2.0f,.25f,.25f,false,.57f,.05f},
    {"BREACH SHOTGUN",6,48,180,210,32,42.0f,24.0f,.65f,3.5f,.8f,2.2f,false,.65f,.04f},
    {"GE COMPACT",25,5,108,138,20,24.0f,40.0f,.65f,2.5f,.12f,1.0f,true,.45f,.045f},
    {"LL CS LAUNCHER",5,48,150,180,30,0,18,.01f,3,.5f,1.8f,false,.62f,.055f},
    {"LL FLASH LAUNCHER",5,48,150,180,30,0,18,.01f,3,.5f,1.8f,false,.62f,.055f},
    {"MULTI-PROBE CEW",9,30,150,180,24,0,10,.01f,2,.2f,.1f,false,.36f,.03f},
    {"TETHER RESTRAINT",1,60,120,150,28,0,6,.01f,2,.4f,.2f,false,.40f,.035f},
};

const SwatWeaponDef* swat_weapon_def(int slot) {
    return &swat_weapon_defs[slot>=0 && slot<SWAT_WEAPON_PROFILES ? slot : 0];
}

const SwatWeaponDef* swat_arsenal_def(const SwatArsenal* a,int slot) {
    return swat_weapon_def(slot==1 ? 1 : a->primary);
}
void swat_weapons_primary(SwatArsenal* a,int definition) {
    a->primary=definition>=0 && definition<SWAT_WEAPON_PROFILES && definition!=1 ? definition : 0;
    const SwatWeaponDef* d=swat_arsenal_def(a,0);
    a->slots[0]=(SwatWeapon){.magazine=d->capacity,.reserve=d->capacity*3,.chambered=true,.mode=SWAT_SEMI,.magazine_seated=true};
    if(a->primary==10) a->slots[0].reserve=0; // Ten independently fired probes, no automatic cartridge refill.
    a->equip_remaining=d->equip_ticks;
}

void swat_weapons_init(SwatArsenal* a, uint32_t seed) {
    memset(a,0,sizeof(*a));
    a->rng = seed ? seed : 1;
    for (int i=0;i<2;i++) {
        a->slots[i].magazine = swat_weapon_defs[i].capacity;
        a->slots[i].reserve = swat_weapon_defs[i].capacity*3;
        a->slots[i].chambered = true;
        a->slots[i].magazine_seated=true;
        a->slots[i].mode = SWAT_SEMI;
    }
}

bool swat_weapons_busy(const SwatArsenal* a) {
    return a->equip_remaining > 0 || a->slots[a->active].reload_remaining > 0;
}

int swat_weapon_rounds(const SwatWeapon* w) {
    int rounds=w->magazine+w->reserve+(w->chambered ? 1 : 0);
    for(int i=0;i<w->magazine_count;i++) rounds+=w->magazines[i];
    return rounds;
}

void swat_weapons_cancel_reload(SwatWeapon* w) {
    w->reload_remaining=w->reload_duration=0;
    w->reload_stage=SWAT_RELOAD_IDLE;
}

void swat_weapons_magazines(SwatWeapon* w,bool enabled,int capacity) {
    if(w->use_magazines==enabled || w->reload_remaining) return;
    if(enabled) {
        w->magazine_count=0;
        while(w->reserve && w->magazine_count<SWAT_MAGAZINES) {
            int rounds=w->reserve<capacity ? w->reserve : capacity;
            w->magazines[w->magazine_count++]=rounds; w->reserve-=rounds;
        }
    } else {
        for(int i=0;i<w->magazine_count;i++) w->reserve+=w->magazines[i];
        memset(w->magazines,0,sizeof(w->magazines)); w->magazine_count=0;
    }
    w->use_magazines=enabled;
}

static void reload_remove(SwatWeapon* w) {
    if(!w->magazine_seated) return;
    if(w->use_magazines && w->magazine_count<SWAT_MAGAZINES) w->magazines[w->magazine_count++]=w->magazine;
    else w->reserve+=w->magazine;
    w->magazine=0; w->magazine_seated=false;
}

static void reload_insert(SwatWeapon* w,int capacity) {
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

SwatShot swat_weapons_step(SwatArsenal* a, const SwatInput* in,
                           float ads, float speed, bool grounded, bool blocked) {
    SwatShot shot = {0};
    bool fire_edge = in->fire && !a->last_fire;
    bool reload_edge = in->reload && !a->last_reload;
    bool selector_edge = in->selector && !a->last_selector;
    a->last_fire = in->fire;
    a->last_reload = in->reload;
    a->last_selector = in->selector;
    for (int i=0;i<2;i++) if (a->slots[i].cooldown > 0) a->slots[i].cooldown--;
    if (a->equip_remaining > 0) a->equip_remaining--;

    int requested = in->weapon == 1 ? 0 : (in->weapon == 2 ? 1 : a->active);
    if (requested != a->active) {
        swat_weapons_cancel_reload(&a->slots[a->active]);
        a->fire_buffer_ticks=a->reload_buffer_ticks=0;
        a->active = requested;
        a->equip_remaining = swat_arsenal_def(a,requested)->equip_ticks;
    }
    SwatWeapon* w = &a->slots[a->active];
    const SwatWeaponDef* def = swat_arsenal_def(a,a->active);
    if (selector_edge) {
        w->mode = (SwatFireMode)(((int)w->mode + 1) % (def->automatic ? 3 : 2));
    }
    if(in->cancel_reload) swat_weapons_cancel_reload(w);
    bool was_reloading=w->reload_remaining>0;
    if(was_reloading) {
        w->reload_remaining--;
        int elapsed=w->reload_duration-w->reload_remaining;
        if(elapsed>=w->reload_duration/4 && w->reload_stage==SWAT_RELOAD_REMOVE) {
            reload_remove(w); w->reload_stage=SWAT_RELOAD_INSERT;
        }
        if(elapsed>=w->reload_duration*2/3 && w->reload_stage==SWAT_RELOAD_INSERT) {
            reload_insert(w,def->capacity); w->reload_stage=SWAT_RELOAD_CHAMBER;
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

    w->chambered = false;
    if (w->magazine > 0) { w->magazine--; w->chambered = true; }
    w->cooldown = def->shot_ticks;
    a->shots++;
    shot.fired = true;
    shot.damage = def->damage;
    shot.range = def->range;
    shot.energy = def->energy;
    float spread = def->hip_spread + (def->ads_spread-def->hip_spread)*swat_clamp(ads,0,1);
    if(a->sight==SWAT_RED_DOT) spread*=1-.12f*ads;
    else if(a->sight==SWAT_OPTIC) spread*=1-.2f*ads;
    spread += fminf(speed,5.0f)*0.18f + (grounded ? 0.0f : 3.0f);
    // Uniform disk around the aim direction, independent RNG for each actor.
    float angle = swat_rand01(&a->rng)*2.0f*SWAT_PI;
    float radius = sqrtf(swat_rand01(&a->rng))*spread*SWAT_RAD;
    shot.pitch_offset = sinf(angle)*radius;
    shot.yaw_offset = cosf(angle)*radius;
    shot.recoil_yaw = (swat_rand01(&a->rng)-0.5f)*def->recoil*0.5f*SWAT_RAD;
    return shot;
}
