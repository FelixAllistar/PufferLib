#include "sim.h"
#include "abilities.h"
#include "progress.h"
#include "eco.h"
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
    return clampf(fmaxf(f / 200.0f, 0.65f * cret_world_grassland(w, x, z)), 0, 1);   // open grassland is good grazing (its ferns were thinned with its trees)
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
// How long a carcass lies: carcass_seconds for a body of 40 kg of meat or
// less, longer for a big one by the square root of its meat, up to four times
// (a stegosaur feeds the island for a day; a raptor is gone in a quarter of one).
float cret_carcass_life(const CretSim* s, const CretCarcass* k) {
    float base = s->cfg.carcass_seconds > 0 ? s->cfg.carcass_seconds : 300.0f;
    return base * clampf(sqrtf(k->max_meat / 40.0f), 1, 4);
}
// A living predator of another species within r of (x, z).
bool cret_predator_near(const CretSim* s, float x, float z, float r, int species) {
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (!b->alive || b->species == species || CRET_SPECIES_TABLE[b->species].diet != CRET_DIET_CARNIVORE) continue;
        float dx = b->x - x, dz = b->z - z; if (dx * dx + dz * dz < r * r) return true;
    }
    return false;
}
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
// An id for a group agent i founds: its slot + 1 when nobody carries that id,
// else the next unused one. The members of a group an animal founded keep its
// id after it dies; founding "slot + 1" again on respawn put it back among
// them and over the species' cap.
int cret_group_new(const CretSim* s, int i) {
    for (int k = 0; k < 255; k++) { int g = 1 + (i + k) % 255; if (cret_group_size(s, g) == 0) return g; }
    return i + 1;
}

