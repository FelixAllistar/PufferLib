// Protocol v14 codec, the server and the client transport. Prediction,
// reconciliation and interpolation are in netclient.c. docs/net.md has the
// wire format.
#include "net_priv.h"
#include "abilities.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static uint8_t q8(float v) { return (uint8_t)(v <= 0 ? 0 : v >= 1 ? 255 : v * 255 + 0.5f); }
static uint32_t mask(int n) { return n >= 32 ? 0xffffffffu : (1u << n) - 1; }

static bool g_enet_up;
static bool enet_up(void) { if (!g_enet_up && enet_initialize() == 0) g_enet_up = true; return g_enet_up; }

// ---------------------------------------------------------------- schema
// Positions in 1/256 m over the island (21 bits for 8 km), heights from
// -2048 m, 12-bit headings, speed in 1/16 m/s. Death and flinch are sent as
// the tick they started or end (16 bits), so they do not change every tick.
void netp_schema(NetSchema* sc, float world_size) {
    int pb = 12; while (pb < 26 && (float)(1u << pb) < world_size * NET_POS_Q) pb++;
    sc->pos_bits = pb;
    NetField* a = sc->agent; memset(sc->agent, 0, sizeof sc->agent); memset(sc->carcass, 0, sizeof sc->carcass);
    a[AF_FLAGS] = (NetField){8, 0, 0, 0}; a[AF_KIND] = (NetField){6, 0, 0, 0};
    a[AF_X] = (NetField){(uint8_t)pb, 10, 15, 0}; a[AF_Y] = (NetField){21, 8, 14, 0}; a[AF_Z] = (NetField){(uint8_t)pb, 10, 15, 0};
    a[AF_YAW] = (NetField){12, 7, 0, 1}; a[AF_SPEED] = (NetField){8, 4, 0, 0};
    a[AF_HEALTH] = (NetField){8, 0, 0, 0}; a[AF_GROWTH] = (NetField){8, 0, 0, 0};
    a[AF_HURT] = (NetField){16, 0, 0, 0}; a[AF_DEAD] = (NetField){16, 0, 0, 0}; a[AF_GROUP] = (NetField){8, 0, 0, 0};
    a[AF_RALLYX] = (NetField){16, 0, 0, 0}; a[AF_RALLYZ] = (NetField){16, 0, 0, 0};
    a[AF_STATUS] = (NetField){8, 0, 0, 0}; a[AF_MARK] = (NetField){8, 0, 0, 0}; a[AF_AIM] = (NetField){6, 0, 0, 0};
    NetField* c = sc->carcass;
    c[CF_KIND] = (NetField){9, 0, 0, 0};   // active, species (3), item kind (2, v13), critter kind (3, v13)
    c[CF_X] = a[AF_X]; c[CF_Y] = a[AF_Y]; c[CF_Z] = a[AF_Z]; c[CF_YAW] = a[AF_YAW];
    c[CF_GROWTH] = (NetField){8, 0, 0, 0}; c[CF_MEAT] = (NetField){8, 0, 0, 0}; c[CF_BORN] = (NetField){16, 0, 0, 0}; c[CF_AGENT] = (NetField){8, 0, 0, 0};
    c[CF_CARRIER] = (NetField){8, 0, 0, 0};   // v13: agent + 1 holding it (0: on the ground)
}
static uint32_t qpos(float v, int bits) { float q = v * NET_POS_Q; return q <= 0 ? 0 : q >= (float)mask(bits) ? mask(bits) : (uint32_t)lroundf(q); }
static uint32_t qyaw(float yaw) {
    float u = yaw / 6.2831853f; u -= floorf(u);
    return (uint32_t)lroundf(u * NET_YAW_Q) & (NET_YAW_Q - 1);
}
static float dqyaw(uint32_t v) { float y = (float)v * (6.2831853f / NET_YAW_Q); return y > 3.14159265f ? y - 6.2831853f : y; }

void netp_quant_agent(const NetSchema* sc, const CretSim* s, int i, NetEnt* e) {
    const CretAgent* a = &s->agents[i]; memset(e, 0, sizeof *e);
    uint32_t t = (uint32_t)s->tick;
    e->f[AF_FLAGS] = (a->alive ? FL_ALIVE : 0) | (a->on_ground ? FL_GROUND : 0) | (a->swimming ? FL_SWIM : 0) | (a->sprinting ? FL_SPRINT : 0) |
                     (a->bleed > 0.002f * a->max_health ? FL_BLEED : 0) | (a->broken > 0 ? FL_BROKEN : 0) | (a->rally_ticks > 0 ? FL_RALLY : 0) |
                     (a->hurt_ticks > 0 ? FL_HURT : 0);
    e->f[AF_KIND] = (uint32_t)(a->species < CRET_SPECIES ? a->species : 0) | (uint32_t)((a->act <= CRET_ACT_MAX ? a->act : 0) << 3);
    e->f[AF_X] = qpos(a->x, sc->pos_bits); e->f[AF_Y] = qpos(a->y + NET_Y_OFS, 21); e->f[AF_Z] = qpos(a->z, sc->pos_bits);
    e->f[AF_YAW] = qyaw(a->yaw);
    e->f[AF_SPEED] = (uint32_t)clampf(lroundf(a->speed * 16), 0, 255);
    e->f[AF_HEALTH] = q8(a->max_health > 0 ? a->health / a->max_health : 0); e->f[AF_GROWTH] = q8(a->growth);
    e->f[AF_HURT] = a->hurt_ticks > 0 ? (t + (uint32_t)a->hurt_ticks) & 0xffff : 0;
    e->f[AF_DEAD] = a->alive ? 0 : (t - (uint32_t)a->dead_ticks) & 0xffff;
    e->f[AF_GROUP] = a->group;
    if (a->rally_ticks > 0) { e->f[AF_RALLYX] = (uint32_t)clampf(a->rally_x * 8, 0, 65535); e->f[AF_RALLYZ] = (uint32_t)clampf(a->rally_z * 8, 0, 65535); }
    e->f[AF_STATUS] = (a->st[CRET_ST_STUN] > 0 ? SF_STUN : 0) | (a->st[CRET_ST_FEAR] > 0 ? SF_FEAR : 0) | (a->latch ? SF_LATCH : 0) | (a->latchers ? SF_LATCHED : 0) |
                      (a->st[CRET_ST_DEFENSIVE] > 0 ? SF_DEFENSIVE : 0) | (a->st[CRET_ST_MARKED] > 0 ? SF_MARKED : 0) | (a->st[CRET_ST_BURST] > 0 ? SF_BURST : 0) |
                      (a->st[CRET_ST_MEND] > 0 ? SF_MEND : 0);
    e->f[AF_MARK] = a->st[CRET_ST_MARKED] > 0 ? a->mark_by : 0;
    e->f[AF_AIM] = (uint32_t)(lroundf((isfinite(a->aim) ? clampf(a->aim, -1, 1) : 0) * NET_AIM_Q) + NET_AIM_Q);
}
void netp_quant_carcass(const NetSchema* sc, const CretSim* s, int c, NetEnt* e) {
    const CretCarcass* k = &s->carcasses[c]; memset(e, 0, sizeof *e);
    if (!k->active) return;
    e->f[CF_KIND] = 1u | (uint32_t)((k->species < CRET_SPECIES ? k->species : 0) << 1) | (uint32_t)((k->item < CRET_ITEM_KINDS ? k->item : 0) << 4) |
                    (uint32_t)((k->critter < CRET_CR_KINDS ? k->critter : 0) << 6);
    e->f[CF_X] = qpos(k->x, sc->pos_bits); e->f[CF_Y] = qpos(k->y + NET_Y_OFS, 21); e->f[CF_Z] = qpos(k->z, sc->pos_bits); e->f[CF_YAW] = qyaw(k->yaw);
    e->f[CF_GROWTH] = q8(k->growth); e->f[CF_MEAT] = q8(k->max_meat > 0 ? k->meat / k->max_meat : 0);
    e->f[CF_BORN] = ((uint32_t)s->tick - (uint32_t)k->age) & 0xffff;
    e->f[CF_AGENT] = k->agent < 0 || k->agent > 254 ? 255 : (uint32_t)k->agent;
    e->f[CF_CARRIER] = k->carrier;
}

