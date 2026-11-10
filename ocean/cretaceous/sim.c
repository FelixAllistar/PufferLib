#include "sim.h"
#include "abilities.h"
#include "progress.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

#define CAT_AGENT CRET_CAT_AGENT
#define CAT_PROP  CRET_CAT_PROP

static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline uint32_t xs32(uint32_t* s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x ? x : 0x9e3779b9u; }
static inline float frand(uint32_t* s) { return (xs32(s) >> 8) * (1.0f / 16777216.0f); }
static inline float wrapa(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }

CretConfig cret_default_config(void) {
    return (CretConfig){.world_seed = 1, .world_n = 2048, .world_cell = 4.0f, .num_agents = 16,
                        .max_ticks = 30 * 60 * 10, .day_seconds = 1200.0f, .seed = 1, .player_species = CRET_SP_RAPTOR,
                        .respawn_ticks = 30 * 10, .growth_seconds = 1200.0f, .spawn_growth = -1.0f, .player_growth = 0.35f,
                        .hunger_seconds = 900.0f, .thirst_seconds = 600.0f, .carcass_seconds = 300.0f,
                        .weather = 1, .cloud_cover = 0.45f};
}

// ---------------------------------------------------------------- growth and food
float cret_growth_scale(float growth) { return clampf(growth, 0.2f, 1.0f); }

// Take-off speed for a hop of `height` metres under the airborne gravity. The
// sim integrates y += vy * dt from the take-off tick on, which adds about
// g * dt / 2 to the continuous apex; the speed takes that back off.
float cret_jump_speed(float height) {
    if (height <= 0) return 0;
    float g = CRET_GRAVITY * CRET_AIR_GRAVITY;
    return fmaxf(sqrtf(2 * g * height) - 0.5f * g * CRET_DT, 0.1f);
}

// Size, speed, turning, health and jump of the current growth stage. Body
// length scales with growth, mass with its cube; juveniles are relatively
// quicker and nimbler. Health keeps its fraction.
void cret_apply_growth(CretAgent* a) {
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    float frac = a->max_health > 0 ? a->health / a->max_health : 1.0f;
    float sp = 0.72f + 0.28f * g;
    a->radius = S->radius * g; a->height = S->height * g;
    a->walk_speed = S->walk * sp; a->trot_speed = S->trot * sp; a->sprint_speed = S->sprint * sp; a->swim_speed = S->swim * sp;
    a->turn_rate = S->turn_rate * (1.4f - 0.4f * g); a->jump_speed = cret_jump_speed(S->jump * (0.7f + 0.3f * g));
    a->max_health = S->health * (0.12f + 0.88f * g * g); a->health = frac * a->max_health;
}
static void update_shape(CretAgent* a) {
    if (!b3Shape_IsValid(a->shape)) return;
    float half = fmaxf(a->height * 0.5f - a->radius, 0.05f);
    b3Capsule cap = {{0, -half, 0}, {0, half, 0}, a->radius};
    b3Shape_SetCapsule(a->shape, &cap); a->shape_growth = a->growth;
}

// Low plants a herbivore can graze: ferns, cycads, horsetails and scrub, and
// some tree-fern and broadleaf browse.
float cret_forage(const CretWorld* w, float x, float z) {
    const TerraMap* m = &w->map; int n = m->n;
    int gx = (int)(x / m->cell + 0.5f), gz = (int)(z / m->cell + 0.5f);
    if (gx < 0 || gz < 0 || gx >= n || gz >= n) return 0;
    const uint8_t* v = m->veg + ((size_t)gz * n + gx) * TERRA_VEG_COUNT;
    float f = v[TERRA_VEG_FERN] + v[TERRA_VEG_CYCAD] + v[TERRA_VEG_HORSETAIL] + v[TERRA_VEG_SHRUB] + 0.5f * v[TERRA_VEG_TREEFERN] + 0.3f * v[TERRA_VEG_BROADLEAF];
    return clampf(f / 200.0f, 0, 1);
}

// Body sizes follow the converted models (tools/dino_convert.py --length).
// Gait speeds are game speeds: the walk and trot sit near the clips' own
// stride speeds. Sprints sit at the high end of the biomechanical estimates
// (about 30 km/h for a 3 m dromaeosaur, 23 km/h for an adult tyrannosaur)
// and keep the old predator-to-prey ratios; they were 18-22% faster, which
// read as too fast behind a close third-person camera. Only the raptor hops
// (0.75 m, about 0.6 s in the air). Mass (accel, decel, grip): a raptor
// reaches its sprint in about 1.5 s and stops in 0.7 s; a tyrannosaur or a
// stegosaur takes about 3 s and turns on a circle of about its own length at
// a run.
const CretSpecies CRET_SPECIES_TABLE[CRET_SPECIES] = {
    // Attack timings: the raptor's bite lands 0.5 s in, the stegosaur's tail
    // 0.55 s, the tyrannosaur's bite 0.85 s, the pachycephalosaur's headbutt
    // 0.7 s (dino.c plays the clips faster to match; their own hit frames are
    // 1.0, 0.8, 1.6 and 1.3 s, which felt sluggish). Sauropelta and
    // Gallimimus have no attack clip; their shoulder bash and kick are
    // procedural (dino.c).
    // Defences: Sauropelta's armour blocks half of every hit, its spikes
    // cut whatever bites it and it barely bleeds; a headbutt throws.
    // Aiming: the neck arc runs from +-70 degrees (raptor) to +-35 (the
    // stegosaur's tail). Swing speed: what is kept of the gait until the blow
    // lands (attacks never root). Secondary attacks (right mouse): tail swipes
    // for the tyrannosaur, pachycephalosaur and sauropelta, a wide tail sweep
    // for the stegosaur (whose primary is an aimed thrust behind), and a
    // pass-through slash (raptor) or kick (gallimimus) at full speed.
    //            name           asset     diet                 len   h     r     kg     walk trot sprint swim turn hop  drain regen  hp     dmg   reach cd    hit   len  tail knock armor bleed thorns group
    //            then: neck arc, swing speed, {secondary: kind, dmg, reach, arc, cd, hit, len, knock, speed}, accel, decel, grip
    [CRET_SP_RAPTOR] = {"Velociraptor", "raptor", CRET_DIET_CARNIVORE, 3.0f, 1.2f, 0.45f, 80.0f, 2.2f, 4.6f, 8.6f, 1.8f, 3.6f, 0.75f, 0.065f, 0.07f, 120.0f, 26.0f, 1.3f, 1.4f, 0.5f, 0.85f, 0, 1.0f, 0.0f, 0.0f, 0.0f, 4,
        1.22f, 0.92f, {CRET_ALT_SLASH, 11.0f, 1.0f, 1.2f, 0.8f, 0.17f, 0.45f, 0.6f, 1.0f}, 9.0f, 12.0f, 10.0f},
    [CRET_SP_STEGO]  = {"Stegosaurus", "stego", CRET_DIET_HERBIVORE, 8.5f, 3.6f, 1.35f, 5000.0f, 2.2f, 3.4f, 4.6f, 1.2f, 1.1f, 0.0f, 0.07f, 0.05f, 900.0f, 60.0f, 2.6f, 2.0f, 0.55f, 1.1f, 1, 1.0f, 0.1f, 0.0f, 0.0f, 8,
        0.61f, 0.7f, {CRET_ALT_TAIL, 38.0f, 7.0f, 1.9f, 3.2f, 0.5f, 1.1f, 1.8f, 0.7f}, 2.0f, 3.0f, 3.0f},
    [CRET_SP_TREX]   = {"Tyrannosaurus", "trex", CRET_DIET_CARNIVORE, 12.0f, 4.6f, 1.5f, 8000.0f, 1.5f, 3.2f, 6.4f, 1.4f, 1.2f, 0.0f, 0.08f, 0.04f, 1800.0f, 260.0f, 2.4f, 2.6f, 0.85f, 1.4f, 0, 1.0f, 0.1f, 0.0f, 0.0f, 0,
        0.87f, 0.75f, {CRET_ALT_TAIL, 110.0f, 8.5f, 1.75f, 4.0f, 0.5f, 1.2f, 2.2f, 0.7f}, 2.5f, 3.5f, 3.5f},
    [CRET_SP_PACHY]  = {"Pachycephalosaurus", "pachy", CRET_DIET_HERBIVORE, 4.5f, 2.0f, 0.55f, 450.0f, 1.7f, 3.7f, 7.0f, 1.3f, 2.6f, 0.0f, 0.11f, 0.06f, 300.0f, 30.0f, 1.0f, 2.6f, 0.7f, 1.2f, 0, 2.2f, 0.15f, 0.0f, 0.0f, 6,
        0.79f, 0.85f, {CRET_ALT_TAIL, 16.0f, 3.6f, 1.7f, 2.8f, 0.35f, 0.8f, 1.6f, 0.85f}, 6.0f, 8.0f, 7.0f},
    [CRET_SP_SAUROPELTA] = {"Sauropelta", "sauropelta", CRET_DIET_HERBIVORE, 5.2f, 1.5f, 0.85f, 1500.0f, 1.3f, 2.2f, 3.1f, 0.9f, 1.3f, 0.0f, 0.06f, 0.05f, 650.0f, 30.0f, 1.4f, 1.6f, 0.4f, 0.9f, 0, 1.5f, 0.55f, 0.75f, 0.25f, 4,
        0.70f, 0.75f, {CRET_ALT_TAIL, 20.0f, 4.2f, 1.7f, 3.0f, 0.4f, 0.9f, 1.6f, 0.75f}, 2.0f, 3.0f, 3.0f},
    [CRET_SP_GALLI]  = {"Gallimimus", "galli", CRET_DIET_HERBIVORE, 6.0f, 2.2f, 0.45f, 400.0f, 2.2f, 5.0f, 9.0f, 1.6f, 3.0f, 0.0f, 0.13f, 0.07f, 160.0f, 12.0f, 1.0f, 1.2f, 0.3f, 0.6f, 0, 1.0f, 0.0f, 0.0f, 0.0f, 8,
        1.05f, 0.95f, {CRET_ALT_SLASH, 9.0f, 1.1f, 1.3f, 0.9f, 0.2f, 0.45f, 0.8f, 1.0f}, 8.0f, 10.0f, 9.0f},
};

// Player in slot 0; the rest a herd-heavy mix with a few predators (with 16
// agents: 3 stegosaurs, 3 pachycephalosaurs, 2 sauropeltas, 2 gallimimus,
// 4 raptors and a tyrannosaur).
int cret_slot_species(const CretConfig* cfg, int slot) {
    if (slot == 0) return cfg->player_species >= 0 && cfg->player_species < CRET_SPECIES ? cfg->player_species : CRET_SP_RAPTOR;
    static const int MIX[15] = {CRET_SP_STEGO, CRET_SP_PACHY, CRET_SP_RAPTOR, CRET_SP_GALLI, CRET_SP_STEGO, CRET_SP_SAUROPELTA, CRET_SP_RAPTOR,
                                CRET_SP_PACHY, CRET_SP_TREX, CRET_SP_GALLI, CRET_SP_RAPTOR, CRET_SP_STEGO, CRET_SP_PACHY, CRET_SP_SAUROPELTA, CRET_SP_RAPTOR};
    return MIX[(slot - 1) % 15];
}
// A slot's species: the one chosen for it (menu, network), else the mix.
static int slot_species(const CretSim* s, int i) {
    int p = s->species_pick[i]; return p > 0 && p <= CRET_SPECIES ? p - 1 : cret_slot_species(&s->cfg, i);
}
int cret_group_size(const CretSim* s, int group) {
    if (!group) return 0;
    int n = 0; for (int j = 0; j < s->cfg.num_agents; j++) n += s->agents[j].alive && s->agents[j].group == group;
    return n;
}

// ---------------------------------------------------------------- mover
typedef struct MoverPush { b3BodyId body; b3Vec3 normal, point; } MoverPush;   // a movable prop touched (dynprops.c)
typedef struct MoverCtx { CretSim* s; int self; b3CollisionPlane planes[16]; int count; MoverPush push[4]; int npush; } MoverCtx;
static void kill(CretSim* s, int i);
static void break_bone(CretAgent* b, float seconds);
#include "dynprops.c"
#include "critters.c"   // small prey around people (critters.h)
// Movable props block like any prop (and are pushed, dyn_push) unless light
// enough to kick aside; they never clip the animal's velocity, so it keeps
// leaning in while the prop gives way.
static bool mover_filter(b3ShapeId shape, void* c) {
    MoverCtx* m = c; b3BodyId body = b3Shape_GetBody(shape);
    if (B3_ID_EQUALS(body, m->s->agents[m->self].body)) return false;
    return !(b3Shape_GetFilter(shape).categoryBits & CRET_CAT_DYN) || !dyn_kickable(m->s, m->self, body);
}
static bool mover_planes(b3ShapeId shape, const b3PlaneResult* res, int n, void* c) {
    MoverCtx* m = c; b3BodyId body = b3Shape_GetBody(shape);
    if (B3_ID_EQUALS(body, m->s->agents[m->self].body)) return true;
    bool dyn = b3Shape_GetFilter(shape).categoryBits & CRET_CAT_DYN;
    if (dyn && n > 0) { dyn_note_contact(m, shape, body, &res[0]); if (dyn_kickable(m->s, m->self, body)) return true; }
    for (int i = 0; i < n && m->count < 16; i++)
        m->planes[m->count++] = (b3CollisionPlane){.plane = res[i].plane, .pushLimit = FLT_MAX, .push = 0, .clipVelocity = !dyn};
    return true;
}

static void spawn_point(CretSim* s, float* x, float* z) {
    const CretWorld* w = s->world; float S = w->size;
    for (int tries = 0; tries < 400; tries++) {
        float px = S * (0.1f + 0.8f * frand(&s->rng)), pz = S * (0.1f + 0.8f * frand(&s->rng));
        float h = cret_height(w, px, pz), wl = cret_water(w, px, pz);
        float n[3]; terra_normal(&w->map, px, pz, n);
        if (h > 2.0f && wl < h - 0.2f && n[1] > 0.85f) { *x = px; *z = pz; return; }
    }
    *x = S * 0.5f; *z = S * 0.5f;
}

// Spawn point between rmin and rmax from (cx, cz); falls back to anywhere.
static void spawn_near(CretSim* s, float cx, float cz, float rmin, float rmax, float* x, float* z) {
    const CretWorld* w = s->world; float S = w->size;
    for (int tries = 0; tries < 200; tries++) {
        float ang = 6.2831853f * frand(&s->rng), d = rmin + (rmax - rmin) * frand(&s->rng);
        float px = cx + cosf(ang) * d, pz = cz + sinf(ang) * d;
        if (px < S * 0.05f || pz < S * 0.05f || px > S * 0.95f || pz > S * 0.95f) continue;
        float h = cret_height(w, px, pz), wl = cret_water(w, px, pz);
        float n[3]; terra_normal(&w->map, px, pz, n);
        if (h > 2.0f && wl < h - 0.2f && n[1] > 0.85f) { *x = px; *z = pz; return; }
    }
    spawn_point(s, x, z);
}

// Mean position of the live herbivores other than i (species >= 0: only
// that species); returns how many.
static int herd_center_of(const CretSim* s, int i, int species, float* cx, float* cz) {
    int n = 0; *cx = *cz = 0;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (j == i || !b->alive || CRET_SPECIES_TABLE[b->species].diet != CRET_DIET_HERBIVORE) continue;
        if (species >= 0 && b->species != species) continue;
        *cx += b->x; *cz += b->z; n++;
    }
    if (n) { *cx /= n; *cz /= n; }
    return n;
}
static int herd_center(const CretSim* s, int i, float* cx, float* cz) { return herd_center_of(s, i, -1, cx, cz); }
// A herbivore's own herd: its group, else its species, else all herbivores.
static int own_herd(const CretSim* s, int i, float* cx, float* cz) {
    const CretAgent* a = &s->agents[i]; int n = 0; *cx = *cz = 0;
    if (a->group) {
        for (int j = 0; j < s->cfg.num_agents; j++) {
            const CretAgent* b = &s->agents[j]; if (j == i || !b->alive || b->group != a->group) continue;
            *cx += b->x; *cz += b->z; n++;
        }
        if (n) { *cx /= n; *cz /= n; return n; }
    }
    n = herd_center_of(s, i, a->species, cx, cz);
    return n ? n : herd_center(s, i, cx, cz);
}