// ---------------------------------------------------------------- mover
typedef struct MoverPush { b3BodyId body; b3Vec3 normal, point; } MoverPush;   // a movable prop touched (dynprops.c)
typedef struct MoverCtx { CretSim* s; int self; b3CollisionPlane planes[16]; int count; MoverPush push[4]; int npush; } MoverCtx;
static void kill(CretSim* s, int i);
static void break_bone(CretAgent* b, float seconds);
#include "dynprops.c"
// Carrying (below): a new small item on the ground (critters.c leaves the rest of a catch with it).
static int item_spawn(CretSim* s, int item, int species, int critter, float x, float z, float yaw, float kg);
#include "critters.c"   // small prey around people (critters.h)
#include "scent.c"      // trails, wind and the hunting sense (scent.h)
#include "eco.c"        // energy, plants and fear (eco.h)
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
    a->group = best >= 0 ? s->agents[best].group : (uint8_t)cret_group_new(s, i);
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
    cret_carry_drop(s, i, false);   // a living animal swapped out (species change) puts its load down
    memset(a, 0, sizeof(*a)); a->body = body; a->shape = shape; a->species = (uint8_t)sp;
    a->alive = true; a->stamina = 1; a->last_attacker = -1; a->def_speed = 1;
    bool person = cret_is_person(s, i);   // the local player or a network peer
    float g0 = person ? s->cfg.player_growth : s->cfg.spawn_growth;
    a->growth = g0 >= 0 ? clampf(g0, 0.35f, 1.0f) : 0.55f + 0.45f * frand(&s->rng);
    // A nest of its species (the player's own group's) hatches it as a juvenile.
    int nest = cret_nest_spawn(s, i, sp, person ? (old_species == sp ? old_group : 0) : -1);
    if (nest >= 0) a->growth = 0.5f;
    a->food = 0.75f + 0.25f * frand(&s->rng); a->water = 0.75f + 0.25f * frand(&s->rng); a->fat = ECO_SPAWN_FAT;
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
        // The nest's group may have filled since the nest was picked (it follows its owner's group).
        else if (n->group && cret_group_size(s, n->group) < S->group_max) a->group = n->group; else join_group(s, i);
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
        // A herbivore does not appear beside a predator (one at the herd killed each newcomer as it came): farther out with each try.
        if (herb && !person && tries < 16 && cret_predator_near(s, a->x, a->z, 90.0f, sp)) { rmax += 25.0f; continue; }
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
    memset(s->carcasses, 0, sizeof s->carcasses); cret_critters_clear(s); cret_scent_clear(s);
    memset(s->nests, 0, sizeof s->nests); memset(s->gquest, 0, sizeof s->gquest);
    memset(s->plants, 0, sizeof s->plants); s->nplants = 0;   // the plants grow back over the island
    memset(s->fear, 0, sizeof s->fear); s->fear_head = 0;
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
// body_near gives the point on that segment nearest (px, pz).
static void body_near(const CretAgent* b, float px, float pz, float* qx, float* qz) {
    float hl = CRET_SPECIES_TABLE[b->species].length * cret_growth_scale(b->growth) * 0.4f;
    float cy = cosf(b->yaw), sy = sinf(b->yaw), t = clampf((px - b->x) * cy + (pz - b->z) * sy, -hl, hl);
    *qx = b->x + cy * t; *qz = b->z + sy * t;
}
static float body_dist(const CretAgent* b, float px, float pz) {
    float qx, qz; body_near(b, px, pz, &qx, &qz);
    return fmaxf(hypotf(px - qx, pz - qz) - b->radius, 0.0f);
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
float cret_mass(const CretAgent* a) { float g = cret_growth_scale(a->growth); return CRET_SPECIES_TABLE[a->species].mass * g * g * g; }

static void kill(CretSim* s, int i) {
    CretAgent* a = &s->agents[i];
    if (!a->alive) return;
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    if (a->latch) cret_release_latch(s, i, false);
    cret_carry_drop(s, i, true);   // the jaws let go
    a->alive = false; a->dead_ticks = 0; a->ev_died = 1; a->act = 0; a->health = 0; a->strike = 0; a->leap = 0;
    int killer = a->last_attacker >= 0 && a->last_attacker < s->cfg.num_agents ? a->last_attacker : -1;
    s->stats.deaths[a->species]++;
    // Thirst with food or fat left is thirst; an empty gut and no reserve is starvation.
    int cause = killer >= 0 && (a->hurt_ticks > 0 || a->bleed > 0) ? CRET_DEATH_PREDATION : a->water <= 0 && (a->food > 0 || a->fat > 0) ? CRET_DEATH_THIRST :
                a->food <= 0 ? CRET_DEATH_STARVED : a->water <= 0 ? CRET_DEATH_THIRST : CRET_DEATH_OTHER;
    s->stats.death_cause[a->species][cause]++;
    if (killer >= 0) { s->stats.kills[s->agents[killer].species][a->species]++; cret_quest_on_kill(s, killer, i); }
    else s->stats.deaths_env[a->species]++;
    int slot = 0, oldest = -1;
    for (int c = 0; c < CRET_MAX_CARCASSES; c++) {
        if (!s->carcasses[c].active) { slot = c; oldest = -2; break; }
        if (s->carcasses[c].carrier) continue;   // held in a mouth: never taken away
        if (oldest < 0 || s->carcasses[c].age > s->carcasses[oldest].age) oldest = c;
    }
    if (oldest >= 0) slot = oldest;   // all taken: the oldest rots away now
    if (oldest == -1 && s->carcasses[slot].carrier) cret_carry_drop(s, s->carcasses[slot].carrier - 1, false);   // every slot carried (never in practice)
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
    // A shove, not a launch: at most CRET_KNOCK_MAX m/s, and only a heavy blow (a tail, a headbutt, a
    // far bigger animal) lifts the feet, a little. One already in the air from a blow is not thrown again
    // (two gallimimus passed a raptor down a mountain between them).
    float k = clampf(CRET_KNOCK * ma / (ma + mb) * knock * (AB ? AB->knock : 1), 0, CRET_KNOCK_MAX);
    if (!b->on_ground && !b->swimming) k *= 0.3f;
    b->vx += dx / dl * k; b->vz += dz / dl * k;
    if (k > CRET_KNOCK_LIFT && b->on_ground && !b->swimming) { b->vy = k * 0.15f; b->on_ground = false; }
    // Bones break under a crushing blow (a third of the victim's health at
    // once) or a hard throw by a heavier or equal animal.
    if (hp > 0.33f * b->max_health || (k > 3.4f && ma > 0.8f * mb && frand(&s->rng) < 0.35f)) break_bone(b, 45.0f + 45.0f * frand(&s->rng));
    // Spikes: biting into a sauropelta cuts the biter (not a tail swing behind).
    if (SB->thorns > 0 && !cret_rear_attack(a) && a->alive) {
        float back = fminf(raw * SB->thorns, a->health);
        a->health -= back; a->bleed += back * 0.03f * (1 - SA->bleed_resist); a->hurt_ticks = 8; a->last_attacker = victim;
        a->ev_taken += back / fmaxf(a->max_health, 1); b->ev_dealt += back / fmaxf(a->max_health, 1);
        if (back > 0) cret_log_hit(s, victim, attacker, CRET_HIT_THORNS, back / fmaxf(a->max_health, 1));
        if (a->health <= 0.01f) kill(s, attacker);
    }
    cret_status_hit(s, attacker, victim, hp, raw, k);
    // A hard blow or a throw makes a carrier drop what it holds (a stun does too, carry_step).
    if (b->carry && (hp >= CRET_CARRY_KNOCK * b->max_health || k > 3.0f)) cret_carry_drop(s, victim, true);
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
    // Small prey (critters.c): caught when it is nearer the jaws than any
    // animal's body. Usually the snap has already taken it (resolve_snap).
    float cd; int c = a->snapped ? -1 : cret_critter_in_reach(s, i, px, pz, reach, &cd);
    if (c >= 0 && (best < 0 || cd < bd)) { cret_critter_bite(s, i, c); return; }
    if (a->snapped) return;
    if (best >= 0) damage(s, i, best, S->damage * g * g * mult * (0.85f + 0.3f * frand(&s->rng)));
}

// The lunge: through the wind-up of a primary blow in front (a bite, a
// headbutt, a bash, a kick), the attacker is carried toward the body the
// blow would land on, so prey that steps back, or an attacker that stopped
// to bite, still meets it. Each tick takes its share of the gap the blow
// would have at the hit (the gap now, plus how fast the target draws away
// along the line between them), aiming to have the body half way into
// reach, at most the lunge (lunge_budget) over the whole wind-up, so reach
// never grows by more than a quarter body length. Only toward an animal whose body is within
// reach plus that budget of the jaws and in front of them (within 60
// degrees of where the blow points); never for tail blows, charges (they run
// in by themselves), in the air or swimming. It follows the attack, not the
// input: bots, people and policies get the same lunge. The step goes through
// the mover, so bodies still block it.
// The body a primary blow in front would reach for: the nearest within
// reach plus the lunge of the jaws and in front of them (-1: none), its gap
// and the direction to it. The lunge is CRET_LUNGE body lengths, or as far as
// the animal can throw itself from a stand in the wind-up at its own
// acceleration (half accel x hit_time squared), whichever is shorter: a
// raptor 0.75 m, a pachycephalosaur 1.1 m, a tyrannosaur 0.9 m, a gallimimus
// 0.36 m, a heavy sauropelta 0.16 m.
static float lunge_budget(const CretAgent* a) {
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    return fminf(CRET_LUNGE * S->length * g, 0.5f * S->accel * (1.3f - 0.3f * g) * S->hit_time * S->hit_time);
}
static int lunge_target(const CretSim* s, int i, float* gap, float* bx, float* bz) {
    const CretAgent* a = &s->agents[i]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    const CretAbility* AB = a->strike ? &CRET_ABILITIES[a->strike] : NULL;
    float reach = S->reach * g * (AB ? AB->reach : 1), budget = lunge_budget(a);
    float px, pz; strike_point(a, &px, &pz);
    float dir = cret_attack_dir(a, false), ux = cosf(dir), uz = sinf(dir);
    int best = -1; float bd = reach + budget;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (j == i || !b->alive || fabsf(b->y - a->y) > a->height + b->height || (a->group && b->group == a->group)) continue;
        float qx, qz; body_near(b, px, pz, &qx, &qz);
        float dx = qx - px, dz = qz - pz, dl = hypotf(dx, dz), d = fmaxf(dl - b->radius, 0);
        if (d >= bd || (dl > 1e-3f && (dx * ux + dz * uz) < 0.5f * dl)) continue;   // nearer than the last, and in front of the jaws
        best = j; bd = d; *bx = dl > 1e-3f ? dx / dl : ux; *bz = dl > 1e-3f ? dz / dl : uz;
    }
    *gap = bd;
    return best;
}
static void lunge_step(const CretSim* s, int i, float* lx, float* lz) {
    const CretAgent* a = &s->agents[i]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    *lx = *lz = 0;
    int hit = (int)(S->hit_time * CRET_TICK_HZ);
    if (a->act != CRET_ACT_ATTACK || S->tail || a->act_ticks >= hit || a->swimming || !a->on_ground || cret_strike_dash(a) > 0) return;
    const CretAbility* AB = a->strike ? &CRET_ABILITIES[a->strike] : NULL;
    float reach = S->reach * g * (AB ? AB->reach : 1), budget = lunge_budget(a);
    float bd, bx = 0, bz = 0; int best = lunge_target(s, i, &bd, &bx, &bz);
    if (best < 0) return;
    const CretAgent* b = &s->agents[best]; int left = hit - a->act_ticks;
    float away = ((b->vx - a->vx) * bx + (b->vz - a->vz) * bz) * left * CRET_DT;   // drawn away by the hit (negative: closing)
    float step = clampf((bd + away - 0.5f * reach) / (float)left, 0, budget / (float)hit);
    *lx = bx * step; *lz = bz * step;
}