void netp_ent_to_agent(const NetEnt* e, CretAgent* a, uint32_t tick_ref) {
    uint32_t fl = e->f[AF_FLAGS];
    a->alive = fl & FL_ALIVE; a->on_ground = (fl & FL_GROUND) != 0; a->swimming = (fl & FL_SWIM) != 0; a->sprinting = (fl & FL_SPRINT) != 0;
    a->species = (uint8_t)(e->f[AF_KIND] & 7); if (a->species >= CRET_SPECIES) a->species = 0;
    a->act = (uint8_t)(e->f[AF_KIND] >> 3); if (a->act > CRET_ACT_MAX) a->act = 0;
    a->x = e->f[AF_X] / NET_POS_Q; a->y = e->f[AF_Y] / NET_POS_Q - NET_Y_OFS; a->z = e->f[AF_Z] / NET_POS_Q;
    a->yaw = dqyaw(e->f[AF_YAW]); a->speed = e->f[AF_SPEED] / 16.0f;
    a->vx = cosf(a->yaw) * a->speed; a->vz = sinf(a->yaw) * a->speed; a->vy = 0;
    a->growth = e->f[AF_GROWTH] / 255.0f; a->max_health = 0; cret_apply_growth(a);
    a->health = e->f[AF_HEALTH] / 255.0f * a->max_health;
    a->bleed = (fl & FL_BLEED) ? a->max_health * 0.01f : 0; a->broken = (fl & FL_BROKEN) ? 1.0f : 0;
    int hurt = (int16_t)(uint16_t)((e->f[AF_HURT] - tick_ref) & 0xffff);
    a->hurt_ticks = (fl & FL_HURT) && hurt > 0 ? hurt : 0;
    a->dead_ticks = a->alive ? 0 : (int)((tick_ref - e->f[AF_DEAD]) & 0xffff);
    a->group = (uint8_t)e->f[AF_GROUP];
    a->rally_ticks = (fl & FL_RALLY) ? 1 : 0; a->rally_x = e->f[AF_RALLYX] / 8.0f; a->rally_z = e->f[AF_RALLYZ] / 8.0f;
    uint32_t sf = e->f[AF_STATUS];   // flags only: the timers stay the server's (the local animal gets them in full)
    a->st[CRET_ST_STUN] = sf & SF_STUN ? 1 : 0; a->st[CRET_ST_FEAR] = sf & SF_FEAR ? 1 : 0; a->latch = sf & SF_LATCH ? 1 : 0; a->latchers = sf & SF_LATCHED ? 1 : 0;
    a->st[CRET_ST_LATCHED] = a->latchers ? 1 : 0; a->st[CRET_ST_DEFENSIVE] = sf & SF_DEFENSIVE ? 1 : 0; a->st[CRET_ST_MARKED] = sf & SF_MARKED ? 1 : 0;
    a->st[CRET_ST_BURST] = sf & SF_BURST ? 1 : 0; a->st[CRET_ST_MEND] = sf & SF_MEND ? 1 : 0;
    a->mark_by = (uint8_t)e->f[AF_MARK];
    a->aim = e->f[AF_AIM] <= 2 * NET_AIM_Q ? ((float)e->f[AF_AIM] - NET_AIM_Q) / NET_AIM_Q : 0;
}
void netp_ent_to_carcass(const NetEnt* e, CretCarcass* k, uint32_t tick_ref) {
    memset(k, 0, sizeof *k);
    if (!(e->f[CF_KIND] & 1)) return;
    k->active = true; k->species = (uint8_t)((e->f[CF_KIND] >> 1) & 7); if (k->species >= CRET_SPECIES) k->species = 0;
    k->x = e->f[CF_X] / NET_POS_Q; k->y = e->f[CF_Y] / NET_POS_Q - NET_Y_OFS; k->z = e->f[CF_Z] / NET_POS_Q; k->yaw = dqyaw(e->f[CF_YAW]);
    k->growth = e->f[CF_GROWTH] / 255.0f; k->max_meat = 1; k->meat = e->f[CF_MEAT] / 255.0f;
    k->age = (int)((tick_ref - e->f[CF_BORN]) & 0xffff); k->agent = e->f[CF_AGENT] == 255 ? -1 : (int)e->f[CF_AGENT];
    k->item = (uint8_t)((e->f[CF_KIND] >> 4) & 3); k->critter = (uint8_t)((e->f[CF_KIND] >> 6) & 7); k->carrier = (uint8_t)e->f[CF_CARRIER];
}

// One field against its baseline. Delta fields: 2-bit code (same, small
// signed delta, medium signed delta, full value); others: changed bit + value.
static bool fits(int32_t d, int n) { return n > 0 && d >= -(1 << (n - 1)) && d < (1 << (n - 1)); }
static void put_field(Bits* b, const NetField* F, uint32_t v, uint32_t base) {
    if (!F->small) { if (v == base) bits_put(b, 0, 1); else { bits_put(b, 1, 1); bits_put(b, v, F->bits); } return; }
    if (v == base) { bits_put(b, 0, 2); return; }
    int32_t d = (int32_t)v - (int32_t)base;
    if (F->wrap) { uint32_t m = mask(F->bits), u = (v - base) & m; d = u > m / 2 ? (int32_t)u - (int32_t)(m + 1) : (int32_t)u; }
    if (fits(d, F->small)) { bits_put(b, 1, 2); bits_put(b, (uint32_t)d & mask(F->small), F->small); }
    else if (fits(d, F->medium)) { bits_put(b, 2, 2); bits_put(b, (uint32_t)d & mask(F->medium), F->medium); }
    else { bits_put(b, 3, 2); bits_put(b, v, F->bits); }
}
static int32_t sext(uint32_t v, int n) { return (int32_t)(v << (32 - n)) >> (32 - n); }
static uint32_t get_field(Bits* b, const NetField* F, uint32_t base) {
    if (!F->small) return bits_get(b, 1) ? bits_get(b, F->bits) : base;
    uint32_t code = bits_get(b, 2), m = mask(F->bits);
    if (code == 0) return base;
    if (code == 3) return bits_get(b, F->bits);
    int n = code == 1 ? F->small : F->medium;
    if (n == 0) { b->bad = true; return 0; }
    int32_t d = sext(bits_get(b, n), n);
    if (F->wrap) return (base + (uint32_t)d) & m;
    int64_t v = (int64_t)base + d;
    if (v < 0 || v > (int64_t)m) { b->bad = true; return 0; }
    return (uint32_t)v;
}
static const NetField* schema_of(const NetSchema* sc, int id, int* nf) {
    if (id < NET_CARCASS0) { *nf = AF_N; return sc->agent; }
    *nf = CF_N; return sc->carcass;
}
static void put_ent(Bits* b, const NetSchema* sc, int id, const NetEnt* v, const NetEnt* base) {
    int nf; const NetField* F = schema_of(sc, id, &nf);
    for (int f = 0; f < nf; f++) put_field(b, &F[f], v->f[f], base->f[f]);
}
// Decodes and range-checks one entity (num_agents bounds agent references).
static void get_ent(Bits* b, const NetSchema* sc, int id, const NetEnt* base, NetEnt* v, int num_agents) {
    int nf; const NetField* F = schema_of(sc, id, &nf);
    memset(v, 0, sizeof *v);
    for (int f = 0; f < nf; f++) v->f[f] = get_field(b, &F[f], base->f[f]);
    if (id < NET_CARCASS0) {
        if ((v->f[AF_KIND] & 7) >= CRET_SPECIES || (v->f[AF_KIND] >> 3) > CRET_ACT_MAX || v->f[AF_GROUP] > (uint32_t)num_agents || v->f[AF_AIM] > 2 * NET_AIM_Q) b->bad = true;
    } else if (v->f[CF_KIND] & 1) {
        if (((v->f[CF_KIND] >> 1) & 7) >= CRET_SPECIES || (v->f[CF_AGENT] != 255 && v->f[CF_AGENT] >= (uint32_t)num_agents) ||
            ((v->f[CF_KIND] >> 4) & 3) >= CRET_ITEM_KINDS || ((v->f[CF_KIND] >> 6) & 7) >= CRET_CR_KINDS || v->f[CF_CARRIER] > (uint32_t)num_agents) b->bad = true;
    } else memset(v, 0, sizeof *v);   // an inactive carcass carries nothing
}

// Inputs: three signed bytes, the flags, and the aim (v9) as a signed byte.
static int sbyte(float v) { return (int)lroundf((isfinite(v) ? clampf(v, -1, 1) : 0) * 127); }
uint64_t netp_pack_input(const CretInput* in) {
    int f = sbyte(in->forward), s = sbyte(in->strafe), t = sbyte(in->turn), aim = sbyte(in->aim);
    // Flags: sprint, jump, 3-bit action, 2-bit ability slot (v8), the carry press (v13, the top bit).
    uint32_t fl = (in->sprint ? 1u : 0) | (in->jump ? 2u : 0) | ((uint32_t)(in->action <= CRET_ACT_MAX ? in->action : 0) << 2) |
                  ((uint32_t)(in->ability <= CRET_AB_SLOTS ? in->ability : 0) << 5) | (in->carry ? 0x80u : 0);
    uint32_t lo = ((uint32_t)(uint8_t)(int8_t)s << 24) | ((uint32_t)(uint8_t)(int8_t)t << 16) | (fl << 8) | (uint32_t)(uint8_t)(int8_t)aim;
    return ((uint64_t)(uint8_t)(int8_t)f << 32) | lo;
}
CretInput netp_unpack_input(uint64_t v) {
    CretInput in = {0};
    int8_t f = (int8_t)(v >> 32), s = (int8_t)(v >> 24), t = (int8_t)(v >> 16), aim = (int8_t)v; uint32_t fl = (uint32_t)(v >> 8) & 0xff;
    in.forward = clampf(f / 127.0f, -1, 1); in.strafe = clampf(s / 127.0f, -1, 1); in.turn = clampf(t / 127.0f, -1, 1); in.aim = clampf(aim / 127.0f, -1, 1);
    in.sprint = fl & 1; in.jump = (fl >> 1) & 1; in.action = (uint8_t)((fl >> 2) & 7);
    in.ability = (uint8_t)((fl >> 5) & 3); in.carry = (uint8_t)(fl >> 7);
    if (in.action > CRET_ACT_MAX) in.action = 0;
    if (in.ability > CRET_AB_SLOTS) in.ability = 0;
    return in;
}
bool netp_input_valid(uint64_t v) {
    uint32_t fl = (uint32_t)(v >> 8) & 0xff;
    return (v >> NET_INPUT_BITS) == 0 && ((fl >> 2) & 7) <= CRET_ACT_MAX && ((fl >> 5) & 3) <= CRET_AB_SLOTS && (uint8_t)v != 0x80;
}
static void put_input(Bits* b, uint64_t v) { bits_put(b, (uint32_t)(v >> 32), NET_INPUT_BITS - 32); bits_put(b, (uint32_t)v, 32); }
static uint64_t get_input(Bits* b) { uint64_t hi = bits_get(b, NET_INPUT_BITS - 32); return (hi << 32) | bits_get(b, 32); }