// Bots join the nearest group of their species with room (packs and herds),
// or found one. The player (slot 0) and remote players group by calling.
static void join_group(CretSim* s, int i) {
    CretAgent* a = &s->agents[i]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    a->group = 0;
    if (S->group_max <= 1) return;
    int best = -1; float bd = 1e30f;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (j == i || !b->alive || b->species != a->species || !b->group) continue;
        if (cret_group_size(s, b->group) >= S->group_max) continue;
        float dx = b->x - a->x, dz = b->z - a->z, d = dx * dx + dz * dz; if (d < bd) { bd = d; best = j; }
    }
    a->group = best >= 0 ? s->agents[best].group : (uint8_t)(i + 1);
}

// True if a capsule standing at (x, y, z) would overlap a prop or another agent.
static bool overlaps(CretSim* s, int i, float x, float y, float z) {
    CretAgent* a = &s->agents[i];
    cret_prop_stream_touch(&s->props, s->world, s->phys, x, z, 40.0f, s->tick);
    MoverCtx ctx = {.s = s, .self = i};
    b3Capsule cap = {{0, a->radius, 0}, {0, a->height - a->radius, 0}, a->radius};
    b3QueryFilter filt = {.categoryBits = CAT_AGENT, .maskBits = CAT_AGENT | CAT_PROP, .id = 1, .name = "cret_spawn"};
    b3World_CollideMover(s->phys, (b3Pos){x, y + 0.05f, z}, &cap, filt, mover_planes, &ctx);
    return ctx.count > 0;
}

bool cret_is_person(const CretSim* s, int i) { return (i == 0 || s->human[i]) && s->cfg.player_growth >= 0; }

void cret_sim_respawn(CretSim* s, int i) {
    CretAgent* a = &s->agents[i]; b3BodyId body = a->body; b3ShapeId shape = a->shape; int sp = slot_species(s, i);
    const CretSpecies* S = &CRET_SPECIES_TABLE[sp];
    uint8_t old_group = a->group, old_species = a->species;
    if (a->latch) cret_release_latch(s, i, false);
    memset(a, 0, sizeof(*a)); a->body = body; a->shape = shape; a->species = (uint8_t)sp;
    a->alive = true; a->stamina = 1; a->last_attacker = -1; a->def_speed = 1;
    bool person = cret_is_person(s, i);   // the local player or a network peer
    float g0 = person ? s->cfg.player_growth : s->cfg.spawn_growth;
    a->growth = g0 >= 0 ? clampf(g0, 0.35f, 1.0f) : 0.55f + 0.45f * frand(&s->rng);
    // A nest of its species (the player's own group's) hatches it as a juvenile.
    int nest = cret_nest_spawn(s, i, sp, person ? (old_species == sp ? old_group : 0) : -1);
    if (nest >= 0) a->growth = 0.5f;
    a->food = 0.75f + 0.25f * frand(&s->rng); a->water = 0.75f + 0.25f * frand(&s->rng);
    cret_apply_growth(a); a->health = a->max_health; update_shape(a);
    cret_loadout_default(s, i);
    b3Body_SetTransform(a->body, (b3Pos){0, -1000.0f - i * 10, 0}, b3Quat_identity);
    if (nest >= 0) {
        const CretNest* n = &s->nests[nest];
        for (int tries = 0; tries < 10; tries++) {
            spawn_near(s, n->x, n->z, 3.0f, 12.0f, &a->x, &a->z); a->y = cret_height(s->world, a->x, a->z);
            if (!overlaps(s, i, a->x, a->y, a->z)) break;
        }
        a->yaw = 6.2831853f * frand(&s->rng); a->bot_timer = (int)(frand(&s->rng) * 90);
        b3Body_SetTransform(a->body, (b3Pos){a->x, a->y + a->height * 0.5f, a->z}, b3Quat_identity);
        if (person) a->group = old_species == sp && old_group && cret_group_size(s, old_group) > 0 ? old_group : 0;
        else if (n->group) a->group = n->group; else join_group(s, i);
        return;
    }
    // Herbivores join the herd, predators start a short hunt away, the player
    // within sight of it. The first herbivore founds the herd.
    // Herbivores spawn by their own species' herd, else by the herbivores.
    bool herb = S->diet == CRET_DIET_HERBIVORE;
    float hx, hz; int nherd = herb ? herd_center_of(s, i, sp, &hx, &hz) : 0;
    if (!nherd) nherd = herd_center(s, i, &hx, &hz);
    float rmin = person ? 40.0f : herb ? 15.0f : 80.0f, rmax = person ? 80.0f : herb ? 60.0f : 200.0f;
    for (int tries = 0; tries < 20; tries++) {
        if (nherd) spawn_near(s, hx, hz, rmin, rmax, &a->x, &a->z); else spawn_point(s, &a->x, &a->z);
        a->y = cret_height(s->world, a->x, a->z);
        if (!overlaps(s, i, a->x, a->y, a->z)) break;
    }
    a->yaw = 6.2831853f * frand(&s->rng);
    a->bot_timer = (int)(frand(&s->rng) * 90);
    b3Body_SetTransform(a->body, (b3Pos){a->x, a->y + a->height * 0.5f, a->z}, b3Quat_identity);
    // Bots (and every RL agent) join packs and herds by themselves; the
    // player keeps their group across lives of the same species.
    if (!person) join_group(s, i);
    else if (old_group && old_species == sp && cret_group_size(s, old_group) > 0) a->group = old_group;
}

int cret_sim_init(CretSim* s, CretConfig cfg) {
    memset(s, 0, sizeof(*s)); s->cfg = cfg; s->rng = cfg.seed * 2654435761u + 1; s->crit_cfg = cret_critter_defaults();
    if (cfg.num_agents > CRET_MAX_AGENTS) cfg.num_agents = s->cfg.num_agents = CRET_MAX_AGENTS;
    s->world = cret_world_acquire(cfg.world_seed, cfg.world_n, cfg.world_cell, 1);
    if (!s->world) return -1;
    b3WorldDef wd = b3DefaultWorldDef(); wd.gravity = (b3Vec3){0, -9.8f, 0};
    s->phys = b3CreateWorld(&wd);
    cret_prop_stream_init(&s->props, s->world);
    dyn_init(s);
    for (int i = 0; i < s->cfg.num_agents; i++) {
        b3BodyDef bd = b3DefaultBodyDef(); bd.type = b3_kinematicBody; bd.position = (b3Pos){0, -1000.0f - i * 10, 0};
        bd.isAwake = false;   // moved by SetTransform, never simulated: asleep, the physics step (dynprops.c) skips it
        s->agents[i].body = b3CreateBody(s->phys, &bd);
        // Queries only (the mover): no contacts with movable props, which dynprops.c pushes and crushes with.
        b3ShapeDef sd = b3DefaultShapeDef(); sd.filter.categoryBits = CAT_AGENT; sd.filter.maskBits = CAT_AGENT;
        const CretSpecies* S = &CRET_SPECIES_TABLE[slot_species(s, i)];
        float half = fmaxf(S->height * 0.5f - S->radius, 0.05f);
        b3Capsule cap = {{0, -half, 0}, {0, half, 0}, S->radius};
        s->agents[i].shape = b3CreateCapsuleShape(s->agents[i].body, &sd, &cap);
    }
    cret_pois_build(s->world);
    for (int i = 0; i < CRET_MAX_AGENTS; i++) { s->agents[i].species = (uint8_t)slot_species(s, i); cret_prog_reset(s, i, cret_is_person(s, i)); }
    cret_sim_reset(s);
    return 0;
}

void cret_sim_reset(CretSim* s) {
    dyn_reset(s);   // moved props back where the scatter put them
    s->tick = 0; s->time_of_day = 0.3f + 0.3f * frand(&s->rng);
    memset(s->hits, 0, sizeof s->hits); s->hit_count = 0;
    s->cloud_cover = s->cloud_target = s->cfg.cloud_cover; s->weather_ticks = 0; s->weather_rng = s->cfg.seed * 747796405u + 0x51ed27u;
    for (int i = 0; i < CRET_MAX_AGENTS; i++) { s->agents[i].alive = false; s->agents[i].latch = 0; cret_quest_reset(s, i); }
    memset(s->carcasses, 0, sizeof s->carcasses); cret_critters_clear(s);
    memset(s->nests, 0, sizeof s->nests); memset(s->gquest, 0, sizeof s->gquest);
    // Herbivores first so everyone else can spawn relative to the herd.
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < s->cfg.num_agents; i++) {
            bool herb = CRET_SPECIES_TABLE[slot_species(s, i)].diet == CRET_DIET_HERBIVORE;
            if ((pass == 0) == (herb && i > 0)) cret_sim_respawn(s, i);
        }
}

void cret_sim_close(CretSim* s) {
    cret_prop_stream_free(&s->props);
    if (b3World_IsValid(s->phys)) b3DestroyWorld(s->phys);
    cret_world_release(s->world); s->world = NULL;
}

void cret_sim_restore(CretSim* s, int i, float growth, float health, float food, float water, bool has_pos, float x, float z) {
    CretAgent* a = &s->agents[i]; if (!a->alive) return;
    a->growth = clampf(growth, 0.35f, 1.0f); a->max_health = 0; cret_apply_growth(a);
    a->health = clampf(health, 0.05f, 1.0f) * a->max_health; update_shape(a);
    a->food = clampf(food, 0.2f, 1.0f); a->water = clampf(water, 0.2f, 1.0f);
    if (has_pos && cret_walkable(s->world, x, z)) {
        float y = cret_height(s->world, x, z);
        if (!overlaps(s, i, x, y, z)) {
            a->x = x; a->z = z; a->y = y;
            b3Body_SetTransform(a->body, (b3Pos){a->x, a->y + a->height * 0.5f, a->z}, b3Quat_identity);
        }
    }
}

void cret_sim_set_species(CretSim* s, int i, int species) {
    if (species < 0 || species >= CRET_SPECIES || i < 0 || i >= s->cfg.num_agents) return;
    if (i == 0) s->cfg.player_species = species;
    bool changed = s->agents[i].species != species;
    s->species_pick[i] = (uint8_t)(species + 1);
    cret_sim_respawn(s, i);
    if (changed) cret_quest_reset(s, i);   // quests depend on diet and prey
}

// ---------------------------------------------------------------- combat, eating, death
// Bodies are horizontal segments along the heading (half the body length,
// capsule radius): long animals can be hit anywhere from snout to tail.
static float body_dist(const CretAgent* b, float px, float pz) {
    float hl = CRET_SPECIES_TABLE[b->species].length * cret_growth_scale(b->growth) * 0.4f;
    float cy = cosf(b->yaw), sy = sinf(b->yaw), dx = px - b->x, dz = pz - b->z;
    float t = clampf(dx * cy + dz * sy, -hl, hl);
    float ex = dx - cy * t, ez = dz - sy * t;
    return fmaxf(sqrtf(ex * ex + ez * ez) - b->radius, 0.0f);
}
// Aimed attacks: the head turns about the neck base (the tail about its
// base) by the aim, up to the species' neck arc. Rear (tail) attacks aim
// the rear arc: an aim of 0 points straight back.
float cret_attack_dir(const CretAgent* a, bool rear) {
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    float aim = isfinite(a->aim) ? clampf(a->aim, -1, 1) : 0;
    return wrapa(a->yaw + (rear ? 3.14159265f : 0) + aim * S->neck_arc);
}
// Where the attack lands: in front of the snout, or behind the tail. The
// neck (tail) base sits 0.18 body lengths from the centre and the snout
// (tail tip) reaches 0.24 (0.27) beyond it, so an unaimed point is where
// it always was: 0.42 lengths ahead, or 0.45 behind.
static void strike_point_aim(const CretAgent* a, float aim, float* px, float* pz) {
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float L = S->length * cret_growth_scale(a->growth);
    aim = isfinite(aim) ? clampf(aim, -1, 1) : 0;
    float base = S->tail ? -0.18f * L : 0.18f * L, arm = (S->tail ? 0.27f : 0.24f) * L;
    float dir = a->yaw + (S->tail ? 3.14159265f : 0) + aim * S->neck_arc;
    *px = a->x + cosf(a->yaw) * base + cosf(dir) * arm; *pz = a->z + sinf(a->yaw) * base + sinf(dir) * arm;
}
static void strike_point(const CretAgent* a, float* px, float* pz) { strike_point_aim(a, a->aim, px, pz); }
bool cret_rear_attack(const CretAgent* a) {
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    return a->act == CRET_ACT_ATTACK2 ? S->alt.kind == CRET_ALT_TAIL : S->tail;
}
float cret_aim_at(const CretAgent* a, float x, float z) {
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    float d = wrapa(atan2f(z - a->z, x - a->x) - a->yaw - (S->tail ? 3.14159265f : 0));
    return clampf(d / fmaxf(S->neck_arc, 0.1f), -1, 1);
}

float cret_body_dist(const CretAgent* b, float px, float pz) { return body_dist(b, px, pz); }
void cret_strike_point(const CretAgent* a, float* px, float* pz) { strike_point(a, px, pz); }
float cret_mass(const CretAgent* a) { return CRET_SPECIES_TABLE[a->species].mass * powf(cret_growth_scale(a->growth), 3); }

static void kill(CretSim* s, int i) {
    CretAgent* a = &s->agents[i];
    if (!a->alive) return;
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    if (a->latch) cret_release_latch(s, i, false);
    a->alive = false; a->dead_ticks = 0; a->ev_died = 1; a->act = 0; a->health = 0; a->strike = 0; a->leap = 0;
    int killer = a->last_attacker >= 0 && a->last_attacker < s->cfg.num_agents ? a->last_attacker : -1;
    s->stats.deaths[a->species]++;
    if (killer >= 0) { s->stats.kills[s->agents[killer].species][a->species]++; cret_quest_on_kill(s, killer, i); }
    else s->stats.deaths_env[a->species]++;
    int slot = 0, oldest = -1;
    for (int c = 0; c < CRET_MAX_CARCASSES; c++) {
        if (!s->carcasses[c].active) { slot = c; oldest = -2; break; }
        if (oldest < 0 || s->carcasses[c].age > s->carcasses[oldest].age) oldest = c;
    }
    if (oldest >= 0) slot = oldest;   // all taken: the oldest rots away now
    float meat = S->mass * g * g * g * 0.5f;
    s->carcasses[slot] = (CretCarcass){.active = true, .species = a->species, .x = a->x, .y = a->y, .z = a->z, .yaw = a->yaw,
                                       .growth = a->growth, .meat = meat, .max_meat = meat, .age = 0, .agent = i};
    if (a->last_attacker >= 0 && a->last_attacker < s->cfg.num_agents) s->agents[a->last_attacker].ev_kills++;
    b3Body_SetTransform(a->body, (b3Pos){0, -1000.0f - i * 10, 0}, b3Quat_identity);
}

// A broken bone: slow and grounded for `seconds` (longer breaks do not stack).
static void break_bone(CretAgent* b, float seconds) { b->broken = fmaxf(b->broken, seconds); }

void cret_kill(CretSim* s, int i) { kill(s, i); }

void cret_log_hit(CretSim* s, int attacker, int victim, int kind, float frac) {
    CretHit* h = &s->hits[s->hit_count++ % CRET_HIT_LOG];
    h->tick = s->tick; h->attacker = (uint8_t)attacker; h->victim = (uint8_t)victim; h->kind = (uint8_t)kind;
    h->dmg = (uint8_t)clampf(frac * 255.0f + 0.5f, 1, 255);
}
// What kind of blow this is, for the hit log.
static int hit_kind(const CretAgent* a) {
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    if (a->act == CRET_ACT_ATTACK2) return S->alt.kind == CRET_ALT_SLASH ? CRET_HIT_SLASH : CRET_HIT_TAIL;
    if (a->act == CRET_ACT_ATTACK) return a->strike ? CRET_HIT_ABILITY : S->tail ? CRET_HIT_TAIL : S->knock > 1.5f ? CRET_HIT_BUTT : CRET_HIT_BITE;
    return CRET_HIT_OTHER;
}

