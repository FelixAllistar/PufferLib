#include "sim.h"
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
                        .respawn_ticks = 30 * 10, .growth_seconds = 1200.0f, .spawn_growth = -1.0f, .player_growth = 0.5f,
                        .hunger_seconds = 900.0f, .thirst_seconds = 600.0f, .carcass_seconds = 300.0f,
                        .weather = 1, .cloud_cover = 0.45f};
}

// ---------------------------------------------------------------- growth and food
float cret_growth_scale(float growth) { return clampf(growth, 0.2f, 1.0f); }

// Size, speed, turning, health and jump of the current growth stage. Body
// length scales with growth, mass with its cube; juveniles are relatively
// quicker and nimbler. Health keeps its fraction.
void cret_apply_growth(CretAgent* a) {
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    float frac = a->max_health > 0 ? a->health / a->max_health : 1.0f;
    float sp = 0.72f + 0.28f * g;
    a->radius = S->radius * g; a->height = S->height * g;
    a->walk_speed = S->walk * sp; a->trot_speed = S->trot * sp; a->sprint_speed = S->sprint * sp; a->swim_speed = S->swim * sp;
    a->turn_rate = S->turn_rate * (1.4f - 0.4f * g); a->jump_speed = S->jump * (0.8f + 0.2f * g);
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
// stride speeds, sprints are faster than the animals likely were.
const CretSpecies CRET_SPECIES_TABLE[CRET_SPECIES] = {
    // Attack timings follow the clips: the raptor's bite lands 1.0 s in, the
    // stegosaur's tail 0.8 s, the tyrannosaur's bite 1.6 s, the
    // pachycephalosaur's headbutt 1.3 s. Sauropelta and Gallimimus have no
    // attack clip; their shoulder bash and kick are procedural (dino.c).
    // Defences: Sauropelta's armour blocks half of every hit, its spikes
    // cut whatever bites it and it barely bleeds; a headbutt throws.
    //            name           asset     diet                 len   h     r     kg     walk trot sprint swim turn jump drain regen  hp     dmg   reach cd    hit   len  tail knock armor bleed thorns group
    [CRET_SP_RAPTOR] = {"Velociraptor", "raptor", CRET_DIET_CARNIVORE, 3.0f, 1.2f, 0.45f, 80.0f, 2.2f, 4.6f, 10.5f, 1.8f, 3.6f, 6.0f, 0.065f, 0.07f, 120.0f, 26.0f, 1.3f, 1.4f, 1.0f, 1.4f, 0, 1.0f, 0.0f, 0.0f, 0.0f, 4},
    [CRET_SP_STEGO]  = {"Stegosaurus", "stego", CRET_DIET_HERBIVORE, 8.5f, 3.6f, 1.35f, 5000.0f, 2.2f, 3.4f, 5.2f, 1.2f, 1.1f, 0.0f, 0.07f, 0.05f, 900.0f, 60.0f, 2.6f, 2.0f, 0.8f, 1.6f, 1, 1.0f, 0.1f, 0.0f, 0.0f, 8},
    [CRET_SP_TREX]   = {"Tyrannosaurus", "trex", CRET_DIET_CARNIVORE, 12.0f, 4.6f, 1.5f, 8000.0f, 1.5f, 3.2f, 7.5f, 1.4f, 1.2f, 0.0f, 0.08f, 0.04f, 1800.0f, 260.0f, 2.4f, 2.6f, 1.6f, 2.4f, 0, 1.0f, 0.1f, 0.0f, 0.0f, 0},
    [CRET_SP_PACHY]  = {"Pachycephalosaurus", "pachy", CRET_DIET_HERBIVORE, 4.5f, 2.0f, 0.55f, 450.0f, 1.7f, 3.7f, 8.5f, 1.3f, 2.6f, 0.0f, 0.11f, 0.06f, 300.0f, 30.0f, 1.0f, 2.6f, 1.3f, 2.0f, 0, 2.2f, 0.15f, 0.0f, 0.0f, 6},
    [CRET_SP_SAUROPELTA] = {"Sauropelta", "sauropelta", CRET_DIET_HERBIVORE, 5.2f, 1.5f, 0.85f, 1500.0f, 1.3f, 2.2f, 3.4f, 0.9f, 1.3f, 0.0f, 0.06f, 0.05f, 650.0f, 30.0f, 1.4f, 1.6f, 0.4f, 0.9f, 0, 1.5f, 0.55f, 0.75f, 0.25f, 4},
    [CRET_SP_GALLI]  = {"Gallimimus", "galli", CRET_DIET_HERBIVORE, 6.0f, 2.2f, 0.45f, 400.0f, 2.2f, 5.0f, 11.0f, 1.6f, 3.0f, 0.0f, 0.13f, 0.07f, 160.0f, 12.0f, 1.0f, 1.2f, 0.3f, 0.6f, 0, 1.0f, 0.0f, 0.0f, 0.0f, 8},
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
typedef struct MoverCtx { CretSim* s; int self; b3CollisionPlane planes[16]; int count; } MoverCtx;
static bool mover_filter(b3ShapeId shape, void* c) {
    MoverCtx* m = c; return !B3_ID_EQUALS(b3Shape_GetBody(shape), m->s->agents[m->self].body);
}
static bool mover_planes(b3ShapeId shape, const b3PlaneResult* res, int n, void* c) {
    MoverCtx* m = c; if (!mover_filter(shape, c)) return true;
    for (int i = 0; i < n && m->count < 16; i++)
        m->planes[m->count++] = (b3CollisionPlane){.plane = res[i].plane, .pushLimit = FLT_MAX, .push = 0, .clipVelocity = true};
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

void cret_sim_respawn(CretSim* s, int i) {
    CretAgent* a = &s->agents[i]; b3BodyId body = a->body; b3ShapeId shape = a->shape; int sp = slot_species(s, i);
    const CretSpecies* S = &CRET_SPECIES_TABLE[sp];
    uint8_t old_group = a->group, old_species = a->species;
    memset(a, 0, sizeof(*a)); a->body = body; a->shape = shape; a->species = (uint8_t)sp;
    a->alive = true; a->stamina = 1; a->last_attacker = -1;
    bool person = (i == 0 || s->human[i]) && s->cfg.player_growth >= 0;   // the local player or a network peer
    float g0 = person ? s->cfg.player_growth : s->cfg.spawn_growth;
    a->growth = g0 >= 0 ? clampf(g0, 0.35f, 1.0f) : 0.55f + 0.45f * frand(&s->rng);
    a->food = 0.75f + 0.25f * frand(&s->rng); a->water = 0.75f + 0.25f * frand(&s->rng);
    cret_apply_growth(a); a->health = a->max_health; update_shape(a);
    b3Body_SetTransform(a->body, (b3Pos){0, -1000.0f - i * 10, 0}, b3Quat_identity);
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
    memset(s, 0, sizeof(*s)); s->cfg = cfg; s->rng = cfg.seed * 2654435761u + 1;
    if (cfg.num_agents > CRET_MAX_AGENTS) cfg.num_agents = s->cfg.num_agents = CRET_MAX_AGENTS;
    s->world = cret_world_acquire(cfg.world_seed, cfg.world_n, cfg.world_cell, 1);
    if (!s->world) return -1;
    b3WorldDef wd = b3DefaultWorldDef(); wd.gravity = (b3Vec3){0, -9.8f, 0};
    s->phys = b3CreateWorld(&wd);
    cret_prop_stream_init(&s->props, s->world);
    for (int i = 0; i < s->cfg.num_agents; i++) {
        b3BodyDef bd = b3DefaultBodyDef(); bd.type = b3_kinematicBody; bd.position = (b3Pos){0, -1000.0f - i * 10, 0};
        s->agents[i].body = b3CreateBody(s->phys, &bd);
        b3ShapeDef sd = b3DefaultShapeDef(); sd.filter.categoryBits = CAT_AGENT; sd.filter.maskBits = CAT_AGENT | CAT_PROP;
        const CretSpecies* S = &CRET_SPECIES_TABLE[slot_species(s, i)];
        float half = fmaxf(S->height * 0.5f - S->radius, 0.05f);
        b3Capsule cap = {{0, -half, 0}, {0, half, 0}, S->radius};
        s->agents[i].shape = b3CreateCapsuleShape(s->agents[i].body, &sd, &cap);
    }
    cret_sim_reset(s);
    return 0;
}

void cret_sim_reset(CretSim* s) {
    s->tick = 0; s->time_of_day = 0.3f + 0.3f * frand(&s->rng);
    s->cloud_cover = s->cloud_target = s->cfg.cloud_cover; s->weather_ticks = 0; s->weather_rng = s->cfg.seed * 747796405u + 0x51ed27u;
    for (int i = 0; i < CRET_MAX_AGENTS; i++) s->agents[i].alive = false;
    memset(s->carcasses, 0, sizeof s->carcasses);
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

void cret_sim_set_species(CretSim* s, int i, int species) {
    if (species < 0 || species >= CRET_SPECIES || i < 0 || i >= s->cfg.num_agents) return;
    if (i == 0) s->cfg.player_species = species;
    s->species_pick[i] = (uint8_t)(species + 1);
    cret_sim_respawn(s, i);
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
// Where the attack lands: in front of the snout, or behind the tail.
static void strike_point(const CretAgent* a, float* px, float* pz) {
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    float d = S->length * g * (S->tail ? -0.45f : 0.42f);
    *px = a->x + cosf(a->yaw) * d; *pz = a->z + sinf(a->yaw) * d;
}

static void kill(CretSim* s, int i) {
    CretAgent* a = &s->agents[i];
    if (!a->alive) return;
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    a->alive = false; a->dead_ticks = 0; a->ev_died = 1; a->act = 0; a->health = 0;
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

static void damage(CretSim* s, int attacker, int victim, float hp) {
    CretAgent* a = &s->agents[attacker]; CretAgent* b = &s->agents[victim];
    if (!b->alive || hp <= 0) return;
    const CretSpecies* SA = &CRET_SPECIES_TABLE[a->species]; const CretSpecies* SB = &CRET_SPECIES_TABLE[b->species];
    // Armour: heavy blows punch through it (a tyrannosaur's bite still crushes a sauropelta).
    float raw = hp, armor = SB->armor * clampf(1.0f - raw / 600.0f, 0.3f, 1.0f);
    hp = fminf(hp * (1 - armor), b->health);
    float blunt = SA->knock > 1.5f ? 0.3f : 1.0f;   // a headbutt bruises more than it cuts
    b->health -= hp; b->bleed += hp * 0.02f * blunt * (1 - SB->bleed_resist); b->last_attacker = attacker;
    b->hurt_ticks = SA->knock > 1.5f ? 24 : 12;   // a headbutt staggers longer
    a->ev_dealt += hp / fmaxf(b->max_health, 1); b->ev_taken += hp / fmaxf(b->max_health, 1);
    // Knockback by the mass ratio: a tail swing throws a raptor, a raptor's bite barely moves a stegosaur.
    float ma = SA->mass * powf(cret_growth_scale(a->growth), 3);
    float mb = SB->mass * powf(cret_growth_scale(b->growth), 3);
    float dx = b->x - a->x, dz = b->z - a->z, dl = fmaxf(sqrtf(dx * dx + dz * dz), 1e-3f);
    float k = clampf(5.0f * ma / (ma + mb) * SA->knock, 0, 7);
    b->vx += dx / dl * k; b->vz += dz / dl * k;
    if (k > 1.5f && b->on_ground && !b->swimming) { b->vy = k * 0.4f; b->on_ground = false; }
    // Bones break under a crushing blow (a third of the victim's health at
    // once) or a hard throw by a heavier or equal animal.
    if (hp > 0.33f * b->max_health || (k > 4.5f && ma > 0.8f * mb && frand(&s->rng) < 0.35f)) break_bone(b, 45.0f + 45.0f * frand(&s->rng));
    // Spikes: biting into a sauropelta cuts the biter (not a tail swing behind).
    if (SB->thorns > 0 && !SA->tail && a->alive) {
        float back = fminf(raw * SB->thorns, a->health);
        a->health -= back; a->bleed += back * 0.03f * (1 - SA->bleed_resist); a->hurt_ticks = 8; a->last_attacker = victim;
        a->ev_taken += back / fmaxf(a->max_health, 1); b->ev_dealt += back / fmaxf(a->max_health, 1);
        if (a->health <= 0.01f) kill(s, attacker);
    }
    if (b->health <= 0.01f) kill(s, victim);
}

// The attack lands hit_time into the swing on everyone within reach of the strike point.
static void resolve_attack(CretSim* s, int i) {
    CretAgent* a = &s->agents[i]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    float px, pz; strike_point(a, &px, &pz);
    float reach = S->reach * g;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        CretAgent* b = &s->agents[j];
        if (j == i || !b->alive || fabsf(b->y - a->y) > a->height + b->height) continue;
        if (a->group && b->group == a->group) continue;   // packmates and herd-mates are spared
        if (body_dist(b, px, pz) > reach) continue;
        damage(s, i, j, S->damage * g * g * (0.85f + 0.3f * frand(&s->rng)));
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

// Timers that gate movement and actions, once per moved tick.
static void agent_timers(CretAgent* a) {
    if (a->rally_ticks > 0) a->rally_ticks--;
    if (a->hurt_ticks > 0) a->hurt_ticks--;
    if (a->attack_cd > 0) a->attack_cd--;
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
            if (k >= 0) {   // a full stomach is a quarter of body mass, eaten in ~12 s
                float mass = S->mass * g * g * g, kg = fminf(fminf(0.02f * mass * dt, s->carcasses[k].meat), (1 - a->food) * 0.25f * mass);
                s->carcasses[k].meat -= kg; float d = kg / (0.25f * mass); a->food += d; a->ev_food += d;
            }
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
    // Heavier animals turn and accelerate slower, and turn wider at speed.
    float turn_k = 1 - 0.45f * clampf(a->speed / fmaxf(a->sprint_speed, 1), 0, 1);
    a->yaw += clampf(in->turn, -1, 1) * a->turn_rate * turn_k * dt;
    if (a->yaw > 3.14159265f) a->yaw -= 6.2831853f; else if (a->yaw < -3.14159265f) a->yaw += 6.2831853f;
    float fwd = clampf(in->forward, -0.5f, 1), str = clampf(in->strafe, -1, 1);
    // Actions: start when (nearly) standing; continuous ones (eat, drink,
    // rest) last while held, one-shots (call, attack) run to the end.
    bool one_shot = a->act == CRET_ACT_CALL || a->act == CRET_ACT_ATTACK;
    int want = in->action;
    if (want == CRET_ACT_EAT || want == CRET_ACT_DRINK) want = water_ahead(s, a) ? CRET_ACT_DRINK : CRET_ACT_EAT;
    if (a->act) {
        a->act_ticks++;
        int len = a->act == CRET_ACT_ATTACK ? (int)(S->attack_time * CRET_TICK_HZ) : 60;
        if (a->act == CRET_ACT_ATTACK && a->act_ticks == (int)(S->hit_time * CRET_TICK_HZ) && !predict) resolve_attack(s, i);
        if (one_shot ? a->act_ticks > len : (want != a->act || fabsf(fwd) > 0.05f || fabsf(str) > 0.05f)) a->act = 0;
    }
    bool can = want == CRET_ACT_ATTACK ? a->attack_cd == 0 && !a->swimming : a->on_ground && !a->swimming && (a->speed < 1.0f || want == CRET_ACT_CALL);
    if (!a->act && want && can) {
        a->act = (uint8_t)want; a->act_ticks = 0;
        if (want == CRET_ACT_ATTACK) a->attack_cd = (int)(S->cooldown * CRET_TICK_HZ);
        if (want == CRET_ACT_CALL && !predict) call_out(s, i);
    }
    if (a->act && !(a->act == CRET_ACT_ATTACK)) { fwd = 0; str = 0; }
    if (a->hurt_ticks > 6) { fwd *= 0.3f; str *= 0.3f; }   // staggered by a hit
    int sprint = in->sprint && fwd > 0.5f && a->stamina > 0.05f && !a->swimming && a->broken <= 0;
    a->sprinting = sprint;
    float top = a->swimming ? a->swim_speed * fmaxf(fwd, 0) * 2.0f / (1.0f + fmaxf(fwd, 0)) : gait_speed(a, fwd, sprint);
    if (a->broken > 0) top *= 0.55f;   // limping
    float side = a->walk_speed * 0.5f * str;
    float cy = cosf(a->yaw), sy = sinf(a->yaw);
    float dir = fwd < 0 ? -1.0f : 1.0f;
    float dvx = cy * top * dir - sy * side, dvz = sy * top * dir + cy * side;
    float vmax = fmaxf(top, fabsf(side));
    // Slope: slower uphill, cliffs are impassable.
    float n[3]; terra_normal(&w->map, a->x, a->z, n);
    float up = -(n[0] * dvx + n[2] * dvz) / (fmaxf(1e-3f, sqrtf(dvx * dvx + dvz * dvz)));
    float steep = 1 - n[1];
    float slow = clampf(1 - up * steep * 3.0f, 0.15f, 1.15f);
    if (n[1] < 0.6f && up > 0) slow = 0;
    dvx *= slow; dvz *= slow;
    float mass = S->mass * powf(cret_growth_scale(a->growth), 3);
    float k = 10.0f / (1.0f + mass / 1500.0f);   // m/s^2 per m/s of top speed: raptors snap, sauropods lumber
    float accel = (a->on_ground || a->swimming ? k : 1.0f) * dt * fmaxf(vmax, a->walk_speed);
    a->vx += clampf(dvx - a->vx, -accel, accel);
    a->vz += clampf(dvz - a->vz, -accel, accel);
    a->stamina = clampf(a->stamina + (sprint ? -S->stamina_drain : S->stamina_regen) * dt, 0, 1);
    // Vertical: gravity, jump, swim buoyancy.
    float ground = cret_height(w, a->x, a->z), water = cret_water(w, a->x, a->z);
    float depth = water - ground;
    a->swimming = depth > a->height * 0.65f;
    if (a->swimming) {
        float target = water - a->height * 0.6f;
        a->vy += (clampf(target - a->y, -2, 2) * 4.0f - a->vy * 2.0f) * dt;
    } else {
        a->vy -= 9.8f * dt;
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
        b3Vec3 want = {target.x, 0, target.z};
        b3PlaneSolverResult r = b3SolvePlanes(want, ctx.planes, ctx.count);
        r.delta.y = 0;
        float f = b3World_CastMover(s->phys, pos, &cap, r.delta, filt, mover_filter, &ctx);
        pos.x += r.delta.x * f; pos.z += r.delta.z * f;
        target.x -= r.delta.x * f; target.z -= r.delta.z * f;
        if (target.x * target.x + target.z * target.z < 1e-6f) break;
    }
    if (ctx.count) { b3Vec3 v = b3ClipVector((b3Vec3){a->vx, 0, a->vz}, ctx.planes, ctx.count); a->vx = v.x; a->vz = v.z; }
    float ws = w->size, ox = a->x, oz = a->z;
    a->x = clampf((float)pos.x, 2, ws - 2); a->z = clampf((float)pos.z, 2, ws - 2);
    a->y += a->vy * dt;
    float g = cret_height(w, a->x, a->z);
    bool was_air = !a->on_ground && !a->swimming; float vx_land = a->vx, vy_land = a->vy, vz_land = a->vz;
    if (!a->swimming && a->y <= g + 0.02f) { a->y = g; if (a->vy < 0) a->vy = 0; a->on_ground = true; }
    else if (!a->swimming && a->y - g < 0.35f && a->vy <= 0) { a->y = g; a->vy = 0; a->on_ground = true; } // stick to slopes
    else a->on_ground = false;
    if (a->swimming && a->y < g) a->y = g;
    // Falls: hitting the ground faster than ~10 m/s (a 5 m drop) hurts and
    // can break a leg; small animals take falls better. The impact is the
    // speed into the ground, so running down a steep slope is not a fall.
    if (was_air && a->on_ground) {
        float gn[3]; terra_normal(&w->map, a->x, a->z, gn);
        float impact = -(vx_land * gn[0] + vy_land * gn[1] + vz_land * gn[2]);
        a->fall_vy = impact;
        float safe = 10.0f + 3.0f * (1.0f - clampf(a->height / 4.0f, 0, 1));
        if (impact > safe && !predict) {
            float sev = (impact - safe) / 6.0f;
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
        CretAgent* a = &s->agents[i]; a->ev_food = a->ev_water = a->ev_dealt = a->ev_taken = 0; a->ev_died = a->ev_kills = 0;
        if (!a->alive && ++a->dead_ticks >= s->cfg.respawn_ticks) cret_sim_respawn(s, i);
    }
    // Prop colliders near every agent first, so movement this tick sees them.
    for (int i = 0; i < s->cfg.num_agents; i++)
        if (s->agents[i].alive) cret_prop_stream_touch(&s->props, s->world, s->phys, s->agents[i].x, s->agents[i].z, 40.0f, s->tick);
    if (s->tick % 64 == 0) cret_prop_stream_trim(&s->props, s->tick, 300);
    for (int i = 0; i < s->cfg.num_agents; i++) {
        if (!s->agents[i].alive || s->hold[i]) continue;
        move_agent(s, i, &inputs[i], false); s->agents[i].ticks_alive++;
    }
    for (int i = 0; i < s->cfg.num_agents; i++) if (s->agents[i].alive) survive(s, i);
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
    move_agent(s, i, in, predict);
    if (!a->alive) return;
    a->ticks_alive++; agent_timers(a);
}

void cret_sim_place(CretSim* s, int i) {
    CretAgent* a = &s->agents[i];
    if (i < 0 || i >= s->cfg.num_agents || !b3Body_IsValid(a->body)) return;
    if (!a->alive) { b3Body_SetTransform(a->body, (b3Pos){0, -1000.0f - i * 10, 0}, b3Quat_identity); return; }
    if (b3Shape_IsValid(a->shape) && fabsf(b3Shape_GetCapsule(a->shape).radius - a->radius) > 0.01f) update_shape(a);   // species or growth changed
    b3Body_SetTransform(a->body, (b3Pos){a->x, a->y + a->height * 0.5f, a->z}, b3Quat_identity);
}

CretInput cret_decode_action(const float* act) {
    static const float throttle[5] = {-0.5f, 0, 0.5f, 1, 1}, turn[5] = {-1, -0.4f, 0, 0.4f, 1}, strafe[3] = {-1, 0, 1};
    static const uint8_t action[4] = {CRET_ACT_NONE, CRET_ACT_ATTACK, CRET_ACT_EAT, CRET_ACT_CALL};   // eat turns into drink facing water
    int t = (int)act[0], r = (int)act[1], st = (int)act[2], j = (int)act[3], ac = (int)act[4];
    t = t < 0 ? 0 : t > 4 ? 4 : t; r = r < 0 ? 0 : r > 4 ? 4 : r; st = st < 0 ? 0 : st > 2 ? 2 : st; ac = ac < 0 ? 0 : ac > 3 ? 3 : ac;
    return (CretInput){.forward = throttle[t], .turn = turn[r], .strafe = strafe[st], .sprint = t == 4, .jump = j == 1, .action = action[ac]};
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
enum { BOT_WANDER, BOT_GRAZE, BOT_FLEE, BOT_HUNT, BOT_REST, BOT_DRINK, BOT_SEEK_WATER, BOT_FEED, BOT_DEFEND };

static bool walkable(const CretWorld* w, float x, float z) {
    float S = w->size; if (x < 30 || z < 30 || x > S - 30 || z > S - 30) return false;
    float h = cret_height(w, x, z), wl = cret_water(w, x, z); float n[3]; terra_normal(&w->map, x, z, n);
    return h > 1.0f && wl < h + 0.3f && n[1] > 0.8f;
}
static float mass_of(const CretAgent* a) { return CRET_SPECIES_TABLE[a->species].mass * powf(cret_growth_scale(a->growth), 3); }
// Next wander goal. Herbivores that strayed head back to their herd;
// predators often roam toward the herbivores.
static void pick_goal(CretSim* s, CretAgent* a, float lo, float hi) {
    int i = (int)(a - s->agents); float hx, hz, base = a->yaw;
    bool herb = CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_HERBIVORE;
    if (herb ? own_herd(s, i, &hx, &hz) : herd_center(s, i, &hx, &hz)) {
        float dx = hx - a->x, dz = hz - a->z, d = sqrtf(dx * dx + dz * dz);
        if ((herb && d > 45.0f) || (!herb && a->food < 0.8f && d > 100.0f && frand(&s->rng) < 0.6f)) base = atan2f(dz, dx);
    }
    // Pack predators drift toward their pack.
    if (!herb && a->group) {
        float px = 0, pz = 0; int n = 0;
        for (int j = 0; j < s->cfg.num_agents; j++) {
            const CretAgent* b = &s->agents[j]; if (j == i || !b->alive || b->group != a->group) continue;
            px += b->x; pz += b->z; n++;
        }
        if (n) { px /= n; pz /= n; float dx = px - a->x, dz = pz - a->z; if (dx * dx + dz * dz > 40.0f * 40.0f) base = atan2f(dz, dx); }
    }
    for (int t = 0; t < 16; t++) {
        float ang = base + (frand(&s->rng) - 0.5f) * (t < 8 ? 1.5f : 3.0f), d = lo + (hi - lo) * frand(&s->rng);
        float gx = a->x + cosf(ang) * d, gz = a->z + sinf(ang) * d;
        if (!walkable(s->world, gx, gz)) continue;
        if (herb && t < 10 && cret_forage(s->world, gx, gz) < 0.25f) continue;   // grazers head for thick plants
        a->goal_x = gx; a->goal_z = gz; return;
    }
    a->goal_x = a->x - cosf(a->yaw) * lo; a->goal_z = a->z - sinf(a->yaw) * lo;   // turn back
}
static float steer(const CretSim* s, const CretAgent* a, float tx, float tz, float* dist) {
    float dx = tx - a->x, dz = tz - a->z; *dist = sqrtf(dx * dx + dz * dz);
    float want = atan2f(dz, dx), d = want - a->yaw;
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

CretInput cret_bot_input(CretSim* s, int i) {
    CretAgent* a = &s->agents[i]; CretInput in = {0};
    if (!a->alive) return in;
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    bool carn = S->diet == CRET_DIET_CARNIVORE; float g = cret_growth_scale(a->growth), L = S->length * g;
    float health = a->health / fmaxf(a->max_health, 1);
    if (a->bot_timer > 0) a->bot_timer--;
    float dist;
    // Reactions that override everything: herbivores to predators, everyone to a hit.
    if (!carn) {
        int p = nearest_of(s, i, CRET_DIET_CARNIVORE, &dist);
        const CretAgent* b = p >= 0 ? &s->agents[p] : NULL;
        bool closing = b && b->speed > b->walk_speed * 1.3f;
        bool threat = b && (dist < 8.0f + L || (dist < 25.0f + L && closing) || (a->hurt_ticks > 0 && a->last_attacker == p));
        bool engaged = b && ((closing && dist < 10.0f + L) || dist < 3.0f + 0.6f * L || (a->hurt_ticks > 0 && a->last_attacker == p) ||
                             (b->bot_state == BOT_HUNT && b->bot_target == i && dist < 14.0f + L));
        if (threat && engaged && can_defend(s, a, b)) {
            if (a->bot_state != BOT_DEFEND) bot_set(a, BOT_DEFEND, 240);
            a->bot_target = p;
        } else if (threat && a->bot_state != BOT_FLEE && a->bot_state != BOT_DEFEND) {
            bot_set(a, BOT_FLEE, 90 + (int)(frand(&s->rng) * 90)); a->bot_target = p;
            // Alarm: group members close by run too.
            for (int j = 0; j < s->cfg.num_agents && a->group; j++) {
                CretAgent* c = &s->agents[j];
                if (j == i || !c->alive || c->group != a->group || c->bot_state == BOT_FLEE || c->bot_state == BOT_DEFEND) continue;
                float dx = c->x - a->x, dz = c->z - a->z; if (dx * dx + dz * dz > 40.0f * 40.0f) continue;
                bot_set(c, BOT_FLEE, 90 + (int)(frand(&s->rng) * 90)); c->bot_target = p;
            }
        }
    } else {
        // Hit while hunting by something bigger, or badly hurt: break off.
        if (health < 0.45f && a->bot_state == BOT_HUNT) bot_set(a, BOT_FLEE, 210);
        // Prey that fights back and already hurt us: not worth it.
        if (a->bot_state == BOT_HUNT && health < 0.65f && s->agents[a->bot_target].bot_state == BOT_DEFEND && a->last_attacker == a->bot_target) bot_set(a, BOT_FLEE, 150);
        if (a->bot_state == BOT_FLEE && a->bot_target >= 0 && CRET_SPECIES_TABLE[s->agents[a->bot_target].species].diet == CRET_DIET_HERBIVORE && a->last_attacker >= 0) a->bot_target = a->last_attacker;
        if (a->bot_state != BOT_HUNT && a->bot_state != BOT_FEED && a->bot_state != BOT_FLEE && a->food < 0.75f) {
            int c = nearest_carcass(s, a, 150.0f);
            if (c >= 0) { bot_set(a, BOT_FEED, 900); a->bot_target = c; }
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
    bool busy = a->bot_state == BOT_FLEE || a->bot_state == BOT_DEFEND || a->bot_state == BOT_HUNT || a->bot_state == BOT_FEED || a->bot_state == BOT_DRINK || a->bot_state == BOT_SEEK_WATER;
    if (a->water < 0.15f && (a->bot_state == BOT_FEED || a->bot_state == BOT_HUNT)) busy = false;   // parched: water first
    if (!busy && a->water < 0.35f) {
        float wx, wz;
        if (find_water(s, a, &wx, &wz)) { bot_set(a, BOT_SEEK_WATER, 1800); a->bot_target = -1; }
    }
    // A group member called: come to it (unless busy with something urgent).
    bool calm = a->bot_state == BOT_WANDER || a->bot_state == BOT_GRAZE || a->bot_state == BOT_REST;
    if (a->rally_ticks > 0 && calm) {
        float dx = a->rally_x - a->x, dz = a->rally_z - a->z;
        if (dx * dx + dz * dz > (12.0f + L) * (12.0f + L)) {
            in.turn = steer(s, a, a->rally_x, a->rally_z, &dist); in.forward = dist > 40 ? 1.0f : 0.5f;
            if (a->bot_state != BOT_WANDER) bot_set(a, BOT_WANDER, 300);
            a->goal_x = a->rally_x; a->goal_z = a->rally_z;
            return in;
        }
    }
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
        if (a->bot_timer == 0) { bot_set(a, BOT_WANDER, 600); pick_goal(s, a, 20, 50); }
        break;
    }
    case BOT_DEFEND: {
        // A tail to the predator (stegosaur), or face it (headbutt, bash); strike in reach.
        const CretAgent* b = &s->agents[a->bot_target];
        float px, pz; strike_point(a, &px, &pz);
        if (S->tail) in.turn = steer(s, a, a->x + (a->x - b->x), a->z + (a->z - b->z), &dist);
        else {
            in.turn = steer(s, a, b->x, b->z, &dist);
            float gap = body_dist(b, px, pz);
            if (gap > S->reach * g * 0.7f && gap < 6.0f) in.forward = 0.5f;   // step in to butt
        }
        if (b->alive && body_dist(b, px, pz) < S->reach * g * 0.9f) in.action = CRET_ACT_ATTACK;
        float bd = sqrtf((b->x - a->x) * (b->x - a->x) + (b->z - a->z) * (b->z - a->z));
        if (!b->alive || bd > 25.0f + L || a->bot_timer == 0 || health < 0.25f) bot_set(a, health < 0.25f && b->alive ? BOT_FLEE : BOT_GRAZE, 120);
        break;
    }
    case BOT_HUNT: {
        const CretAgent* b = &s->agents[a->bot_target];
        if (!b->alive) {   // killed: eat
            int c = nearest_carcass(s, a, 40.0f);
            if (c >= 0) { bot_set(a, BOT_FEED, 900); a->bot_target = c; } else bot_set(a, BOT_REST, 600);
            break;
        }
        float px, pz; strike_point(a, &px, &pz);
        float gap = body_dist(b, px, pz);
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
        if (gap > 22.0f && b->bot_state != BOT_FLEE && b->bot_state != BOT_DEFEND) { in.forward = 0.6f; in.sprint = 0; }
        if (gap < S->reach * g * 0.9f) in.action = CRET_ACT_ATTACK;
        float bd = sqrtf((b->x - a->x) * (b->x - a->x) + (b->z - a->z) * (b->z - a->z));
        if (a->bot_timer == 0 || bd > 90) bot_set(a, BOT_REST, 900 + (int)(frand(&s->rng) * 900));   // gave up: a long rest
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
        if (a->bot_state == BOT_DRINK && (a->water > 0.97f || !water_ahead(s, a))) a->bot_timer = 0;
        if (a->bot_state == BOT_GRAZE && (a->food > 0.97f && a->bot_timer > 60)) a->bot_timer = 60;
        if (a->bot_timer == 0) { bot_set(a, BOT_WANDER, 900); pick_goal(s, a, 30, carn ? 160 : 70); }
        break;
    default: {   // wander to the goal at a walk (sometimes a trot), then graze or rest
        in.turn = steer(s, a, a->goal_x, a->goal_z, &dist);
        in.forward = (i + a->ticks_alive / 600) % 4 == 0 ? 1.0f : 0.5f;
        if (fabsf(in.turn) > 0.8f) in.forward *= 0.6f;
        if (dist < 4 + a->radius || a->bot_timer == 0) {
            bool graze = !carn && cret_forage(s->world, a->x + cosf(a->yaw) * L * 0.45f, a->z + sinf(a->yaw) * L * 0.45f) > 0.1f;
            if (water_ahead(s, a) && a->water < 0.8f) bot_set(a, BOT_DRINK, 300);
            else if (carn || !graze) { if (carn) bot_set(a, BOT_REST, 120 + (int)(frand(&s->rng) * 180)); else { pick_goal(s, a, 20, 60); a->bot_timer = 600; } }
            else bot_set(a, BOT_GRAZE, 180 + (int)(frand(&s->rng) * 240));
        }
        if (a->goal_x == 0 && a->goal_z == 0) pick_goal(s, a, 30, 90);
    }
    }
    if (a->broken > 0 && a->bot_state != BOT_FLEE) in.sprint = 0;
    // Stuck (against a rock, in a hollow, at a cliff foot) while trying to
    // move: walk off in a random direction for a few seconds.
    if (a->escape_ticks > 0) {
        a->escape_ticks--;
        float d = wrapa(a->escape_yaw - a->yaw);
        CretInput e = {.turn = clampf(d * 2, -1, 1), .forward = fabsf(d) < 1.0f ? 1.0f : 0.3f};
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