// ---------------------------------------------------------------- v8 blocks: own statuses, progress, nests
#define NET_SECS_BITS 20     // timers in ms, up to 1048 s
static void put_secs(Bits* b, float v) { bits_put(b, (uint32_t)clampf(lroundf((isfinite(v) ? v : 0) * 1000), 0, (1 << NET_SECS_BITS) - 1), NET_SECS_BITS); }
static float get_secs(Bits* b) { return bits_get(b, NET_SECS_BITS) / 1000.0f; }
static void put_f32(Bits* b, float v) { v = isfinite(v) ? clampf(v, -1e6f, 1e6f) : 0.0f; uint32_t u; memcpy(&u, &v, 4); bits_put(b, u, 32); }
static float get_f32(Bits* b) { uint32_t u = bits_get(b, 32); float v; memcpy(&v, &u, 4); if (!isfinite(v) || fabsf(v) > 1e6f) { b->bad = true; return 0; } return v; }

static void put_self_ext(Bits* b, const CretAgent* a) {
    bits_put(b, a->strike < CRET_AB_COUNT ? a->strike : 0, 5); bits_put(b, a->latch, 8); bits_put(b, a->latchers, 8);
    bits_put(b, a->leap < CRET_AB_COUNT ? a->leap : 0, 5); bits_put(b, (uint32_t)(a->nest_ticks < 0 ? 0 : a->nest_ticks > 65535 ? 65535 : a->nest_ticks), 16);
    bits_put(b, q8(a->slow_amt), 8); bits_put(b, (uint32_t)clampf(lroundf(a->def_speed * 16384), 0, 65535), 16); put_secs(b, a->leap_t);
    for (int k = 0; k < CRET_ST_COUNT; k++) { bool on = a->st[k] > 0; bits_put(b, on, 1); if (on) put_secs(b, a->st[k]); }
    for (int k = 0; k < CRET_AB_SLOTS; k++) put_secs(b, a->ab_cd[k]);
    // v9: secondary cooldown, the queued attack, the aim (the input's, so exactly k/127).
    bits_put(b, (uint32_t)(a->attack2_cd > 255 ? 255 : a->attack2_cd < 0 ? 0 : a->attack2_cd), 8);
    bool queued = cret_act_is_attack(a->buf_act) && a->buf_ticks > 0;   // never anything the decoder refuses
    // v15: the queue holds CRET_ATTACK_BUFFER + 1 = 10 ticks, 4 bits.
    bits_put(b, queued ? a->buf_act : 0, 3); bits_put(b, queued ? (a->buf_ticks > CRET_ATTACK_BUFFER + 1 ? CRET_ATTACK_BUFFER + 1 : a->buf_ticks) : 0, 4);
    bits_put(b, (uint32_t)(uint8_t)(int8_t)sbyte(a->aim), 8);
    // v13: the carried slot and its load (prediction slows the animal by it).
    bits_put(b, a->carry <= CRET_MAX_CARCASSES ? a->carry : 0, 6); if (a->carry) put_f32(b, a->carry_kg);
}
static void get_self_ext(Bits* b, NetSelf* m, int num_agents) {
    m->strike = (uint8_t)bits_get(b, 5); m->latch = (uint8_t)bits_get(b, 8); m->latchers = (uint8_t)bits_get(b, 8);
    m->leap = (uint8_t)bits_get(b, 5); m->nest_ticks = (uint16_t)bits_get(b, 16);
    m->slow_amt = bits_get(b, 8) / 255.0f; m->def_speed = bits_get(b, 16) / 16384.0f; m->leap_t = get_secs(b);
    for (int k = 0; k < CRET_ST_COUNT; k++) m->st[k] = bits_get(b, 1) ? get_secs(b) : 0;
    for (int k = 0; k < CRET_AB_SLOTS; k++) m->ab_cd[k] = get_secs(b);
    if (m->strike >= CRET_AB_COUNT || m->leap >= CRET_AB_COUNT || m->latch > num_agents || m->latchers > num_agents) b->bad = true;
    m->attack2_cd = (uint8_t)bits_get(b, 8); m->buf_act = (uint8_t)bits_get(b, 3); m->buf_ticks = (uint8_t)bits_get(b, 4);
    int8_t aim = (int8_t)bits_get(b, 8); m->aim = aim / 127.0f;
    if ((m->buf_act && !cret_act_is_attack(m->buf_act)) || m->buf_ticks > CRET_ATTACK_BUFFER + 1 || (m->buf_act != 0) != (m->buf_ticks != 0) || aim == -128) b->bad = true;
    m->carry = (uint8_t)bits_get(b, 6); m->carry_kg = m->carry ? get_f32(b) : 0;
    if (m->carry > CRET_MAX_CARCASSES || m->carry_kg < 0) b->bad = true;
}

static void put_quest(Bits* b, const CretQuest* q) {
    bits_put(b, q->kind < CRET_Q_COUNT ? q->kind : 0, 4); bits_put(b, q->group, 8);   // v13: 4-bit kinds
    for (int k = 0; k < 3; k++) bits_put(b, q->poi[k], 8);
    bits_put(b, q->step > 3 ? 3 : q->step, 2); bits_put(b, q->steps > 3 ? 3 : q->steps, 2); bits_put(b, q->species < CRET_SPECIES ? q->species : 0, 3);
    put_f32(b, q->progress); put_f32(b, q->goal); bits_put(b, q8(q->xp), 8); bits_put(b, (uint32_t)(q->marks < 0 ? 0 : q->marks > 65535 ? 65535 : q->marks), 16);
    bits_put(b, q->spot != 0, 1);   // v12: a spot instead of a named place
    if (q->spot) { put_f32(b, q->spot_x); put_f32(b, q->spot_z); put_f32(b, clampf(q->spot_r, 0, 1000)); }
    bits_put(b, q->chain <= CRET_CHAIN_STEPS ? q->chain : 0, 3);   // v13: a first-steps quest
}
static void get_quest(Bits* b, CretQuest* q, int num_agents) {
    memset(q, 0, sizeof *q);
    q->kind = (uint8_t)bits_get(b, 4); q->group = (uint8_t)bits_get(b, 8);
    for (int k = 0; k < 3; k++) q->poi[k] = (uint8_t)bits_get(b, 8);
    q->step = (uint8_t)bits_get(b, 2); q->steps = (uint8_t)bits_get(b, 2); q->species = (uint8_t)bits_get(b, 3);
    q->progress = get_f32(b); q->goal = get_f32(b); q->xp = bits_get(b, 8) / 255.0f; q->marks = (int)bits_get(b, 16);
    q->spot = (uint8_t)bits_get(b, 1);
    if (q->spot) { q->spot_x = get_f32(b); q->spot_z = get_f32(b); q->spot_r = get_f32(b); if (q->spot_r < 0 || q->spot_r > 1000) b->bad = true; }
    q->chain = (uint8_t)bits_get(b, 3);
    if (q->chain > CRET_CHAIN_STEPS || q->kind >= CRET_Q_COUNT || q->group > num_agents || q->species >= CRET_SPECIES || q->step > q->steps) b->bad = true;
}
// The peer's own progress: marks, unlocks, loadout, its quest and its group's.
static void put_prog(Bits* b, const CretSim* s, int i) {
    const CretProgress* p = &s->prog[i]; const CretAgent* a = &s->agents[i];
    bits_put(b, (uint32_t)(p->marks < 0 ? 0 : p->marks > 65535 ? 65535 : p->marks), 16); bits_put(b, p->unlocked, 32);
    bits_put(b, (uint32_t)(p->quests_done < 0 ? 0 : p->quests_done > 65535 ? 65535 : p->quests_done), 16);
    for (int k = 0; k < CRET_AB_SLOTS; k++) bits_put(b, p->loadout[k] < CRET_AB_COUNT ? p->loadout[k] : 0, 5);
    put_quest(b, &p->quest);
    const CretQuest* g = a->group ? &s->gquest[a->group] : NULL;
    bits_put(b, g && g->kind, 1); if (g && g->kind) put_quest(b, g);
    bits_put(b, p->chain <= CRET_CHAIN_STEPS ? p->chain : CRET_CHAIN_STEPS, 3); bits_put(b, p->visited, 32);   // v13
}
static void get_prog(Bits* b, NetProg* p, int num_agents) {
    memset(p, 0, sizeof *p);
    p->marks = (int)bits_get(b, 16); p->unlocked = bits_get(b, 32); p->quests_done = (int)bits_get(b, 16);
    for (int k = 0; k < CRET_AB_SLOTS; k++) { p->loadout[k] = (uint8_t)bits_get(b, 5); if (p->loadout[k] >= CRET_AB_COUNT) b->bad = true; }
    get_quest(b, &p->quest, num_agents);
    p->has_group = bits_get(b, 1); if (p->has_group) get_quest(b, &p->group, num_agents);
    p->chain = (uint8_t)bits_get(b, 3); p->visited = bits_get(b, 32);
    if (p->chain > CRET_CHAIN_STEPS) b->bad = true;
}
static void put_nests(Bits* b, const CretSim* s) {
    int nn = 0; for (int k = 0; k < CRET_MAX_NESTS; k++) nn += s->nests[k].active;
    bits_put(b, (uint32_t)nn, 5);
    for (int k = 0; k < CRET_MAX_NESTS; k++) {
        const CretNest* q = &s->nests[k]; if (!q->active) continue;
        bits_put(b, (uint32_t)k, 4); bits_put(b, q->species < CRET_SPECIES ? q->species : 0, 3); bits_put(b, q->group, 8); bits_put(b, q->owner, 8);
        bits_put(b, (uint32_t)(q->eggs < 0 ? 0 : q->eggs > 255 ? 255 : q->eggs), 8); put_f32(b, q->x); put_f32(b, q->y); put_f32(b, q->z);
    }
}
static void get_nests(Bits* b, CretNest* nests, int num_agents) {
    memset(nests, 0, sizeof(CretNest) * CRET_MAX_NESTS);
    int nn = (int)bits_get(b, 5); if (nn > CRET_MAX_NESTS) b->bad = true;
    for (int q = 0; q < nn && !b->bad; q++) {
        int k = (int)bits_get(b, 4); CretNest t = {.active = true};
        t.species = (uint8_t)bits_get(b, 3); t.group = (uint8_t)bits_get(b, 8); t.owner = (uint8_t)bits_get(b, 8); t.eggs = (int)bits_get(b, 8);
        t.x = get_f32(b); t.y = get_f32(b); t.z = get_f32(b);
        if (t.species >= CRET_SPECIES || t.group > num_agents || t.owner > num_agents) b->bad = true; else nests[k] = t;
    }
}
// v10: recent hits near the client (net_priv.h). Its own come first.
void netp_put_hits(Bits* b, const CretSim* s, int slot) {
    const CretAgent* me = &s->agents[slot]; int idx[NET_HIT_MAX], n = 0;
    uint32_t k0 = s->hit_count > CRET_HIT_LOG ? s->hit_count - CRET_HIT_LOG : 0;
    for (int pass = 0; pass < 2; pass++)
        for (uint32_t k = s->hit_count; k-- > k0 && n < NET_HIT_MAX;) {
            const CretHit* h = &s->hits[k % CRET_HIT_LOG];
            int age = s->tick - h->tick; if (age < 0 || age >= NET_HIT_WINDOW) break;
            if (h->attacker >= s->cfg.num_agents || h->victim >= s->cfg.num_agents || h->kind >= CRET_HIT_KINDS || h->attacker == h->victim) continue;
            bool own = h->attacker == slot || h->victim == slot;
            const CretAgent* v = &s->agents[h->victim]; float dx = v->x - me->x, dz = v->z - me->z;
            if (pass == 0 ? !own : own || dx * dx + dz * dz > NET_HIT_RANGE * NET_HIT_RANGE) continue;
            idx[n++] = (int)(k % CRET_HIT_LOG);
        }
    bits_put(b, (uint32_t)n, 5);
    for (int i = 0; i < n; i++) {
        const CretHit* h = &s->hits[idx[i]];
        bits_put(b, (uint32_t)(s->tick - h->tick), 7); bits_put(b, h->attacker, 7); bits_put(b, h->victim, 7);
        bits_put(b, h->kind, NET_HIT_KIND_BITS); bits_put(b, h->dmg ? h->dmg : 1, 8);
    }
}
int netp_get_hits(Bits* b, CretHit* hits, uint32_t tick, int num_agents) {
    int n = (int)bits_get(b, 5); if (n > NET_HIT_MAX) { b->bad = true; return 0; }
    for (int i = 0; i < n && !b->bad; i++) {
        uint32_t age = bits_get(b, 7), at = bits_get(b, 7), vi = bits_get(b, 7), kind = bits_get(b, NET_HIT_KIND_BITS), dmg = bits_get(b, 8);
        if (age >= NET_HIT_WINDOW || (int)at >= num_agents || (int)vi >= num_agents || at == vi || kind >= CRET_HIT_KINDS || dmg == 0) { b->bad = true; return 0; }
        hits[i] = (CretHit){(int32_t)(tick - age), (uint8_t)at, (uint8_t)vi, (uint8_t)kind, (uint8_t)dmg};
    }
    return b->bad ? 0 : n;
}