static void damage(CretSim* s, int attacker, int victim, float hp) {
    CretAgent* a = &s->agents[attacker]; CretAgent* b = &s->agents[victim];
    if (!b->alive || hp <= 0) return;
    const CretSpecies* SA = &CRET_SPECIES_TABLE[a->species]; const CretSpecies* SB = &CRET_SPECIES_TABLE[b->species];
    // Status effects: a dodge misses, fear weakens, a mark exposes; defensive armour (abilities.c).
    const CretAbility* AB = a->strike && a->act == CRET_ACT_ATTACK ? &CRET_ABILITIES[a->strike] : NULL;
    float extra; hp = cret_status_damage(s, attacker, victim, hp, &extra);
    if (hp <= 0) return;
    // The secondary attack: its own knockback; a club-less tail swipe bruises like a headbutt.
    bool alt = a->act == CRET_ACT_ATTACK2, swipe = alt && SA->alt.kind == CRET_ALT_TAIL && !SA->tail;
    float knock = alt ? SA->alt.knock : SA->knock;
    // Armour: heavy blows punch through it (a tyrannosaur's bite still crushes a sauropelta).
    float raw = hp, armor = clampf(SB->armor * clampf(1.0f - raw / 600.0f, 0.3f, 1.0f) + extra, 0, 0.85f);
    hp = fminf(hp * (1 - armor), b->health);
    float blunt = (!alt && SA->knock > 1.5f) || swipe ? 0.3f : 1.0f;   // a headbutt bruises more than it cuts
    b->health -= hp; b->bleed += hp * 0.02f * blunt * (AB ? AB->bleed : 1) * (1 - SB->bleed_resist); b->last_attacker = attacker;
    b->hurt_ticks = knock > 1.5f ? 24 : 12;   // a headbutt or a tail swipe staggers longer
    a->ev_dealt += hp / fmaxf(b->max_health, 1); b->ev_taken += hp / fmaxf(b->max_health, 1);
    cret_log_hit(s, attacker, victim, hit_kind(a), hp / fmaxf(b->max_health, 1));
    cret_quest_on_damage(s, attacker, victim, hp / fmaxf(b->max_health, 1));
    // Knockback by the mass ratio: a tail swing throws a raptor, a raptor's bite barely moves a stegosaur.
    float ma = SA->mass * powf(cret_growth_scale(a->growth), 3);
    float mb = SB->mass * powf(cret_growth_scale(b->growth), 3);
    float dx = b->x - a->x, dz = b->z - a->z, dl = fmaxf(sqrtf(dx * dx + dz * dz), 1e-3f);
    float k = clampf(5.0f * ma / (ma + mb) * knock * (AB ? AB->knock : 1), 0, 9);
    b->vx += dx / dl * k; b->vz += dz / dl * k;
    if (k > 1.5f && b->on_ground && !b->swimming) { b->vy = k * 0.4f; b->on_ground = false; }
    // Bones break under a crushing blow (a third of the victim's health at
    // once) or a hard throw by a heavier or equal animal.
    if (hp > 0.33f * b->max_health || (k > 4.5f && ma > 0.8f * mb && frand(&s->rng) < 0.35f)) break_bone(b, 45.0f + 45.0f * frand(&s->rng));
    // Spikes: biting into a sauropelta cuts the biter (not a tail swing behind).
    if (SB->thorns > 0 && !cret_rear_attack(a) && a->alive) {
        float back = fminf(raw * SB->thorns, a->health);
        a->health -= back; a->bleed += back * 0.03f * (1 - SA->bleed_resist); a->hurt_ticks = 8; a->last_attacker = victim;
        a->ev_taken += back / fmaxf(a->max_health, 1); b->ev_dealt += back / fmaxf(a->max_health, 1);
        if (back > 0) cret_log_hit(s, victim, attacker, CRET_HIT_THORNS, back / fmaxf(a->max_health, 1));
        if (a->health <= 0.01f) kill(s, attacker);
    }
    cret_status_hit(s, attacker, victim, hp, raw, k);
    if (b->health <= 0.01f) kill(s, victim);
}
void cret_damage(CretSim* s, int attacker, int victim, float hp) { damage(s, attacker, victim, hp); }

// The attack lands hit_time into the swing on everyone within reach of the strike point.
static void resolve_attack(CretSim* s, int i) {
    CretAgent* a = &s->agents[i]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    float px, pz; strike_point(a, &px, &pz);
    const CretAbility* AB = a->strike ? &CRET_ABILITIES[a->strike] : NULL;   // a special attack (abilities.c)
    float reach = S->reach * g * (AB ? AB->reach : 1), mult = AB ? AB->dmg : 1;
    // One bite, one victim: the body nearest the jaws (tail swipes and slashes sweep, resolve_alt).
    int best = -1; float bd = reach;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        CretAgent* b = &s->agents[j];
        if (j == i || !b->alive || fabsf(b->y - a->y) > a->height + b->height) continue;
        if (a->group && b->group == a->group) continue;   // packmates and herd-mates are spared
        float d = body_dist(b, px, pz); if (d <= bd) { bd = d; best = j; }
    }
    // Small prey (critters.c): caught when it is nearer the jaws than any animal's body.
    float cd; int c = cret_critter_in_reach(s, i, px, pz, reach, &cd);
    if (c >= 0 && (best < 0 || cd < bd)) { cret_critter_bite(s, i, c); return; }
    if (best >= 0) damage(s, i, best, S->damage * g * g * mult * (0.85f + 0.3f * frand(&s->rng)));
}

// The secondary attack lands alt.hit_time into the swing. A tail swipe hits
// every animal whose body comes within reach of the centre inside the arc
// behind (swung toward the aimed side); a slash rakes what is in reach of
// the snout, or close alongside within the arc, as the animal runs past.
static bool alt_hits(const CretAgent* a, const CretAgent* b, float* along) {
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; const CretAltAttack* A = &S->alt; float g = cret_growth_scale(a->growth);
    float aim = isfinite(a->aim) ? clampf(a->aim, -1, 1) : 0;
    float centre = A->kind == CRET_ALT_TAIL ? wrapa(a->yaw + 3.14159265f + (S->tail ? 0.5f * aim * S->neck_arc : -0.6f * aim)) : cret_attack_dir(a, false);
    // Nearest point of b's body to a's centre.
    float hl = CRET_SPECIES_TABLE[b->species].length * cret_growth_scale(b->growth) * 0.4f, cy = cosf(b->yaw), sy = sinf(b->yaw);
    float dx = a->x - b->x, dz = a->z - b->z, t = clampf(dx * cy + dz * sy, -hl, hl);
    float qx = b->x + cy * t - a->x, qz = b->z + sy * t - a->z, d = fmaxf(sqrtf(qx * qx + qz * qz) - b->radius, 0);
    float off = fabsf(wrapa(atan2f(qz, qx) - centre));
    *along = d;
    if (A->kind == CRET_ALT_TAIL) return d <= A->reach * g && off <= A->arc;
    float px, pz; strike_point(a, &px, &pz);
    return body_dist(b, px, pz) <= A->reach * g || (d <= S->length * g * 0.5f + A->reach * g && off <= A->arc);
}
static void resolve_alt(CretSim* s, int i) {
    CretAgent* a = &s->agents[i]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    if (S->alt.kind == CRET_ALT_NONE) return;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        CretAgent* b = &s->agents[j]; float d;
        if (j == i || !b->alive || fabsf(b->y - a->y) > a->height + b->height) continue;
        if (a->group && b->group == a->group) continue;
        if (!alt_hits(a, b, &d)) continue;
        damage(s, i, j, S->alt.damage * g * g * (0.85f + 0.3f * frand(&s->rng)));
        if (!a->alive) break;
    }
}

// Nearest carcass with meat whose body is within `reach` of (x, z); -1 if none.
static int carcass_near(const CretSim* s, float x, float z, float reach) {
    int best = -1; float bd = reach;
    for (int c = 0; c < CRET_MAX_CARCASSES; c++) {
        const CretCarcass* k = &s->carcasses[c]; if (!k->active || k->meat <= 0) continue;
        CretAgent tmp = {.species = k->species, .growth = k->growth, .x = k->x, .z = k->z, .yaw = k->yaw,
                         .radius = CRET_SPECIES_TABLE[k->species].radius * cret_growth_scale(k->growth)};
        float d = body_dist(&tmp, x, z); if (d < bd) { bd = d; best = c; }
    }
    return best;
}
static bool water_ahead(const CretSim* s, const CretAgent* a) {
    float d = a->radius + CRET_SPECIES_TABLE[a->species].length * cret_growth_scale(a->growth) * 0.45f + 0.8f;
    float ax = a->x + cosf(a->yaw) * d, az = a->z + sinf(a->yaw) * d;
    return cret_water(s->world, ax, az) > cret_height(s->world, ax, az) + 0.05f;
}
bool cret_water_ahead(const CretSim* s, const CretAgent* a) { return water_ahead(s, a); }
// What agent i could eat now (the HUD's prompt): 1 food in reach (carcass,
// eggs, plants for a herbivore), 2 a carcass with meat within 20 m but out of
// reach (*dist its distance), 3 small prey a bite would catch, 4 small prey
// within 20 m (*dist), 0 nothing.
int cret_food_state(const CretSim* s, int i, float* dist) {
    const CretAgent* a = &s->agents[i]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    float hx = a->x + cosf(a->yaw) * S->length * g * 0.45f, hz = a->z + sinf(a->yaw) * S->length * g * 0.45f;
    if (S->diet == CRET_DIET_HERBIVORE) return cret_forage(s->world, hx, hz) > 0.1f ? 1 : 0;
    if (carcass_near(s, hx, hz, 1.0f + S->reach * g) >= 0 || carcass_near(s, a->x, a->z, S->length * g * 0.55f + S->reach * g + 1.0f) >= 0) return 1;
    int k = carcass_near(s, a->x, a->z, 20.0f);
    float px, pz, cd; strike_point(a, &px, &pz);
    if (cret_critter_in_reach(s, i, px, pz, S->reach * g, NULL) >= 0) return 3;
    if (k >= 0) { if (dist) *dist = hypotf(s->carcasses[k].x - a->x, s->carcasses[k].z - a->z); return 2; }
    if (cret_critter_nearest(s, i, 20.0f, &cd) >= 0) { if (dist) *dist = cd; return 4; }
    return 0;
}

// Timers that gate movement and actions, once per moved tick.
static void agent_timers(CretAgent* a) {
    if (a->rally_ticks > 0) a->rally_ticks--;
    if (a->hurt_ticks > 0) a->hurt_ticks--;
    if (a->attack_cd > 0) a->attack_cd--;
    if (a->attack2_cd > 0) a->attack2_cd--;
}

// Eating, drinking, hunger, thirst, bleeding, healing and growth, once per tick.
static void survive(CretSim* s, int i) {
    CretAgent* a = &s->agents[i]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; const CretConfig* c = &s->cfg;
    float dt = CRET_DT, g = cret_growth_scale(a->growth);
    if (a->act == CRET_ACT_DRINK && water_ahead(s, a)) {
        float d = fminf(dt / 8.0f, 1 - a->water); a->water += d; a->ev_water += d;
    } else if (a->act == CRET_ACT_EAT) {
        float hx = a->x + cosf(a->yaw) * S->length * g * 0.45f, hz = a->z + sinf(a->yaw) * S->length * g * 0.45f;
        if (S->diet == CRET_DIET_HERBIVORE) {
            float d = fminf(dt * cret_forage(s->world, hx, hz) / 15.0f, 1 - a->food); a->food += d; a->ev_food += d;
        } else {
            int k = carcass_near(s, hx, hz, 1.0f + S->reach * g);
            if (k < 0) k = carcass_near(s, a->x, a->z, S->length * g * 0.55f + S->reach * g + 1.0f);   // lying beside it, not just square on
            if (k >= 0) {   // a full stomach is a quarter of body mass, eaten in ~12 s
                float mass = S->mass * g * g * g, kg = fminf(fminf(0.02f * mass * dt, s->carcasses[k].meat), (1 - a->food) * 0.25f * mass);
                s->carcasses[k].meat -= kg; float d = kg / (0.25f * mass); a->food += d; a->ev_food += d;
            } else { float d = cret_nest_eat(s, i, hx, hz); a->food += d; a->ev_food += d; }   // eggs from a nest
        }
    }
    float burn = a->sprinting ? 2.0f : a->act == CRET_ACT_REST ? 0.6f : 1.0f;
    a->food = fmaxf(a->food - burn * dt / c->hunger_seconds, 0); a->water = fmaxf(a->water - burn * dt / c->thirst_seconds, 0);
    float loss = a->bleed;
    if (a->food <= 0) loss += a->max_health * 0.01f;
    if (a->water <= 0) loss += a->max_health * 0.015f;
    a->bleed *= expf(-0.03f * dt); if (a->bleed < 0.01f) a->bleed = 0;
    if (a->food > 0.3f && a->water > 0.3f && a->bleed < 0.002f * a->max_health)
        a->health = fminf(a->health + a->max_health * (a->act == CRET_ACT_REST ? 0.012f : 0.004f) * dt, a->max_health);
    if (loss > 0) { float d = fminf(loss * dt, a->health); a->health -= d; a->ev_taken += d / fmaxf(a->max_health, 1); }
    if (a->health <= 0.01f) { kill(s, i); return; }
    if (a->growth < 1 && a->food > 0.25f && a->water > 0.25f) {
        a->growth = fminf(a->growth + 0.65f * dt / c->growth_seconds, 1.0f); cret_apply_growth(a);
        if (a->growth - a->shape_growth > 0.02f || a->growth >= 1) update_shape(a);
    }
    if (a->broken > 0) a->broken = fmaxf(a->broken - dt * (a->act == CRET_ACT_REST ? 2.5f : 1.0f), 0);
    if (!s->hold[i]) agent_timers(a);   // a held agent's timers wait for its input (cret_sim_move)
}

// Target ground speed for an input: forward 0.5 walks, 1 trots, sprint (with
// stamina) runs; backward and sideways steps are slow.
static float gait_speed(const CretAgent* a, float fwd, int sprint) {
    if (sprint) return a->sprint_speed;
    if (fwd <= 0) return a->walk_speed * 0.6f * fminf(-fwd * 2, 1);
    if (fwd <= 0.5f) return a->walk_speed * fwd * 2;
    return a->walk_speed + (a->trot_speed - a->walk_speed) * (fwd - 0.5f) * 2;
}

// A call: animals of the same species within earshot that are alone or in a
// group with room form or join a group with the caller; then every member
// within 200 m is rallied to the caller (bots come; players see a marker).
static void call_out(CretSim* s, int i) {
    CretAgent* a = &s->agents[i]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    int cap = S->group_max > 1 ? S->group_max : 4;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        CretAgent* b = &s->agents[j];
        if (j == i || !b->alive || b->species != a->species) continue;
        float dx = b->x - a->x, dz = b->z - a->z; if (dx * dx + dz * dz > 60.0f * 60.0f) continue;
        if (!a->group && b->group && cret_group_size(s, b->group) < cap) a->group = b->group;   // join theirs
        else if (!a->group && !b->group) { a->group = (uint8_t)(i + 1); b->group = a->group; }   // found one together
        else if (a->group && !b->group && cret_group_size(s, a->group) < cap) b->group = a->group;   // they join
    }
    if (!a->group) return;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        CretAgent* b = &s->agents[j];
        if (j == i || !b->alive || b->group != a->group) continue;
        float dx = b->x - a->x, dz = b->z - a->z; if (dx * dx + dz * dz > 200.0f * 200.0f) continue;
        b->rally_ticks = 20 * CRET_TICK_HZ; b->rally_x = a->x; b->rally_z = a->z;
    }
}