// The snap: a bite at small prey is a quick dart of the head, not the full
// swing a blow on an animal needs. CRET_SNAP_TICKS into a bite, a critter
// within the bite's reach (plus a quarter for the lunge of the neck) of the
// strike point is caught, if no animal's body is nearer; the bite then lands
// on nothing else. A mouse that hears the bite coming still gets its 0.15 s
// reaction (critters.c), so it has to be caught unawares or cornered.
static void resolve_snap(CretSim* s, int i) {
    CretAgent* a = &s->agents[i]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    if (!s->ncrit || a->strike) return;   // special bites (abilities) keep their own timing
    float px, pz; strike_point(a, &px, &pz);
    float reach = S->reach * g * 1.25f, cd; int c = cret_critter_in_reach(s, i, px, pz, reach, &cd);
    if (c < 0) return;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (j == i || !b->alive || fabsf(b->y - a->y) > a->height + b->height || (a->group && b->group == a->group)) continue;
        if (body_dist(b, px, pz) < cd) return;   // an animal is in the way: the full bite decides
    }
    cret_critter_bite(s, i, c); a->snapped = 1;
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

// Distance from (x, z) to carcass k: to the body of a dead animal, to the small item itself.
static float item_dist(const CretCarcass* k, float x, float z) {
    if (k->item != CRET_ITEM_BODY) return fmaxf(hypotf(k->x - x, k->z - z) - 0.15f, 0);
    CretAgent tmp = {.species = k->species, .growth = k->growth, .x = k->x, .z = k->z, .yaw = k->yaw,
                     .radius = CRET_SPECIES_TABLE[k->species].radius * cret_growth_scale(k->growth)};
    return body_dist(&tmp, x, z);
}
// Nearest carcass with meat on the ground whose body is within `reach` of (x, z); -1 if none.
static int carcass_near(const CretSim* s, float x, float z, float reach) {
    int best = -1; float bd = reach;
    for (int c = 0; c < CRET_MAX_CARCASSES; c++) {
        const CretCarcass* k = &s->carcasses[c]; if (!k->active || k->meat <= 0 || k->carrier) continue;
        float d = item_dist(k, x, z); if (d < bd) { bd = d; best = c; }
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
    if (S->diet == CRET_DIET_HERBIVORE) return cret_graze_at(s, hx, hz) > 0.1f ? 1 : 0;
    if (carcass_near(s, hx, hz, 1.0f + S->reach * g) >= 0 || carcass_near(s, a->x, a->z, S->length * g * 0.55f + S->reach * g + 1.0f) >= 0) return 1;
    int k = carcass_near(s, a->x, a->z, 20.0f);
    float px, pz, cd; strike_point(a, &px, &pz);
    if (cret_critter_in_reach(s, i, px, pz, S->reach * g, NULL) >= 0) return 3;
    if (k >= 0) { if (dist) *dist = hypotf(s->carcasses[k].x - a->x, s->carcasses[k].z - a->z); return 2; }
    if (cret_critter_nearest(s, i, 20.0f, &cd) >= 0) { if (dist) *dist = cd; return 4; }
    return 0;
}

// ---------------------------------------------------------------- carrying
// Carnivores carry food in the mouth: a carcass light enough (at most
// CRET_CARRY_RATIO of the carrier's mass), the rest of a caught critter, or
// an egg taken from another group's nest. A carry press (CretInput.carry)
// picks the nearest such thing up at the mouth, or puts down what it holds.
// Carrying slows the animal by the load and blocks biting, eating and
// drinking; a hard blow, a stun or death makes it drop. The item stays a
// carcass slot the whole time, so it keeps rotting and anyone can eat from
// it once it lies on the ground again (a parent's kill for the young at the nest).
float cret_item_kg(const CretCarcass* k) { return k->item == CRET_ITEM_BODY ? 2.0f * k->meat : k->meat; }
float cret_carry_limit(const CretAgent* a) {
    return CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_CARNIVORE ? CRET_CARRY_RATIO * cret_mass(a) : 0;
}
void cret_mouth_point(const CretAgent* a, float* x, float* y, float* z) {
    float L = CRET_SPECIES_TABLE[a->species].length * cret_growth_scale(a->growth);
    *x = a->x + cosf(a->yaw) * L * 0.45f; *z = a->z + sinf(a->yaw) * L * 0.45f; *y = a->y + a->height * 0.7f;
}
// A free slot for a new item, else the oldest item nobody holds (bodies are kept); -1 if none.
static int item_slot(const CretSim* s) {
    int best = -1;
    for (int c = 0; c < CRET_MAX_CARCASSES; c++) {
        const CretCarcass* k = &s->carcasses[c];
        if (!k->active) return c;
        if (k->item != CRET_ITEM_BODY && !k->carrier && (best < 0 || k->age > s->carcasses[best].age)) best = c;
    }
    return best;
}
static int item_spawn(CretSim* s, int item, int species, int critter, float x, float z, float yaw, float kg) {
    int c = item_slot(s); if (c < 0) return -1;
    s->carcasses[c] = (CretCarcass){.active = true, .species = (uint8_t)species, .x = x, .y = cret_height(s->world, x, z), .z = z, .yaw = yaw,
                                    .growth = 1, .meat = kg, .max_meat = kg, .agent = -1, .item = (uint8_t)item, .critter = (uint8_t)critter};
    return c;
}
// What lies at agent i's mouth that it could lift: the nearest carcass slot
// (*slot) within reach, or an egg at a nest (*nest). *heavy: a carcass in
// reach that is too heavy (-1: none).
static int carry_find(const CretSim* s, int i, int* slot, int* nest, int* heavy) {
    const CretAgent* a = &s->agents[i]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    float mx, my, mz, limit = cret_carry_limit(a); cret_mouth_point(a, &mx, &my, &mz);
    *slot = *nest = *heavy = -1;
    if (limit <= 0) return 0;
    float bd = 1.0f + S->reach * g, hd = bd;
    for (int c = 0; c < CRET_MAX_CARCASSES; c++) {
        const CretCarcass* k = &s->carcasses[c]; if (!k->active || k->meat <= 0 || k->carrier) continue;
        float d = item_dist(k, mx, mz);
        if (cret_item_kg(k) > limit) { if (d < hd) { hd = d; *heavy = c; } continue; }
        if (d < bd) { bd = d; *slot = c; }
    }
    if (*slot >= 0) return 2;
    if (CRET_EGG_KG <= limit && (*nest = cret_nest_egg_at(s, i, mx, mz)) >= 0) return 3;
    return 0;
}
int cret_carry_state(const CretSim* s, int i, int* what) {
    const CretAgent* a = &s->agents[i]; int slot, nest, heavy;
    if (what) *what = -1;
    if (!a->alive) return 0;
    if (a->carry) { if (what) *what = a->carry - 1; return 1; }
    int r = carry_find(s, i, &slot, &nest, &heavy);
    if (what) *what = r == 2 ? slot : r == 3 ? nest : heavy >= 0 ? -(heavy + 2) : -1;
    return r;
}
void cret_carry_drop(CretSim* s, int i, bool forced) {
    CretAgent* a = &s->agents[i];
    if (!a->carry) return;
    int c = a->carry - 1; a->carry = 0; a->carry_kg = 0;
    CretCarcass* k = &s->carcasses[c];
    if (!k->active || k->carrier != i + 1) return;
    float x, y, z; cret_mouth_point(a, &x, &y, &z);
    k->carrier = 0; k->x = clampf(x, 2, s->world->size - 2); k->z = clampf(z, 2, s->world->size - 2); k->y = cret_height(s->world, k->x, k->z); k->yaw = a->yaw;
    if (forced) s->stats.forced_drops++;
    else {   // put down at its own (or its group's) nest: food brought home
        int n = cret_nest_of(s, i); float r = n >= 0 ? cret_nest_radius(s->nests[n].species) + 2.0f : 0;
        if (n >= 0 && (k->x - s->nests[n].x) * (k->x - s->nests[n].x) + (k->z - s->nests[n].z) * (k->z - s->nests[n].z) < r * r) s->stats.carried_home++;
    }
}
int cret_carry_tear(CretSim* s, int i, int c, float kg) {
    CretAgent* a = &s->agents[i];
    if (c < 0 || c >= CRET_MAX_CARCASSES || a->carry || !a->alive || cret_carry_limit(a) <= 0) return -1;
    CretCarcass* k = &s->carcasses[c]; float mx, my, mz; cret_mouth_point(a, &mx, &my, &mz);
    if (!k->active || k->item != CRET_ITEM_BODY || k->carrier || item_dist(k, mx, mz) > 1.0f + CRET_SPECIES_TABLE[a->species].reach * cret_growth_scale(a->growth)) return -1;
    kg = fminf(fminf(kg, cret_carry_limit(a)), k->meat - 0.1f);
    if (kg < 0.5f) return -1;
    int p = item_spawn(s, CRET_ITEM_MEAT, k->species, 0, mx, mz, a->yaw, kg);
    if (p < 0) return -1;
    k = &s->carcasses[c]; k->meat -= kg;
    s->carcasses[p].carrier = (uint8_t)(i + 1); a->carry = (uint8_t)(p + 1); a->carry_kg = kg; s->stats.pickups++;
    return p;
}
// A carry press: put down what it holds, else pick up what lies at the mouth.
static void carry_press(CretSim* s, int i) {
    CretAgent* a = &s->agents[i];
    if (a->carry) { cret_carry_drop(s, i, false); return; }
    if (cret_act_is_attack(a->act) || a->leap || a->latch) return;
    int slot, nest, heavy, r = carry_find(s, i, &slot, &nest, &heavy);
    if (r == 3) {   // an egg from another group's nest, taken whole
        float mx, my, mz; cret_mouth_point(a, &mx, &my, &mz);
        slot = item_spawn(s, CRET_ITEM_EGG, s->nests[nest].species, 0, mx, mz, a->yaw, CRET_EGG_KG);
        if (slot < 0) return;
        cret_nest_raided(s, i, nest);   // a raid, as eating one there (and a raid quest's egg)
    } else if (r != 2) return;
    CretCarcass* k = &s->carcasses[slot];
    k->carrier = (uint8_t)(i + 1); a->carry = (uint8_t)(slot + 1); a->carry_kg = cret_item_kg(k);
    if (a->act == CRET_ACT_EAT || a->act == CRET_ACT_DRINK) a->act = 0;
    s->stats.pickups++;
}
void cret_carry_follow(CretSim* s) {
    for (int c = 0; c < CRET_MAX_CARCASSES; c++) {
        CretCarcass* k = &s->carcasses[c]; int i = k->carrier - 1;
        if (!k->active || i < 0 || i >= s->cfg.num_agents || !s->agents[i].alive) continue;
        cret_mouth_point(&s->agents[i], &k->x, &k->y, &k->z); k->yaw = s->agents[i].yaw;
    }
}
// Once a tick: the carried item follows the mouth; a stunned carrier drops
// it; an item that rotted away (or was taken over) frees the mouth.
static void carry_step(CretSim* s) {
    for (int i = 0; i < s->cfg.num_agents; i++) {
        CretAgent* a = &s->agents[i]; if (!a->carry) continue;
        CretCarcass* k = &s->carcasses[a->carry - 1];
        if (!a->alive || !k->active || k->carrier != i + 1) { a->carry = 0; a->carry_kg = 0; continue; }
        if (a->st[CRET_ST_STUN] > 0) { cret_carry_drop(s, i, true); continue; }
        cret_mouth_point(a, &k->x, &k->y, &k->z); k->yaw = a->yaw;
    }
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
        if (S->diet == CRET_DIET_HERBIVORE) {   // Holling intake on the plants at the mouth, taken off the cell (eco.c)
            float G = cret_eco_gut_kg(a), d = cret_eco_graze(s, a, hx, hz, (1 - a->food) * G) / G; a->food += d; a->ev_food += d;
        } else {
            int k = carcass_near(s, hx, hz, 1.0f + S->reach * g);
            if (k < 0) k = carcass_near(s, a->x, a->z, S->length * g * 0.55f + S->reach * g + 1.0f);   // lying beside it, not just square on
            if (k >= 0) {   // a full gut is a quarter of body mass (ECO_GUT_CARN), eaten in ~12 s
                float mass = S->mass * g * g * g, G = cret_eco_gut_kg(a), kg = fminf(fminf(0.02f * mass * dt, s->carcasses[k].meat), (1 - a->food) * G);
                s->carcasses[k].meat -= kg; float d = kg / G; a->food += d; a->ev_food += d;
                cret_quest_on_scavenge(s, i, kg);
            } else { float d = cret_nest_eat(s, i, hx, hz); a->food += d; a->ev_food += d; }   // eggs from a nest
        }
    }
    // Energy (eco.h): basal and movement burn from the gut, the surplus of a
    // well-filled gut laid down as fat, fat burned once the gut is empty. A
    // big meal makes a carnivore digest until its gut is half empty.
    float gain = a->ev_food, burned = cret_eco_spend(a, cret_eco_burn(c, a) * dt);
    s->stats.eaten_kg[a->species] += gain * cret_eco_gut_kg(a); s->stats.burned_kg[a->species] += burned;
    a->meal = gain > 0 ? a->meal + gain : a->meal * expf(-dt / ECO_MEAL_FADE);
    if (a->meal >= ECO_DIGEST_MEAL && S->diet == CRET_DIET_CARNIVORE) a->digest = 1;
    if (a->food < ECO_DIGEST_END) a->digest = 0;
    a->water = fmaxf(a->water - cret_eco_thirst(s, a) * dt, 0);
    float loss = a->bleed;
    if (a->food <= 0 && a->fat <= 0) loss += a->max_health * 0.01f;
    if (a->water <= 0) loss += a->max_health * 0.015f;
    a->bleed *= expf(-0.03f * dt); if (a->bleed < 0.01f) a->bleed = 0;
    if (a->food > 0.3f && a->water > 0.3f && a->bleed < 0.002f * a->max_health)
        a->health = fminf(a->health + a->max_health * (a->act == CRET_ACT_REST ? CRET_HEAL_REST : CRET_HEAL) * dt, a->max_health);   // wounds last: half a game day on the move, a tenth of one lying up
    if (loss > 0) { float d = fminf(loss * dt, a->health); a->health -= d; a->ev_taken += d / fmaxf(a->max_health, 1); }
    if (a->health <= 0.01f) { kill(s, i); return; }
    if (a->growth < 1 && a->food > ECO_GROW_FOOD && a->fat > ECO_GROW_FAT && a->water > 0.25f) {
        a->growth = fminf(a->growth + 0.65f * dt / c->growth_seconds, 1.0f); cret_apply_growth(a);
        if (a->growth - a->shape_growth > 0.02f || a->growth >= 1) update_shape(a);
    }
    if (a->broken > 0) a->broken = fmaxf(a->broken - dt * (a->act == CRET_ACT_REST ? 2.5f : 1.0f), 0);
    if (!s->hold[i]) agent_timers(a);   // a held agent's timers wait for its input (cret_sim_move)
}

