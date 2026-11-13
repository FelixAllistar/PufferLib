// Client side of protocol v9 (v11 adds movable props: netprops.c): prediction of the local animal with
// server reconciliation, and remote agents drawn from a snapshot buffer.
//
// Prediction: every client tick the input is sent (with the ones the server
// has not applied yet) and the local animal moves through cret_sim_move,
// the server's own movement code. Snapshots carry the animal's full state
// after the last input the server applied; if the prediction made then
// differs, the client rewinds to the server's state and replays the newer
// inputs. The visible animal keeps its place and the difference decays over
// about 100 ms; only a jump of more than SNAP_M (a respawn, a teleport) snaps.
//
// Remote agents: each has a buffer of samples by server tick. They are drawn
// at the estimated server tick minus an adaptive delay (about 100 ms: one
// snapshot interval plus margin for jitter and recent underruns),
// interpolated between the samples around it. Past the newest sample they
// are extrapolated for at most EXTRAP_TICKS, then held. Agents the server
// updates less often (far away) are drawn further back by their own gap.
#include "net_priv.h"
#include "progress.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SNAP_M 4.0f          // corrections bigger than this snap
#define SMOOTH_S 0.1f        // correction offset time constant
#define EXTRAP_TICKS 6.0f    // 200 ms
#define DELAY_MIN 3.0        // ticks (100 ms)
#define DELAY_MAX 15.0       // ticks (500 ms)

// A render tick as a server tick number (wrapping like the server's).
static uint32_t tick32(double R) { return R > -1e15 && R < 1e15 ? (uint32_t)(int64_t)floor(R) : 0; }
static float wrap_pi(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }

NetClient* netp_client_new(const CretConfig* cfg, int slot) {
    NetClient* c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->num_agents = cfg->num_agents; c->me = slot;
    netp_schema(&c->sc, cfg->world_n * cfg->world_cell);
    c->delay = DELAY_MIN; c->t0 = cret_netsim_now();
    for (int i = 0; i < CRET_MAX_AGENTS; i++) c->gap[i] = CRET_SNAPSHOT_DIV;
    return c;
}

// Server clock: each snapshot says which tick it is; the offset follows the
// earliest arrivals (fast when packets come early, slowly when they come
// late), and the jitter is the mean lateness relative to it.
void netp_client_snapshot_arrived(NetClient* c, uint32_t tick, double now) {
    double o = (double)tick - now * CRET_TICK_HZ;
    if (!c->clock_ok) { c->clock_off = o; c->clock_ok = true; return; }
    double d = o - c->clock_off;
    if (d > 20 || d < -60) { c->clock_off = o; return; }   // a stall or a server restart: start over
    c->clock_off += d > 0 ? d * 0.25 : d * 0.01;
    c->jitter += (fabs(d) - c->jitter) * 0.05;
}

static double server_tick_now(const NetClient* c, double now) { return now * CRET_TICK_HZ + c->clock_off; }

// Everything about the local animal except its movement (which is predicted).
static void self_stats(NetClient* c, CretAgent* a) {
    CretAgent t = *a;
    netp_ent_to_agent(&c->latest[c->me], &t, c->newest_tick);
    a->species = t.species; a->group = t.group; a->rally_ticks = t.rally_ticks; a->rally_x = t.rally_x; a->rally_z = t.rally_z;
    a->dead_ticks = t.dead_ticks; a->bleed = t.bleed; a->mark_by = t.mark_by;
    a->latchers = c->self.latchers; a->nest_ticks = c->self.nest_ticks;
    a->food = c->self.food / 255.0f; a->water = c->self.water / 255.0f; a->fat = c->self.fat / 255.0f;
    float frac = t.max_health > 0 ? t.health / t.max_health : 0;
    a->growth = c->self.growth; a->max_health = 0; cret_apply_growth(a); a->health = frac * a->max_health;
}