static void move_agent(CretSim* s, int i, const CretInput* in, bool predict) {
    CretAgent* a = &s->agents[i]; const CretWorld* w = s->world; float dt = CRET_DT;
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    // Turning: the species' rate at rest; at speed the grip holds at most
    // grip / v, so the turning circle (v^2 / grip) widens with speed and mass.
    // Juveniles are lighter: they grip and accelerate a little better.
    float gs = cret_growth_scale(a->growth), nimble = 1.3f - 0.3f * gs, grip = S->grip * nimble;
    float yaw_rate = a->speed > 0.5f ? fminf(a->turn_rate, grip / a->speed) : a->turn_rate;
    a->yaw += clampf(in->turn, -1, 1) * yaw_rate * dt;
    if (a->yaw > 3.14159265f) a->yaw -= 6.2831853f; else if (a->yaw < -3.14159265f) a->yaw += 6.2831853f;
    float fwd = clampf(in->forward, -0.5f, 1), str = clampf(in->strafe, -1, 1);
    // Actions: start when (nearly) standing; continuous ones (eat, drink,
    // rest) last while held, one-shots (call, attacks) run to the end.
    // Attacks start at any speed and never root the animal.
    a->aim = isfinite(in->aim) ? clampf(in->aim, -1, 1) : 0;
    bool one_shot = a->act == CRET_ACT_CALL || cret_act_is_attack(a->act);
    int want = in->action <= CRET_ACT_MAX ? in->action : CRET_ACT_NONE;   // out of range (a buggy client or caller): no action
    if (want == CRET_ACT_ATTACK2 && S->alt.kind == CRET_ALT_NONE) want = CRET_ACT_NONE;
    if (want == CRET_ACT_EAT || want == CRET_ACT_DRINK) want = water_ahead(s, a) ? CRET_ACT_DRINK : CRET_ACT_EAT;
    // An attack pressed while one is swinging or cooling down waits up to
    // CRET_ATTACK_BUFFER ticks and starts the moment it can.
    if (cret_act_is_attack(want)) { a->buf_act = (uint8_t)want; a->buf_ticks = CRET_ATTACK_BUFFER + 1; }
    else if (!want && a->buf_ticks) want = a->buf_act;
    if (a->buf_ticks && --a->buf_ticks == 0) a->buf_act = 0;
    int hit = (int)(S->hit_time * CRET_TICK_HZ), hit2 = (int)(S->alt.hit_time * CRET_TICK_HZ);
    if (a->act) {
        a->act_ticks++;
        int len = a->act == CRET_ACT_ATTACK ? (int)(S->attack_time * CRET_TICK_HZ) : a->act == CRET_ACT_ATTACK2 ? (int)(S->alt.attack_time * CRET_TICK_HZ) : 60;
        if (a->act == CRET_ACT_ATTACK && a->act_ticks == hit && !predict) { resolve_attack(s, i); if (!a->alive) return; }
        if (a->act == CRET_ACT_ATTACK2 && a->act_ticks == hit2 && !predict) { resolve_alt(s, i); if (!a->alive) return; }
        if (one_shot ? a->act_ticks > len : (want != a->act || fabsf(fwd) > 0.05f || fabsf(str) > 0.05f)) { a->act = 0; a->strike = 0; }
    }
    bool can = want == CRET_ACT_ATTACK ? a->attack_cd == 0 && !a->swimming :
               want == CRET_ACT_ATTACK2 ? a->attack2_cd == 0 && !a->swimming :
               a->on_ground && !a->swimming && (a->speed < 1.0f || want == CRET_ACT_CALL);
    if (!a->act && want && can) {
        a->act = (uint8_t)want; a->act_ticks = 0; a->strike = 0;
        if (want == CRET_ACT_ATTACK) a->attack_cd = (int)(S->cooldown * CRET_TICK_HZ);
        if (want == CRET_ACT_ATTACK2) a->attack2_cd = (int)(S->alt.cooldown * CRET_TICK_HZ);
        if (cret_act_is_attack(want)) { a->buf_act = 0; a->buf_ticks = 0; }
        if (want == CRET_ACT_CALL && !predict) call_out(s, i);
    }
    // A swing keeps most of the gait until the blow lands (swing_speed), then all of it.
    float swing = a->act == CRET_ACT_ATTACK && a->act_ticks <= hit + 3 ? S->swing_speed :
                  a->act == CRET_ACT_ATTACK2 && a->act_ticks <= hit2 + 3 ? S->alt.speed : 1.0f;
    float dash = cret_strike_dash(a);   // a charge runs in until the blow lands
    if (dash > 0) { fwd = 1; str = 0; swing = 1; }
    if (a->act && !cret_act_is_attack(a->act)) { fwd = 0; str = 0; }
    if (a->hurt_ticks > 6 && dash <= 0) { fwd *= 0.3f; str *= 0.3f; }   // staggered by a hit
    bool burst = a->st[CRET_ST_BURST] > 0;   // sprint burst: faster, no stamina spent
    int sprint = in->sprint && fwd > 0.5f && (a->stamina > 0.05f || burst) && !a->swimming && a->broken <= 0;
    a->sprinting = sprint;
    float top = a->swimming ? a->swim_speed * fmaxf(fwd, 0) * 2.0f / (1.0f + fmaxf(fwd, 0)) : gait_speed(a, fwd, sprint);
    if (sprint && burst) top *= CRET_ABILITIES[CRET_AB_SPRINT_BURST].speed;
    if (dash > 0 && !a->swimming) top = a->sprint_speed * dash;
    if (a->broken > 0) top *= 0.55f;   // limping
    top *= cret_status_speed(a) * swing;   // slowed, stunned, defensive, raptors hanging on; mid-swing
    float side = a->walk_speed * 0.5f * str;
    float cy = cosf(a->yaw), sy = sinf(a->yaw);
    float dir = fwd < 0 ? -1.0f : 1.0f;
    float dvx = cy * top * dir - sy * side, dvz = sy * top * dir + cy * side;
    // Slope: slower uphill, cliffs are impassable.
    float n[3]; terra_normal(&w->map, a->x, a->z, n);
    float up = -(n[0] * dvx + n[2] * dvz) / (fmaxf(1e-3f, sqrtf(dvx * dvx + dvz * dvz)));
    float steep = 1 - n[1];
    float slow = clampf(1 - up * steep * 3.0f, 0.15f, 1.15f);
    if (n[1] < 0.6f && up > 0) slow = 0;
    dvx *= slow; dvz *= slow;
    // Mass: the velocity error splits along the current motion (speeding up
    // at `accel`, which falls off toward the sprint, or braking at `decel`)
    // and across it (turning, held by the grip). Little control in the air;
    // water drags (half the push, full braking).
    {
        float sp = sqrtf(a->vx * a->vx + a->vz * a->vz), tl = sqrtf(dvx * dvx + dvz * dvz), ux = cy, uz = sy;
        if (sp > 0.3f) { ux = a->vx / sp; uz = a->vz / sp; } else if (tl > 1e-3f) { ux = dvx / tl; uz = dvz / tl; }
        float ex = dvx - a->vx, ez = dvz - a->vz, el = ex * ux + ez * uz, ec = -ex * uz + ez * ux;
        float ramp = fmaxf(1 - 0.7f * (sp / fmaxf(a->sprint_speed, 1)) * (sp / fmaxf(a->sprint_speed, 1)), 0.3f);
        float up_a = S->accel * nimble * ramp, dn_a = S->decel * nimble, lat = grip * 1.25f;
        if (a->swimming) up_a *= 0.5f;
        else if (!a->on_ground) { up_a *= 0.15f; dn_a *= 0.15f; lat *= 0.15f; }
        el = clampf(el, -dn_a * dt, up_a * dt); ec = clampf(ec, -lat * dt, lat * dt);
        a->vx += el * ux - ec * uz; a->vz += el * uz + ec * ux;
    }
    a->stamina = clampf(a->stamina + (sprint ? (burst ? 0 : -S->stamina_drain) : S->stamina_regen) * dt, 0, 1);
    // Vertical: gravity, jump, swim buoyancy.
    float ground = cret_height(w, a->x, a->z), water = cret_water(w, a->x, a->z);
    float depth = water - ground;
    a->swimming = depth > a->height * 0.65f;
    if (a->swimming) {
        float target = water - a->height * 0.6f;
        a->vy += (clampf(target - a->y, -2, 2) * 4.0f - a->vy * 2.0f) * dt;
    } else {
        // Airborne bodies fall CRET_AIR_GRAVITY times faster than g (a hop
        // that is snappy, not floaty); ability leaps keep g, their arcs are
        // tuned in abilities.c.
        a->vy -= CRET_GRAVITY * (a->leap ? 1.0f : CRET_AIR_GRAVITY) * dt;
        if (in->jump && a->on_ground && a->jump_speed > 0 && a->stamina > 0.1f && a->broken <= 0) { a->vy = a->jump_speed; a->on_ground = false; a->stamina -= 0.08f; a->act = 0; }
    }
    // Horizontal collision with agents and props (Box3D mover); ground is analytic.
    MoverCtx ctx = {.s = s, .self = i};
    b3Capsule cap = {{0, a->radius, 0}, {0, a->height - a->radius, 0}, a->radius};
    b3QueryFilter filt = {.categoryBits = CAT_AGENT, .maskBits = CAT_AGENT | CAT_PROP, .id = 1, .name = "cret_mover"};
    b3Pos pos = {a->x, a->y + 0.05f, a->z};
    b3Vec3 target = {a->vx * dt, 0, a->vz * dt};
    for (int it = 0; it < 3; it++) {
        ctx.count = 0;
        b3World_CollideMover(s->phys, pos, &cap, filt, mover_planes, &ctx);
        // The move is horizontal (the ground is analytic), so solve against the
        // planes' horizontal parts: a sloped face (a boulder's flank) otherwise
        // gets only part of its push and the capsule creeps into it.
        for (int p = 0; p < ctx.count; p++) {
            b3Plane* pl = &ctx.planes[p].plane; float h = sqrtf(pl->normal.x * pl->normal.x + pl->normal.z * pl->normal.z);
            if (h < 0.2f) { ctx.planes[p].pushLimit = 0; continue; }   // a top or underside: no sideways push
            pl->normal = (b3Vec3){pl->normal.x / h, 0, pl->normal.z / h}; pl->offset /= h;
        }
        b3Vec3 want = {target.x, 0, target.z};
        b3PlaneSolverResult r = b3SolvePlanes(want, ctx.planes, ctx.count);
        r.delta.y = 0;
        float f = b3World_CastMover(s->phys, pos, &cap, r.delta, filt, mover_filter, &ctx);
        pos.x += r.delta.x * f; pos.z += r.delta.z * f;
        target.x -= r.delta.x * f; target.z -= r.delta.z * f;
        if (target.x * target.x + target.z * target.z < 1e-6f) break;
    }
    float lean_vx = a->vx, lean_vz = a->vz;   // before the planes clip it: how hard it pushes (dyn_push)
    if (ctx.count) { b3Vec3 v = b3ClipVector((b3Vec3){a->vx, 0, a->vz}, ctx.planes, ctx.count); a->vx = v.x; a->vz = v.z; }
    if (ctx.npush && !predict) dyn_push(s, i, &ctx, lean_vx, lean_vz);
    float ws = w->size, ox = a->x, oz = a->z;
    a->x = clampf((float)pos.x, 2, ws - 2); a->z = clampf((float)pos.z, 2, ws - 2);
    a->y += a->vy * dt;
    float g = cret_height(w, a->x, a->z);
    bool was_air = !a->on_ground && !a->swimming; float vx_land = a->vx, vy_land = a->vy, vz_land = a->vz;
    if (!a->swimming && a->y <= g + 0.02f) { a->y = g; if (a->vy < 0) a->vy = 0; a->on_ground = true; }
    else if (!a->swimming && !was_air && a->y - g < 0.35f && a->vy <= 0) { a->y = g; a->vy = 0; a->on_ground = true; } // stick to slopes (not mid-hop)
    else a->on_ground = false;
    if (a->swimming && a->y < g) a->y = g;
    // Falls: a drop of more than about 5 m hurts and can break a leg; small
    // animals take falls better. The impact is the speed into the ground, so
    // running down a steep slope is not a fall. Speeds scale with the root of
    // the gravity, so the same drops hurt as under plain g.
    // Landing weight: the legs soak up the impact and the animal loses up to
    // a third of its ground speed (a hop's landing about a fifth).
    if (was_air && a->on_ground) {
        float gn[3]; terra_normal(&w->map, a->x, a->z, gn);
        float impact = -(vx_land * gn[0] + vy_land * gn[1] + vz_land * gn[2]);
        a->fall_vy = impact;
        float soak = 1 - 0.33f * clampf(impact / 8.0f, 0, 1); a->vx *= soak; a->vz *= soak;
        float gk = sqrtf(a->leap ? 1.0f : CRET_AIR_GRAVITY);
        float safe = (10.0f + 3.0f * (1.0f - clampf(a->height / 4.0f, 0, 1))) * gk;
        if (impact > safe && !predict) {
            float sev = (impact - safe) / (6.0f * gk);
            float d = fminf(a->max_health * 0.25f * sev, a->health);
            a->health -= d; a->ev_taken += d / fmaxf(a->max_health, 1); a->hurt_ticks = 12;
            if (sev > 0.3f) break_bone(a, 40.0f + 40.0f * clampf(sev, 0, 1));
            if (a->health <= 0.01f) { kill(s, i); return; }
        }
    }
    float moved = sqrtf((a->x - ox) * (a->x - ox) + (a->z - oz) * (a->z - oz));
    a->distance += moved; a->speed = moved / dt;
    b3Body_SetTransform(a->body, (b3Pos){a->x, a->y + a->height * 0.5f, a->z}, b3Quat_identity);
}

// Weather: spells of fair sky, overcast and rain, each a few minutes to a
// quarter hour long, with the cover easing between them over a few minutes.
// Its own random stream, so weather never changes what the animals do.
static void weather_step(CretSim* s) {
    if (!s->cfg.weather) { s->cloud_cover = s->cfg.cloud_cover; return; }
    if (--s->weather_ticks <= 0) {
        float r = frand(&s->weather_rng), lo, hi;
        if (r < 0.2f) { lo = 0.90f; hi = 0.98f; }        // rain
        else if (r < 0.5f) { lo = 0.62f; hi = 0.84f; }   // overcast
        else { lo = 0.25f; hi = 0.55f; }                 // fair
        s->cloud_target = lo + (hi - lo) * frand(&s->weather_rng);
        s->weather_ticks = (int)((240 + 660 * frand(&s->weather_rng)) * CRET_TICK_HZ);
    }
    s->cloud_cover += (s->cloud_target - s->cloud_cover) * (CRET_DT / 150.0f);
}

void cret_sim_step(CretSim* s, const CretInput* inputs) {
    for (int i = 0; i < s->cfg.num_agents; i++) {
        CretAgent* a = &s->agents[i]; a->ev_food = a->ev_water = a->ev_dealt = a->ev_taken = 0; a->ev_died = a->ev_kills = a->ev_quest = 0;
        if (!a->alive && ++a->dead_ticks >= s->cfg.respawn_ticks) cret_sim_respawn(s, i);
    }
    // Prop colliders near every agent first, so movement this tick sees them.
    for (int i = 0; i < s->cfg.num_agents; i++)
        if (s->agents[i].alive) cret_prop_stream_touch(&s->props, s->world, s->phys, s->agents[i].x, s->agents[i].z, 40.0f, s->tick);
    if (s->tick % 64 == 0) cret_prop_stream_trim(&s->props, s->tick, 300);
    for (int i = 0; i < s->cfg.num_agents; i++) {
        if (!s->agents[i].alive || s->hold[i]) continue;   // held: a network player's input has not arrived
        CretInput in = inputs[i];
        cret_status_input(s, i, &in);   // stunned, latched on, abilities
        if (!s->agents[i].alive) continue;
        if (!s->agents[i].latch) move_agent(s, i, &in, false);   // a latched raptor rides its victim (cret_status_step)
        s->agents[i].ticks_alive++;
    }
    dyn_step(s);   // movable props: physics while any is moving, crush damage
    cret_status_step(s);
    for (int i = 0; i < s->cfg.num_agents; i++) if (s->agents[i].alive) survive(s, i);
    cret_progress_step(s);
    cret_critters_step(s);   // small prey around people (critters.c)
    // Carcasses rot (meat spoils at 0.3% a second) and vanish when eaten or old.
    for (int c = 0; c < CRET_MAX_CARCASSES; c++) {
        CretCarcass* k = &s->carcasses[c]; if (!k->active) continue;
        k->age++; k->meat -= k->max_meat * 0.003f * CRET_DT;
        if (k->meat <= 0 || k->age > s->cfg.carcass_seconds * CRET_TICK_HZ) k->active = false;
    }
    s->tick++;
    s->time_of_day += CRET_DT / s->cfg.day_seconds;
    if (s->time_of_day >= 1) s->time_of_day -= 1;
    weather_step(s);
}

void cret_sim_move(CretSim* s, int i, const CretInput* in, bool predict) {
    CretAgent* a = &s->agents[i];
    if (i < 0 || i >= s->cfg.num_agents || !a->alive) return;
    cret_prop_stream_touch(&s->props, s->world, s->phys, a->x, a->z, 40.0f, s->tick);
    CretInput q = *in;
    if (!predict) cret_status_input(s, i, &q);   // as cret_sim_step: stun, latch, abilities
    else {   // prediction: the same gating, but abilities and letting go are the server's
        if (a->latch || a->st[CRET_ST_STUN] > 0) q = (CretInput){0};
        q.ability = 0;
    }
    if (!a->alive) return;
    if (!a->latch) move_agent(s, i, &q, predict);
    if (!a->alive) return;
    a->ticks_alive++; agent_timers(a);
    if (predict) {   // the status and cooldown timers cret_status_step runs down (movement reads slow, burst, defensive)
        for (int k = 0; k < CRET_ST_COUNT; k++) if (k != CRET_ST_LATCHED && k != CRET_ST_LATCHING) a->st[k] = fmaxf(a->st[k] - CRET_DT, 0);
        for (int k = 0; k < CRET_AB_SLOTS; k++) a->ab_cd[k] = fmaxf(a->ab_cd[k] - CRET_DT, 0);
        if (a->st[CRET_ST_SLOW] <= 0) a->slow_amt = 0;
        if (a->st[CRET_ST_DEFENSIVE] <= 0) a->def_speed = 1;
    }
}