// Target ground speed for an input: forward 0.5 walks, 1 trots, sprint (with
// stamina) runs; backward and sideways steps are slow.
static float gait_speed(const CretAgent* a, float fwd, int sprint, bool winded) {
    if (sprint) return a->sprint_speed;
    if (fwd <= 0) return a->walk_speed * 0.6f * fminf(-fwd * 2, 1);
    if (fwd <= 0.5f) return a->walk_speed * fwd * 2;
    // Winded (the scripted animals only: a person keeps their trot, it felt like crawling): out of breath
    // the trot falls to CRET_WINDED_TROT of the way from a walk and comes back as the stamina does (full at
    // CRET_WINDED). A chase ends: prey that ran itself empty is caught up by a hunter with wind left.
    float trot = winded ? CRET_WINDED_TROT + (1 - CRET_WINDED_TROT) * clampf(a->stamina / CRET_WINDED, 0, 1) : 1;
    return a->walk_speed + (a->trot_speed - a->walk_speed) * (fwd - 0.5f) * 2 * trot;
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
        else if (!a->group && !b->group) { a->group = (uint8_t)cret_group_new(s, i); b->group = a->group; }   // found one together
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
    // One key eats and drinks. With water ahead and food in reach too (a carcass at the shore, plants at
    // the bank) it takes what it needs more; animals stood drinking beside their food until they starved.
    if (want == CRET_ACT_EAT || want == CRET_ACT_DRINK)
        want = !water_ahead(s, a) ? CRET_ACT_EAT : a->food < a->water && cret_food_state(s, i, NULL) == 1 ? CRET_ACT_EAT : CRET_ACT_DRINK;
    if (in->carry && !predict) carry_press(s, i);   // the server's, like eating (clients get the result in snapshots)
    // A full mouth: no eating, drinking or biting (tail swings and slashes still work).
    if (a->carry && (want == CRET_ACT_EAT || want == CRET_ACT_DRINK || (want == CRET_ACT_ATTACK && !S->tail))) want = CRET_ACT_NONE;
    // An attack pressed while one is swinging or cooling down waits up to
    // CRET_ATTACK_BUFFER ticks and starts the moment it can.
    if (cret_act_is_attack(want)) { a->buf_act = (uint8_t)want; a->buf_ticks = CRET_ATTACK_BUFFER + 1; }
    else if (!want && a->buf_ticks) want = a->buf_act;
    if (a->buf_ticks && --a->buf_ticks == 0) a->buf_act = 0;
    int hit = (int)(S->hit_time * CRET_TICK_HZ), hit2 = (int)(S->alt.hit_time * CRET_TICK_HZ);
    if (a->act) {
        a->act_ticks++;
        int len = a->act == CRET_ACT_ATTACK ? (int)(S->attack_time * CRET_TICK_HZ) : a->act == CRET_ACT_ATTACK2 ? (int)(S->alt.attack_time * CRET_TICK_HZ) : 60;
        if (a->act == CRET_ACT_ATTACK && hit > CRET_SNAP_TICKS && a->act_ticks == CRET_SNAP_TICKS && !predict) resolve_snap(s, i);
        if (a->act == CRET_ACT_ATTACK && a->act_ticks == hit && !predict) { resolve_attack(s, i); if (!a->alive) return; }
        if (a->act == CRET_ACT_ATTACK2 && a->act_ticks == hit2 && !predict) { resolve_alt(s, i); if (!a->alive) return; }
        if (one_shot ? a->act_ticks > len : (want != a->act || fabsf(fwd) > 0.05f || fabsf(str) > 0.05f)) { a->act = 0; a->strike = 0; }
    }
    bool can = want == CRET_ACT_ATTACK ? a->attack_cd == 0 && !a->swimming :
               want == CRET_ACT_ATTACK2 ? a->attack2_cd == 0 && !a->swimming :
               a->on_ground && !a->swimming && (a->speed < 1.0f || want == CRET_ACT_CALL);
    // A bite queued during the cooldown waits in the queue while the animal
    // is off its feet (thrown by a blow: it could not lunge) or while its
    // target draws away faster than the lunge could follow by the hit, and
    // lapses if, when it comes due, nothing is within reach and lunge in
    // front any more: the click was meant for an animal that has gone. A
    // fresh click always swings; so do tail blows, sweeps and slashes,
    // queued or not.
    if (!a->act && want == CRET_ACT_ATTACK && can && in->action != CRET_ACT_ATTACK && !S->tail) {
        float gap, bx, bz; int t = lunge_target(s, i, &gap, &bx, &bz);
        if (t < 0) { want = CRET_ACT_NONE; a->buf_act = 0; a->buf_ticks = 0; }
        else {
            const CretAgent* b = &s->agents[t]; float g = cret_growth_scale(a->growth);
            float away = ((b->vx - a->vx) * bx + (b->vz - a->vz) * bz) * S->hit_time;
            if ((!a->on_ground && !a->swimming) || gap + away > S->reach * g + lunge_budget(a)) want = CRET_ACT_NONE;
        }
    }
    if (!a->act && want && can) {
        a->act = (uint8_t)want; a->act_ticks = 0; a->strike = 0; a->snapped = 0;
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
    float top = a->swimming ? a->swim_speed * fmaxf(fwd, 0) * 2.0f / (1.0f + fmaxf(fwd, 0)) : gait_speed(a, fwd, sprint, !predict && !cret_is_person(s, i));
    if (sprint && burst) top *= CRET_ABILITIES[CRET_AB_SPRINT_BURST].speed;
    if (dash > 0 && !a->swimming) top = a->sprint_speed * dash;
    if (a->broken > 0) top *= 0.55f;   // limping
    if (a->health < CRET_HURT * a->max_health) top *= 1 - CRET_HURT_SLOW * (1 - a->health / (CRET_HURT * a->max_health));   // hurt: slower the worse it is
    if (a->carry) top *= 1 - CRET_CARRY_SLOW * clampf(a->carry_kg / fmaxf(cret_mass(a), 1e-3f), 0, CRET_CARRY_RATIO);   // the load
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
    float lx, lz; lunge_step(s, i, &lx, &lz);   // a blow in its wind-up reaches for the body in front (the step, not the momentum)
    b3Vec3 target = {a->vx * dt + lx, 0, a->vz * dt + lz};
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
    cret_eco_step(s);   // the landscape of fear (eco.c)
    cret_progress_step(s);
    cret_critters_step(s);   // small prey around people (critters.c)
    // Carcasses rot (nine tenths of the meat spoils over its life) and vanish when eaten or old.
    for (int c = 0; c < CRET_MAX_CARCASSES; c++) {
        CretCarcass* k = &s->carcasses[c]; if (!k->active) continue;
        float life = cret_carcass_life(s, k);
        k->age++; k->meat -= k->max_meat * 0.9f / life * CRET_DT;
        if (k->meat <= 0 || k->age > life * CRET_TICK_HZ) k->active = false;
    }
    carry_step(s);   // after the rot: a carried item that is gone frees the mouth
    cret_scent_record(s, false);   // trails behind critters and animals (scent.c), only with people about
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
            const CretCarcass* cc = &s->carcasses[c]; if (!cc->active || cc->meat <= 0 || cc->carrier) continue;
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
    // The terrain patch: every sample point first, then the heights and water
    // levels in one batched lookup each (the same values as cret_height and
    // cret_water point by point, in a fraction of the time).
    {
        enum { NP = CRET_PATCH * CRET_PATCH };
        int half = CRET_PATCH / 2, p = 0; float px_[NP], pz_[NP], h[NP], wa[NP], d[2 * NP];
        for (int pz = -half; pz <= half; pz++) for (int px = -half; px <= half; px++, p++) {
            float lx = px * CRET_PATCH_SPACING, lz = pz * CRET_PATCH_SPACING;
            px_[p] = a->x + cy * lz - sy * lx; pz_[p] = a->z + sy * lz + cy * lx;
        }
        cret_heights(w, px_, pz_, NP, h); cret_waters(w, px_, pz_, NP, wa);
        for (p = 0; p < NP; p++) { d[2 * p] = (h[p] - g) / 20; d[2 * p + 1] = (wa[p] - h[p]) / 3; }
        for (p = 0; p < NP; p++) { o[k++] = clampf(d[2 * p], -2, 2); o[k++] = clampf(d[2 * p + 1], -1, 2); }
    }
    // Nearest agents, egocentric.
    int best[CRET_NEIGHBORS]; float bd[CRET_NEIGHBORS];
    for (int q = 0; q < CRET_NEIGHBORS; q++) { best[q] = -1; bd[q] = 1e30f; }
    // Squared distances first (itself and the dead at infinity, so never picked),
    // then an insertion into the nearest CRET_NEIGHBORS; ties keep the lower slot.
    float dist[CRET_MAX_AGENTS]; int na = s->cfg.num_agents;
    for (int j = 0; j < na; j++) {
        float dx = s->agents[j].x - a->x, dz = s->agents[j].z - a->z;
        dist[j] = j == i || !s->agents[j].alive ? INFINITY : dx * dx + dz * dz;
    }
    for (int j = 0; j < na; j++) {
        float d = dist[j]; if (!(d < bd[CRET_NEIGHBORS - 1])) continue;   // not nearer than the sixth
        for (int q = 0; q < CRET_NEIGHBORS; q++) if (d < bd[q]) {
            for (int r = CRET_NEIGHBORS - 1; r > q; r--) { bd[r] = bd[r - 1]; best[r] = best[r - 1]; }
            bd[q] = d; best[q] = j; break;
        }
    }
    float ma = CRET_SPECIES_TABLE[a->species].mass * powf(cret_growth_scale(a->growth), 3);   // own mass, for the relative sizes
    for (int q = 0; q < CRET_NEIGHBORS; q++) {
        if (best[q] < 0) { k += CRET_NEIGHBOR_OBS; continue; }
        const CretAgent* b = &s->agents[best[q]]; float dx = b->x - a->x, dz = b->z - a->z;
        o[k++] = 1; o[k++] = clampf((cy * dx + sy * dz) / 100, -2, 2); o[k++] = clampf((-sy * dx + cy * dz) / 100, -2, 2);
        o[k++] = clampf((b->y - a->y) / 20, -2, 2); o[k++] = cosf(b->yaw - a->yaw);
        float mb = CRET_SPECIES_TABLE[b->species].mass * powf(cret_growth_scale(b->growth), 3);
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

#include "bots.c"   // scripted bots until policies take over (cret_bot_input)