static void set_from_self(CretAgent* a, const NetSelf* S) {
    a->alive = S->alive; a->on_ground = S->on_ground; a->swimming = S->swimming; a->sprinting = S->sprinting;
    a->act = S->act; a->act_ticks = S->act_ticks; a->attack_cd = S->attack_cd; a->hurt_ticks = S->hurt_ticks;
    a->x = S->x; a->y = S->y; a->z = S->z; a->yaw = S->yaw; a->vx = S->vx; a->vy = S->vy; a->vz = S->vz;
    a->speed = S->speed; a->stamina = S->stamina; a->broken = S->broken;
    a->strike = S->strike; a->latch = S->latch; a->latchers = S->latchers; a->leap = S->leap; a->nest_ticks = S->nest_ticks;
    a->slow_amt = S->slow_amt; a->def_speed = S->def_speed; a->leap_t = S->leap_t;
    memcpy(a->st, S->st, sizeof a->st); memcpy(a->ab_cd, S->ab_cd, sizeof a->ab_cd);
    a->attack2_cd = S->attack2_cd; a->buf_act = S->buf_act; a->buf_ticks = S->buf_ticks; a->aim = S->aim;
    a->carry = S->carry; a->carry_kg = S->carry_kg;
}
// Statuses and cooldowns agree (prediction runs their timers down; casting and hits are the server's).
static bool same_status(const CretAgent* P, const NetSelf* S) {
    if (P->carry != S->carry || fabsf(P->carry_kg - S->carry_kg) > 0.01f) return false;   // picking up and putting down are the server's
    if (P->strike != S->strike || P->latch != S->latch || P->leap != S->leap || fabsf(P->slow_amt - S->slow_amt) > 0.01f || fabsf(P->def_speed - S->def_speed) > 0.01f) return false;
    for (int k = 0; k < CRET_ST_COUNT; k++) if ((P->st[k] > 0) != (S->st[k] > 0) || fabsf(P->st[k] - S->st[k]) > 0.1f) return false;
    for (int k = 0; k < CRET_AB_SLOTS; k++) if (fabsf(P->ab_cd[k] - S->ab_cd[k]) > 0.1f) return false;
    return true;
}

static void reconcile(NetClient* c, CretSim* s) {
    CretAgent* a = &s->agents[c->me]; const NetSelf* S = &c->self;
    bool was = c->pred_ok && a->alive;
    float ox = a->x, oy = a->y, oz = a->z, oyaw = a->yaw;
    self_stats(c, a);
    if (!S->alive) {
        set_from_self(a, S); cret_sim_place(s, c->me);
        c->pred_ok = false; c->off[0] = c->off[1] = c->off[2] = c->off_yaw = 0;
        return;
    }
    uint32_t A = c->self_ack;
    bool have = was && A > 0 && A <= c->seq && c->seq - A < NET_IN_RING && c->pred_seq[A % NET_IN_RING] == A && c->pred[A % NET_IN_RING].alive;
    if (have) {
        const CretAgent* P = &c->pred[A % NET_IN_RING];
        float dx = P->x - S->x, dy = P->y - S->y, dz = P->z - S->z, err = sqrtf(dx * dx + dy * dy + dz * dz);
        c->st.err_n++; c->st.err_sum_m += err; if (err > c->st.err_max_m) c->st.err_max_m = err;
        int bin = (int)(err * 1000); c->st.err_hist[bin > 199 ? 199 : bin]++;
        bool same = err < 0.002f && fabsf(wrap_pi(P->yaw - S->yaw)) < 0.001f && fabsf(P->vx - S->vx) + fabsf(P->vy - S->vy) + fabsf(P->vz - S->vz) < 0.01f &&
                    P->on_ground == S->on_ground && P->swimming == S->swimming && P->act == S->act && fabsf(P->stamina - S->stamina) < 0.01f &&
                    P->attack_cd == S->attack_cd && P->attack2_cd == S->attack2_cd && P->buf_act == S->buf_act && P->buf_ticks == S->buf_ticks &&
                    (P->broken > 0) == (S->broken > 0) && same_status(P, S);
        if (same) return;
    }
    // Rewind to the server's state after input A and replay the inputs since.
    set_from_self(a, S); cret_sim_place(s, c->me);
    if (A < c->seq) {
        uint32_t from = c->seq - A >= NET_IN_RING ? c->seq - NET_IN_RING + 1 : A + 1;
        for (uint32_t q = from; q <= c->seq; q++) {
            int h = (int)(q % NET_IN_RING);
            if (c->pred_seq[h] != q) continue;
            if (a->alive) cret_sim_move(s, c->me, &c->in[h], true);
            c->pred[h] = *a;
        }
    }
    c->pred_ok = true;
    float sx = ox - a->x, sy = oy - a->y, sz = oz - a->z, shift = sqrtf(sx * sx + sy * sy + sz * sz);
    if (!was || shift > SNAP_M) {
        c->off[0] = c->off[1] = c->off[2] = c->off_yaw = 0;
        if (was) c->st.snaps++;
        return;
    }
    c->off[0] += sx; c->off[1] += sy; c->off[2] += sz; c->off_yaw = wrap_pi(c->off_yaw + wrap_pi(oyaw - a->yaw));
    c->st.corrections++; c->st.corr_sum_m += shift; if (shift > c->st.corr_max_m) c->st.corr_max_m = shift;
}