float cret_ground_at(const CretSim* s, float x, float z, float y_top, float normal[3]) {
    const CretWorld* w = s->world; float g = cret_height(w, x, z);
    if (normal) {   // the drawn (cubic) surface's normal
        const float e = 0.25f;
        float hx = cret_height(w, x + e, z) - cret_height(w, x - e, z), hz = cret_height(w, x, z + e) - cret_height(w, x, z - e);
        float n[3] = {-hx, 2 * e, -hz}, l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (int k = 0; k < 3; k++) normal[k] = n[k] / l;
    }
    if (!(y_top > g) || !b3World_IsValid(s->phys)) return g;
    b3QueryFilter f = {.categoryBits = CAT_AGENT, .maskBits = CAT_PROP, .id = 2, .name = "cret_ground"};
    b3RayResult r = b3World_CastRayClosest(s->phys, (b3Pos){x, y_top, z}, (b3Vec3){0, g - y_top, 0}, f);
    if (!r.hit || (float)r.point.y <= g) return g;
    if (normal) { normal[0] = r.normal.x; normal[1] = r.normal.y; normal[2] = r.normal.z; }
    return (float)r.point.y;
}

void cret_sim_place(CretSim* s, int i) {
    CretAgent* a = &s->agents[i];
    if (i < 0 || i >= s->cfg.num_agents || !b3Body_IsValid(a->body)) return;
    if (!a->alive) { b3Body_SetTransform(a->body, (b3Pos){0, -1000.0f - i * 10, 0}, b3Quat_identity); return; }
    if (b3Shape_IsValid(a->shape) && fabsf(b3Shape_GetCapsule(a->shape).radius - a->radius) > 0.01f) update_shape(a);   // species or growth changed
    b3Body_SetTransform(a->body, (b3Pos){a->x, a->y + a->height * 0.5f, a->z}, b3Quat_identity);
}

CretInput cret_decode_action(const float* act) {
    static const float throttle[5] = {-0.5f, 0, 0.5f, 1, 1}, turn[5] = {-1, -0.4f, 0, 0.4f, 1}, strafe[3] = {-1, 0, 1}, aim[5] = {-1, -0.5f, 0, 0.5f, 1};
    // Eat turns into drink facing water; 4..6 use ability slots 1..3; 7 is the secondary attack.
    static const uint8_t action[8] = {CRET_ACT_NONE, CRET_ACT_ATTACK, CRET_ACT_EAT, CRET_ACT_CALL, CRET_ACT_NONE, CRET_ACT_NONE, CRET_ACT_NONE, CRET_ACT_ATTACK2};
    int t = (int)act[0], r = (int)act[1], st = (int)act[2], j = (int)act[3], ac = (int)act[4], am = (int)act[5];
    t = t < 0 ? 0 : t > 4 ? 4 : t; r = r < 0 ? 0 : r > 4 ? 4 : r; st = st < 0 ? 0 : st > 2 ? 2 : st; ac = ac < 0 ? 0 : ac > 7 ? 7 : ac; am = am < 0 ? 0 : am > 4 ? 4 : am;
    return (CretInput){.forward = throttle[t], .turn = turn[r], .strafe = strafe[st], .sprint = t == 4, .jump = j == 1, .action = action[ac],
                       .ability = (uint8_t)(ac >= 4 && ac <= 6 ? ac - 3 : 0), .aim = aim[am]};
}

void cret_sim_observe(const CretSim* s, int i, float* o) {
    const CretAgent* a = &s->agents[i]; const CretWorld* w = s->world; float S = w->size;
    memset(o, 0, sizeof(float) * CRET_OBS_SIZE);
    float cy = cosf(a->yaw), sy = sinf(a->yaw);
    float g = cret_height(w, a->x, a->z), wl = cret_water(w, a->x, a->z);
    float n[3]; terra_normal(&w->map, a->x, a->z, n);
    int k = 0;
    o[k++] = cy; o[k++] = sy;
    o[k++] = (cy * a->vx + sy * a->vz) / 10; o[k++] = (-sy * a->vx + cy * a->vz) / 10; o[k++] = a->vy / 10;
    o[k++] = a->x / S * 2 - 1; o[k++] = a->z / S * 2 - 1; o[k++] = g / 500;
    o[k++] = clampf((wl - g) / 5, -1, 2); o[k++] = a->swimming; o[k++] = a->on_ground; o[k++] = a->stamina;
    o[k++] = n[0]; o[k++] = n[2];
    o[k++] = sinf(6.2831853f * s->time_of_day); o[k++] = cosf(6.2831853f * s->time_of_day);
    // Survival: species, alive, health, hunger, thirst, growth, bleeding, attack ready, forage here, nearest carcass.
    for (int q = 0; q < CRET_SPECIES; q++) o[k++] = a->species == q;
    o[k++] = a->alive; o[k++] = a->max_health > 0 ? a->health / a->max_health : 0; o[k++] = a->food; o[k++] = a->water; o[k++] = a->growth;
    o[k++] = clampf(a->bleed / fmaxf(a->max_health, 1) * 100, 0, 2); o[k++] = a->attack_cd == 0; o[k++] = cret_forage(w, a->x, a->z);
    {
        int best = -1; float bd = 60.0f * 60.0f;
        for (int c = 0; c < CRET_MAX_CARCASSES; c++) {
            const CretCarcass* cc = &s->carcasses[c]; if (!cc->active || cc->meat <= 0) continue;
            float dx = cc->x - a->x, dz = cc->z - a->z, d = dx * dx + dz * dz; if (d < bd) { bd = d; best = c; }
        }
        if (best >= 0) {
            float dx = s->carcasses[best].x - a->x, dz = s->carcasses[best].z - a->z;
            o[k++] = 1; o[k++] = (cy * dx + sy * dz) / 60; o[k++] = (-sy * dx + cy * dz) / 60;
        } else k += 3;
    }
    o[k++] = (float)a->hurt_ticks / 12.0f;
    // Contract 3: broken bones, group size, rally point (egocentric).
    o[k++] = clampf(a->broken / 60.0f, 0, 1.5f); o[k++] = cret_group_size(s, a->group) / 8.0f;
    if (a->rally_ticks > 0) {
        float dx = a->rally_x - a->x, dz = a->rally_z - a->z;
        o[k++] = 1; o[k++] = clampf((cy * dx + sy * dz) / 100, -2, 2); o[k++] = clampf((-sy * dx + cy * dz) / 100, -2, 2);
    } else k += 3;
    int half = CRET_PATCH / 2;
    for (int pz = -half; pz <= half; pz++) for (int px = -half; px <= half; px++) {
        float lx = px * CRET_PATCH_SPACING, lz = pz * CRET_PATCH_SPACING;
        float wx = a->x + cy * lz - sy * lx, wz = a->z + sy * lz + cy * lx;
        float h = cret_height(w, wx, wz), wa = cret_water(w, wx, wz);
        o[k++] = clampf((h - g) / 20, -2, 2); o[k++] = clampf((wa - h) / 3, -1, 2);
    }
    // Nearest agents, egocentric.
    int best[CRET_NEIGHBORS]; float bd[CRET_NEIGHBORS];
    for (int q = 0; q < CRET_NEIGHBORS; q++) { best[q] = -1; bd[q] = 1e30f; }
    for (int j = 0; j < s->cfg.num_agents; j++) {
        if (j == i || !s->agents[j].alive) continue;
        float dx = s->agents[j].x - a->x, dz = s->agents[j].z - a->z, d = dx * dx + dz * dz;
        for (int q = 0; q < CRET_NEIGHBORS; q++) if (d < bd[q]) {
            for (int r = CRET_NEIGHBORS - 1; r > q; r--) { bd[r] = bd[r - 1]; best[r] = best[r - 1]; }
            bd[q] = d; best[q] = j; break;
        }
    }
    for (int q = 0; q < CRET_NEIGHBORS; q++) {
        if (best[q] < 0) { k += CRET_NEIGHBOR_OBS; continue; }
        const CretAgent* b = &s->agents[best[q]]; float dx = b->x - a->x, dz = b->z - a->z;
        o[k++] = 1; o[k++] = clampf((cy * dx + sy * dz) / 100, -2, 2); o[k++] = clampf((-sy * dx + cy * dz) / 100, -2, 2);
        o[k++] = clampf((b->y - a->y) / 20, -2, 2); o[k++] = cosf(b->yaw - a->yaw);
        float ma = CRET_SPECIES_TABLE[a->species].mass * powf(cret_growth_scale(a->growth), 3), mb = CRET_SPECIES_TABLE[b->species].mass * powf(cret_growth_scale(b->growth), 3);
        o[k++] = CRET_SPECIES_TABLE[b->species].diet == CRET_DIET_CARNIVORE; o[k++] = clampf(logf(mb / ma) / 3, -2, 2);
        o[k++] = b->max_health > 0 ? b->health / b->max_health : 0;
        o[k++] = a->group && b->group == a->group; o[k++] = b->species == a->species;
    }
    // Contract 4 (appended): status effects, abilities, quest and nest, then per neighbour.
    o[k++] = clampf(a->st[CRET_ST_STUN] / 2, 0, 1); o[k++] = clampf(a->st[CRET_ST_FEAR] / 6, 0, 1); o[k++] = a->st[CRET_ST_SLOW] > 0 ? a->slow_amt : 0;
    o[k++] = fminf(a->latchers, 3) / 3.0f; o[k++] = a->latch ? clampf(a->st[CRET_ST_LATCHING] / 6, 0.05f, 1) : 0;
    o[k++] = a->st[CRET_ST_DEFENSIVE] > 0 ? a->def_armor + a->def_flank : 0; o[k++] = a->st[CRET_ST_MARKED] > 0;
    o[k++] = clampf(a->st[CRET_ST_BURST] / 4, 0, 1); o[k++] = a->st[CRET_ST_EVADE] > 0;
    for (int q = 0; q < CRET_AB_SLOTS; q++) {
        int ab = cret_ability_of(s, i, q);
        o[k++] = ab ? clampf(a->ab_cd[q] / fmaxf(CRET_ABILITIES[ab].cooldown, 1), 0, 1) : 1;
    }
    for (int q = 0; q < CRET_AB_SLOTS; q++) o[k++] = cret_ability_ready(s, i, q);
    {
        const CretQuest* qs = cret_quest_of(s, i, 0); if (!qs) qs = cret_quest_of(s, i, 1);
        float tx, tz;
        if (qs) {
            o[k++] = 1;
            if (cret_quest_target(s, qs, &tx, &tz, NULL)) {
                float dx = tx - a->x, dz = tz - a->z;
                o[k++] = clampf((cy * dx + sy * dz) / 1000, -2, 2); o[k++] = clampf((-sy * dx + cy * dz) / 1000, -2, 2);
            } else k += 2;
            o[k++] = clampf(qs->progress / fmaxf(qs->goal, 1e-3f), 0, 1);
        } else k += 4;
        int nk = cret_nest_of(s, i);
        if (nk >= 0) {
            float dx = s->nests[nk].x - a->x, dz = s->nests[nk].z - a->z;
            o[k++] = 1; o[k++] = clampf((cy * dx + sy * dz) / 300, -2, 2); o[k++] = clampf((-sy * dx + cy * dz) / 300, -2, 2);
        } else k += 3;
    }
    for (int q = 0; q < CRET_NEIGHBORS; q++) {
        if (best[q] < 0) { k += CRET_NEIGHBOR_OBS4; continue; }
        const CretAgent* b = &s->agents[best[q]];
        o[k++] = b->st[CRET_ST_STUN] > 0; o[k++] = b->st[CRET_ST_FEAR] > 0; o[k++] = b->latchers > 0 || b->latch; o[k++] = b->st[CRET_ST_MARKED] > 0;
    }
    if (k != CRET_OBS_SIZE) { fprintf(stderr, "cretaceous: observation layout %d != %d\n", k, CRET_OBS_SIZE); abort(); }
}

// Scripted bots until policies take over, using the same mechanics:
//   - herbivores graze where plants are thick, drink when thirsty and keep to
//     their herd; a herd flees as one (an alarmed animal sets off the members
//     near it). Defences by species: a stegosaur turns its tail to a predator
//     and swings, a pachycephalosaur faces a predator its size and headbutts,
//     a sauropelta stands its ground and trusts its armour, a gallimimus runs;
//   - predators hunt when hungry. Raptors hunt as a pack: the one that finds
//     prey calls, packmates in earshot join, and they come at it from
//     different sides; a pack takes bigger prey than a lone raptor. They bite
//     in reach, eat the carcass, drink and rest;
//   - a call from a group member draws the others to the caller.
//   - abilities when the moment fits (bot_abilities); frightened animals run
//     from the roar, a herd runs early from predators its alert call marked,
//     and an animal with raptors on its back bucks to shake them off;
//   - fed adults in a group sometimes build a nest; hungry predators raid
//     unguarded nests for eggs; idle wandering drifts toward quest places.
//   - reactions take time (bot_perceive): a bot notices a new threat or a
//     hit after a delay that grows with its mass (about 0.2 s for a raptor,
//     0.75 s for a tyrannosaur), and its head (a stegosaur's tail) turns to
//     aim at a limited speed that falls with mass (bot_aim), so a quick
//     animal can bite a big one's flank and get clear before it turns. The
//     aim moves at that speed through an attack's wind-up too, so a swing,
//     once started, cannot snap onto a target that slipped aside.
//   - a hit leaves the bot alert to its attacker for 20-60 s. A herbivore
//     keeps defending against it, or keeps away from it with its herd, rather
//     than grazing beside it. A predator turns on an attacker it can take
//     (can_take) and chases it, and backs off from one it cannot. The alert
//     runs down faster with the attacker far off and ends once it is 120 m
//     away or dead.
enum { BOT_WANDER, BOT_GRAZE, BOT_FLEE, BOT_HUNT, BOT_REST, BOT_DRINK, BOT_SEEK_WATER, BOT_FEED, BOT_DEFEND, BOT_NEST, BOT_RAID };