static uint32_t fnv(const uint8_t* d, size_t n) { uint32_t h = 2166136261u; for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u; return h; }
// Writes a block (encoded into tmp) behind a presence bit when it changed in
// the last NET_BLOCK_RESEND snapshots or is due for a refresh.
static void put_block(Bits* b, const Bits* tmp, uint32_t seq, uint32_t* hash, uint32_t* changed) {
    uint32_t h = fnv(tmp->p, bits_bytes(tmp)) ^ (uint32_t)tmp->pos;
    if (h != *hash || !*changed) { *hash = h; *changed = seq; }
    bool send = !tmp->bad && (seq - *changed < NET_BLOCK_RESEND || seq % NET_BLOCK_REFRESH == 0);
    bits_put(b, send, 1);
    if (!send) return;
    for (size_t i = 0; i < tmp->pos; i++) bits_put(b, (tmp->p[i >> 3] >> (7 - (i & 7))) & 1u, 1);
}

static void send_bits(ENetPeer* peer, int ch, const uint8_t* d, size_t len, bool reliable) {
    if (!peer || !len) return;
    ENetPacket* pk = enet_packet_create(d, len, reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED);
    if (pk && enet_peer_send(peer, (enet_uint8)ch, pk) < 0) enet_packet_destroy(pk);
}
// ENet throttles unreliable packets when the round trip varies (it drops
// them on purpose); under jitter that would throw snapshots away.
static void no_throttle(ENetPeer* p) { enet_peer_throttle_configure(p, ENET_PEER_PACKET_THROTTLE_INTERVAL, ENET_PEER_PACKET_THROTTLE_SCALE, 0); }

// ---------------------------------------------------------------- server
CretNet* cret_net_host(int port, int first_slot) {
    if (!enet_up()) return NULL;
    ENetAddress addr = {.host = ENET_HOST_ANY, .port = (enet_uint16)port};
    CretNet* n = calloc(1, sizeof(*n)); n->first_slot = first_slot;
    n->host = enet_host_create(&addr, CRET_MAX_PEERS, CH_COUNT, 0, 0);
    if (!n->host) { free(n); fprintf(stderr, "cretaceous: cannot listen on port %d\n", port); return NULL; }
    fprintf(stderr, "cretaceous: hosting on UDP %d (protocol %d)\n", port, CRET_NET_VERSION);
    return n;
}

static bool have_input(const NetPeer* np, uint32_t seq) { return np->in_seq[seq % NET_IN_RING] == seq; }

bool netp_server_input(NetPeer* np, const uint8_t* d, size_t len) {
    Bits b = {(uint8_t*)d, len, 0, false};
    if (bits_get(&b, 8) != MSG_INPUT) return false;
    uint32_t ack = bits_get(&b, 32), ack_mask = bits_get(&b, 32), newest = bits_get(&b, 32);
    int cnt = (int)bits_get(&b, 5) + 1;
    CretInput in[NET_REDUNDANCY]; uint64_t raw = 0;
    for (int i = 0; i < cnt; i++) {
        if (i == 0 || !bits_get(&b, 1)) raw = get_input(&b);
        in[i] = netp_unpack_input(raw);
        if (!netp_input_valid(raw)) b.bad = true;   // every action head in range
    }
    if (b.bad || newest == 0) return false;
    for (int i = 0; i < cnt; i++) {   // in[i] is input newest - i
        uint32_t seq = newest - (uint32_t)i;
        if (seq == 0 || seq <= np->applied) break;
        if (seq - np->applied > NET_IN_RING - 1) continue;   // too far ahead to queue
        np->in[seq % NET_IN_RING] = in[i]; np->in_seq[seq % NET_IN_RING] = seq;
        if (seq > np->newest) np->newest = seq;
    }
    // Snapshot acks: each newly acknowledged snapshot moves the baselines of the entities it held.
    for (int i = 0; i <= 32; i++) {
        if (i > 0 && !((ack_mask >> (i - 1)) & 1)) continue;
        uint32_t s = ack - (uint32_t)i;
        if (s == 0 || s > np->seq || np->seq - s >= NET_HIST) continue;
        int h = (int)(s % NET_HIST);
        if (np->sent_seq[h] != s || np->sent_acked[h]) continue;
        np->sent_acked[h] = 1;
        netp_props_acked(np, h, s);   // v11
        for (int e = 0; e < NET_NENT; e++)
            if (np->sent_has[h][e] && s > np->base_seq[e]) { np->base[e] = np->sent[h][e]; np->base_seq[e] = s; }
    }
    return true;
}

static NetPeer* peer_new(int slot, ENetPeer* p, int tick) {
    NetPeer* np = calloc(1, sizeof *np);
    if (!np) return NULL;
    np->peer = p; np->slot = slot; np->qmin = 1 << 30;
    np->req_tick = tick - CRET_NET_REQUEST_COOLDOWN; np->hurt_tick = tick - CRET_NET_COMBAT_LOCK; np->pending = REQ_NONE;
    return np;
}

// ---------------------------------------------------------------- respawn requests
static void send_spawn_reply(NetPeer* np, int seq, int result, int species, int wait_s) {
    uint8_t raw[8] = {0}; Bits b = {raw, sizeof raw, 0, false};
    bits_put(&b, MSG_SPAWN_REPLY, 8); bits_put(&b, (uint32_t)seq & 255, 8); bits_put(&b, (uint32_t)result, 8);
    bits_put(&b, (uint32_t)species, 8); bits_put(&b, (uint32_t)(wait_s < 0 ? 0 : wait_s > 255 ? 255 : wait_s), 8);
    send_bits(np->peer, CH_RELIABLE, raw, bits_bytes(&b), true);
}

bool netp_server_request(NetPeer* np, const CretSim* s, const uint8_t* d, size_t len) {
    Bits b = {(uint8_t*)d, len, 0, false};
    if (bits_get(&b, 8) != MSG_REQUEST) return false;
    int seq = (int)bits_get(&b, 8), sp = (int)bits_get(&b, 8);
    if (b.bad || np->slot >= s->cfg.num_agents) return false;
    if (sp == 255 || sp < CRET_SPECIES) { np->pending = sp == 255 ? REQ_SAME : sp; np->pending_seq = (uint8_t)seq; return true; }
    send_spawn_reply(np, seq, CRET_SPAWN_BAD_SPECIES, s->agents[np->slot].species, 0);
    return false;
}