void netp_client_apply(CretNet* n, CretSim* s) {
    NetClient* c = n->cl;
    if (s->cfg.num_agents != c->num_agents) return;   // not this server's sim
    s->tick = (int)c->newest_tick; s->time_of_day = c->tod; s->cloud_cover = c->cloud;
    for (int i = 0; i < c->num_agents; i++) {
        if (i == c->me) continue;
        if (c->applied_seq[i] == c->latest_seq[i]) {   // unchanged: only the timers move
            CretAgent* a = &s->agents[i]; CretAgent t = *a;
            netp_ent_to_agent(&c->latest[i], &t, c->newest_tick); a->hurt_ticks = t.hurt_ticks; a->dead_ticks = t.dead_ticks;
            continue;
        }
        c->applied_seq[i] = c->latest_seq[i];
        netp_ent_to_agent(&c->latest[i], &s->agents[i], c->newest_tick);
        cret_sim_place(s, i);
    }
    for (int k = 0; k < CRET_MAX_CARCASSES; k++) netp_ent_to_carcass(&c->latest[NET_CARCASS0 + k], &s->carcasses[k], c->newest_tick);
    if (c->prog_new) {   // own progress (the name stays the client's)
        c->prog_new = false; CretProgress* p = &s->prog[c->me]; const NetProg* P = &c->prog;
        p->marks = P->marks; p->unlocked = P->unlocked; p->quests_done = P->quests_done; memcpy(p->loadout, P->loadout, sizeof p->loadout);
        p->quest = P->quest; p->chain = P->chain; p->visited = P->visited;
        int g = s->agents[c->me].group;
        if (g > 0 && g <= CRET_MAX_AGENTS) { if (P->has_group) s->gquest[g] = P->group; else memset(&s->gquest[g], 0, sizeof s->gquest[g]); }
        // Places index the client's own world (the same one): drop a quest naming a place it lacks.
        if (s->world && !s->world->pois_ok) cret_pois_build(s->world);
        int npoi = s->world ? s->world->npois : 0;
        for (int k = 0; k < 3; k++) if (p->quest.poi[k] >= npoi && !p->quest.spot) p->quest.kind = CRET_Q_NONE;   // a spot quest names no place
        if (g > 0 && g <= CRET_MAX_AGENTS) for (int k = 0; k < 3; k++) if (s->gquest[g].poi[k] >= npoi && !s->gquest[g].spot) s->gquest[g].kind = CRET_Q_NONE;
    }
    if (c->nest_new) { c->nest_new = false; memcpy(s->nests, c->nests, sizeof s->nests); }
    for (int k = 0; k < c->nhit_new; k++) s->hits[s->hit_count++ % CRET_HIT_LOG] = c->hit_new[k];   // v10: hit feedback reads the log
    netp_client_apply_props(c, s);   // v11
    if (c->crit_new) { c->crit_new = false; netp_client_apply_critters(c, s); }   // v12
    cret_scent_record(s, true);   // trails for the scent view, from what the snapshots show (scent.h)
    c->nhit_new = 0;
    if (c->self_new) { c->self_new = false; reconcile(c, s); }
    else if (c->latest_seq[c->me] != c->applied_seq[c->me]) self_stats(c, &s->agents[c->me]);
    c->applied_seq[c->me] = c->latest_seq[c->me];
}

// One remote agent at render tick R: interpolated between the samples
// around R, extrapolated a little past the newest, held after that.
static bool draw_agent(NetClient* c, int i, double R, CretAgent* out) {
    int ns = c->nsamp[i]; const NetSample* S = c->samp[i];
    if (ns == 0) return false;
    int k = ns - 1; while (k >= 0 && (double)S[k].tick > R) k--;
    CretAgent a0 = *out, a1;
    if (k < 0) { netp_ent_to_agent(&S[0].e, &a0, (uint32_t)S[0].tick); *out = a0; return false; }
    netp_ent_to_agent(&S[k].e, &a0, tick32(R));
    if (k == ns - 1) {   // past the newest sample
        bool ex = false;
        if (R > (double)S[k].tick + 0.01 && k > 0 && a0.alive) {
            a1 = a0; netp_ent_to_agent(&S[k - 1].e, &a1, tick32(R));
            double gt = (double)(S[k].tick - S[k - 1].tick), dt = fmin(R - (double)S[k].tick, EXTRAP_TICKS);
            if (a1.alive && gt > 0 && gt <= 12) {
                // Direction from the last two samples, speed from the newest
                // (an animal that stopped is not carried on).
                float vx = (a0.x - a1.x) / (float)gt, vy = (a0.y - a1.y) / (float)gt, vz = (a0.z - a1.z) / (float)gt;
                float v = sqrtf(vx * vx + vz * vz), want = a0.speed / CRET_TICK_HZ;
                if (v > 1e-4f && v < 1.0f) {   // under 30 m/s: not a teleport
                    float k = fminf(want / v, 1.5f); vx *= k; vy *= k; vz *= k;
                    ex = want > 0;                // a moving agent ran out of samples
                    a0.x += vx * (float)dt; a0.y += vy * (float)dt; a0.z += vz * (float)dt;
                    a0.yaw = wrap_pi(a0.yaw + wrap_pi(a0.yaw - a1.yaw) / (float)gt * (float)fmin(dt, 2.0));
                }
            }
        }
        *out = a0; return ex;
    }
    a1 = a0; netp_ent_to_agent(&S[k + 1].e, &a1, tick32(R));
    double u = (R - (double)S[k].tick) / (double)(S[k + 1].tick - S[k].tick);
    float dx = a1.x - a0.x, dz = a1.z - a0.z;
    if (a0.alive && a1.alive && a0.species == a1.species && dx * dx + dz * dz < 400.0f) {
        float t = (float)u;
        a0.x += dx * t; a0.y += (a1.y - a0.y) * t; a0.z += dz * t;
        a0.yaw = wrap_pi(a0.yaw + wrap_pi(a1.yaw - a0.yaw) * t); a0.speed += (a1.speed - a0.speed) * t;
    }
    *out = a0; return false;
}