static bool walkable(const CretWorld* w, float x, float z) {
    float S = w->size; if (x < 30 || z < 30 || x > S - 30 || z > S - 30) return false;
    float h = cret_height(w, x, z), wl = cret_water(w, x, z); float n[3]; terra_normal(&w->map, x, z, n);
    return h > 1.0f && wl < h + 0.3f && n[1] > 0.8f;
}
bool cret_walkable(const CretWorld* w, float x, float z) { return walkable(w, x, z); }
// The quest place a bot heads for: its group's quest, else the quest of the
// group's first member (so a herd travels together), else its own.
static bool quest_goal(const CretSim* s, int i, float* x, float* z) {
    const CretAgent* a = &s->agents[i];
    if (a->group) {
        if (cret_quest_target(s, cret_quest_of(s, i, 1), x, z, NULL)) return true;
        for (int j = 0; j < s->cfg.num_agents; j++)
            if (s->agents[j].alive && s->agents[j].group == a->group) return cret_quest_target(s, cret_quest_of(s, j, 0), x, z, NULL);
    }
    return cret_quest_target(s, cret_quest_of(s, i, 0), x, z, NULL);
}
// Standing in the place of a drinking quest.
static bool at_drink_quest(const CretSim* s, int i) {
    const CretQuest* q = cret_quest_of(s, i, 0); float x, z, r;
    if (!q || q->kind != CRET_Q_DRINK || !cret_quest_target(s, q, &x, &z, &r)) return false;
    float dx = x - s->agents[i].x, dz = z - s->agents[i].z; return dx * dx + dz * dz < r * r;
}
static float mass_of(const CretAgent* a) { return CRET_SPECIES_TABLE[a->species].mass * powf(cret_growth_scale(a->growth), 3); }
// Next wander goal. Herbivores that strayed head back to their herd;
// predators often roam toward the herbivores.
static void pick_goal(CretSim* s, CretAgent* a, float lo, float hi) {
    int i = (int)(a - s->agents); float hx, hz, base = a->yaw, ox = a->x, oz = a->z;
    bool herb = CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_HERBIVORE;
    // Pack predators drift toward their pack.
    bool pack = false; float px = 0, pz = 0;
    if (!herb && a->group) {
        int n = 0;
        for (int j = 0; j < s->cfg.num_agents; j++) {
            const CretAgent* b = &s->agents[j]; if (j == i || !b->alive || b->group != a->group) continue;
            px += b->x; pz += b->z; n++;
        }
        if (n) { px /= n; pz /= n; pack = true; }
    }
    bool herd = herb ? own_herd(s, i, &hx, &hz) : herd_center(s, i, &hx, &hz);
    // A fed bot with a quest travels toward it in longer legs, measured from
    // its herd or pack centre so the group moves on as one.
    float qx, qz;
    if (quest_goal(s, i, &qx, &qz) && a->food > 0.5f && a->water > 0.45f && frand(&s->rng) < 0.85f) {
        if (herb && herd) { ox = hx; oz = hz; } else if (pack) { ox = px; oz = pz; }
        float dx = qx - ox, dz = qz - oz;
        lo = hi * 0.8f; hi *= 2;
        if (dx * dx + dz * dz < hi * hi * 2.25f)   // the quest place is near: go right there
            for (int t = 0; t < 6; t++) {
                float gx = qx + (frand(&s->rng) - 0.5f) * 16, gz = qz + (frand(&s->rng) - 0.5f) * 16;
                if (walkable(s->world, gx, gz)) { a->goal_x = gx; a->goal_z = gz; return; }
            }
        base = atan2f(dz, dx);
        for (int t = 0; t < 12; t++) {
            float ang = base + (frand(&s->rng) - 0.5f) * (t < 8 ? 0.6f : 1.5f), d = lo + (hi - lo) * frand(&s->rng);
            float gx = ox + cosf(ang) * d, gz = oz + sinf(ang) * d;
            if (walkable(s->world, gx, gz)) { a->goal_x = gx; a->goal_z = gz; return; }
        }
        ox = a->x; oz = a->z; lo *= 0.5f; hi *= 0.5f;   // blocked that way: an ordinary leg
    }
    // Herbivores that strayed head back to their herd; predators often roam toward the herbivores.
    if (herd) {
        float dx = hx - a->x, dz = hz - a->z, d = sqrtf(dx * dx + dz * dz);
        if ((herb && d > 45.0f) || (!herb && a->food < 0.8f && d > 100.0f && frand(&s->rng) < 0.6f)) base = atan2f(dz, dx);
    }
    if (pack) { float dx = px - a->x, dz = pz - a->z; if (dx * dx + dz * dz > 40.0f * 40.0f) base = atan2f(dz, dx); }
    for (int t = 0; t < 16; t++) {
        float ang = base + (frand(&s->rng) - 0.5f) * (t < 8 ? 1.5f : 3.0f), d = lo + (hi - lo) * frand(&s->rng);
        float gx = ox + cosf(ang) * d, gz = oz + sinf(ang) * d;
        if (!walkable(s->world, gx, gz)) continue;
        if (herb && t < 10 && cret_forage(s->world, gx, gz) < 0.25f) continue;   // grazers head for thick plants
        a->goal_x = gx; a->goal_z = gz; return;
    }
    a->goal_x = a->x - cosf(a->yaw) * lo; a->goal_z = a->z - sinf(a->yaw) * lo;   // turn back
}
// Feel ahead for trunks and rocks: cast the body (above knee height, so
// stones it kicks aside do not count) a few metres along the heading to the
// goal; when blocked, the nearest clear heading within 80 degrees, on the side
// it already favours. Returns the offset from the goal heading (rad).
static float avoid_props(const CretSim* s, const CretAgent* a, float want, float dist) {
    if (!b3World_IsValid(s->phys) || a->swimming || !a->on_ground) return 0;
    float sp = sqrtf(a->vx * a->vx + a->vz * a->vz), look = fminf(dist, 2.0f + a->radius * 2 + sp * 0.7f);
    if (look < 0.8f) return 0;
    float lo = fmaxf(a->radius, 0.45f) + 0.1f, hi = fmaxf(a->height - a->radius, lo + 0.05f);
    b3Capsule cap = {{0, lo, 0}, {0, hi, 0}, a->radius * 0.9f};
    b3QueryFilter f = {.categoryBits = CAT_AGENT, .maskBits = CAT_PROP, .id = 3, .name = "cret_feel"};
    b3Pos o = {a->x, a->y + 0.05f, a->z};
    float side = a->avoid < 0 ? -1.0f : a->avoid > 0 ? 1.0f : ((a - s->agents) & 1 ? 1.0f : -1.0f);
    static const float OFF[] = {0, 0.35f, -0.35f, 0.7f, -0.7f, 1.05f, -1.05f, 1.4f, -1.4f};
    for (int k = 0; k < 9; k++) {
        float h = want + OFF[k] * side;
        if (b3World_CastMover(s->phys, o, &cap, (b3Vec3){cosf(h) * look, 0, sinf(h) * look}, f, NULL, NULL) >= 0.999f) return OFF[k] * side;
    }
    return a->avoid;   // boxed in: keep the last choice (the stuck escape takes over)
}
// Scripted movers (scenario controllers, hostile actors): the heading to take
// toward `want` (rad) around trunks and rocks, the way the bots steer.
float cret_avoid_heading(CretSim* s, int i, float want, float dist) {
    CretAgent* a = &s->agents[i];
    if ((i + s->tick) % 4 == 0) a->avoid = avoid_props(s, a, want, dist);
    return want + a->avoid;
}
static float steer(const CretSim* s, CretAgent* a, float tx, float tz, float* dist) {
    float dx = tx - a->x, dz = tz - a->z; *dist = sqrtf(dx * dx + dz * dz);
    float want = atan2f(dz, dx);
    if (((a - s->agents) + s->tick) % 4 == 0) a->avoid = avoid_props(s, a, want, *dist);   // every 4th tick, staggered
    want += a->avoid;
    float d = want - a->yaw;
    while (d > 3.14159265f) d -= 6.2831853f; while (d < -3.14159265f) d += 6.2831853f;
    // Feel ahead: deep water or a cliff turns the bot along it.
    float cy = cosf(a->yaw), sy = sinf(a->yaw), L = 6 + a->radius * 4;
    if (!walkable(s->world, a->x + cy * L, a->z + sy * L) && walkable(s->world, a->x, a->z)) d += d >= 0 ? 1.2f : -1.2f;
    return clampf(d * 2.0f, -1, 1);
}
static int nearest_of(const CretSim* s, int i, int diet, float* dist) {
    const CretAgent* a = &s->agents[i]; int best = -1; float bd = 1e30f;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (j == i || !b->alive || CRET_SPECIES_TABLE[b->species].diet != diet) continue;
        float dx = b->x - a->x, dz = b->z - a->z, d = dx * dx + dz * dz;
        if (d < bd) { bd = d; best = j; }
    }
    *dist = sqrtf(bd); return best;
}

// Nearest water edge: rings of samples out to 700 m; goal_x/z is the dry
// point just before the water, the water point is returned in wx, wz.
static bool find_water(CretSim* s, CretAgent* a, float* wx, float* wz) {
    const CretWorld* w = s->world;
    for (float r = 12; r <= 700; r += r < 200 ? 12 : 30) {
        int n = (int)(r * 0.4f) + 8; float off = frand(&s->rng) * 6.2831853f;
        for (int q = 0; q < n; q++) {
            float ang = off + q * 6.2831853f / n, px = a->x + cosf(ang) * r, pz = a->z + sinf(ang) * r;
            if (px < 30 || pz < 30 || px > w->size - 30 || pz > w->size - 30) continue;
            if (cret_water(w, px, pz) < cret_height(w, px, pz) + 0.3f) continue;
            float bx = a->x + cosf(ang) * (r - 6), bz = a->z + sinf(ang) * (r - 6);
            if (!walkable(w, bx, bz)) continue;
            a->goal_x = bx; a->goal_z = bz; *wx = px; *wz = pz; return true;
        }
    }
    return false;
}
static int nearest_carcass(const CretSim* s, const CretAgent* a, float range) {
    int best = -1; float bd = range * range;
    for (int c = 0; c < CRET_MAX_CARCASSES; c++) {
        const CretCarcass* k = &s->carcasses[c]; if (!k->active || k->meat < k->max_meat * 0.05f) continue;
        float dx = k->x - a->x, dz = k->z - a->z, d = dx * dx + dz * dz; if (d < bd) { bd = d; best = c; }
    }
    return best;
}
// A nest with eggs within 150 m that is not the predator's own and has no
// grown guard of its kind within 20 m (-1: none).
static int raid_target(const CretSim* s, int i) {
    const CretAgent* a = &s->agents[i];
    int k = cret_nest_near(s, a->x, a->z, 150.0f, a->group);
    if (k < 0 || s->nests[k].owner == i + 1) return -1;
    const CretNest* n = &s->nests[k];
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (!b->alive || b->species != n->species || b->growth < 0.6f || !(j + 1 == n->owner || (n->group && b->group == n->group))) continue;
        float dx = b->x - n->x, dz = b->z - n->z; if (dx * dx + dz * dz < 20.0f * 20.0f) return -1;
    }
    return k;
}

// Packmates of i that could join a hunt: alive, healthy, not busy, within `range`.
static int pack_near(const CretSim* s, int i, float range) {
    const CretAgent* a = &s->agents[i]; int n = 0;
    if (!a->group) return 0;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (j == i || !b->alive || b->group != a->group) continue;
        float dx = b->x - a->x, dz = b->z - a->z; if (dx * dx + dz * dz < range * range) n++;
    }
    return n;
}
// Prey a predator takes on: herbivores up to 10x its mass per pack member in
// earshot (a lone raptor takes a pachycephalosaur, a gallimimus or a young
// stegosaur; a pack of four a sub-adult stegosaur). Armour puts raptors off
// a sauropelta unless it is young; a tyrannosaur takes anything. Weak and
// young animals are preferred.
static int pick_prey(const CretSim* s, int i, float range, float* dist) {
    const CretAgent* a = &s->agents[i]; float ma = mass_of(a); bool trex = a->species == CRET_SP_TREX;
    int pack = 1 + pack_near(s, i, 150.0f);
    int best = -1; float bs = 1e30f;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (j == i || !b->alive || CRET_SPECIES_TABLE[b->species].diet != CRET_DIET_HERBIVORE) continue;
        float mb = mass_of(b);
        if (!trex && mb > 10 * ma * pack) continue;
        if (!trex && CRET_SPECIES_TABLE[b->species].armor > 0.4f && b->growth > 0.6f) continue;
        float dx = b->x - a->x, dz = b->z - a->z, d = sqrtf(dx * dx + dz * dz); if (d > range) continue;
        // Prefer weak, young and slow prey: a gallimimus outruns a raptor in the open.
        float outrun = fmaxf(b->sprint_speed - a->sprint_speed, 0);
        if (outrun > 0 && b->broken <= 0 && b->growth > 0.5f && b->health > 0.6f * b->max_health) continue;   // cannot be caught
        float score = d * (0.5f + (b->max_health > 0 ? b->health / b->max_health : 1)) * (0.6f + b->growth) * (1 + 0.5f * outrun);
        if (j == a->bot_target && a->bot_state == BOT_REST) score *= 3;   // just lost it
        if (score < bs) { bs = score; best = j; }
    }
    if (best >= 0) { float dx = s->agents[best].x - a->x, dz = s->agents[best].z - a->z; *dist = sqrtf(dx * dx + dz * dz); }
    return best;
}
static void bot_set(CretAgent* a, int state, int timer) { a->bot_state = state; a->bot_timer = timer; }

// Could a herbivore stand and fight predator p (else it runs)?
static bool can_defend(const CretSim* s, const CretAgent* a, const CretAgent* p) {
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    float health = a->health / fmaxf(a->max_health, 1);
    if (health < 0.3f) return false;
    if (S->tail) return true;                                   // stegosaur: tail
    if (S->armor > 0.4f) return true;                           // sauropelta: armour (it cannot outrun anything anyway)
    if (S->knock > 1.5f) {                                      // pachycephalosaur: headbutts its own size, alone
        int n = 0;
        for (int j = 0; j < s->cfg.num_agents; j++) {
            const CretAgent* c = &s->agents[j];
            if (!c->alive || CRET_SPECIES_TABLE[c->species].diet != CRET_DIET_CARNIVORE) continue;
            float dx = c->x - a->x, dz = c->z - a->z; n += dx * dx + dz * dz < 18.0f * 18.0f;
        }
        return n < 2 && mass_of(p) < 2.5f * mass_of(a);
    }
    return false;                                               // gallimimus: runs
}

// Could predator i take on animal b that hurt it (else it backs off)? Prey
// it would hunt (pick_prey's rule: up to 10x its mass per pack member in
// earshot, no grown sauropelta for raptors), or any other animal up to 1.5
// times its mass; a tyrannosaur takes anything.
static bool can_take(const CretSim* s, int i, const CretAgent* b) {
    const CretAgent* a = &s->agents[i]; float ma = mass_of(a), mb = mass_of(b);
    if (a->species == CRET_SP_TREX) return true;
    if (CRET_SPECIES_TABLE[b->species].diet == CRET_DIET_HERBIVORE)
        return mb <= 10 * ma * (1 + pack_near(s, i, 150.0f)) && !(CRET_SPECIES_TABLE[b->species].armor > 0.4f && b->growth > 0.6f);
    return mb < 1.5f * ma;
}

// Reaction delay in ticks before a bot notices a new threat or a hit. It
// grows with the log of the mass: about 0.2 s for a raptor, 0.4 s for a
// pachycephalosaur or gallimimus, 0.55 s for a sauropelta, 0.7 s for a
// stegosaur and 0.75 s for a tyrannosaur (juveniles are lighter and
// quicker), give or take 15% each time from the sim RNG.
static int bot_react_ticks(CretSim* s, const CretAgent* a) {
    float k = clampf(logf(fmaxf(mass_of(a), 1.0f) / 80.0f) / logf(100.0f), 0, 1);
    float sec = (0.2f + 0.55f * k) * (0.85f + 0.3f * frand(&s->rng));
    return (int)(sec * CRET_TICK_HZ + 0.5f) > 1 ? (int)(sec * CRET_TICK_HZ + 0.5f) : 1;
}
// Starts noticing agent j (a threat in view, or the one that just hit it).
// A cue already waiting keeps its time; a hit takes over from a threat that
// is still being noticed, without waiting longer.
static void bot_cue(CretSim* s, CretAgent* a, int j, bool hit) {
    if (a->bot_cue == j + 1) { a->bot_cue_hit |= hit; return; }
    if (a->bot_cue && !hit) return;
    int d = bot_react_ticks(s, a);
    a->bot_react = a->bot_cue && a->bot_react < d ? a->bot_react : d;
    a->bot_cue = j + 1; a->bot_cue_hit = hit;
}
// Hurt by agent j and aware of it: alert to it for 20-60 s (a fresh hit
// can only lengthen the alert).
static void bot_alarm(CretSim* s, CretAgent* a, int j) {
    int t = (int)((20.0f + 40.0f * frand(&s->rng)) * CRET_TICK_HZ);
    if (a->bot_foe != j + 1 || t > a->bot_alert) a->bot_alert = t;
    a->bot_foe = j + 1; a->bot_seen = j + 1;
}
static float agent_dist(const CretAgent* a, const CretAgent* b) { return hypotf(b->x - a->x, b->z - a->z); }
// Perception, once per bot tick. A hit is a rise of hurt_ticks (damage()
// sets it) with an attacker close by; the bot notices it, or a threat in
// view (cret_bot_input), once the reaction delay has run out. An attacker
// it is already alert to is noticed at once. The alert runs down, three
// times as fast with the attacker more than 60 m off, and ends once it is
// 120 m away or dead. A noticed threat is forgotten past 80 m.
static void bot_perceive(CretSim* s, int i) {
    CretAgent* a = &s->agents[i]; int n = s->cfg.num_agents;
    float L = CRET_SPECIES_TABLE[a->species].length * cret_growth_scale(a->growth);
    int by = a->last_attacker;
    if (a->hurt_ticks > a->bot_hurt && by >= 0 && by < n && by != i && s->agents[by].alive &&
        agent_dist(a, &s->agents[by]) < 20.0f + L + CRET_SPECIES_TABLE[s->agents[by].species].length) {
        if (a->bot_alert > 0 && a->bot_foe == by + 1) bot_alarm(s, a, by);
        else bot_cue(s, a, by, true);
    }
    a->bot_hurt = a->hurt_ticks;
    if (a->bot_react > 0 && --a->bot_react == 0) {
        int c = a->bot_cue - 1;
        if (c >= 0 && c < n && s->agents[c].alive) { a->bot_seen = a->bot_cue; if (a->bot_cue_hit) bot_alarm(s, a, c); }
        a->bot_cue = 0; a->bot_cue_hit = 0;
    }
    if (a->bot_alert > 0) {
        int f = a->bot_foe - 1; float d = f >= 0 && f < n ? agent_dist(a, &s->agents[f]) : 1e9f;
        if (f < 0 || f >= n || !s->agents[f].alive || d > 120.0f) a->bot_alert = 0;
        else a->bot_alert -= d > 60.0f + L ? 3 : 1;
        if (a->bot_alert <= 0) { a->bot_alert = 0; a->bot_foe = 0; }
    }
    if (a->bot_seen) {
        int w = a->bot_seen - 1;
        if (w >= n || !s->agents[w].alive || (agent_dist(a, &s->agents[w]) > 80.0f && a->bot_foe != a->bot_seen)) a->bot_seen = 0;
    }
}
// The aim a bot gives this tick on its way to `want`: the head (a
// stegosaur's tail) swings at most 6.5 rad/s for a raptor, about 3.7 for a
// pachycephalosaur or gallimimus, 2.5 for a sauropelta, 1.6 for a
// stegosaur and 1.4 for a tyrannosaur (the cube root of the mass), from
// where it pointed last tick. It never snaps, also not during the wind-up
// of an attack, which lands where the head has got to by the hit tick.
static float bot_aim(const CretAgent* a, float want) {
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    float rate = clampf(6.5f * cbrtf(80.0f / fmaxf(mass_of(a), 1.0f)), 1.0f, 8.0f);
    float step = rate * CRET_DT / fmaxf(S->neck_arc, 0.1f), cur = isfinite(a->aim) ? clampf(a->aim, -1, 1) : 0;
    return cur + clampf(want - cur, -step, step);
}