bool netp_server_profile(NetPeer* np, CretSim* s, const uint8_t* d, size_t len) {
    Bits b = {(uint8_t*)d, len, 0, false};
    int type = (int)bits_get(&b, 8), slot = np->slot;
    if (slot >= s->cfg.num_agents) return false;
    if (type == MSG_PROFILE) {   // the host loads that character (cretaceous.c, server_characters)
        int n = (int)bits_get(&b, 5); char name[24] = {0};
        for (int c = 0; c < n; c++) name[c < 23 ? c : 23] = (char)bits_get(&b, 8);
        name[23] = 0;
        if (b.bad || !name[0]) return false;
        memcpy(s->prog[slot].name, name, sizeof name); s->prog[slot].load_pending = 1;
        return true;
    }
    if (type == MSG_UNLOCK) {
        int ch = (int)bits_get(&b, 1);
        return !b.bad && s->agents[slot].alive && cret_choice_take(s, slot, ch);
    }
    return false;
}

// Settles a held request once the cooldown is up: refused while the animal
// lives and was hit recently, else the sim respawns it where it chooses.
void netp_server_settle(NetPeer* np, CretSim* s) {
    int slot = np->slot, want = np->pending;
    if (want == REQ_NONE || slot >= s->cfg.num_agents) return;
    if (s->tick < np->req_tick) np->req_tick = s->tick - CRET_NET_REQUEST_COOLDOWN;   // the sim was reset
    if (s->tick - np->req_tick < CRET_NET_REQUEST_COOLDOWN) return;
    int seq = np->pending_seq; np->pending = REQ_NONE;
    const CretAgent* a = &s->agents[slot];
    int since_hit = s->tick - np->hurt_tick;
    if (a->alive && since_hit >= 0 && since_hit < CRET_NET_COMBAT_LOCK) {
        send_spawn_reply(np, seq, CRET_SPAWN_IN_COMBAT, a->species, (CRET_NET_COMBAT_LOCK - since_hit + CRET_TICK_HZ - 1) / CRET_TICK_HZ);
        return;
    }
    if (want >= 0) cret_sim_set_species(s, slot, want); else cret_sim_respawn(s, slot);
    np->req_tick = s->tick; np->hurt_tick = s->tick - CRET_NET_COMBAT_LOCK;
    send_spawn_reply(np, seq, CRET_SPAWN_OK, a->species, 0);
    fprintf(stderr, "cretaceous: slot %d respawned as %s\n", slot, CRET_SPECIES_TABLE[a->species].name);
}

void cret_net_server_poll(CretNet* n, CretSim* s, CretInput* inputs, bool* remote) {
    if (n->sc.pos_bits == 0) netp_schema(&n->sc, s->world ? s->world->size : s->cfg.world_n * s->cfg.world_cell);
    ENetEvent ev;
    while (enet_host_service(n->host, &ev, 0) > 0) {
        if (ev.type == ENET_EVENT_TYPE_CONNECT) {
            if (ev.data != CRET_NET_VERSION) {
                fprintf(stderr, "cretaceous: refused a client with protocol %u (this server speaks %d)\n", ev.data, CRET_NET_VERSION);
                enet_peer_disconnect(ev.peer, NET_REJECT_VERSION | CRET_NET_VERSION); continue;
            }
            int k = -1; for (int i = 0; i < CRET_MAX_PEERS; i++) if (!n->peers[i]) { k = i; break; }
            int slot = n->first_slot + k;
            if (k < 0 || slot >= s->cfg.num_agents) { enet_peer_disconnect(ev.peer, 0); continue; }
            NetPeer* np = peer_new(slot, ev.peer, s->tick);
            if (!np) { enet_peer_disconnect(ev.peer, 0); continue; }
            n->peers[k] = np; ev.peer->data = (void*)(intptr_t)(k + 1);
            no_throttle(ev.peer);
            uint8_t raw[32] = {0}; Bits b = {raw, sizeof raw, 0, false};
            float cell = s->cfg.world_cell; uint32_t cu; memcpy(&cu, &cell, 4);
            bits_put(&b, MSG_WELCOME, 8); bits_put(&b, CRET_NET_VERSION, 8); bits_put(&b, s->cfg.world_seed, 32);
            bits_put(&b, (uint32_t)s->cfg.world_n, 32); bits_put(&b, cu, 32); bits_put(&b, (uint32_t)s->cfg.num_agents, 8); bits_put(&b, (uint32_t)slot, 8);
            bits_put(&b, CRET_TICK_HZ, 8); bits_put(&b, CRET_SNAPSHOT_DIV, 8);
            send_bits(ev.peer, CH_RELIABLE, raw, bits_bytes(&b), true);
            s->human[slot] = 1; s->species_pick[slot] = 0;   // the slot's default species until the player picks one
            s->prog[slot].name[0] = 0; cret_prog_reset(s, slot, true);   // a fresh character until the client names one (MSG_PROFILE)
            cret_sim_respawn(s, slot);
            fprintf(stderr, "cretaceous: player joined slot %d\n", slot);
        } else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
            int k = (int)(intptr_t)ev.peer->data - 1;
            if (k >= 0 && k < CRET_MAX_PEERS && n->peers[k]) {
                int slot = n->peers[k]->slot;
                free(n->peers[k]); n->peers[k] = NULL; ev.peer->data = NULL;
                if (slot < CRET_MAX_AGENTS) { s->human[slot] = 0; s->hold[slot] = 0; s->species_pick[slot] = 0; }   // the bot taking over goes back to the default mix
                fprintf(stderr, "cretaceous: player left slot %d\n", slot);
            }
        } else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
            int k = (int)(intptr_t)ev.peer->data - 1;
            NetPeer* np = k >= 0 && k < CRET_MAX_PEERS ? n->peers[k] : NULL;
            const uint8_t* d = ev.packet->data; size_t len = ev.packet->dataLength;
            if (np && len > 0 && d[0] == MSG_INPUT) netp_server_input(np, d, len);
            else if (np && len > 0 && d[0] == MSG_REQUEST) netp_server_request(np, s, d, len);   // respawn, optionally as another species
            else if (np && len > 0) netp_server_profile(np, s, d, len);   // character name, slot-3 unlock
            enet_packet_destroy(ev.packet);
        }
    }
    for (int k = 0; k < CRET_MAX_PEERS; k++) {
        int slot = n->first_slot + k; if (slot >= s->cfg.num_agents) break;
        remote[slot] = false; s->hold[slot] = 0;
    }
    // Each peer's inputs in sequence: one per tick. A missing one holds the
    // animal (so the client's prediction stays exact); a queue that stays
    // deeper than needed for 2 s, or beyond NET_BUFFER_MAX, is drained by
    // applying extra inputs at once.
    for (int k = 0; k < CRET_MAX_PEERS; k++) {
        NetPeer* np = n->peers[k]; if (!np) continue;
        int slot = np->slot; remote[slot] = true; n->sst.ticks++;
        int queued = 0; while (queued < NET_IN_RING - 1 && have_input(np, np->applied + 1 + (uint32_t)queued)) queued++;
        if (queued == 0 && np->newest > np->applied + NET_REDUNDANCY) {   // the client stopped resending these: skip the gap
            uint32_t s0 = np->applied + 1; while (s0 < np->newest && !have_input(np, s0)) s0++;
            n->sst.inputs_lost += s0 - 1 - np->applied; np->applied = s0 - 1;
            while (queued < NET_IN_RING - 1 && have_input(np, np->applied + 1 + (uint32_t)queued)) queued++;
        }
        int extra = queued - 1 > NET_BUFFER_MAX ? queued - 1 - NET_BUFFER_MAX : 0;
        if (queued - 1 < np->qmin) np->qmin = queued - 1;
        if (++np->qwin >= 2 * CRET_TICK_HZ) { if (np->qmin >= 1 && extra == 0) extra = 1; np->qwin = 0; np->qmin = 1 << 30; }
        if (extra > 2) extra = 2;
        for (int e = 0; e < extra; e++) {
            np->applied++; cret_sim_move(s, slot, &np->in[np->applied % NET_IN_RING], false); n->sst.caught_up++;
        }
        if (queued - extra > 0) { np->applied++; inputs[slot] = np->in[np->applied % NET_IN_RING]; }
        else { s->hold[slot] = 1; inputs[slot] = (CretInput){0}; n->sst.held++; }
        if (s->agents[slot].alive && s->agents[slot].hurt_ticks > 0) np->hurt_tick = s->tick;
        netp_server_settle(np, s);
    }
    enet_host_flush(n->host);   // replies go out now, not with the next snapshot
}

static int cmp_prio(const void* a, const void* b) {
    float x = ((const float*)a)[1], y = ((const float*)b)[1];
    return x < y ? 1 : x > y ? -1 : 0;
}

#include "netprops.c"   // v11 movable props
#include "netcritters.c"   // v12 critters

