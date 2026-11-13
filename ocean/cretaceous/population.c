// Population director for solo and host play (population.h).
#include "population.h"
#include "eco.h"
#include <float.h>
#include <math.h>
#include <string.h>

static inline uint32_t pop_xs(uint32_t* s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x ? x : 0x9e3779b9u; }
static inline float pop_rand(uint32_t* s) { return (pop_xs(s) >> 8) * (1.0f / 16777216.0f); }
static inline bool herb_of(const CretAgent* a) { return CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_HERBIVORE; }

#define POP_PULL_MIN 130.0f       // pulled groups land this far from the person (and from every person)
#define POP_PULL_MAX 230.0f
#define POP_THREAT_NEAR 600.0f
#define POP_COOLDOWN (90 * CRET_TICK_HZ)   // a moved group stays put this long
#define POP_LOCAL 6               // a person with fewer animals within reach gets respawning bots

void cret_pop_config(CretConfig* c) { if (c->num_agents < CRET_POP_AGENTS) c->num_agents = CRET_POP_AGENTS; }

void cret_pop_init(CretPop* p, CretSim* s, bool on) {
    memset(p, 0, sizeof *p); p->on = on; p->rng = s->cfg.seed * 2246822519u + 0x7f4a7c15u;
    for (int i = 0; i < CRET_MAX_AGENTS; i++) { p->moved[i] = -POP_COOLDOWN; p->was_alive[i] = s->agents[i].alive; }
}

float cret_pop_nearest(const CretSim* s, float x, float z, int skip, int diet, int* who) {
    float bd = FLT_MAX; int best = -1;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (j == skip || !b->alive || (diet >= 0 && CRET_SPECIES_TABLE[b->species].diet != diet)) continue;
        float d = hypotf(b->x - x, b->z - z); if (d < bd) { bd = d; best = j; }
    }
    if (who) *who = best;
    return bd;
}

static float nearest_person(const CretSim* s, float x, float z, int skip) {
    float bd = FLT_MAX;
    for (int j = 0; j < s->cfg.num_agents; j++)
        if (j != skip && s->agents[j].alive && cret_is_person(s, j)) bd = fminf(bd, hypotf(s->agents[j].x - x, s->agents[j].z - z));
    return bd;
}

// Members of i's unit: its group, or i alone.
static int unit_of(const CretSim* s, int i, int* out) {
    const CretAgent* a = &s->agents[i]; int n = 0;
    if (!a->group) { out[n++] = i; return n; }
    for (int j = 0; j < s->cfg.num_agents; j++) if (s->agents[j].alive && s->agents[j].group == a->group) out[n++] = j;
    return n;
}

// A capsule standing at (x, y, z) would overlap a prop or another agent (as sim.c's spawn check).
typedef struct { const CretSim* s; int self; int hits; } PopOv;
static bool pop_ov_fn(b3ShapeId shape, const b3PlaneResult* res, int n, void* c) {
    PopOv* o = c; (void)res;
    if (B3_ID_EQUALS(b3Shape_GetBody(shape), o->s->agents[o->self].body)) return true;
    o->hits += n; return true;
}
static bool blocked(CretSim* s, int i, float x, float z) {
    if (!cret_walkable(s->world, x, z)) return true;
    const CretAgent* a = &s->agents[i]; float y = cret_height(s->world, x, z);
    if (!b3World_IsValid(s->phys)) return false;
    cret_prop_stream_touch(&s->props, s->world, s->phys, x, z, 40.0f, s->tick);
    b3Capsule cap = {{0, a->radius, 0}, {0, fmaxf(a->height - a->radius, a->radius + 0.01f), 0}, a->radius};
    b3QueryFilter f = {.categoryBits = CRET_CAT_AGENT, .maskBits = CRET_CAT_AGENT | CRET_CAT_PROP, .id = 5, .name = "cret_pop"};
    PopOv o = {s, i, 0};
    b3World_CollideMover(s->phys, (b3Pos){x, y + 0.05f, z}, &cap, f, pop_ov_fn, &o);
    return o.hits > 0;
}

static void put(CretSim* s, int j, float x, float z) {
    CretAgent* a = &s->agents[j];
    a->x = x; a->z = z; a->y = cret_height(s->world, x, z); a->vx = a->vy = a->vz = 0; a->speed = 0; a->on_ground = true; a->swimming = false;
    if (!cret_is_person(s, j)) {   // a fresh mind: wander from here (sim.c picks a new goal), no rally, not stuck
        a->bot_state = BOT_WANDER; a->bot_timer = 30; a->goal_x = a->goal_z = 0; a->rally_ticks = 0;
        a->stuck_ticks = a->escape_ticks = 0; a->stuck_x = x; a->stuck_z = z;
    }
    cret_sim_place(s, j);
}