// Secondary attack: a tail swipe at foes beside or behind that the primary
// cannot reach (or at two or more at once), a raptor's slash in passing at
// a run, a gallimimus's kick at a predator on its heels.
static void bot_attack2(CretSim* s, int i, CretInput* in) {
    CretAgent* a = &s->agents[i]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    if (S->alt.kind == CRET_ALT_NONE || a->attack2_cd > 0 || cret_act_is_attack(a->act) || in->action == CRET_ACT_ATTACK || a->latch || a->swimming) return;
    if (a->bot_state != BOT_HUNT && a->bot_state != BOT_DEFEND && a->bot_state != BOT_FLEE) return;
    bool carn = S->diet == CRET_DIET_CARNIVORE; float g = cret_growth_scale(a->growth);
    int t = a->bot_target; const CretAgent* tb = t >= 0 && t < s->cfg.num_agents && t != i && s->agents[t].alive ? &s->agents[t] : NULL;
    float px, pz; strike_point_aim(a, in->aim, &px, &pz);
    bool primary = tb && a->attack_cd == 0 && body_dist(tb, px, pz) < S->reach * g * 0.9f;
    int n = 0;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* c = &s->agents[j]; float d;
        if (j == i || !c->alive || (a->group && c->group == a->group)) continue;
        if (carn ? j != t : CRET_SPECIES_TABLE[c->species].diet != CRET_DIET_CARNIVORE) continue;
        if (fabsf(c->y - a->y) <= a->height + c->height && alt_hits(a, c, &d)) n++;
    }
    if (!n) return;
    if (S->alt.kind == CRET_ALT_TAIL) { if (n >= 2 || !primary) in->action = CRET_ACT_ATTACK2; }
    else if (a->speed > a->trot_speed * 0.8f && !primary) in->action = CRET_ACT_ATTACK2;
}

// Abilities, at most one a tick, when the moment fits: pounce on prey a few
// metres ahead, sprint after (or away from) it, cripple or crush in reach;
// roar off smaller hunters; stance, hunker or charge when defending; dodge,
// call the alarm, bellow for the herd; mend bleeding when safe.
static void bot_abilities(CretSim* s, int i, CretInput* in) {
    CretAgent* a = &s->agents[i];
    if (a->latch || a->leap || a->st[CRET_ST_STUN] > 0 || in->ability) return;
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth), L = S->length * g, ma = mass_of(a);
    bool carn = S->diet == CRET_DIET_CARNIVORE, busy = cret_act_is_attack(a->act);
    float health = a->health / fmaxf(a->max_health, 1);
    int t = a->bot_state == BOT_HUNT || a->bot_state == BOT_DEFEND || a->bot_state == BOT_FLEE ? a->bot_target : -1;
    const CretAgent* b = t >= 0 && t < s->cfg.num_agents && t != i && s->agents[t].alive ? &s->agents[t] : NULL;
    float dist = 1e9f, gap = 1e9f, ang = 0, reach = S->reach * g;
    if (b) {
        float dx = b->x - a->x, dz = b->z - a->z, px, pz; dist = sqrtf(dx * dx + dz * dz);
        strike_point(a, &px, &pz); gap = body_dist(b, px, pz); ang = wrapa(atan2f(dz, dx) - a->yaw);
    }
    float pd = 1e9f; int p = carn ? -1 : nearest_of(s, i, CRET_DIET_CARNIVORE, &pd);
    bool seen = p >= 0 && a->bot_seen == p + 1;   // a dodge waits until the predator is noticed (bot_perceive)
    bool calm = a->bot_state != BOT_HUNT && a->bot_state != BOT_DEFEND && a->bot_state != BOT_FLEE && a->bot_state != BOT_FEED;
    if (calm && pd > 60 && !a->latchers && a->bleed < 0.004f * a->max_health) return;   // nothing to react to
    int foes_near = 0, small_near = 0, mates_near = 0;   // in reach of the strike point; smaller hostiles within 25 m; group within 40 m
    float sx, sz; strike_point(a, &sx, &sz);
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* c = &s->agents[j];
        if (j == i || !c->alive) continue;
        float dx = c->x - a->x, dz = c->z - a->z, d2 = dx * dx + dz * dz;
        if (a->group && c->group == a->group) { mates_near += d2 < 40.0f * 40.0f; continue; }
        bool hostile = CRET_SPECIES_TABLE[c->species].diet == CRET_DIET_CARNIVORE;
        if (hostile && body_dist(c, sx, sz) < reach * 1.5f) foes_near++;
        if (hostile && d2 < 25.0f * 25.0f && mass_of(c) < 0.6f * ma) small_near++;
    }
    bool hunting = a->bot_state == BOT_HUNT && b, defending = a->bot_state == BOT_DEFEND && b;
    for (int slot = 0; slot < CRET_AB_SLOTS; slot++) {
        if (!cret_ability_ready(s, i, slot)) continue;
        int ab = cret_ability_of(s, i, slot); const CretAbility* A = &CRET_ABILITIES[ab]; bool use = false;
        switch (ab) {
        case CRET_AB_POUNCE: use = hunting && gap > 2.5f && gap < 9.0f && fabsf(ang) < 0.3f && a->on_ground && b->latchers < 2; break;
        case CRET_AB_SPRINT_BURST: use = (hunting && gap > 12 && gap < 45 && b->speed > b->walk_speed) || (a->bot_state == BOT_FLEE && p >= 0 && pd < 20); break;
        case CRET_AB_HAMSTRING: use = hunting && !busy && gap < reach * 0.9f && b->st[CRET_ST_SLOW] <= 0; break;
        case CRET_AB_CRUSH_BITE: use = hunting && !busy && gap < reach * 0.9f && b->health > 0.3f * b->max_health; break;
        case CRET_AB_REND_BITE: use = hunting && !busy && gap < reach * 0.9f; break;
        case CRET_AB_FEAR_ROAR: use = a->latchers > 0 || small_near >= 2 || (a->bot_state == BOT_FEED && small_near >= 1) ||
                                      (hunting && dist < 20 && mass_of(b) < 0.6f * ma && b->bot_state == BOT_DEFEND); break;
        case CRET_AB_TAIL_SLAM: case CRET_AB_SHOVE: use = defending && !busy && gap < reach * A->reach * 0.9f; break;
        case CRET_AB_SPIKE_SWEEP: use = !busy && (foes_near >= 2 || (defending && a->latchers > 0 && gap < reach * A->reach)); break;
        case CRET_AB_DEF_STANCE: use = defending && dist < 14 + L && a->st[CRET_ST_DEFENSIVE] <= 0; break;
        case CRET_AB_CHARGE: use = defending && !busy && gap > 3 && gap < 12 && fabsf(ang) < 0.25f; break;
        case CRET_AB_HERD_BELLOW: use = mates_near >= 1 && p >= 0 && pd < 25 && (defending || a->bot_state == BOT_FLEE) && a->st[CRET_ST_DEFENSIVE] <= 0; break;
        case CRET_AB_HUNKER: use = a->latchers > 0 || (defending && dist < 8 + L && (health < 0.9f || a->hurt_ticks > 0)); break;
        case CRET_AB_DODGE_LEAP: use = a->latchers > 0 || (seen && pd < 4 + 0.5f * L && s->agents[p].speed > 2); break;
        case CRET_AB_ALERT_CALL: use = a->group && p >= 0 && pd < 60 && s->agents[p].st[CRET_ST_MARKED] <= 0; break;
        case CRET_AB_MEND: use = a->bleed > 0.004f * a->max_health && !a->latchers && (carn ? a->bot_state != BOT_HUNT && a->bot_state != BOT_FLEE : pd > 40); break;
        }
        if (!use) continue;
        in->ability = (uint8_t)(slot + 1);
        if (ab == CRET_AB_DODGE_LEAP && p >= 0) {   // hop to the side away from the predator
            float pa = wrapa(atan2f(s->agents[p].z - a->z, s->agents[p].x - a->x) - a->yaw);
            in->strafe = pa > 0 ? -1.0f : 1.0f;
        }
        return;
    }
}

// Is animal p (dist away) a threat to herbivore i, and is it engaged with
// it (close, charging or hunting it), so that a defender stands? The one it
// is alert to (foe, bot_perceive) stays a threat out to 30 m and engaged
// within 10 m, plus the body length.
static bool herb_threat(const CretSim* s, int i, int p, float dist, int foe, bool* engaged) {
    const CretAgent* a = &s->agents[i]; *engaged = false;
    if (p < 0) return false;
    const CretAgent* b = &s->agents[p]; float L = CRET_SPECIES_TABLE[a->species].length * cret_growth_scale(a->growth);
    bool hurt_by = p == foe;
    bool closing = b->speed > b->walk_speed * 1.3f;
    bool marked = a->group && b->st[CRET_ST_MARKED] > 0 && b->mark_by == a->group;   // the herd's alert call: run early
    *engaged = (closing && dist < 10.0f + L) || dist < 3.0f + 0.6f * L || (hurt_by && dist < 10.0f + L) ||
               (b->bot_state == BOT_HUNT && b->bot_target == i && dist < 14.0f + L);
    return dist < 8.0f + L || (dist < 25.0f + L && closing) || (marked && dist < 45.0f + L) || (hurt_by && dist < 30.0f + L);
}