size_t netp_server_snapshot(CretNet* n, NetPeer* np, const CretSim* s, uint8_t* out, size_t cap) {
    if (cap > NET_SNAP_BUDGET) cap = NET_SNAP_BUDGET;
    uint32_t seq = ++np->seq; int h = (int)(seq % NET_HIST);
    memset(np->sent_has[h], 0, sizeof np->sent_has[h]); np->sent_seq[h] = seq; np->sent_acked[h] = 0;
    Bits b = {out, cap, 0, false};
    const CretAgent* me = &s->agents[np->slot];
    bits_put(&b, MSG_SNAPSHOT, 8); bits_put(&b, seq, 32); bits_put(&b, (uint32_t)s->tick, 32); bits_put(&b, np->applied, 32);
    int q = 0; while (q < 15 && have_input(np, np->applied + 1 + (uint32_t)q)) q++;
    bits_put(&b, (uint32_t)q, 4);
    bits_put(&b, (uint32_t)clampf(lroundf(s->time_of_day * 65535), 0, 65535), 16); bits_put(&b, q8(s->cloud_cover), 8);
    // Own state, full precision.
    bits_put(&b, 1, 1);
    bits_put(&b, me->alive, 1); bits_put(&b, me->on_ground, 1); bits_put(&b, me->swimming, 1); bits_put(&b, me->sprinting, 1);
    bits_put(&b, me->act <= CRET_ACT_MAX ? me->act : 0, 3);
    bits_put(&b, (uint32_t)(me->act_ticks > 255 ? 255 : me->act_ticks < 0 ? 0 : me->act_ticks), 8);
    bits_put(&b, (uint32_t)(me->attack_cd > 255 ? 255 : me->attack_cd < 0 ? 0 : me->attack_cd), 8);
    bits_put(&b, (uint32_t)(me->hurt_ticks > 255 ? 255 : me->hurt_ticks < 0 ? 0 : me->hurt_ticks), 8);
    bits_put(&b, q8(me->food), 8); bits_put(&b, q8(me->water), 8);
    const float fv[11] = {me->x, me->y, me->z, me->yaw, me->vx, me->vy, me->vz, me->speed, me->stamina, me->broken, me->growth};
    for (int i = 0; i < 11; i++) {   // never anything the decoder refuses
        float v = isfinite(fv[i]) ? clampf(fv[i], -1e6f, 1e6f) : 0.0f;
        if (i == 3 && (v < -7 || v > 7)) v = dqyaw(qyaw(v));
        if (i == 10) v = clampf(v, 0, 1);
        uint32_t u; memcpy(&u, &v, 4); bits_put(&b, u, 32);
    }
    put_self_ext(&b, me);
    {   // v8: own progress, then the nests (each only while it changes, and once a second)
        uint8_t raw[160] = {0}; Bits t = {raw, sizeof raw, 0, false};
        put_prog(&t, s, np->slot); put_block(&b, &t, seq, &np->prog_hash, &np->prog_changed);
        uint8_t rn[CRET_MAX_NESTS * 20 + 4] = {0}; Bits u = {rn, sizeof rn, 0, false};
        put_nests(&u, s); put_block(&b, &u, seq, &np->nest_hash, &np->nest_changed);
    }
    netp_put_hits(&b, s, np->slot);   // v10
    netp_put_props(&b, n, np, s, seq, b.pos + NET_PROP_BUDGET * 8 < cap * 8 ? b.pos + NET_PROP_BUDGET * 8 : cap * 8);   // v11
    netp_put_critters(&b, s, np->slot);   // v12
    // Entities by priority until the budget is spent.
    size_t count_pos = b.pos; bits_put(&b, 0, 8);
    float cand[NET_NENT][2]; int nc = 0;
    int na = s->cfg.num_agents;
    for (int e = 0; e < NET_NENT; e++) {
        if (e >= na && e < NET_CARCASS0) continue;
        const NetEnt* cur = &n->q[e];
        // Worth sending when it differs from what the client acknowledged, or
        // when an unacknowledged send differs from it (or may have been lost).
        static const NetEnt zero;
        const NetEnt* base = np->base_seq[e] ? &np->base[e] : &zero;
        bool changed = memcmp(cur, base, sizeof *cur) != 0;
        bool unacked = np->last_seq[e] > np->base_seq[e];
        bool needs = changed || (unacked && (memcmp(cur, &np->last[e], sizeof *cur) != 0 || seq - np->last_seq[e] > 8));
        float x, z;
        if (e < NET_CARCASS0) { x = s->agents[e].x; z = s->agents[e].z; }
        else { x = s->carcasses[e - NET_CARCASS0].x; z = s->carcasses[e - NET_CARCASS0].z; }
        float d = sqrtf((x - me->x) * (x - me->x) + (z - me->z) * (z - me->z));
        np->prio[e] += netp_interest(d);
        // Discrete changes (death, a new action, a flinch) go out at once.
        if (e < NET_CARCASS0 && (cur->f[AF_FLAGS] != np->last[e].f[AF_FLAGS] || cur->f[AF_KIND] != np->last[e].f[AF_KIND])) np->prio[e] += 1.0f;
        if (e >= NET_CARCASS0 && cur->f[CF_KIND] != np->last[e].f[CF_KIND]) np->prio[e] += 1.0f;
        if (!needs) { if (np->prio[e] > 2) np->prio[e] = 2; continue; }
        if (e == np->slot) np->prio[e] = 1e9f;
        if (np->prio[e] >= 1.0f) { cand[nc][0] = (float)e; cand[nc][1] = np->prio[e]; nc++; }
    }
    qsort(cand, (size_t)nc, sizeof cand[0], cmp_prio);
    int count = 0; size_t limit = cap * 8;
    for (int c = 0; c < nc && count < 255; c++) {
        int e = (int)cand[c][0];
        bool has_base = np->base_seq[e] && seq - np->base_seq[e] < NET_MAX_AGE;
        static const NetEnt zero;
        size_t p0 = b.pos;
        bits_put(&b, (uint32_t)e, 8); bits_put(&b, has_base ? seq - np->base_seq[e] : 0, 5);
        put_ent(&b, &n->sc, e, &n->q[e], has_base ? &np->base[e] : &zero);
        if (b.bad || b.pos > limit) { b.pos = p0; b.bad = false; n->sst.budget_full++; break; }
        np->sent[h][e] = n->q[e]; np->sent_has[h][e] = 1; np->last[e] = n->q[e]; np->last_seq[e] = seq; np->prio[e] = 0;
        count++;
    }
    size_t end = b.pos; b.pos = count_pos; bits_put(&b, (uint32_t)count, 8); b.pos = end;
    if (b.bad) return 0;
    n->sst.entities_sent += (uint64_t)count;
    return bits_bytes(&b);
}

void cret_net_server_broadcast(CretNet* n, const CretSim* s) {
    if (n->sc.pos_bits == 0) netp_schema(&n->sc, s->world ? s->world->size : s->cfg.world_n * s->cfg.world_cell);
    for (int i = 0; i < s->cfg.num_agents; i++) netp_quant_agent(&n->sc, s, i, &n->q[i]);
    for (int c = 0; c < CRET_MAX_CARCASSES; c++) netp_quant_carcass(&n->sc, s, c, &n->q[NET_CARCASS0 + c]);
    uint8_t raw[NET_SNAP_BUDGET];
    for (int k = 0; k < CRET_MAX_PEERS; k++) {
        NetPeer* np = n->peers[k]; if (!np) continue;
        size_t len = netp_server_snapshot(n, np, s, raw, sizeof raw);
        if (len) { send_bits(np->peer, CH_STATE, raw, len, false); n->sst.snapshots++; n->sst.snapshot_bytes += len; }
    }
    enet_host_flush(n->host);
}

void cret_net_server_stats(const CretNet* n, CretNetServerStats* st) {
    *st = n->sst; st->peers = 0;
    for (int k = 0; k < CRET_MAX_PEERS; k++) st->peers += n->peers[k] != NULL;
}

// ---------------------------------------------------------------- client transport
CretNet* cret_net_join_start(const char* address, int port, const CretNetsimCfg* netsim, uint32_t seed) {
    if (!enet_up()) return NULL;
    CretNet* n = calloc(1, sizeof(*n));
    if (!n) return NULL;
    n->host = enet_host_create(NULL, 1, CH_COUNT, 0, 0);
    ENetAddress addr; addr.port = (enet_uint16)port;
    if (!n->host || enet_address_set_host(&addr, address) != 0) { fprintf(stderr, "cretaceous: cannot resolve %s\n", address); cret_net_close(n); return NULL; }
    CretNetsimCfg env;
    const char* spec = getenv("CRET_NETSIM");
    if (!netsim && spec && *spec) {
        if (!cret_netsim_parse(spec, &env)) { fprintf(stderr, "cretaceous: bad CRET_NETSIM \"%s\" (netsim.h lists the keys)\n", spec); cret_net_close(n); return NULL; }
        netsim = &env;
    }
    if (netsim) {
        ENetAddress local;
        n->netsim = cret_netsim_open(netsim, &addr, seed, &local);
        if (!n->netsim) { fprintf(stderr, "cretaceous: netsim relay failed\n"); cret_net_close(n); return NULL; }
        char d[160]; cret_netsim_describe(netsim, d, sizeof d);
        if (spec && netsim == &env) fprintf(stderr, "cretaceous: netsim %s\n", d);
        addr = local;
    }
    n->server = enet_host_connect(n->host, &addr, CH_COUNT, CRET_NET_VERSION);
    if (!n->server) { cret_net_close(n); return NULL; }
    enet_host_flush(n->host); cret_netsim_pump(n->netsim);
    return n;
}

CretNet* cret_net_join(const char* address, int port, float timeout_s) {
    CretNet* n = cret_net_join_start(address, port, NULL, (uint32_t)time(NULL));
    if (!n) return NULL;
    double t0 = cret_netsim_now();
    while (!n->welcomed && !n->failed && cret_netsim_now() - t0 < timeout_s) {
        cret_net_client_poll(n, NULL);
        struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL);
    }
    if (!n->welcomed) {
        if (n->reject >> 24 == (NET_REJECT_VERSION >> 24))
            fprintf(stderr, "cretaceous: %s:%d speaks protocol %u, this client %d\n", address, port, n->reject & 0xff, CRET_NET_VERSION);
        else if (!n->connected) fprintf(stderr, "cretaceous: could not reach %s:%d\n", address, port);
        else fprintf(stderr, "cretaceous: server did not send a compatible welcome\n");
        cret_net_close(n); return NULL;
    }
    enet_host_flush(n->host); cret_netsim_pump(n->netsim);   // acknowledge the welcome before the world loads
    return n;
}