void cret_net_client_draw(CretNet* n, const CretSim* s, CretSim* out, float alpha, float dt) {
    if (out != s) memcpy(out, s, sizeof *s);
    if (!n || !n->cl || !n->welcomed || s->cfg.num_agents != n->cl->num_agents) return;
    NetClient* c = n->cl;
    double now = cret_netsim_now();
    // Adaptive delay: one snapshot interval, a tick of margin, the jitter,
    // and a boost after underruns that wears off over tens of seconds.
    double target = CRET_SNAPSHOT_DIV + 1.0 + 2.5 * c->jitter + c->boost;
    target = target < DELAY_MIN ? DELAY_MIN : target > DELAY_MAX ? DELAY_MAX : target;
    double rate = (target > c->delay ? 0.1 : 0.05) * CRET_TICK_HZ * dt;   // change at most 10% (5%) of real time
    c->delay += fmax(-rate, fmin(rate, target - c->delay));
    c->boost = fmax(0.0, c->boost - 0.05 * dt);
    double R = server_tick_now(c, now) - c->delay;
    if (c->clock_ok) {
        bool under = R > (double)c->newest_tick;
        c->st.frames++; c->st.underruns += under;
        if (under && !c->in_underrun) c->boost = fmin(c->boost + 0.5, 6.0);
        c->in_underrun = under;
    }
    for (int i = 0; i < c->num_agents; i++) {
        if (i == c->me) continue;
        float want = fmaxf(0.0f, c->gap[i] - (float)CRET_SNAPSHOT_DIV);   // updated less often (far away): draw further back
        c->extra[i] += (want - c->extra[i]) * fminf(1.0f, dt * 1.5f);
        CretAgent* a = &out->agents[i];
        bool ex = draw_agent(c, i, R - c->extra[i], a);
        if (c->nsamp[i] && a->alive) { c->st.ent_frames++; c->st.ent_extrapolated += ex; }
    }
    netp_client_draw_props(c, out, R);   // v11 movable props, at the render tick
    for (int k = 0; k < CRET_MAX_CARCASSES; k++) {   // a carcass appears when its death is drawn
        CretCarcass* q = &out->carcasses[k]; if (!q->active) continue;
        int born = (int)c->latest[NET_CARCASS0 + k].f[CF_BORN];
        int ahead = (int16_t)(uint16_t)((uint32_t)born - tick32(R));
        if (ahead > 0) q->active = false; else q->age = (int)((tick32(R) - (uint32_t)born) & 0xffff);
    }
    // Local animal: between its last two predicted ticks, plus the decaying correction.
    CretAgent* a = &out->agents[c->me];
    float k = expf(-dt / SMOOTH_S);
    c->off[0] *= k; c->off[1] *= k; c->off[2] *= k; c->off_yaw *= k;
    if (c->pred_ok && a->alive) {
        uint32_t p = c->seq - 1; const CretAgent* P = &c->pred[p % NET_IN_RING];
        if (c->seq > 1 && c->pred_seq[p % NET_IN_RING] == p && P->alive) {
            float dx = a->x - P->x, dz = a->z - P->z;
            if (dx * dx + dz * dz < 100.0f) {
                float t = alpha < 0 ? 0 : alpha > 1 ? 1 : alpha, u = 1 - t;
                a->x = P->x + (a->x - P->x) * t; a->y = P->y + (a->y - P->y) * t; a->z = P->z + (a->z - P->z) * t;
                a->yaw = wrap_pi(a->yaw - wrap_pi(a->yaw - P->yaw) * u);
            }
        }
        a->x += c->off[0]; a->y += c->off[1]; a->z += c->off[2]; a->yaw = wrap_pi(a->yaw + c->off_yaw);
    }
    cret_carry_follow(out);   // carried food at the drawn jaws, not where the last snapshot left it
}
