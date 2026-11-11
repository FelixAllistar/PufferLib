#include "abilities.h"
#include <math.h>
#include <string.h>

static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline uint32_t xs32(uint32_t* s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x ? x : 0x9e3779b9u; }
static inline float frand(uint32_t* s) { return (xs32(s) >> 8) * (1.0f / 16777216.0f); }

// Cooldowns and stamina are tuned so a fight sees each ability once or
// twice: strikes every 10-18 s, defences about half the time, the big calls
// every 30-40 s. Stamina is the same 0..1 pool sprinting uses.
#define STRIKE(id, nm, ds, cd, st, dmg, reach, bleed, knock, bone, stun, slow, slow_s, dash) \
    {id, nm, ds, CRET_ABK_STRIKE, CRET_ACT_ATTACK, cd, st, dmg, reach, bleed, knock, bone, stun, slow, slow_s, dash, 0, 0, 1, 0, 0, 0, 0}
#define EFFECT(id, nm, ds, kind, anim, cd, st, radius, dur, speed, armor, flank, reflect, heal) \
    {id, nm, ds, kind, anim, cd, st, 1, 1, 1, 1, 0, 0, 0, 0, 0, radius, dur, speed, armor, flank, reflect, heal}

const CretAbility CRET_ABILITIES[CRET_AB_COUNT] = {
    [CRET_AB_NONE] = {"none", "", "", 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0},
    //                       id               name               description                                                     cd    stam   dmg   reach bleed knock bone  stun  slow  slow_s dash
    [CRET_AB_HAMSTRING]   = STRIKE("hamstring", "Hamstring Bite", "A low bite at the legs: slows the prey by 40% for 6 s.",      12.0f, 0.15f, 0.9f, 1.1f, 1.3f, 0.5f, 0.0f, 0.0f, 0.4f, 6.0f, 0.0f),
    [CRET_AB_CRUSH_BITE]  = STRIKE("crush_bite", "Crushing Bite", "A bone-crushing bite: more damage and it breaks bones.",       18.0f, 0.20f, 1.4f, 1.0f, 0.6f, 1.2f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f),
    [CRET_AB_REND_BITE]   = STRIKE("rend_bite", "Rending Bite", "A tearing bite that leaves the victim bleeding heavily.",       14.0f, 0.15f, 0.8f, 1.0f, 3.0f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
    [CRET_AB_TAIL_SLAM]   = STRIKE("tail_slam", "Tail Slam", "A heavy swing of the spikes: big damage and deep bleeding.",       10.0f, 0.15f, 1.4f, 1.15f, 2.5f, 1.3f, 0.25f, 0.0f, 0.0f, 0.0f, 0.0f),
    [CRET_AB_SPIKE_SWEEP] = STRIKE("spike_sweep", "Spike Sweep", "A wide sweep behind: hits everything there and throws it.",     16.0f, 0.25f, 0.9f, 1.6f, 1.5f, 1.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
    [CRET_AB_CHARGE]      = STRIKE("charge", "Charge Headbutt", "Charge and ram: stuns (longer on lighter animals).",            15.0f, 0.30f, 1.3f, 1.2f, 1.0f, 1.4f, 0.2f, 2.0f, 0.0f, 0.0f, 1.25f),
    [CRET_AB_SHOVE]       = STRIKE("shove", "Shoulder Shove", "Drive the shoulder spikes in: throws the attacker back.",         10.0f, 0.20f, 1.2f, 1.2f, 1.5f, 2.6f, 0.15f, 0.6f, 0.0f, 0.0f, 0.0f),
    //                       id               name                description                                                kind             anim            cd    stam   radius dur   speed armor flank reflect heal
    [CRET_AB_POUNCE]      = EFFECT("pounce", "Pounce", "Leap onto prey and latch on: bites and bleeds until shaken off.",   CRET_ABK_LEAP, 0,               14.0f, 0.25f, 0.0f, 6.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f),
    [CRET_AB_DODGE_LEAP]  = EFFECT("dodge_leap", "Dodge Leap", "A quick hop (aim with the strafe keys); attacks miss mid-leap.", CRET_ABK_LEAP, 0,             8.0f, 0.15f, 0.0f, 0.6f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f),
    [CRET_AB_FEAR_ROAR]   = EFFECT("fear_roar", "Fear Roar", "Smaller animals nearby flee and deal 30% less damage.",       CRET_ABK_AREA, CRET_ACT_CALL,   30.0f, 0.20f, 35.0f, 6.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f),
    [CRET_AB_ALERT_CALL]  = EFFECT("alert_call", "Alert Call", "Marks predators within 90 m for your group (+15% damage).",   CRET_ABK_AREA, 0,               25.0f, 0.10f, 90.0f, 20.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f),
    [CRET_AB_HERD_BELLOW] = EFFECT("herd_bellow", "Herd Bellow", "Rallies the group nearby: +15% armour for 10 s.",          CRET_ABK_AREA, CRET_ACT_CALL,   40.0f, 0.15f, 45.0f, 10.0f, 1.0f, 0.15f, 0.0f, 0.0f, 0.0f),
    [CRET_AB_SPRINT_BURST]= EFFECT("sprint_burst", "Sprint Burst", "4 s at 130% sprint speed without spending stamina.",     CRET_ABK_SELF, 0,               20.0f, 0.10f, 0.0f, 4.0f, 1.3f, 0.0f, 0.0f, 0.0f, 0.0f),
    [CRET_AB_DEF_STANCE]  = EFFECT("def_stance", "Defensive Stance", "Half speed; +35% armour on the flanks, +10% elsewhere.", CRET_ABK_SELF, 0,             20.0f, 0.10f, 0.0f, 10.0f, 0.5f, 0.10f, 0.35f, 0.0f, 0.0f),
    [CRET_AB_HUNKER]      = EFFECT("hunker", "Hunker", "Drop low: +30% armour and biters take 40% of their bite back.", CRET_ABK_SELF, 0,                 24.0f, 0.10f, 0.0f, 8.0f, 0.15f, 0.30f, 0.0f, 0.4f, 0.0f),
    [CRET_AB_MEND]        = EFFECT("mend", "Mend", "Stops bleeding and heals 15% of health over 10 s.",                 CRET_ABK_SELF, 0,               60.0f, 0.10f, 0.0f, 10.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.15f),
};

const CretSpeciesAbilities CRET_SPECIES_ABILITIES[CRET_SPECIES] = {
    [CRET_SP_RAPTOR]     = {{CRET_AB_POUNCE, CRET_AB_SPRINT_BURST, CRET_AB_HAMSTRING, CRET_AB_MEND}, {120, 60}},
    [CRET_SP_STEGO]      = {{CRET_AB_TAIL_SLAM, CRET_AB_DEF_STANCE, CRET_AB_SPIKE_SWEEP, CRET_AB_MEND}, {120, 60}},
    [CRET_SP_TREX]       = {{CRET_AB_FEAR_ROAR, CRET_AB_CRUSH_BITE, CRET_AB_REND_BITE, CRET_AB_MEND}, {120, 60}},
    [CRET_SP_PACHY]      = {{CRET_AB_CHARGE, CRET_AB_HERD_BELLOW, CRET_AB_SPRINT_BURST, CRET_AB_MEND}, {100, 60}},
    [CRET_SP_SAUROPELTA] = {{CRET_AB_HUNKER, CRET_AB_SHOVE, CRET_AB_HERD_BELLOW, CRET_AB_MEND}, {100, 60}},
    [CRET_SP_GALLI]      = {{CRET_AB_DODGE_LEAP, CRET_AB_ALERT_CALL, CRET_AB_SPRINT_BURST, CRET_AB_MEND}, {100, 60}},
};

// ---------------------------------------------------------------- loadout and unlocks
void cret_prog_reset(CretSim* s, int i, bool person) {
    CretProgress* p = &s->prog[i]; char name[24]; memcpy(name, p->name, sizeof name);
    memset(p, 0, sizeof *p); memcpy(p->name, name, sizeof name);
    p->unlocked = person ? 0 : 0xffffffffu;
    p->quest.wait = (int)((5 + 25 * frand(&s->rng)) * CRET_TICK_HZ);
    cret_loadout_default(s, i);
}

bool cret_choice_unlocked(const CretSim* s, int i, int choice) {
    int sp = s->agents[i].species; return choice >= 0 && choice < 2 && (s->prog[i].unlocked >> (sp * 2 + choice) & 1);
}

void cret_loadout_default(CretSim* s, int i) {
    CretProgress* p = &s->prog[i]; const CretSpeciesAbilities* P = &CRET_SPECIES_ABILITIES[s->agents[i].species % CRET_SPECIES];
    int keep = -1;
    for (int c = 0; c < 2; c++) if (p->loadout[2] == P->ab[2 + c] && cret_choice_unlocked(s, i, c)) keep = c;
    if (keep < 0) for (int c = 0; c < 2 && keep < 0; c++) if (cret_choice_unlocked(s, i, c)) keep = c;
    p->loadout[0] = P->ab[0]; p->loadout[1] = P->ab[1]; p->loadout[2] = keep >= 0 ? P->ab[2 + keep] : CRET_AB_NONE;
}

bool cret_choice_take(CretSim* s, int i, int choice) {
    if (choice < 0 || choice > 1) return false;
    CretProgress* p = &s->prog[i]; int sp = s->agents[i].species % CRET_SPECIES;
    if (!cret_choice_unlocked(s, i, choice)) {
        int cost = CRET_SPECIES_ABILITIES[sp].cost[choice];
        if (p->marks < cost) return false;
        p->marks -= cost; p->unlocked |= 1u << (sp * 2 + choice);
    }
    p->loadout[2] = CRET_SPECIES_ABILITIES[sp].ab[2 + choice];
    return true;
}

int cret_ability_of(const CretSim* s, int i, int slot) {
    if (slot < 0 || slot >= CRET_AB_SLOTS) return 0;
    int ab = s->prog[i].loadout[slot]; const CretSpeciesAbilities* P = &CRET_SPECIES_ABILITIES[s->agents[i].species % CRET_SPECIES];
    for (int k = 0; k < 4; k++) if (P->ab[k] == ab) return ab;
    return 0;   // stale loadout from another species
}

bool cret_ability_ready(const CretSim* s, int i, int slot) {
    const CretAgent* a = &s->agents[i]; int ab = cret_ability_of(s, i, slot);
    return ab && a->alive && a->ab_cd[slot] <= 0 && a->stamina >= CRET_ABILITIES[ab].stamina && a->st[CRET_ST_STUN] <= 0 && !a->latch;
}

// ---------------------------------------------------------------- casting
static void apply_defensive(CretAgent* b, const CretAbility* A) {
    // Overlapping defences (a stance under a bellow) keep the stronger of each.
    bool on = b->st[CRET_ST_DEFENSIVE] > 0;
    b->def_armor = on ? fmaxf(b->def_armor, A->armor) : A->armor;
    b->def_flank = on ? fmaxf(b->def_flank, A->flank) : A->flank;
    b->def_reflect = on ? fmaxf(b->def_reflect, A->reflect) : A->reflect;
    b->def_speed = on ? fminf(b->def_speed, A->speed) : A->speed;
    b->st[CRET_ST_DEFENSIVE] = fmaxf(b->st[CRET_ST_DEFENSIVE], A->duration);
}

static void area(CretSim* s, int i, int ab) {
    CretAgent* a = &s->agents[i]; const CretAbility* A = &CRET_ABILITIES[ab];
    float g = cret_growth_scale(a->growth), r = A->radius * (0.5f + 0.5f * g), ma = cret_mass(a);
    for (int j = 0; j < s->cfg.num_agents; j++) {
        CretAgent* b = &s->agents[j];
        if (!b->alive) continue;
        float dx = b->x - a->x, dz = b->z - a->z, d2 = dx * dx + dz * dz; if (d2 > r * r) continue;
        bool mate = j == i || (a->group && b->group == a->group);
        if (ab == CRET_AB_FEAR_ROAR && !mate && cret_mass(b) < 0.6f * ma) {
            b->st[CRET_ST_FEAR] = fmaxf(b->st[CRET_ST_FEAR], A->duration * (1 - 0.5f * sqrtf(d2) / r)); b->fear_src = (uint8_t)(i + 1);
            if (b->latch) cret_release_latch(s, j, false);   // the raptors on it let go
            s->stats.fears++; s->stats.ability_hits[ab]++;
        } else if (ab == CRET_AB_ALERT_CALL && !mate && CRET_SPECIES_TABLE[b->species].diet == CRET_DIET_CARNIVORE) {
            b->st[CRET_ST_MARKED] = fmaxf(b->st[CRET_ST_MARKED], A->duration);
            b->mark_by = a->group ? a->group : (uint8_t)(100 + i + 1);
            s->stats.marked++; s->stats.ability_hits[ab]++;
        } else if (ab == CRET_AB_HERD_BELLOW && mate) {
            apply_defensive(b, A); if (j != i) s->stats.ability_hits[ab]++;
        }
    }
}

static bool cast(CretSim* s, int i, int slot, const CretInput* in) {
    if (!cret_ability_ready(s, i, slot)) return false;
    CretAgent* a = &s->agents[i]; int ab = cret_ability_of(s, i, slot); const CretAbility* A = &CRET_ABILITIES[ab];
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    switch (A->kind) {
    case CRET_ABK_STRIKE:
        // A special attack replaces the swing (eating or resting stops); fear unnerves.
        if (a->swimming || cret_act_is_attack(a->act) || a->st[CRET_ST_FEAR] > 0 || a->leap) return false;
        a->act = CRET_ACT_ATTACK; a->act_ticks = 0; a->strike = (uint8_t)ab; a->attack_cd = (int)(S->cooldown * CRET_TICK_HZ);
        break;
    case CRET_ABK_AREA:
        area(s, i, ab);
        if (A->anim && a->on_ground && !a->swimming && !cret_act_is_attack(a->act)) { a->act = A->anim; a->act_ticks = 0; }
        break;
    case CRET_ABK_SELF:
        if (ab == CRET_AB_SPRINT_BURST) a->st[CRET_ST_BURST] = A->duration;
        else if (ab == CRET_AB_MEND) {
            if (a->latchers) return false;   // not with raptors on its back
            a->bleed = 0; a->st[CRET_ST_MEND] = A->duration; a->heal_rate = A->heal * a->max_health / A->duration;
        } else apply_defensive(a, A);
        break;
    case CRET_ABK_LEAP: {
        if (!a->on_ground || a->swimming || a->broken > 0) return false;
        float dir = a->yaw, h, up, sp = 0.72f + 0.28f * g;
        if (ab == CRET_AB_POUNCE) { h = 11.0f * sp; up = 4.2f; }
        else {   // dodge: sideways with the strafe keys, back with S, else forward
            if (in->strafe > 0.3f) dir += 1.5707963f; else if (in->strafe < -0.3f) dir -= 1.5707963f; else if (in->forward < -0.1f) dir += 3.14159265f;
            h = 8.5f * sp; up = 3.0f;
            a->st[CRET_ST_EVADE] = A->duration;
            for (int j = 0; j < s->cfg.num_agents; j++) if (s->agents[j].latch == i + 1) cret_release_latch(s, j, true);
        }
        a->vx = cosf(dir) * h; a->vz = sinf(dir) * h; a->vy = up; a->on_ground = false;
        a->act = 0; a->strike = 0; a->leap = (uint8_t)ab; a->leap_t = ab == CRET_AB_POUNCE ? 1.2f : 0.8f;
        break; }
    }
    a->ab_cd[slot] = A->cooldown; a->stamina = fmaxf(a->stamina - A->stamina, 0);
    s->stats.ability_uses[ab]++;
    return true;
}

void cret_status_input(CretSim* s, int i, CretInput* in) {
    CretAgent* a = &s->agents[i];
    if (a->latch) {   // holding on: no steering; Space lets go
        bool let_go = in->jump; *in = (CretInput){0};
        if (let_go) cret_release_latch(s, i, false);
    } else if (a->st[CRET_ST_STUN] > 0) {
        *in = (CretInput){0};
        if (cret_act_is_attack(a->act)) { a->act = 0; a->strike = 0; }
    } else if (in->ability >= 1 && in->ability <= CRET_AB_SLOTS) {
        // A full mouth (sim.c, carrying) cannot bite: no special bites or pounce while carrying.
        int ab = cret_ability_of(s, i, in->ability - 1);
        bool bite = ab == CRET_AB_POUNCE || (ab != CRET_AB_NONE && CRET_ABILITIES[ab].kind == CRET_ABK_STRIKE && !CRET_SPECIES_TABLE[a->species].tail);
        if (!(a->carry && bite)) cast(s, i, in->ability - 1, in);
    }
    a->in_turn = fabsf(in->turn); a->in_sprint = in->sprint && in->forward > 0.5f; a->in_jump = in->jump; a->in_act = in->action;
}

float cret_status_speed(const CretAgent* a) {
    if (a->st[CRET_ST_STUN] > 0) return 0;
    float f = 1;
    if (a->st[CRET_ST_SLOW] > 0) f *= 1 - a->slow_amt;
    if (a->st[CRET_ST_DEFENSIVE] > 0) f *= a->def_speed;
    if (a->latchers) f *= fmaxf(0.4f, 1 - 0.2f * a->latchers);   // raptors weigh it down
    return f;
}

float cret_strike_dash(const CretAgent* a) {
    if (a->act != CRET_ACT_ATTACK || !a->strike) return 0;
    const CretAbility* A = &CRET_ABILITIES[a->strike];
    return A->dash > 0 && a->act_ticks < (int)(CRET_SPECIES_TABLE[a->species].hit_time * CRET_TICK_HZ) ? A->dash : 0;
}

// ---------------------------------------------------------------- damage hooks
float cret_status_damage(const CretSim* s, int attacker, int victim, float hp, float* extra) {
    const CretAgent* a = &s->agents[attacker]; const CretAgent* b = &s->agents[victim];
    *extra = 0;
    if (b->st[CRET_ST_EVADE] > 0) return 0;      // mid-leap: the blow misses
    if (a->st[CRET_ST_FEAR] > 0) hp *= 0.7f;
    if (b->st[CRET_ST_MARKED] > 0) hp *= 1.15f;
    if (b->st[CRET_ST_DEFENSIVE] > 0) {
        float dx = a->x - b->x, dz = a->z - b->z, l = fmaxf(sqrtf(dx * dx + dz * dz), 1e-3f);
        float along = fabsf(cosf(b->yaw) * dx + sinf(b->yaw) * dz) / l;   // 1 head or tail on, 0 broadside
        *extra = b->def_armor + b->def_flank * clampf((0.8f - along) / 0.45f, 0, 1);
    }
    return hp;
}

void cret_status_hit(CretSim* s, int attacker, int victim, float hp, float raw, float knock) {
    CretAgent* a = &s->agents[attacker]; CretAgent* b = &s->agents[victim];
    const CretAbility* A = a->strike && a->act == CRET_ACT_ATTACK ? &CRET_ABILITIES[a->strike] : NULL;
    if (A && hp > 0) {
        s->stats.ability_hits[a->strike]++;
        if (A->stun > 0 && b->alive) {
            float m = clampf(sqrtf(cret_mass(a) / fmaxf(cret_mass(b), 1)), 0.4f, 1.5f);
            b->st[CRET_ST_STUN] = fmaxf(b->st[CRET_ST_STUN], A->stun * m); s->stats.stuns++;
            if (b->latch) cret_release_latch(s, victim, true);
        }
        if (A->slow > 0) { b->st[CRET_ST_SLOW] = fmaxf(b->st[CRET_ST_SLOW], A->slow_s); b->slow_amt = fmaxf(b->slow_amt, A->slow); }
        if (A->bone > 0 && hp > 0.1f * b->max_health && frand(&s->rng) < A->bone) b->broken = fmaxf(b->broken, 45.0f + 45.0f * frand(&s->rng));
    }
    // Hunkered: whatever bites (not a tail swing behind) takes part of its bite back.
    if (b->alive && a->alive && b->st[CRET_ST_DEFENSIVE] > 0 && b->def_reflect > 0 && !cret_rear_attack(a) && attacker != victim) {
        float back = fminf(raw * b->def_reflect, a->health);
        a->health -= back; a->hurt_ticks = 8; a->last_attacker = victim;
        a->ev_taken += back / fmaxf(a->max_health, 1); b->ev_dealt += back / fmaxf(a->max_health, 1);
        if (a->health <= 0.01f) cret_kill(s, attacker);
    }
    if (b->latch && knock > 2.5f) cret_release_latch(s, victim, true);   // a heavy blow knocks a latched raptor off
}

// ---------------------------------------------------------------- latching
static void flank_dir(const CretAgent* b, float side, float* px, float* pz) { *px = -sinf(b->yaw) * side; *pz = cosf(b->yaw) * side; }

void cret_release_latch(CretSim* s, int i, bool thrown) {
    CretAgent* a = &s->agents[i]; int j = a->latch - 1;
    a->latch = 0; a->st[CRET_ST_LATCHING] = 0; a->shake = 0;
    if (j < 0 || j >= s->cfg.num_agents || !a->alive) return;
    CretAgent* b = &s->agents[j]; float px, pz; flank_dir(b, a->latch_side, &px, &pz);
    float d = b->radius + a->radius + 0.6f, S = s->world->size;
    a->x = clampf(b->x + px * d, 2, S - 2); a->z = clampf(b->z + pz * d, 2, S - 2); a->y = fmaxf(a->y, cret_height(s->world, a->x, a->z));
    float push = thrown ? 6.0f : 3.0f;
    a->vx = b->vx + px * push; a->vz = b->vz + pz * push; a->vy = thrown ? 3.0f : 1.5f; a->on_ground = false;
    b3Body_SetTransform(a->body, (b3Pos){a->x, a->y + a->height * 0.5f, a->z}, b3Quat_identity);
    if (thrown) { a->st[CRET_ST_STUN] = fmaxf(a->st[CRET_ST_STUN], 1.2f); s->stats.shake_offs++; }
}

// In a pounce, the first animal the raptor's head reaches (not a packmate,
// not one much lighter than itself) is held.
static void try_latch(CretSim* s, int i) {
    CretAgent* a = &s->agents[i]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    float hx = a->x + cosf(a->yaw) * S->length * g * 0.45f, hz = a->z + sinf(a->yaw) * S->length * g * 0.45f;
    int best = -1; float bd = 0.8f + a->radius, ma = cret_mass(a);
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (j == i || !b->alive || b->latch || b->swimming || (a->group && b->group == a->group) || cret_mass(b) < 0.4f * ma) continue;
        if (fabsf(a->y - (b->y + b->height * 0.4f)) > b->height * 0.6f + 1.2f) continue;
        float d = cret_body_dist(b, hx, hz); if (d < bd) { bd = d; best = j; }
    }
    if (best < 0) return;
    CretAgent* b = &s->agents[best];
    float cross = cosf(b->yaw) * (a->z - b->z) - sinf(b->yaw) * (a->x - b->x);
    a->latch = (uint8_t)(best + 1); a->latch_side = cross >= 0 ? 1.0f : -1.0f; a->shake = 0; a->latch_t = 0.3f;
    a->st[CRET_ST_LATCHING] = CRET_ABILITIES[CRET_AB_POUNCE].duration; a->leap = 0;
    b3Body_SetTransform(a->body, (b3Pos){0, -1000.0f - i * 10, 0}, b3Quat_identity);   // rides along; no collider in the way
    s->stats.latches++; s->stats.ability_hits[CRET_AB_POUNCE]++;
    cret_damage(s, i, best, S->damage * g * g * 0.5f);
}

// Holding on: ride the victim's flank, bite every 0.6 s (bleeding), tire.
// The victim shakes it off by turning hard, sprinting, jumping and
// attacking; heavier animals shake harder.
static void hold(CretSim* s, int i) {
    CretAgent* a = &s->agents[i]; int j = a->latch - 1; float dt = CRET_DT;
    CretAgent* b = j >= 0 && j < s->cfg.num_agents ? &s->agents[j] : NULL;
    if (!b || !b->alive || b->swimming || a->st[CRET_ST_LATCHING] <= 0 || a->stamina <= 0.02f) { cret_release_latch(s, i, false); return; }
    float massf = clampf(sqrtf(cret_mass(b) / fmaxf(cret_mass(a), 1)) / 4.0f, 0.35f, 1.6f);
    a->shake += dt * (0.08f + 0.5f * b->in_turn + 0.3f * b->in_sprint) * massf + (b->in_jump ? 0.2f : 0) +
                (cret_act_is_attack(b->act) && b->act_ticks == 1 ? 0.15f : 0);
    if (a->shake >= 1) { cret_release_latch(s, i, true); return; }
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    float px, pz; flank_dir(b, a->latch_side, &px, &pz);
    float hl = CRET_SPECIES_TABLE[b->species].length * cret_growth_scale(b->growth) * 0.4f, t = (float)((i * 7) % 5 - 2) * 0.25f * hl;
    a->x = b->x + cosf(b->yaw) * t + px * (b->radius + 0.3f * a->radius); a->z = b->z + sinf(b->yaw) * t + pz * (b->radius + 0.3f * a->radius);
    a->y = b->y + fmaxf(0, b->height * 0.55f - a->height * 0.5f);
    a->yaw = atan2f(b->z - a->z, b->x - a->x); a->vx = b->vx; a->vz = b->vz; a->vy = 0; a->speed = b->speed;
    a->on_ground = true; a->swimming = false; a->sprinting = false;
    a->stamina = fmaxf(a->stamina - 0.07f * dt, 0);
    if ((a->latch_t -= dt) <= 0) {
        a->latch_t = 0.6f;
        b->bleed += S->damage * g * g * 0.01f * (1 - CRET_SPECIES_TABLE[b->species].bleed_resist);
        cret_damage(s, i, j, S->damage * g * g * 0.16f);
    }
}

void cret_status_step(CretSim* s) {
    float dt = CRET_DT; int n = s->cfg.num_agents;
    for (int j = 0; j < n; j++) s->agents[j].latchers = 0;
    for (int i = 0; i < n; i++) {
        const CretAgent* a = &s->agents[i]; int j = a->latch - 1;
        if (a->alive && j >= 0 && j < n && s->agents[j].alive) s->agents[j].latchers++;
    }
    for (int i = 0; i < n; i++) {
        CretAgent* a = &s->agents[i];
        if (!a->alive) continue;
        if (a->leap) {
            a->leap_t -= dt;
            if (a->leap == CRET_AB_POUNCE) try_latch(s, i);
            if (a->leap_t <= 0 || a->on_ground) a->leap = 0;
        }
        if (a->latch) { hold(s, i); if (!a->alive) continue; }
        for (int k = 0; k < CRET_ST_COUNT; k++) if (k != CRET_ST_LATCHED && k != CRET_ST_LATCHING) a->st[k] = fmaxf(a->st[k] - dt, 0);
        if (a->latch) a->st[CRET_ST_LATCHING] = fmaxf(a->st[CRET_ST_LATCHING] - dt, 0);
        a->st[CRET_ST_LATCHED] = a->latchers ? 1.0f : 0.0f;
        if (a->st[CRET_ST_SLOW] <= 0) a->slow_amt = 0;
        if (a->st[CRET_ST_DEFENSIVE] <= 0) { a->def_armor = a->def_flank = a->def_reflect = 0; a->def_speed = 1; }
        if (a->st[CRET_ST_FEAR] <= 0) a->fear_src = 0;
        if (a->st[CRET_ST_MARKED] <= 0) a->mark_by = 0;
        if (a->st[CRET_ST_MEND] > 0) { a->health = fminf(a->health + a->heal_rate * dt, a->max_health); a->bleed = 0; }
        for (int k = 0; k < CRET_AB_SLOTS; k++) a->ab_cd[k] = fmaxf(a->ab_cd[k] - dt, 0);
    }
}