CretInput cret_bot_input(CretSim* s, int i) {
    CretAgent* a = &s->agents[i]; CretInput in = {0};
    if (!a->alive) return in;
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    bool carn = S->diet == CRET_DIET_CARNIVORE; float g = cret_growth_scale(a->growth), L = S->length * g;
    float health = a->health / fmaxf(a->max_health, 1);
    if (a->bot_timer > 0) a->bot_timer--;
    float dist;
    bot_perceive(s, i);
    // Holding on to prey: let go when badly hurt (Space); stunned: nothing. The head stays where it was.
    in.aim = isfinite(a->aim) ? a->aim : 0;
    if (a->latch) { in.jump = health < 0.3f; return in; }
    if (a->st[CRET_ST_STUN] > 0) return in;
    in.aim = 0;
    // Frightened by a roar: run from it.
    if (a->st[CRET_ST_FEAR] > 0 && a->fear_src && a->bot_state != BOT_FLEE) { bot_set(a, BOT_FLEE, (int)(a->st[CRET_ST_FEAR] * CRET_TICK_HZ) + 30); a->bot_target = a->fear_src - 1; }
    // Reactions that override everything: herbivores to predators, everyone to a hit.
    // A threat counts only once noticed (bot_perceive); until then the bot carries on.
    int foe = a->bot_alert > 0 ? a->bot_foe - 1 : -1;
    if (!carn && a->st[CRET_ST_FEAR] <= 0) {
        int p = nearest_of(s, i, CRET_DIET_CARNIVORE, &dist);
        // The animal that hurt it comes first while it is alert (any animal, a little farther off than the nearest predator).
        if (foe >= 0) { float fd = agent_dist(a, &s->agents[foe]); if (p < 0 || fd < dist + 10.0f) { p = foe; dist = fd; } }
        bool engaged, threat = herb_threat(s, i, p, dist, foe, &engaged);
        // A new threat: start noticing it, and meanwhile keep watching the
        // predator it already saw. The one it is alert to is always in view.
        if (threat && a->bot_seen != p + 1 && p != foe) {
            bot_cue(s, a, p, false);
            int w = a->bot_seen - 1;
            p = w >= 0 && (CRET_SPECIES_TABLE[s->agents[w].species].diet == CRET_DIET_CARNIVORE || w == foe) ? w : -1;
            dist = p >= 0 ? agent_dist(a, &s->agents[p]) : 1e9f;
            threat = herb_threat(s, i, p, dist, foe, &engaged);
        }
        const CretAgent* b = p >= 0 ? &s->agents[p] : NULL;
        if (threat && engaged && can_defend(s, a, b)) {
            if (a->bot_state != BOT_DEFEND) bot_set(a, BOT_DEFEND, 240);
            a->bot_target = p;
        } else if (threat && a->bot_state != BOT_FLEE && a->bot_state != BOT_DEFEND) {
            bot_set(a, BOT_FLEE, 90 + (int)(frand(&s->rng) * 90)); a->bot_target = p;
            // Alarm: group members close by run too, and now know the predator.
            for (int j = 0; j < s->cfg.num_agents && a->group; j++) {
                CretAgent* c = &s->agents[j];
                if (j == i || !c->alive || c->group != a->group || c->bot_state == BOT_FLEE || c->bot_state == BOT_DEFEND) continue;
                float dx = c->x - a->x, dz = c->z - a->z; if (dx * dx + dz * dz > 40.0f * 40.0f) continue;
                bot_set(c, BOT_FLEE, 90 + (int)(frand(&s->rng) * 90)); c->bot_target = p; c->bot_seen = p + 1;
            }
        }
    } else {
        // Hit while hunting by something bigger, or badly hurt: break off.
        if (health < 0.45f && a->bot_state == BOT_HUNT) bot_set(a, BOT_FLEE, 210);
        // Prey that fights back and already hurt us: not worth it.
        if (a->bot_state == BOT_HUNT && health < 0.65f && s->agents[a->bot_target].bot_state == BOT_DEFEND && a->last_attacker == a->bot_target) bot_set(a, BOT_FLEE, 150);
        if (a->bot_state == BOT_FLEE && a->bot_target >= 0 && CRET_SPECIES_TABLE[s->agents[a->bot_target].species].diet == CRET_DIET_HERBIVORE && a->last_attacker >= 0) a->bot_target = a->last_attacker;
        // Hurt by an animal it has noticed: turn on it and chase it while
        // alert if it can take it and is fit to fight (as fit as the break-off
        // rules above ask), else back off from it while it is close.
        if (carn && foe >= 0 && a->bot_state != BOT_FLEE && !(a->bot_state == BOT_HUNT && a->bot_target == foe)) {
            float fd = agent_dist(a, &s->agents[foe]);
            if (can_take(s, i, &s->agents[foe]) && health > 0.65f && a->broken <= 0) {
                if (fd < 30.0f + L) { bot_set(a, BOT_HUNT, a->bot_alert); a->bot_target = foe; }
            } else if (fd < 20.0f + L) { bot_set(a, BOT_FLEE, 90 + (int)(frand(&s->rng) * 60)); a->bot_target = foe; }
        }
        if (a->bot_state != BOT_HUNT && a->bot_state != BOT_FEED && a->bot_state != BOT_FLEE && a->bot_state != BOT_RAID && a->food < 0.75f) {
            int c = nearest_carcass(s, a, 150.0f), nk = a->food < 0.7f ? raid_target(s, i) : -1;
            if (c >= 0) { bot_set(a, BOT_FEED, 900); a->bot_target = c; }
            else if (nk >= 0) { bot_set(a, BOT_RAID, 900); a->bot_target = nk; }
            else if (a->stamina > 0.6f && health > 0.5f && a->broken <= 0 && a->water > 0.25f) {
                int p = pick_prey(s, i, a->group ? 90.0f : 60.0f, &dist);
                if (p >= 0) {
                    bot_set(a, BOT_HUNT, 1200); a->bot_target = p;
                    // The pack joins: a call (rallies it) and packmates in earshot take the same prey.
                    if (a->group) {
                        in.action = CRET_ACT_CALL;
                        for (int j = 0; j < s->cfg.num_agents; j++) {
                            CretAgent* c = &s->agents[j];
                            if (j == i || j == 0 || !c->alive || c->group != a->group || c->bot_state == BOT_HUNT || c->bot_state == BOT_FLEE) continue;
                            float dx = c->x - a->x, dz = c->z - a->z; if (dx * dx + dz * dz > 150.0f * 150.0f) continue;
                            if (c->health < 0.4f * c->max_health || c->broken > 0) continue;
                            bot_set(c, BOT_HUNT, 1200); c->bot_target = p;
                        }
                    }
                }
            }
        }
    }
    // Thirst, when nothing more urgent is going on.
    bool busy = a->bot_state == BOT_FLEE || a->bot_state == BOT_DEFEND || a->bot_state == BOT_HUNT || a->bot_state == BOT_FEED || a->bot_state == BOT_DRINK ||
                a->bot_state == BOT_SEEK_WATER || a->bot_state == BOT_RAID || a->bot_state == BOT_NEST;
    if (a->water < 0.15f && (a->bot_state == BOT_FEED || a->bot_state == BOT_HUNT || a->bot_state == BOT_RAID)) busy = false;   // parched: water first
    // Searched once a second per bot: a failed search scans ~4000 points, and
    // bots far out at sea used to rescan every tick (12+ ms a tick for 32 bots).
    if (!busy && a->water < 0.35f && (s->tick + i) % CRET_TICK_HZ == 0) {
        float wx, wz;
        if (find_water(s, a, &wx, &wz)) { bot_set(a, BOT_SEEK_WATER, 1800); a->bot_target = -1; }
    }
    // A group member called: come to it (unless busy with something urgent).
    bool calm = a->bot_state == BOT_WANDER || a->bot_state == BOT_GRAZE || a->bot_state == BOT_REST;
    // A fed adult in a group with no nest sometimes builds one (about once in a few calm minutes).
    if (calm && a->growth >= 0.95f && a->group && a->food > 0.6f && a->water > 0.5f && frand(&s->rng) < 1.0f / 3600 &&
        cret_group_size(s, a->group) >= 2 && cret_nest_of(s, i) < 0) bot_set(a, BOT_NEST, 7 * CRET_TICK_HZ);
    if (a->rally_ticks > 0 && calm) {
        float dx = a->rally_x - a->x, dz = a->rally_z - a->z;
        if (dx * dx + dz * dz > (12.0f + L) * (12.0f + L)) {
            in.turn = steer(s, a, a->rally_x, a->rally_z, &dist); in.forward = dist > 40 ? 1.0f : 0.5f;
            if (a->bot_state != BOT_WANDER) bot_set(a, BOT_WANDER, 300);
            a->goal_x = a->rally_x; a->goal_z = a->rally_z;
            in.aim = bot_aim(a, 0);
            return in;
        }
    }
    bool aimed = false;   // defending and hunting aim at the target; otherwise the head comes back to centre
    switch (a->bot_state) {
    case BOT_FLEE: {
        // Away from the threat, bending toward the herd so it flees as one.
        const CretAgent* b = &s->agents[a->bot_target < 0 ? i : a->bot_target];
        float ax = a->x - b->x, az = a->z - b->z, al = fmaxf(sqrtf(ax * ax + az * az), 1e-3f), hx, hz;
        ax /= al; az /= al;
        if (!carn && own_herd(s, i, &hx, &hz)) {
            float dx = hx - a->x, dz = hz - a->z, dl = sqrtf(dx * dx + dz * dz);
            if (dl > 12) { ax += 0.6f * dx / dl; az += 0.6f * dz / dl; }
        }
        in.turn = steer(s, a, a->x + ax * 20, a->z + az * 20, &dist); in.forward = 1; in.sprint = a->stamina > 0.15f;
        // Running from the animal that hurt it: keep going until it is 45 m
        // off (plus the body), at a trot once it is past 25 m and not coming.
        if (a->bot_alert > 0 && a->bot_foe == a->bot_target + 1 && b->alive) {
            if (al > 25.0f + L && b->speed <= b->walk_speed * 1.3f) in.sprint = 0;
            if (a->bot_timer == 0 && al < 45.0f + L) a->bot_timer = 30;
        }
        if (a->bot_timer == 0) { bot_set(a, BOT_WANDER, 600); pick_goal(s, a, 20, 50); }
        break;
    }
    case BOT_DEFEND: {
        // A tail to the predator (stegosaur), or face it (headbutt, bash); strike in reach.
        const CretAgent* b = &s->agents[a->bot_target];
        in.aim = bot_aim(a, cret_aim_at(a, b->x, b->z));   // the head (the stegosaur's tail) turns after the predator
        aimed = true;
        float px, pz; strike_point_aim(a, in.aim, &px, &pz);
        if (S->tail) in.turn = steer(s, a, a->x + (a->x - b->x), a->z + (a->z - b->z), &dist);
        else {
            in.turn = steer(s, a, b->x, b->z, &dist);
            float gap = body_dist(b, px, pz);
            if (gap > S->reach * g * 0.7f && gap < 6.0f) in.forward = 0.5f;   // step in to butt
        }
        if (b->alive && body_dist(b, px, pz) < S->reach * g * 0.9f) in.action = CRET_ACT_ATTACK;
        float bd = sqrtf((b->x - a->x) * (b->x - a->x) + (b->z - a->z) * (b->z - a->z));
        // Alert to this attacker: the guard stays up while it is near. When it
        // backs off, or this animal is badly hurt, it moves off toward its
        // herd (FLEE) instead of grazing beside it.
        bool hurt_by = a->bot_alert > 0 && a->bot_foe == a->bot_target + 1 && b->alive;
        if (hurt_by && bd < 25.0f + L && a->bot_timer < 30) a->bot_timer = 30;
        if (!b->alive || bd > 25.0f + L || a->bot_timer == 0 || health < 0.25f) bot_set(a, (health < 0.25f || hurt_by) && b->alive ? BOT_FLEE : BOT_GRAZE, 120);
        break;
    }
    case BOT_HUNT: {
        const CretAgent* b = &s->agents[a->bot_target];
        if (!b->alive) {   // killed: eat
            int c = nearest_carcass(s, a, 40.0f);
            if (c >= 0) { bot_set(a, BOT_FEED, 900); a->bot_target = c; } else bot_set(a, BOT_REST, 600);
            break;
        }
        in.aim = bot_aim(a, cret_aim_at(a, b->x, b->z)); aimed = true;
        float px, pz; strike_point_aim(a, in.aim, &px, &pz);
        float gap = body_dist(b, px, pz);
        bool payback = a->bot_alert > 0 && a->bot_foe == a->bot_target + 1;   // chasing the animal that hurt it
        // Flank: packmates on the same prey spread around it while closing,
        // then rush in. Each takes a side by its rank among the hunters.
        float tx = b->x, tz = b->z;
        if (a->group && gap > 4.0f) {
            int rank = 0, n = 0;
            for (int j = 0; j < s->cfg.num_agents; j++) {
                const CretAgent* c = &s->agents[j];
                if (!c->alive || c->group != a->group || c->bot_state != BOT_HUNT || c->bot_target != a->bot_target) continue;
                if (j < i) rank++;
                n++;
            }
            if (n > 1) {
                float ang = atan2f(a->z - b->z, a->x - b->x);
                float off = (rank - 0.5f * (n - 1)) * 1.1f;   // ~60 degrees apart
                float r = clampf(gap * 0.6f, 4.0f, 14.0f);
                tx = b->x + cosf(ang + off) * r; tz = b->z + sinf(ang + off) * r;
            }
        }
        in.turn = steer(s, a, tx, tz, &dist);
        in.forward = gap > 0.5f ? 1.0f : 0.3f; in.sprint = a->stamina > 0.1f && gap > 3;
        // Stalk: close in at a walk until near or seen (the prey reacts to a charge), then rush.
        // An animal that hurt it is rushed at once.
        if (gap > 22.0f && b->bot_state != BOT_FLEE && b->bot_state != BOT_DEFEND && !payback) { in.forward = 0.6f; in.sprint = 0; }
        if (gap < S->reach * g * 0.9f) in.action = CRET_ACT_ATTACK;
        float bd = sqrtf((b->x - a->x) * (b->x - a->x) + (b->z - a->z) * (b->z - a->z));
        // Gave up: a long rest. A predator that hurt it and ran off is let
        // go sooner, at 40 m (plus the body), than prey at 90 m.
        bool rival = payback && CRET_SPECIES_TABLE[b->species].diet == CRET_DIET_CARNIVORE;
        if (a->bot_timer == 0 || bd > (rival ? 40.0f + L : 90.0f)) bot_set(a, BOT_REST, 900 + (int)(frand(&s->rng) * 900));
        break;
    }
    case BOT_FEED: {
        CretCarcass* k = &s->carcasses[a->bot_target];
        if (!k->active || k->meat <= 0 || a->food > 0.97f || a->bot_timer == 0) {   // full: a long rest; still hungry: hunt again soon
            if (a->food > 0.6f) bot_set(a, BOT_REST, 900 + (int)(frand(&s->rng) * 600)); else { bot_set(a, BOT_WANDER, 300); pick_goal(s, a, 40, 120); }
            break;
        }
        float hx = a->x + cosf(a->yaw) * L * 0.45f, hz = a->z + sinf(a->yaw) * L * 0.45f;
        if (carcass_near(s, hx, hz, 1.0f + S->reach * g) == a->bot_target) in.action = CRET_ACT_EAT;
        else { in.turn = steer(s, a, k->x, k->z, &dist); in.forward = dist > 12 ? 1.0f : 0.5f; }
        break;
    }
    case BOT_SEEK_WATER: {
        if (water_ahead(s, a)) { bot_set(a, BOT_DRINK, 600); in.action = CRET_ACT_DRINK; break; }
        in.turn = steer(s, a, a->goal_x, a->goal_z, &dist); in.forward = dist > 20 ? 1.0f : 0.5f;
        if (dist < 3 + a->radius) {   // at the shore: turn to the water and step in
            float wx, wz;
            if (!find_water(s, a, &wx, &wz)) { bot_set(a, BOT_WANDER, 600); break; }
            float dx = wx - a->x, dz = wz - a->z, d = wrapa(atan2f(dz, dx) - a->yaw);
            in.turn = clampf(d * 2, -1, 1); in.forward = fabsf(d) < 0.4f ? 0.4f : 0.0f;
        }
        if (a->bot_timer == 0) bot_set(a, BOT_WANDER, 600);
        break;
    }
    case BOT_GRAZE: case BOT_REST: case BOT_DRINK:
        in.action = a->bot_state == BOT_GRAZE ? CRET_ACT_EAT : a->bot_state == BOT_DRINK ? CRET_ACT_DRINK : CRET_ACT_REST;
        if (a->bot_state == BOT_REST && a->bot_timer % 600 == 60 && frand(&s->rng) < 0.5f) in.action = CRET_ACT_CALL;
        if (a->bot_state == BOT_DRINK && ((a->water > 0.97f && !at_drink_quest(s, i)) || !water_ahead(s, a))) a->bot_timer = 0;
        if (a->bot_state == BOT_GRAZE && (a->food > 0.97f && a->bot_timer > 60)) a->bot_timer = 60;
        if (a->bot_timer == 0) { bot_set(a, BOT_WANDER, 900); pick_goal(s, a, 30, carn ? 160 : 70); }
        break;
    case BOT_NEST:   // hold still and build (progress.c); done once the nest stands
        in.action = CRET_ACT_NEST;
        if (cret_nest_of(s, i) >= 0 || a->bot_timer == 0) { bot_set(a, BOT_GRAZE, 300); in.action = 0; }
        break;
    case BOT_RAID: {   // walk up to the nest and eat the eggs
        const CretNest* n = &s->nests[a->bot_target % CRET_MAX_NESTS];
        if (!n->active || n->eggs <= 0 || a->food > 0.95f || a->bot_timer == 0) { bot_set(a, BOT_REST, 300 + (int)(frand(&s->rng) * 300)); break; }
        float hx = a->x + cosf(a->yaw) * L * 0.45f, hz = a->z + sinf(a->yaw) * L * 0.45f;
        float dx = n->x - hx, dz = n->z - hz;
        if (dx * dx + dz * dz < (2.0f + a->radius) * (2.0f + a->radius)) in.action = CRET_ACT_EAT;
        else { in.turn = steer(s, a, n->x, n->z, &dist); in.forward = dist > 12 ? 1.0f : 0.4f; }
        break;
    }
    default: {   // wander to the goal at a walk (sometimes a trot; a trot on a quest), then graze or rest
        float qx, qz; bool questing = a->food > 0.6f && a->water > 0.5f && quest_goal(s, i, &qx, &qz);
        in.turn = steer(s, a, a->goal_x, a->goal_z, &dist);
        in.forward = questing || (i + a->ticks_alive / 600) % 4 == 0 ? 1.0f : 0.5f;
        if (fabsf(in.turn) > 0.8f) in.forward *= 0.6f;
        if (dist < 4 + a->radius || a->bot_timer == 0) {
            bool graze = !carn && cret_forage(s->world, a->x + cosf(a->yaw) * L * 0.45f, a->z + sinf(a->yaw) * L * 0.45f) > 0.1f;
            if (questing && graze) {   // a quick bite and on, unless the quest is grazing here
                const CretQuest* q = cret_quest_of(s, i, 1); if (!q || q->kind != CRET_Q_GATHER) q = cret_quest_of(s, i, 0);
                float tx, tz, r; bool here = q && q->kind == CRET_Q_GATHER && cret_quest_target(s, q, &tx, &tz, &r) && (tx - a->x) * (tx - a->x) + (tz - a->z) * (tz - a->z) < r * r;
                if (!here && frand(&s->rng) < 0.7f) graze = false;
            }
            float wx, wz;
            if (water_ahead(s, a) && (a->water < 0.8f || at_drink_quest(s, i))) bot_set(a, BOT_DRINK, 300);
            else if (at_drink_quest(s, i) && find_water(s, a, &wx, &wz)) { bot_set(a, BOT_SEEK_WATER, 900); a->bot_target = -1; }
            else if (carn || !graze) { if (carn && !questing) bot_set(a, BOT_REST, 120 + (int)(frand(&s->rng) * 180)); else { pick_goal(s, a, 20, 60); a->bot_timer = 600; } }
            else bot_set(a, BOT_GRAZE, 180 + (int)(frand(&s->rng) * 240));
        }
        if (a->goal_x == 0 && a->goal_z == 0) pick_goal(s, a, 30, 90);
    }
    }
    if (!aimed) in.aim = bot_aim(a, 0);
    // Hungry hatchling predators with nothing better to hunt catch small prey
    // (critters.c); the head still turns at its limited speed (bot_aim).
    if ((a->bot_state == BOT_WANDER || a->bot_state == BOT_REST || a->bot_state == BOT_GRAZE) && cret_critter_bot(s, i, &in)) in.aim = bot_aim(a, in.aim);
    // Raptors on its back: buck and twist to throw them off.
    if (a->latchers > 0) { in.turn = (a->ticks_alive / 12) % 2 ? 1.0f : -1.0f; in.forward = 1; in.sprint = a->stamina > 0.1f; }
    bot_attack2(s, i, &in);
    bot_abilities(s, i, &in);
    if (a->broken > 0 && a->bot_state != BOT_FLEE) in.sprint = 0;
    // Stuck (against a rock, in a hollow, at a cliff foot) while trying to
    // move: walk off in a random direction for a few seconds.
    if (a->escape_ticks > 0) {
        a->escape_ticks--;
        float d = wrapa(a->escape_yaw - a->yaw);
        CretInput e = {.turn = clampf(d * 2, -1, 1), .forward = fabsf(d) < 1.0f ? 1.0f : 0.3f, .aim = bot_aim(a, 0)};
        return e;
    }
    if (in.forward > 0.2f && !in.action) {
        if (++a->stuck_ticks >= 150) {   // 5 s
            float dx = a->x - a->stuck_x, dz = a->z - a->stuck_z;
            if (dx * dx + dz * dz < 4.0f) { a->escape_ticks = 90 + (int)(frand(&s->rng) * 60); a->escape_yaw = a->yaw + 1.6f + frand(&s->rng) * 3.0f; }
            a->stuck_ticks = 0; a->stuck_x = a->x; a->stuck_z = a->z;
        }
    } else { a->stuck_ticks = 0; a->stuck_x = a->x; a->stuck_z = a->z; }
    return in;
}