int cret_pop_move_group(CretSim* s, int i, float x, float z, bool whole) {
    int mem[CRET_MAX_AGENTS], n = whole ? unit_of(s, i, mem) : (mem[0] = i, 1), moved = 0;
    const CretAgent* a = &s->agents[i]; uint32_t rng = (uint32_t)(s->tick * 2654435761u) ^ (uint32_t)i;
    for (int k = 0; k < n; k++) {
        int j = mem[k]; const CretAgent* b = &s->agents[j];
        if (cret_is_person(s, j) && j != i) continue;   // never drag a person along
        float ox = b->x - a->x, oz = b->z - a->z, ol = hypotf(ox, oz);
        if (ol > 30) { ox *= 30 / ol; oz *= 30 / ol; }   // keep the group together
        float tx = x + ox, tz = z + oz;
        bool ok = !blocked(s, j, tx, tz);
        for (int t = 0; t < 24 && !ok; t++) {
            float ang = 6.2831853f * pop_rand(&rng), r = 1.5f + t * 1.1f + 2 * pop_rand(&rng);   // the nearest free ground first
            tx = x + ox + cosf(ang) * r; tz = z + oz + sinf(ang) * r; ok = !blocked(s, j, tx, tz);
        }
        if (!ok) continue;
        put(s, j, tx, tz); moved++;
    }
    return moved;
}

// A calm unit (wandering, grazing or resting; nobody latched, hurt, hunting,
// fleeing or a person; not moved lately; not exempt) of the given diet and
// species (-1: any), every member at least CRET_POP_FAR m from every person.
// Prefers larger units, then the nearest to (x, z). Returns a member or -1.
static int far_unit(const CretPop* p, const CretSim* s, int diet, int species, float x, float z) {
    int best = -1; float bs = FLT_MAX; uint8_t seen[CRET_MAX_AGENTS + 1] = {0};
    for (int i = 0; i < s->cfg.num_agents; i++) {
        const CretAgent* a = &s->agents[i];
        if (!a->alive || cret_is_person(s, i) || CRET_SPECIES_TABLE[a->species].diet != diet || (species >= 0 && a->species != species)) continue;
        if (a->group && seen[a->group]) continue;
        if (a->group) seen[a->group] = 1;
        int mem[CRET_MAX_AGENTS], n = unit_of(s, i, mem); bool ok = true; float cx = 0, cz = 0;
        for (int k = 0; k < n && ok; k++) {
            const CretAgent* b = &s->agents[mem[k]];
            ok = !cret_is_person(s, mem[k]) && !p->exempt[mem[k]] && s->tick - p->moved[mem[k]] >= POP_COOLDOWN &&
                 (b->bot_state == BOT_WANDER || b->bot_state == BOT_GRAZE || b->bot_state == BOT_REST) && !b->latch && !b->latchers && b->hurt_ticks <= 0 && !b->leap &&
                 nearest_person(s, b->x, b->z, -1) >= CRET_POP_FAR;
            cx += b->x / n; cz += b->z / n;
        }
        if (!ok) continue;
        float sc = hypotf(cx - x, cz - z) / (n >= 2 ? 3.0f : 1.0f);
        if (sc < bs) { bs = sc; best = i; }
    }
    return best;
}

// A spot rmin..rmax from person `who`, at least rmin from every person, walkable.
static bool spot_near(CretPop* p, CretSim* s, int who, int mover, float rmin, float rmax, float* ox, float* oz) {
    const CretAgent* a = &s->agents[who];
    for (int t = 0; t < 64; t++) {
        float ang = 6.2831853f * pop_rand(&p->rng), d = rmin + (rmax - rmin) * pop_rand(&p->rng);
        float x = a->x + cosf(ang) * d, z = a->z + sinf(ang) * d;
        if (nearest_person(s, x, z, -1) < rmin * 0.95f || blocked(s, mover, x, z)) continue;
        *ox = x; *oz = z; return true;
    }
    return false;
}