bool cret_net_client_welcomed(const CretNet* n, CretConfig* cfg, int* slot) {
    if (!n || !n->welcomed) return false;
    *cfg = n->cfg; *slot = n->slot; return true;
}

static bool client_welcome(CretNet* n, const uint8_t* d, size_t len) {
    Bits b = {(uint8_t*)d, len, 0, false};
    if (bits_get(&b, 8) != MSG_WELCOME || bits_get(&b, 8) != CRET_NET_VERSION) return false;
    CretConfig cfg = cret_default_config();
    cfg.world_seed = bits_get(&b, 32); cfg.world_n = (int)bits_get(&b, 32);
    uint32_t cu = bits_get(&b, 32); memcpy(&cfg.world_cell, &cu, 4);
    cfg.num_agents = (int)bits_get(&b, 8); int slot = (int)bits_get(&b, 8);
    int hz = (int)bits_get(&b, 8), div = (int)bits_get(&b, 8);
    if (b.bad || cfg.world_n < 256 || cfg.world_n > 8192 || !isfinite(cfg.world_cell) || cfg.world_cell < 0.25f || cfg.world_cell > 64 ||
        cfg.num_agents < 1 || cfg.num_agents > CRET_MAX_AGENTS || slot >= cfg.num_agents || hz != CRET_TICK_HZ || div < 1) return false;
    NetClient* c = netp_client_new(&cfg, slot);
    if (!c) return false;
    free(n->cl); n->cl = c; n->cfg = cfg; n->slot = slot; n->welcomed = true;
    return true;
}

bool netp_client_packet(CretNet* n, const uint8_t* d, size_t len, double now) {
    if (len == 0) return false;
    if (d[0] == MSG_WELCOME) return !n->welcomed && client_welcome(n, d, len);
    if (d[0] == MSG_SPAWN_REPLY) {
        Bits b = {(uint8_t*)d, len, 0, false}; bits_get(&b, 8);
        CretSpawnReply r; r.seq = (int)bits_get(&b, 8); r.result = (int)bits_get(&b, 8); r.species = (int)bits_get(&b, 8); r.wait_s = (int)bits_get(&b, 8);
        if (b.bad || r.result > CRET_SPAWN_IN_COMBAT || r.species >= CRET_SPECIES) return false;
        n->reply = r; n->reply_ready = true; return true;
    }
    if (d[0] != MSG_SNAPSHOT || !n->cl) return false;
    NetClient* c = n->cl;
    Bits b = {(uint8_t*)d, len, 0, false};
    bits_get(&b, 8);
    uint32_t seq = bits_get(&b, 32), tick = bits_get(&b, 32), input_ack = bits_get(&b, 32);
    int qdepth = (int)bits_get(&b, 4);
    float tod = bits_get(&b, 16) / 65535.0f, cloud = bits_get(&b, 8) / 255.0f;
    NetSelf me = {0}; bool has_self = bits_get(&b, 1);
    if (has_self) {
        me.alive = bits_get(&b, 1); me.on_ground = bits_get(&b, 1); me.swimming = bits_get(&b, 1); me.sprinting = bits_get(&b, 1);
        me.act = (uint8_t)bits_get(&b, 3); me.act_ticks = (uint8_t)bits_get(&b, 8); me.attack_cd = (uint8_t)bits_get(&b, 8); me.hurt_ticks = (uint8_t)bits_get(&b, 8);
        me.food = (uint8_t)bits_get(&b, 8); me.water = (uint8_t)bits_get(&b, 8);
        float* fv[11] = {&me.x, &me.y, &me.z, &me.yaw, &me.vx, &me.vy, &me.vz, &me.speed, &me.stamina, &me.broken, &me.growth};
        for (int i = 0; i < 11; i++) {
            uint32_t u = bits_get(&b, 32); memcpy(fv[i], &u, 4);
            if (!isfinite(*fv[i]) || fabsf(*fv[i]) > 1e6f) b.bad = true;
        }
        if (me.act > CRET_ACT_MAX || me.yaw < -7 || me.yaw > 7 || me.growth < 0 || me.growth > 1.01f) b.bad = true;
        get_self_ext(&b, &me, c->num_agents);
    }
    static NetProg prog; static CretNest nests[CRET_MAX_NESTS];
    bool has_prog = bits_get(&b, 1); if (has_prog) get_prog(&b, &prog, c->num_agents);
    bool has_nests = bits_get(&b, 1); if (has_nests) get_nests(&b, nests, c->num_agents);
    CretHit hits[NET_HIT_MAX]; int nhits = netp_get_hits(&b, hits, tick, c->num_agents);
    static NetPropSent props[NET_PROP_MAX]; int nprops = netp_get_props(&b, &c->sc, props);   // v11
    static CretCritter crit[NET_CRIT_MAX]; static uint16_t crit_slot[NET_CRIT_MAX]; int ncrit = netp_get_critters(&b, crit, crit_slot);   // v12
    if (b.bad || seq == 0) { c->st.snapshots_dropped++; return false; }
    // Duplicates and snapshots too old to acknowledge.
    if (c->recv_max) {
        if (seq == c->recv_max || (seq < c->recv_max && (c->recv_max - seq > NET_REORDER || ((c->recv_mask >> (c->recv_max - seq - 1)) & 1)))) {
            c->st.snapshots_dropped++; return false;
        }
    }
    int count = (int)bits_get(&b, 8);
    static NetEnt ents[256]; int ids[256];
    for (int k = 0; k < count && !b.bad; k++) {
        int id = (int)bits_get(&b, 8), age = (int)bits_get(&b, 5);
        if (b.bad || (id >= c->num_agents && id < NET_CARCASS0) || id >= NET_NENT) { b.bad = true; break; }
        static const NetEnt zero;
        const NetEnt* base = &zero;
        if (age) {
            uint32_t bs = seq - (uint32_t)age; int bh = (int)(bs % NET_HIST);
            if (bs == 0 || c->hist_seq[id][bh] != bs) { b.bad = true; break; }   // baseline missing: refuse (and do not ack) the snapshot
            base = &c->hist[id][bh];
        }
        get_ent(&b, &c->sc, id, base, &ents[k], c->num_agents);
        ids[k] = id;
    }
    if (b.bad) { c->st.snapshots_dropped++; return false; }
    // Accepted: store, ack, interpolate.
    for (int k = 0; k < count; k++) {
        int id = ids[k], bh = (int)(seq % NET_HIST);
        c->hist[id][bh] = ents[k]; c->hist_seq[id][bh] = seq;
        if (seq > c->latest_seq[id]) { c->latest[id] = ents[k]; c->latest_seq[id] = seq; }
        if (id < c->num_agents) {   // interpolation buffer, sorted by tick
            // The server sends an agent every `gap` ticks at its distance, and
            // not at all while it stands still: after a longer silence it was
            // where it last was until one gap before this sample.
            const NetEnt* m = &c->latest[c->me]; const NetEnt* e = &ents[k];
            float dx = ((float)e->f[AF_X] - (float)m->f[AF_X]) / NET_POS_Q, dz = ((float)e->f[AF_Z] - (float)m->f[AF_Z]) / NET_POS_Q;
            float gap = CRET_SNAPSHOT_DIV / netp_interest(sqrtf(dx * dx + dz * dz));
            c->gap[id] += (gap - c->gap[id]) * 0.3f;
            NetSample* S = c->samp[id]; int ns = c->nsamp[id];
            for (int pass = 0; pass < 2; pass++) {
                NetSample add = {tick, ents[k]};
                if (pass == 0) {
                    if (ns == 0 || S[ns - 1].tick >= tick || (float)(tick - S[ns - 1].tick) <= 1.5f * gap) continue;
                    add = (NetSample){tick - (uint32_t)gap, S[ns - 1].e};
                }
                int at = ns; while (at > 0 && S[at - 1].tick > add.tick) at--;
                if (at > 0 && S[at - 1].tick == add.tick) { S[at - 1].e = add.e; continue; }
                if (ns == NET_INTERP) { if (at == 0) continue; memmove(S, S + 1, sizeof(NetSample) * (size_t)(ns - 1)); ns--; at--; }
                memmove(S + at + 1, S + at, sizeof(NetSample) * (size_t)(ns - at));
                S[at] = add; ns++;
            }
            c->nsamp[id] = ns;
        }
    }
    if (seq > c->recv_max) {
        uint32_t sh = seq - c->recv_max;
        c->recv_mask = c->recv_max == 0 || sh > 32 ? 0 : sh == 32 ? (1u << 31) : (c->recv_mask << sh) | (1u << (sh - 1));
        c->recv_max = seq;
    } else c->recv_mask |= 1u << (c->recv_max - seq - 1);
    if (seq > c->newest_seq) { c->newest_seq = seq; c->tod = tod; c->cloud = cloud; c->qdepth = qdepth; }
    if (tick > c->newest_tick) c->newest_tick = tick;
    if (has_self && seq > c->self_seq) { c->self = me; c->self_seq = seq; c->self_ack = input_ack; c->self_new = true; }
    if (has_prog && seq > c->prog_seq) { c->prog = prog; c->prog_seq = seq; c->prog_new = true; }
    if (has_nests && seq > c->nest_seq) { memcpy(c->nests, nests, sizeof nests); c->nest_seq = seq; c->nest_new = true; }
    for (int k = 0; k < nhits; k++) {   // new hits only: snapshots repeat recent ones
        const CretHit* h = &hits[k]; bool seen = false;
        uint32_t lo = c->nhit_seen > CRET_HIT_LOG ? c->nhit_seen - CRET_HIT_LOG : 0;
        for (uint32_t q = lo; q < c->nhit_seen && !seen; q++) seen = !memcmp(&c->hit_seen[q % CRET_HIT_LOG], h, sizeof *h);
        if (seen) continue;
        c->hit_seen[c->nhit_seen++ % CRET_HIT_LOG] = *h;
        if (c->nhit_new < CRET_HIT_LOG) c->hit_new[c->nhit_new++] = *h;
    }
    // Input-to-ack latency: inputs this snapshot shows applied for the first time.
    if (input_ack > c->acked && input_ack <= c->seq) {
        for (uint32_t q = c->acked + 1; q <= input_ack; q++) {
            if (c->seq - q >= NET_IN_RING || c->in_time[q % NET_IN_RING] <= 0) continue;
            double ms = (now - c->in_time[q % NET_IN_RING]) * 1000.0;
            c->st.ack_n++; c->st.ack_sum_ms += ms; if (ms > c->st.ack_max_ms) c->st.ack_max_ms = ms;
            int bin = (int)(ms / 5); c->st.ack_hist[bin < 0 ? 0 : bin > 199 ? 199 : bin]++;
        }
        c->acked = input_ack;
    }
    if (seq > c->crit_seq) { memcpy(c->crit, crit, sizeof crit); memcpy(c->crit_slot, crit_slot, sizeof crit_slot); c->ncrit = ncrit; c->crit_seq = seq; c->crit_new = true; }
    if (nprops > 0) netp_client_props(c, props, nprops, tick, seq);
    netp_client_snapshot_arrived(c, tick, now);
    c->st.snapshots++;
    return true;
}

