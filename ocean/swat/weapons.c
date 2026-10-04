#include "weapons.h"
#include <string.h>

// Fictional game tuning. The inventory retains partially used magazines as
// pooled reserve ammunition; it does not model individual magazine objects.
static const SwatWeaponDef swat_weapon_defs[5] = {
    {"GE CARBINE",30,6,120,156,24,34.0f,60.0f,1.0f,2.0f,0.08f,1.2f,true},
    {"P9 SIDEARM",15,10,90,120,18,26.0f,40.0f,0.45f,2.5f,0.15f,1.7f,false},
    {"LL IMPACT LAUNCHER",5,48,150,180,30,6.0f,18.0f,0.14f,3.0f,0.5f,1.8f,false},
    {"GE PRECISION",4,90,180,210,36,82.0f,150.0f,2.6f,3.0f,.025f,2.5f,false},
    {"GE MARKSMAN",10,24,150,180,30,48.0f,100.0f,1.6f,2.5f,.065f,1.5f,false},
};

const SwatWeaponDef* swat_weapon_def(int slot) {
    return &swat_weapon_defs[slot>=0 && slot<5 ? slot : 0];
}

const SwatWeaponDef* swat_arsenal_def(const SwatArsenal* a,int slot) {
    return swat_weapon_def(slot==1 ? 1 : a->primary);
}
void swat_weapons_primary(SwatArsenal* a,int definition) {
    a->primary=definition>=2 && definition<=4 ? definition : 0;
    const SwatWeaponDef* d=swat_arsenal_def(a,0);
    a->slots[0]=(SwatWeapon){.magazine=d->capacity,.reserve=d->capacity*3,.chambered=true,.mode=SWAT_SEMI};
    a->equip_remaining=d->equip_ticks;
}

void swat_weapons_init(SwatArsenal* a, uint32_t seed) {
    memset(a,0,sizeof(*a));
    a->rng = seed ? seed : 1;
    for (int i=0;i<2;i++) {
        a->slots[i].magazine = swat_weapon_defs[i].capacity;
        a->slots[i].reserve = swat_weapon_defs[i].capacity*3;
        a->slots[i].chambered = true;
        a->slots[i].mode = SWAT_SEMI;
    }
}

bool swat_weapons_busy(const SwatArsenal* a) {
    return a->equip_remaining > 0 || a->slots[a->active].reload_remaining > 0;
}

int swat_weapon_rounds(const SwatWeapon* w) {
    return w->magazine + w->reserve + (w->chambered ? 1 : 0);
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
        // Reload commits only at completion. Cancelling never creates ammo.
        a->slots[a->active].reload_remaining = 0;
        a->slots[a->active].reload_duration = 0;
        a->active = requested;
        a->equip_remaining = swat_arsenal_def(a,requested)->equip_ticks;
    }
    SwatWeapon* w = &a->slots[a->active];
    const SwatWeaponDef* def = swat_arsenal_def(a,a->active);
    if (selector_edge) {
        w->mode = (SwatFireMode)(((int)w->mode + 1) % (def->automatic ? 3 : 2));
    }
    bool was_reloading = w->reload_remaining > 0;
    if (was_reloading && --w->reload_remaining == 0) {
        int needed = def->capacity - w->magazine;
        int added = w->reserve < needed ? w->reserve : needed;
        w->magazine += added;
        w->reserve -= added;
        if (!w->chambered && w->magazine > 0) {
            w->magazine--;
            w->chambered = true;
        }
        w->reload_duration = 0;
    }
    if (reload_edge && !was_reloading && !a->equip_remaining && w->reserve > 0 &&
        (w->magazine < def->capacity || !w->chambered)) {
        w->reload_duration = w->chambered ? def->reload_ticks : def->empty_reload_ticks;
        w->reload_remaining = w->reload_duration;
    }
    if (was_reloading || swat_weapons_busy(a) || w->cooldown > 0 ||
        w->mode == SWAT_SAFE || !w->chambered || blocked ||
        !(w->mode == SWAT_AUTO ? in->fire : fire_edge)) return shot;

    w->chambered = false;
    if (w->magazine > 0) { w->magazine--; w->chambered = true; }
    w->cooldown = def->shot_ticks;
    a->shots++;
    shot.fired = true;
    shot.damage = def->damage;
    shot.range = def->range;
    shot.energy = def->energy;
    float spread = def->hip_spread + (def->ads_spread-def->hip_spread)*swat_clamp(ads,0,1);
    spread += fminf(speed,5.0f)*0.18f + (grounded ? 0.0f : 3.0f);
    // Uniform disk around the aim direction, independent RNG for each actor.
    float angle = swat_rand01(&a->rng)*2.0f*SWAT_PI;
    float radius = sqrtf(swat_rand01(&a->rng))*spread*SWAT_RAD;
    shot.pitch_offset = sinf(angle)*radius;
    shot.yaw_offset = cosf(angle)*radius;
    shot.recoil_yaw = (swat_rand01(&a->rng)-0.5f)*def->recoil*0.5f*SWAT_RAD;
    return shot;
}