static bool pull(CretPop* p, CretSim* s, int who, int diet, int species, float rmin, float rmax) {
    const CretAgent* a = &s->agents[who];
    int u = far_unit(p, s, diet, species, a->x, a->z); if (u < 0) return false;
    float x, z; if (!spot_near(p, s, who, u, rmin, rmax, &x, &z)) return false;
    int mem[CRET_MAX_AGENTS], n = unit_of(s, u, mem);
    if (cret_pop_move_group(s, u, x, z, true) <= 0) return false;
    for (int k = 0; k < n; k++) p->moved[mem[k]] = s->tick;
    p->pulls++;
    return true;
}

// What a person wants within reach: prey for a carnivore; for a herbivore,
// its own kind, else any herbivore.
// Also at least POP_MIN_AROUND animals of any kind within reach, so a lone
// straggler does not count as a populated neighbourhood.
#define POP_MIN_AROUND 3
static bool has_company(const CretSim* s, int who) {
    const CretAgent* a = &s->agents[who]; bool carn = !herb_of(a), want = false; int around = 0, own = 0, herbs = 0;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (j == who || !b->alive || cret_is_person(s, j)) continue;
        own += b->species == a->species;
        if (hypotf(b->x - a->x, b->z - a->z) >= CRET_POP_NEAR) continue;
        around++;
        if (!herb_of(b)) continue;
        herbs++;
        if (carn || b->species == a->species) want = true;
    }
    if (!carn && !own) want = herbs > 0;   // none of its kind left anywhere: any herbivores
    return want && around >= POP_MIN_AROUND;
}
static bool bring_company(CretPop* p, CretSim* s, int who) {
    const CretAgent* a = &s->agents[who];
    if (!herb_of(a)) return pull(p, s, who, CRET_DIET_HERBIVORE, -1, POP_PULL_MIN, POP_PULL_MAX);
    return pull(p, s, who, CRET_DIET_HERBIVORE, a->species, POP_PULL_MIN, POP_PULL_MAX) || pull(p, s, who, CRET_DIET_HERBIVORE, -1, POP_PULL_MIN, POP_PULL_MAX);
}

bool cret_pop_place_person(CretPop* p, CretSim* s, int i) {
    CretAgent* a = &s->agents[i]; if (!a->alive) return false;
    bool carn = !herb_of(a);
    // Herds of its prey (carnivore) or of its own kind (herbivore), then any
    // herbivores, nearest first (a group counts as half as far). A herd with a
    // predator at hand is skipped for the next one.
    int cand[CRET_MAX_AGENTS]; float cd[CRET_MAX_AGENTS]; int nc = 0;
    for (int pass = 0; pass < 2 && nc == 0; pass++)
        for (int j = 0; j < s->cfg.num_agents; j++) {
            const CretAgent* b = &s->agents[j];
            if (j == i || !b->alive || cret_is_person(s, j) || !herb_of(b) || (pass == 0 && !carn && b->species != a->species)) continue;
            float d = hypotf(b->x - a->x, b->z - a->z) / (b->group ? 2.0f : 1.0f);
            int k = nc++; while (k > 0 && cd[k - 1] > d) { cd[k] = cd[k - 1]; cand[k] = cand[k - 1]; k--; }
            cd[k] = d; cand[k] = j;
        }
    float rmin = carn ? 70.0f : 20.0f, rmax = carn ? 120.0f : 45.0f;
    float fx = 0, fz = 0, fd = -1; int fh = -1;   // fallback: the spot farthest from any danger
    for (int c = 0; c < nc && c < 8; c++) {
        const CretAgent* h = &s->agents[cand[c]];
        for (int t = 0; t < 48; t++) {
            float ang = 6.2831853f * pop_rand(&p->rng), d = rmin + (rmax - rmin) * pop_rand(&p->rng);
            float x = h->x + cosf(ang) * d, z = h->z + sinf(ang) * d;
            if (blocked(s, i, x, z)) continue;
            // Never next to a predator that hunts it (herbivores), nor at a tyrannosaur's feet.
            int pr; float dp = cret_pop_nearest(s, x, z, i, CRET_DIET_CARNIVORE, &pr);
            bool danger = pr >= 0 && (carn ? s->agents[pr].species == CRET_SP_TREX && dp < 60 : dp < 90);
            if (danger) {
                float sc = carn && s->agents[pr].species != CRET_SP_TREX ? 1e6f : dp;
                if (sc > fd) { fd = sc; fx = x; fz = z; fh = cand[c]; }
                continue;
            }
            put(s, i, x, z);
            a->yaw = atan2f(h->z - z, h->x - x);   // facing the herd
            p->placed++;
            return true;
        }
    }
    // Predators near every herd (a crowded island): the safest spot found, if it beats where the sim put it.
    int pr; float here = cret_pop_nearest(s, a->x, a->z, i, CRET_DIET_CARNIVORE, &pr);
    if (fh >= 0 && (pr < 0 ? false : fd > here)) {
        put(s, i, fx, fz); a->yaw = atan2f(s->agents[fh].z - fz, s->agents[fh].x - fx); p->placed++;
        return true;
    }
    return false;
}