size_t netp_client_input(const NetClient* c, uint8_t* out, size_t cap) {
    Bits b = {out, cap, 0, false};
    uint32_t n = c->seq > c->acked ? c->seq - c->acked : 1;
    if (n > NET_REDUNDANCY) n = NET_REDUNDANCY;
    if (n > c->seq) n = c->seq;
    bits_put(&b, MSG_INPUT, 8); bits_put(&b, c->recv_max, 32); bits_put(&b, c->recv_mask, 32); bits_put(&b, c->seq, 32);
    bits_put(&b, n - 1, 5);
    uint64_t prev = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t raw = netp_pack_input(&c->in[(c->seq - i) % NET_IN_RING]);
        if (i == 0) put_input(&b, raw);
        else if (raw == prev) bits_put(&b, 1, 1);
        else { bits_put(&b, 0, 1); put_input(&b, raw); }
        prev = raw;
    }
    return b.bad ? 0 : bits_bytes(&b);
}

void cret_net_client_tick(CretNet* n, CretSim* s, const CretInput* in) {
    if (!n || !n->welcomed || !n->cl || n->failed) return;
    NetClient* c = n->cl;
    CretInput q = netp_unpack_input(netp_pack_input(in));   // predict exactly what the server will apply
    c->seq++; int h = (int)(c->seq % NET_IN_RING);
    c->in[h] = q; c->in_time[h] = cret_netsim_now();
    uint8_t raw[16 + NET_REDUNDANCY * 6];
    size_t len = netp_client_input(c, raw, sizeof raw);
    send_bits(n->server, CH_STATE, raw, len, false);
    if (n->host) enet_host_flush(n->host);
    cret_netsim_pump(n->netsim);
    if (s) {
        CretAgent* a = &s->agents[c->me];
        if (c->pred_ok && a->alive) cret_sim_move(s, c->me, &q, true);
        c->pred[h] = *a; c->pred_seq[h] = c->seq;
        if (c->seq % 64 == 0) cret_prop_stream_trim(&s->props, s->tick, 300);
    }
}

int cret_net_client_request(CretNet* n, int species) {
    if (!n || !n->server) return -1;
    int seq = ++n->req_seq;
    uint8_t raw[4] = {0}; Bits b = {raw, sizeof raw, 0, false};
    bits_put(&b, MSG_REQUEST, 8); bits_put(&b, (uint32_t)seq, 8); bits_put(&b, (uint32_t)(species >= 0 && species < CRET_SPECIES ? species : 255), 8);
    send_bits(n->server, CH_RELIABLE, raw, bits_bytes(&b), true); enet_host_flush(n->host); cret_netsim_pump(n->netsim);
    return seq;
}
void cret_net_client_profile(CretNet* n, const char* name) {
    if (!n || !n->server || !name) return;
    uint8_t raw[32] = {0}; Bits b = {raw, sizeof raw, 0, false}; int len = (int)strnlen(name, 23);
    bits_put(&b, MSG_PROFILE, 8); bits_put(&b, (uint32_t)len, 5);
    for (int c = 0; c < len; c++) bits_put(&b, (uint8_t)name[c], 8);
    send_bits(n->server, CH_RELIABLE, raw, bits_bytes(&b), true); enet_host_flush(n->host); cret_netsim_pump(n->netsim);
}
void cret_net_client_unlock(CretNet* n, int choice) {
    if (!n || !n->server) return;
    uint8_t raw[2] = {0}; Bits b = {raw, sizeof raw, 0, false};
    bits_put(&b, MSG_UNLOCK, 8); bits_put(&b, (uint32_t)(choice & 1), 1);
    send_bits(n->server, CH_RELIABLE, raw, bits_bytes(&b), true); enet_host_flush(n->host); cret_netsim_pump(n->netsim);
}
bool cret_net_client_spawn_reply(CretNet* n, CretSpawnReply* out) {
    if (!n || !n->reply_ready) return false;
    *out = n->reply; n->reply_ready = false; return true;
}

bool cret_net_client_poll(CretNet* n, CretSim* s) {
    if (!n) return false;
    cret_netsim_pump(n->netsim);
    ENetEvent ev;
    while (!n->failed && enet_host_service(n->host, &ev, 0) > 0) {
        if (ev.type == ENET_EVENT_TYPE_CONNECT) { n->connected = true; no_throttle(ev.peer); }
        else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) { n->failed = true; n->reject = ev.data; n->server = NULL; }
        else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
            netp_client_packet(n, ev.packet->data, ev.packet->dataLength, cret_netsim_now());
            enet_packet_destroy(ev.packet);
        }
    }
    enet_host_flush(n->host); cret_netsim_pump(n->netsim);
    if (s && n->welcomed && n->cl) netp_client_apply(n, s);
    return !n->failed;
}

void cret_net_pump(CretNet* n) { if (n) cret_netsim_pump(n->netsim); }

void cret_net_client_stats(const CretNet* n, CretNetStats* st) {
    memset(st, 0, sizeof *st);
    if (!n || !n->cl) return;
    const NetClient* c = n->cl;
    *st = c->st;
    st->seconds = cret_netsim_now() - c->t0;
    st->up_packets = n->host->totalSentPackets - c->base_up_pk; st->down_packets = n->host->totalReceivedPackets - c->base_down_pk;
    st->up_bytes = n->host->totalSentData - c->base_up + 28 * st->up_packets;
    st->down_bytes = n->host->totalReceivedData - c->base_down + 28 * st->down_packets;
    st->delay_ms = c->delay * 1000.0 / CRET_TICK_HZ;
    st->rtt_ms = n->server ? n->server->roundTripTime : 0;
}
void cret_net_client_stats_reset(CretNet* n) {
    if (!n || !n->cl) return;
    NetClient* c = n->cl;
    memset(&c->st, 0, sizeof c->st); c->t0 = cret_netsim_now();
    c->base_up = n->host->totalSentData; c->base_down = n->host->totalReceivedData;
    c->base_up_pk = n->host->totalSentPackets; c->base_down_pk = n->host->totalReceivedPackets;
}
float cret_net_hist_pct(const uint32_t* hist, int bins, float p) {
    uint64_t tot = 0; for (int i = 0; i < bins; i++) tot += hist[i];
    if (!tot) return 0;
    uint64_t want = (uint64_t)(p * (double)tot), acc = 0;
    for (int i = 0; i < bins; i++) { acc += hist[i]; if (acc > want) return (float)i + 0.5f; }
    return (float)bins;
}

void cret_net_close(CretNet* n) {
    if (!n) return;
    if (n->server && n->host) { enet_peer_disconnect_now(n->server, 0); enet_host_flush(n->host); }
    cret_netsim_pump(n->netsim);
    if (n->host) enet_host_destroy(n->host);
    for (int k = 0; k < CRET_MAX_PEERS; k++) free(n->peers[k]);
    cret_netsim_close(n->netsim);
    free(n->cl);
    free(n);
}

// ---------------------------------------------------------------- fakes for tests
CretNet* netp_fake_server(int first_slot, float world_size) {
    CretNet* n = calloc(1, sizeof *n); if (!n) return NULL;
    n->first_slot = first_slot; netp_schema(&n->sc, world_size);
    return n;
}
NetPeer* netp_fake_peer(CretNet* n, int k, int slot) {
    if (k < 0 || k >= CRET_MAX_PEERS) return NULL;
    free(n->peers[k]); n->peers[k] = peer_new(slot, NULL, 0);
    return n->peers[k];
}
CretNet* netp_fake_client(int num_agents, int slot, int world_n, float world_cell) {
    CretNet* n = calloc(1, sizeof *n); if (!n) return NULL;
    CretConfig cfg = cret_default_config(); cfg.num_agents = num_agents; cfg.world_n = world_n; cfg.world_cell = world_cell;
    n->cl = netp_client_new(&cfg, slot); n->cfg = cfg; n->slot = slot; n->welcomed = n->cl != NULL; n->connected = true;
    return n;
}