void cret_pop_player_placed(CretPop* p, CretSim* s, int i) {
    if (i < 0 || i >= CRET_MAX_AGENTS) return;
    p->keep[i] = 1;
    if (p->on && s->agents[i].alive && !has_company(s, i)) { p->lonely[i] = 0; bring_company(p, s, i); }
}

// A bot that respawned far from everyone joins its kind near a person with few animals around.
static void rehome(CretPop* p, CretSim* s, int j) {
    CretAgent* a = &s->agents[j];
    if (p->exempt[j] || nearest_person(s, a->x, a->z, -1) < CRET_POP_FAR) return;
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    for (int who = 0; who < s->cfg.num_agents; who++) {
        const CretAgent* pa = &s->agents[who];
        if (!pa->alive || !cret_is_person(s, who)) continue;
        int local = 0, mate = -1; float md = FLT_MAX;
        for (int k = 0; k < s->cfg.num_agents; k++) {
            const CretAgent* b = &s->agents[k];
            if (k == who || k == j || !b->alive || cret_is_person(s, k)) continue;
            float d = hypotf(b->x - pa->x, b->z - pa->z); if (d >= CRET_POP_NEAR + 100) continue;
            local += d < CRET_POP_NEAR;
            if (b->species == a->species && (!b->group || cret_group_size(s, b->group) < S->group_max) && d < md) { md = d; mate = k; }
        }
        if (local >= POP_LOCAL || mate < 0) continue;
        const CretAgent* m = &s->agents[mate];
        for (int t = 0; t < 32; t++) {
            float ang = 6.2831853f * pop_rand(&p->rng), r = 12 + 28 * pop_rand(&p->rng);
            float x = m->x + cosf(ang) * r, z = m->z + sinf(ang) * r;
            if (nearest_person(s, x, z, -1) < POP_PULL_MIN || blocked(s, j, x, z)) continue;
            put(s, j, x, z);
            if (S->group_max > 1) a->group = m->group ? m->group : (uint8_t)(mate + 1);
            if (!m->group && S->group_max > 1) s->agents[mate].group = a->group;
            p->moved[j] = s->tick; p->joins++;
            return;
        }
    }
}

void cret_pop_step(CretPop* p, CretSim* s) {
    if (!p->on) return;
    int N = s->cfg.num_agents;
    for (int i = 0; i < N; i++) {
        CretAgent* a = &s->agents[i];
        bool spawned = a->alive && (!p->was_alive[i] || a->ticks_alive == 1);
        p->was_alive[i] = a->alive;
        if (!spawned) continue;
        if (cret_is_person(s, i)) {
            if (p->keep[i]) p->keep[i] = 0;          // a restored position: stays, company comes to it
            else if (!p->exempt[i]) cret_pop_place_person(p, s, i);
            p->lonely[i] = CRET_POP_LONELY_S * CRET_TICK_HZ;   // checked at once
        } else if (a->ticks_alive <= 1) rehome(p, s, i);
    }
    if (s->tick % CRET_TICK_HZ) return;   // the rest once a second
    for (int i = 0; i < N; i++) {
        const CretAgent* a = &s->agents[i];
        if (!a->alive || !cret_is_person(s, i)) { p->lonely[i] = p->calm[i] = 0; continue; }
        if (has_company(s, i)) p->lonely[i] = 0;
        else if ((p->lonely[i] += CRET_TICK_HZ) >= CRET_POP_LONELY_S * CRET_TICK_HZ) { bring_company(p, s, i); p->lonely[i] = 0; }
        if (herb_of(a)) {   // herbivores: now and then a predator prowls nearby
            if (cret_pop_nearest(s, a->x, a->z, i, CRET_DIET_CARNIVORE, NULL) < POP_THREAT_NEAR) p->calm[i] = 0;
            else if ((p->calm[i] += CRET_TICK_HZ) >= CRET_POP_THREAT_S * CRET_TICK_HZ) { pull(p, s, i, CRET_DIET_CARNIVORE, -1, 300, 420); p->calm[i] = 0; }
        }
    }
}
