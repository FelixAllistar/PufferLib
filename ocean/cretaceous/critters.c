// Critters around people (critters.h). Included by sim.c (after the mover),
// so the build lists need no new file; it uses only sim.h's public helpers
// and sim.c's inline maths (clampf, frand, wrapa).
#include "critters.h"
#include "progress.h"

// Body masses from the guides: multituberculates 0.3-2 kg, Repenomamus
// 8-14 kg, lizards 0.2-2 kg, small birds 0.1-1 kg, fish 1-10 kg. Masses are
// drawn skewed to the small end (kg_min + range x r^2, mean a third of the
// way up). Density is Damuth's law, (mean mass)^-0.75: a 10 kg mammal is
// about a seventh as common as a 0.9 kg one. Fish get three times their
// Damuth share because shallows are a small, rich part of the island.
// Flee speeds: a hatchling raptor sprints about 7 m/s, so it can run down a
// mouse or a fish but loses a lizard's first burst and never catches a bird
// on the wing. A slow approach halves the distance a critter notices it at.
//                                  name               kg         walk  flee  detect react fear  hp    density
const CretCritterKind CRET_CRITTER_KINDS[CRET_CR_KINDS] = {
    [CRET_CR_MOUSE]  = {"multituberculate", 0.3f, 2.0f,  0.5f, 4.6f, 9.0f,  0.30f, 2.5f, 1.0f, 1.10f},
    [CRET_CR_BADGER] = {"Repenomamus",      8.0f, 14.0f, 0.7f, 4.2f, 8.0f,  0.40f, 4.0f, 2.5f, 0.18f},
    [CRET_CR_LIZARD] = {"lizard",           0.2f, 2.0f,  0.4f, 5.5f, 6.0f,  0.15f, 2.5f, 1.0f, 1.18f},
    [CRET_CR_BIRD]   = {"small bird",       0.1f, 1.0f,  0.4f, 9.0f, 12.0f, 0.20f, 2.0f, 1.0f, 2.00f},
    [CRET_CR_FISH]   = {"fish",             1.0f, 10.0f, 0.5f, 3.0f, 5.0f,  0.25f, 3.0f, 1.0f, 1.05f},
};

// Senses (cr_think). Hearing: footfalls carry with the square of the
// animal's speed, so a sprint is heard at the full detection range, a walk at
// a sixth of it and a creep hardly at all. Sight: an animal moving in view at
// CR_SIGHT of the range (a third of that while it stands still), plus half
// its height; a foraging critter (head down, cret_critter_watch) sees CR_DOWN
// of that, coming up to all of it over CR_LOOKUP s as it raises its head (so
// a stalker that freezes when the head comes up is not seen), and a hiding
// one CR_HIDDEN. Smell: an animal's smell carried downwind to the critter,
// CR_SMELL of the range times the plume (cr_plume); birds and fish do not
// smell it.
#define CR_SIGHT 0.45f
#define CR_LOOKUP 0.3f
#define CR_DOWN 0.3f
#define CR_HIDDEN 0.15f
static const float CR_SMELL[CRET_CR_KINDS] = {1.2f, 1.2f, 0.7f, 0.0f, 0.0f};
// Head up watching, then nose down foraging (s), per kind: mice and lizards
// look up briefly between longer spells of feeding; birds peck in short bouts.
static const float CR_WATCH[CRET_CR_KINDS][2] = {{1.2f, 2.8f}, {2.0f, 3.0f}, {1.5f, 2.5f}, {0.8f, 1.6f}, {1.0f, 1.0f}};
// Escapes: a critter jinks (turns 60-90 degrees) when the chaser is within
// CR_JINK_RANGE m, at most every CR_JINK_TICKS; the chaser at full speed
// overshoots. A mouse or lizard still chased at the end of its second flee
// leg dives into a burrow or under a stone and is gone.
#define CR_JINK_RANGE 2.5f
#define CR_JINK_TICKS 12
#define CR_ESCAPE_LEGS 2

#define CR_BITE_HP 2.5f       // the large mammal's bite: hp to the animal it bites (before armour)
#define CR_BITE_BLEED 0.04f   // ... and bleeding, hp/s (decays, about 1.3 hp in all)
#define CR_BITE_CD 1.2f       // s between its bites
#define CR_BITE_REACH 0.45f   // m from its snout to the animal's body

CretCritterConfig cret_critter_defaults(void) {
    CretCritterConfig c = {.enabled = 1, .per_person = 80, .fresh_per_person = 128, .fresh_seconds = 240,
                           .ring_min = 25, .ring_max = 120, .fresh_ring_min = 12, .fresh_ring_max = 70, .despawn = 160};
    for (int k = 0; k < CRET_CR_KINDS; k++) c.density[k] = 1;
    return c;
}

bool cret_critter_load(CretCritterConfig* c, const char* path) {
    FILE* f = fopen(path, "r"); if (!f) return false;
    static const char* KEY[CRET_CR_KINDS] = {"density_mouse", "density_badger", "density_lizard", "density_bird", "density_fish"};
    char line[256]; bool in = false;
    while (fgets(line, sizeof line, f)) {
        char* p = line; while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') { in = !strncmp(p, "[critters]", 10); continue; }
        char key[64]; float v;
        if (!in || *p == '#' || *p == ';' || sscanf(p, "%63[a-z_] = %f", key, &v) != 2) continue;
        if (!strcmp(key, "enabled")) c->enabled = v != 0;
        else if (!strcmp(key, "per_person")) c->per_person = (int)clampf(v, 0, CRET_MAX_CRITTERS);
        else if (!strcmp(key, "fresh_per_person")) c->fresh_per_person = (int)clampf(v, 0, CRET_MAX_CRITTERS);
        else if (!strcmp(key, "fresh_seconds")) c->fresh_seconds = fmaxf(v, 0);
        else if (!strcmp(key, "ring_min")) c->ring_min = fmaxf(v, 0);
        else if (!strcmp(key, "ring_max")) c->ring_max = fmaxf(v, 1);
        else if (!strcmp(key, "fresh_ring_min")) c->fresh_ring_min = fmaxf(v, 0);
        else if (!strcmp(key, "fresh_ring_max")) c->fresh_ring_max = fmaxf(v, 1);
        else if (!strcmp(key, "despawn")) c->despawn = fmaxf(v, 10);
        else for (int k = 0; k < CRET_CR_KINDS; k++) if (!strcmp(key, KEY[k])) c->density[k] = fmaxf(v, 0);
    }
    fclose(f);
    return true;
}

void cret_critters_clear(CretSim* s) { memset(s->critters, 0, sizeof s->critters); s->ncrit = 0; }

// Length grows with the cube root of mass: a 1 kg mouse is 0.28 m nose to
// tail, a 12 kg Repenomamus 0.64 m, a 0.3 kg bird 0.19 m.
float cret_critter_length(int kind, float kg) { return (kind == CRET_CR_FISH ? 0.32f : 0.28f) * cbrtf(fmaxf(kg, 0.01f)); }
static float cr_radius(const CretCritter* k) { return 0.3f * cret_critter_length(k->kind, k->kg); }

// ---------------------------------------------------------------- habitat
// From the world data alone, so it costs a few texel reads: plant cover
// (cret_forage) for the mammals and lizards, open ground and trees for the
// birds, the shallow edges of rivers and lakes for fish.
static float cr_trees(const CretWorld* w, float x, float z) {
    const TerraMap* m = &w->map; int n = m->n;
    int gx = (int)(x / m->cell + 0.5f), gz = (int)(z / m->cell + 0.5f);
    if (gx < 0 || gz < 0 || gx >= n || gz >= n) return 0;
    const uint8_t* v = m->veg + ((size_t)gz * n + gx) * TERRA_VEG_COUNT;
    return clampf((v[TERRA_VEG_CONIFER] + v[TERRA_VEG_BROADLEAF] + v[TERRA_VEG_TREEFERN]) / 160.0f, 0, 1);
}
static int cr_water_kind(const CretWorld* w, float x, float z) {
    const TerraMap* m = &w->map; int n = m->n;
    int gx = (int)(x / m->cell + 0.5f), gz = (int)(z / m->cell + 0.5f);
    if (gx < 0 || gz < 0 || gx >= n || gz >= n) return TERRA_WATER_SEA;
    return m->water_kind[(size_t)gz * n + gx];
}
// Fish keep to 0.15-2 m of fresh water.
static bool cr_shallows(const CretWorld* w, float x, float z) {
    if (x < 30 || z < 30 || x > w->size - 30 || z > w->size - 30) return false;
    float d = cret_water(w, x, z) - cret_height(w, x, z);
    if (d < 0.15f || d > 2.0f) return false;
    int k = cr_water_kind(w, x, z); return k == TERRA_WATER_RIVER || k == TERRA_WATER_LAKE;
}
// Land critters stand on dry, walkable ground.
static bool cr_dry(const CretWorld* w, float x, float z) {
    return cret_walkable(w, x, z) && cret_water(w, x, z) < cret_height(w, x, z) + 0.02f;
}
float cret_critter_habitat(const CretSim* s, int kind, float x, float z) {
    const CretWorld* w = s->world;
    if (kind == CRET_CR_FISH) return cr_shallows(w, x, z) ? 1.0f : 0.0f;
    if (!cr_dry(w, x, z)) return 0;
    float v = cret_forage(w, x, z), t = cr_trees(w, x, z);
    switch (kind) {
    case CRET_CR_MOUSE: return 0.2f + 0.8f * v;                        // seeds and insects in the undergrowth
    case CRET_CR_BADGER: return 0.1f + 0.9f * fmaxf(v, 0.7f * t);       // cover: thick plants or woods
    case CRET_CR_LIZARD: return 0.35f + 0.65f * v;                     // everywhere dry, more in the plants
    default: return clampf(0.15f + 0.55f * (1 - v) + 0.6f * t, 0, 1); // birds: open ground and trees
    }
}

// ---------------------------------------------------------------- spawning
static bool cr_free_slot(CretSim* s, int* slot) {
    for (int q = 0; q < CRET_MAX_CRITTERS; q++) if (!s->critters[q].active) { *slot = q; return true; }
    return false;
}
// One try at (x, z): a kind drawn by density x habitat, accepted with the
// total over its best case, so busy habitat fills up first and a bare
// slope stays nearly empty. Fresh hatchlings get more of what they can catch.
static bool cr_try_spawn(CretSim* s, int owner, float x, float z, bool fresh) {
    const CretCritterConfig* C = &s->crit_cfg; float wk[CRET_CR_KINDS], W = 0, Wmax = 0;
    for (int k = 0; k < CRET_CR_KINDS; k++) {
        float d = CRET_CRITTER_KINDS[k].density * C->density[k];
        if (fresh && (k == CRET_CR_MOUSE || k == CRET_CR_LIZARD || k == CRET_CR_FISH)) d *= 1.5f;
        if (fresh && k == CRET_CR_BIRD) d *= 0.5f;
        wk[k] = d * cret_critter_habitat(s, k, x, z); W += wk[k];
        if (k != CRET_CR_FISH) Wmax += d;   // land kinds share a point; fish have the water to themselves
    }
    Wmax = fmaxf(Wmax, 1e-3f);
    if (W <= 0 || frand(&s->rng) * Wmax > W) return false;
    float r = frand(&s->rng) * W; int kind = 0;
    while (kind < CRET_CR_KINDS - 1 && r > wk[kind]) r -= wk[kind++];
    if (wk[kind] <= 0) return false;
    int slot; if (!cr_free_slot(s, &slot)) return false;
    const CretCritterKind* K = &CRET_CRITTER_KINDS[kind]; float u = frand(&s->rng);
    CretCritter* c = &s->critters[slot]; memset(&s->crit_trail[slot], 0, sizeof s->crit_trail[slot]);   // a new animal, no trail yet
    *c = (CretCritter){.active = 1, .kind = (uint8_t)kind, .state = CRET_CRS_IDLE, .owner = (uint8_t)owner, .threat = -1,
                       .x = x, .z = z, .yaw = 6.2831853f * frand(&s->rng), .kg = K->kg_min + (K->kg_max - K->kg_min) * u * u, .hp = K->hp};
    c->timer = (uint16_t)(CRET_TICK_HZ * (0.5f + 3 * frand(&s->rng)));
    c->y = kind == CRET_CR_FISH ? cret_water(s->world, x, z) - 0.25f : cret_height(s->world, x, z);
    s->ncrit++; s->stats.critters.spawned[kind]++;
    return true;
}

// ---------------------------------------------------------------- behaviour
// Animals near people, gathered once a tick so each critter scans a short list.
typedef struct CrNear { float x, z, mass, speed, height; int i; bool wet; } CrNear;

static bool cr_can_stand(const CretSim* s, const CretCritter* c, float x, float z) {
    return c->kind == CRET_CR_FISH ? cr_shallows(s->world, x, z) : cr_dry(s->world, x, z);
}
// Moves along the heading at `speed`, veering up to 135 degrees round
// ground it cannot stand on (water, cliffs; dry land for fish); stops if
// every way is barred (cornered).
static void cr_move(CretSim* s, CretCritter* c, float speed) {
    c->speed = speed; if (speed <= 0) return;
    float step = speed * CRET_DT;
    static const float OFF[] = {0, 0.6f, -0.6f, 1.2f, -1.2f, 2.0f, -2.0f, 3.1416f};
    for (int k = 0; k < 8; k++) {
        float h = c->yaw + OFF[k], nx = c->x + cosf(h) * step, nz = c->z + sinf(h) * step;
        if (!cr_can_stand(s, c, nx, nz)) continue;
        c->yaw = wrapa(h); c->x = nx; c->z = nz; c->hop += step;
        c->y = c->kind == CRET_CR_FISH ? cret_water(s->world, nx, nz) - fminf(0.25f, 0.5f * (cret_water(s->world, nx, nz) - cret_height(s->world, nx, nz)))
                                       : cret_height(s->world, nx, nz);
        return;
    }
    c->speed = 0;
}
static void cr_set(CretCritter* c, int state, float seconds) { c->state = (uint8_t)state; c->timer = (uint16_t)clampf(seconds * CRET_TICK_HZ, 1, 60000); }

// The large mammal bites an animal: light damage and a little bleeding,
// enough to scare off a hatchling, nothing to an adult. Not in the hit log
// (its attacker field names agents).
static void cr_bite_agent(CretSim* s, CretCritter* c, int j) {
    CretAgent* b = &s->agents[j]; const CretSpecies* S = &CRET_SPECIES_TABLE[b->species];
    float hp = fminf(CR_BITE_HP * (1 - S->armor), b->health);
    b->health -= hp; b->bleed += CR_BITE_BLEED * (1 - S->bleed_resist); b->hurt_ticks = b->hurt_ticks > 8 ? b->hurt_ticks : 8;
    b->ev_taken += hp / fmaxf(b->max_health, 1);
    c->bite_cd = CR_BITE_CD; s->stats.critters.bites_taken++;
    if (b->health <= 0.01f) cret_kill(s, j);
}

float cret_critter_watch(const CretSim* s, int k) {
    const CretCritter* c = &s->critters[k];
    if (c->state != CRET_CRS_IDLE && c->state != CRET_CRS_WANDER) return 1;
    uint32_t h = (uint32_t)k * 2654435761u; h ^= h >> 13;
    const float* W = CR_WATCH[c->kind < CRET_CR_KINDS ? c->kind : 0];
    float jit = 0.8f + 0.4f * (h & 255) / 255.0f, period = (W[0] + W[1]) * jit;
    float t = fmodf(s->tick * CRET_DT + period * ((h >> 8) & 255) / 255.0f, period);
    return t < W[0] * jit ? clampf(t / CR_LOOKUP, 0, 1) : 0;
}

// A critter only smells an animal in the core of its scent plume: 1.25 of
// the range straight downwind of it, falling off steeply to 0.08 across the
// wind and upwind (a sniffing hunter's own nose takes in more, scent.c).
static float cr_plume(const CretSim* s, float dx, float dz) {
    float wx, wz, w; cret_wind(s, &wx, &wz, &w);
    float l = sqrtf(dx * dx + dz * dz), c = l > 1e-4f ? (dx * wx + dz * wz) / l : 1;
    return fmaxf(1.25f * powf(fmaxf(c, 0), 1.5f), 0.08f);
}

// How far critter k notices animal n (see the senses above).
static float cr_detect(const CretSim* s, int k, const CrNear* n) {
    const CretCritter* c = &s->critters[k]; const CretCritterKind* K = &CRET_CRITTER_KINDS[c->kind];
    if (c->kind == CRET_CR_FISH) return n->wet ? K->detect * (0.4f + 0.6f * clampf(n->speed / 4.5f, 0, 1)) + 0.5f * n->height : 1.5f;   // fish see the bank only when something is right over them
    float sp = clampf(n->speed / 4.5f, 0, 1), hear = K->detect * sp * sp;
    float look = c->state == CRET_CRS_HIDE ? CR_HIDDEN : CR_DOWN + (1 - CR_DOWN) * cret_critter_watch(s, k);
    float see = (K->detect * CR_SIGHT * (0.35f + 0.65f * clampf(n->speed / 1.5f, 0, 1)) + 0.5f * n->height) * look;
    float smell = K->detect * CR_SMELL[c->kind] * cr_plume(s, c->x - n->x, c->z - n->z);
    return fmaxf(hear, fmaxf(see, smell));
}

static void cr_think(CretSim* s, int idx, const CrNear* near, int nn) {
    CretCritter* c = &s->critters[idx]; const CretCritterKind* K = &CRET_CRITTER_KINDS[c->kind];
    if (c->bite_cd > 0) c->bite_cd -= CRET_DT;
    if (c->timer > 0) c->timer--;
    if (c->state == CRET_CRS_FLY) {   // up and away, then gone for good (another lands somewhere later)
        c->alt += 2.5f * CRET_DT; c->speed = K->flee;
        c->x += cosf(c->yaw) * K->flee * CRET_DT; c->z += sinf(c->yaw) * K->flee * CRET_DT; c->hop += K->flee * CRET_DT;
        if (!c->timer) { c->active = 0; s->ncrit--; }
        return;
    }
    // Look round every third tick (staggered): the nearest animal it fears
    // that it hears, sees or smells (cr_detect); the large mammal also watches
    // animals too small to fear. A hiding mouse sees little, but hears a run.
    if ((idx + s->tick) % 3 == 0) {
        int best = -1; float bd = 1e30f;
        for (int q = 0; q < nn; q++) {
            const CrNear* n = &near[q];
            float dx = n->x - c->x, dz = n->z - c->z, d2 = dx * dx + dz * dz;
            if (d2 >= bd || d2 > 40.0f * 40.0f) continue;
            float det = cr_detect(s, idx, n);
            if (d2 > det * det) continue;
            if (n->mass < K->fear * c->kg && c->kind != CRET_CR_BADGER) continue;
            bd = d2; best = n->i;
        }
        if (best != c->threat && best >= 0) { c->threat = (int8_t)best; c->react = (uint8_t)(K->react * CRET_TICK_HZ + 4 * frand(&s->rng)); }
        else if (best < 0 && c->state != CRET_CRS_FLEE && c->state != CRET_CRS_FIGHT) c->threat = -1;
    }
    const CretAgent* t = c->threat >= 0 && c->threat < s->cfg.num_agents && s->agents[c->threat].alive ? &s->agents[c->threat] : NULL;
    if (!t) c->threat = -1;
    if (t && c->react > 0) c->react--;
    float tm = t ? cret_mass(t) : 0, td = t ? cret_body_dist(t, c->x, c->z) : 1e9f;
    bool feared = t && tm >= K->fear * c->kg;
    // Respond once the reaction delay is over (a hiding mouse bolts again).
    if (t && !c->react && (c->state == CRET_CRS_IDLE || c->state == CRET_CRS_WANDER || c->state == CRET_CRS_HIDE)) {
        if (c->kind == CRET_CR_BIRD) { cr_set(c, CRET_CRS_FLY, 2.5f); c->yaw = atan2f(c->z - t->z, c->x - t->x) + (frand(&s->rng) - 0.5f); s->stats.critters.flushed++; return; }
        if (c->kind == CRET_CR_BADGER && !feared) { if (td < 3.0f) cr_set(c, CRET_CRS_FIGHT, 6); }   // stands its ground against small animals
        else cr_set(c, CRET_CRS_FLEE, c->kind == CRET_CR_LIZARD ? 0.5f + 0.3f * frand(&s->rng) : 1.2f + 1.3f * frand(&s->rng));
    }
    switch (c->state) {
    case CRET_CRS_FLEE: {
        if (c->jink) c->jink--;
        if (t && td < CR_JINK_RANGE && !c->jink && c->kind != CRET_CR_BADGER) {   // a jink: hard to one side, the chaser overshoots
            c->yaw = wrapa(c->yaw + (frand(&s->rng) < 0.5f ? -1.0f : 1.0f) * (1.05f + 0.5f * frand(&s->rng)));
            c->jink = CR_JINK_TICKS;
        } else if (t && c->jink < CR_JINK_TICKS - 6) c->yaw = wrapa(atan2f(c->z - t->z, c->x - t->x) + 0.6f * (frand(&s->rng) - 0.5f));
        cr_move(s, c, K->flee);
        // Cornered (or caught up with): the large mammal turns and bites.
        if (c->kind == CRET_CR_BADGER && t && td < 1.5f) { cr_set(c, CRET_CRS_FIGHT, 6); break; }
        if (!c->timer) {
            // Still chased hard (the chaser close and running): another leg, or
            // after CR_ESCAPE_LEGS into a burrow or under a stone, gone.
            bool chased = t && td < 0.6f * K->detect && t->speed > 2.5f;
            if (chased && (c->kind == CRET_CR_MOUSE || c->kind == CRET_CR_LIZARD) && ++c->legs >= CR_ESCAPE_LEGS) {
                c->active = 0; s->ncrit--; s->stats.critters.escaped++; break;
            }
            if (chased) { cr_set(c, CRET_CRS_FLEE, c->kind == CRET_CR_LIZARD ? 0.5f + 0.3f * frand(&s->rng) : 1.0f + 0.8f * frand(&s->rng)); break; }
            c->legs = 0;
            if (c->kind == CRET_CR_MOUSE) cr_set(c, CRET_CRS_HIDE, 5 + 4 * frand(&s->rng));   // freezes in the undergrowth
            else cr_set(c, CRET_CRS_IDLE, c->kind == CRET_CR_LIZARD ? 0.4f + 0.5f * frand(&s->rng) : 1 + frand(&s->rng));   // a lizard pauses between bursts
            c->speed = 0;
        }
        break;
    }
    case CRET_CRS_FIGHT: {
        c->speed = 0;
        if (!t || td > 6.0f || !c->timer) { cr_set(c, feared && t ? CRET_CRS_FLEE : CRET_CRS_WANDER, 1.5f); break; }
        c->yaw = atan2f(t->z - c->z, t->x - c->x);
        float L = cret_critter_length(c->kind, c->kg), sx = c->x + cosf(c->yaw) * L * 0.5f, sz = c->z + sinf(c->yaw) * L * 0.5f;
        if (cret_body_dist(t, sx, sz) > CR_BITE_REACH) cr_move(s, c, 1.5f);   // lunges in
        else if (c->bite_cd <= 0 && fabsf(t->y - c->y) < t->height + 0.5f) cr_bite_agent(s, c, c->threat);
        break;
    }
    case CRET_CRS_HIDE:
        c->speed = 0;
        if (!c->timer) { cr_set(c, CRET_CRS_IDLE, 1); c->threat = -1; }
        break;
    case CRET_CRS_WANDER:
        if (frand(&s->rng) < 0.05f) c->yaw = wrapa(c->yaw + (frand(&s->rng) - 0.5f) * 1.5f);
        cr_move(s, c, K->walk * (c->kind == CRET_CR_LIZARD ? 2.5f : 1.0f));
        if (!c->timer) cr_set(c, CRET_CRS_IDLE, 1 + 3 * frand(&s->rng));
        break;
    default:   // idle: the large mammal faces small animals it watches
        c->speed = 0;
        if (t && c->kind == CRET_CR_BADGER && !feared) c->yaw = atan2f(t->z - c->z, t->x - c->x);
        if (!c->timer) { cr_set(c, CRET_CRS_WANDER, c->kind == CRET_CR_LIZARD ? 0.4f + 0.6f * frand(&s->rng) : 1 + 2.5f * frand(&s->rng)); c->yaw = wrapa(c->yaw + (frand(&s->rng) - 0.5f) * 2.5f); }
    }
}

void cret_critters_step(CretSim* s) {
    const CretCritterConfig* C = &s->crit_cfg; int na = s->cfg.num_agents;
    int P[CRET_MAX_AGENTS], np = 0;
    if (C->enabled) for (int i = 0; i < na; i++) if (s->agents[i].alive && cret_is_person(s, i)) P[np++] = i;
    if (!np) { if (s->ncrit) cret_critters_clear(s); return; }
    // Animals near anyone: the only ones a critter can meet.
    CrNear near[CRET_MAX_AGENTS]; int nn = 0; float reach2 = (C->despawn + 20) * (C->despawn + 20);
    for (int j = 0; j < na; j++) {
        const CretAgent* b = &s->agents[j]; if (!b->alive) continue;
        bool in = false;
        for (int q = 0; q < np && !in; q++) { float dx = b->x - s->agents[P[q]].x, dz = b->z - s->agents[P[q]].z; in = dx * dx + dz * dz < reach2; }
        if (!in) continue;
        near[nn++] = (CrNear){b->x, b->z, cret_mass(b), b->speed, b->height, j, b->swimming || cret_water(s->world, b->x, b->z) > b->y + 0.05f};
    }
    // Each critter belongs to the nearest person within the despawn range; past it, it is gone.
    int count[CRET_MAX_AGENTS] = {0}; float desp2 = C->despawn * C->despawn;
    for (int k = 0; k < CRET_MAX_CRITTERS; k++) {
        CretCritter* c = &s->critters[k]; if (!c->active) continue;
        const CretAgent* o = &s->agents[c->owner % CRET_MAX_AGENTS];
        float dx = o->x - c->x, dz = o->z - c->z;
        if (c->owner >= na || !o->alive || !cret_is_person(s, c->owner) || dx * dx + dz * dz > desp2) {
            int best = -1; float bd = desp2;
            for (int q = 0; q < np; q++) {
                float ex = s->agents[P[q]].x - c->x, ez = s->agents[P[q]].z - c->z, d = ex * ex + ez * ez;
                if (d < bd) { bd = d; best = P[q]; }
            }
            if (best < 0) { c->active = 0; s->ncrit--; continue; }
            c->owner = (uint8_t)best;
        }
        cr_think(s, k, near, nn);
        if (c->active) count[c->owner]++;
    }
    // Top up each person's ring: a few tries a tick, more while it is nearly
    // empty (a fresh spawn), a closer and fuller ring for a new hatchling.
    for (int q = 0; q < np; q++) {
        int p = P[q]; const CretAgent* a = &s->agents[p];
        bool fresh = a->growth < 0.5f && a->ticks_alive < C->fresh_seconds * CRET_TICK_HZ;
        int cap = fresh ? C->fresh_per_person : C->per_person;
        float r0 = fresh ? C->fresh_ring_min : C->ring_min, r1 = fresh ? C->fresh_ring_max : C->ring_max;
        int tries = count[p] < cap / 4 ? 24 : fresh ? 4 : 2;
        for (int t = 0; t < tries && count[p] < cap && s->ncrit < CRET_MAX_CRITTERS; t++) {
            float ang = 6.2831853f * frand(&s->rng), d = sqrtf(r0 * r0 + (r1 * r1 - r0 * r0) * frand(&s->rng));   // even over the ring's area
            float x = a->x + cosf(ang) * d, z = a->z + sinf(ang) * d;
            bool close = false;   // never right under anyone's nose
            for (int e = 0; e < np && !close; e++) { float ex = s->agents[P[e]].x - x, ez = s->agents[P[e]].z - z; close = ex * ex + ez * ez < r0 * r0 * 0.8f; }
            if (!close && cr_try_spawn(s, p, x, z, fresh)) count[p]++;
        }
    }
}

// ---------------------------------------------------------------- eating
static bool cr_wading(const CretSim* s, const CretAgent* a) { return a->swimming || cret_water(s->world, a->x, a->z) > a->y + 0.05f; }

int cret_critter_in_reach(const CretSim* s, int i, float px, float pz, float reach, float* dist) {
    if (!s->ncrit) return -1;
    const CretAgent* a = &s->agents[i]; int best = -1; float bd = reach; bool wet = cr_wading(s, a);
    for (int k = 0; k < CRET_MAX_CRITTERS; k++) {
        const CretCritter* c = &s->critters[k];
        if (!c->active || c->alt > 0.5f || (c->kind == CRET_CR_FISH && !wet)) continue;   // fish only from the water
        if (fabsf(c->y - a->y) > a->height + 0.5f) continue;
        float d = fmaxf(hypotf(c->x - px, c->z - pz) - cr_radius(c), 0);
        if (d <= bd) { bd = d; best = k; }
    }
    if (dist) *dist = bd;
    return best;
}

int cret_critter_nearest(const CretSim* s, int i, float range, float* dist) {
    if (!s->ncrit) return -1;
    const CretAgent* a = &s->agents[i]; int best = -1; float bd = range * range; bool wet = cr_wading(s, a);
    for (int k = 0; k < CRET_MAX_CRITTERS; k++) {
        const CretCritter* c = &s->critters[k];
        if (!c->active || c->alt > 0.5f || (c->kind == CRET_CR_FISH && !wet)) continue;
        float dx = c->x - a->x, dz = c->z - a->z, d = dx * dx + dz * dz; if (d < bd) { bd = d; best = k; }
    }
    if (dist && best >= 0) *dist = sqrtf(bd);
    return best;
}

// A bite's power against a critter: 1 for a hatchling, 2 for an adult
// raptor, 3 for anything of 160 kg or more. The large mammal (2.5) takes three
// bites from a hatchling raptor, two from an adult, one from a big animal.
static float critter_bite_power(const CretAgent* a) { return 1 + clampf(cret_mass(a) / 80.0f, 0, 2); }

void cret_critter_bite(CretSim* s, int i, int k) {
    CretCritter* c = &s->critters[k]; CretAgent* a = &s->agents[i];
    if (!c->active) return;
    c->hp -= critter_bite_power(a);
    if (c->hp > 0.01f) {   // the large mammal, hurt: fights back a small biter, runs from a big one
        c->threat = (int8_t)i; c->react = 0;
        bool feared = cret_mass(a) >= CRET_CRITTER_KINDS[c->kind].fear * c->kg;
        cr_set(c, feared ? CRET_CRS_FLEE : CRET_CRS_FIGHT, feared ? 2 : 6);
        return;
    }
    // Caught: carnivores swallow it whole, food by the carcass formula (sim.c
    // survive). One too full to eat all of it leaves the rest lying (at least
    // 100 g), food it can carry home (sim.c, carrying) or come back to.
    if (CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_CARNIVORE) {
        float m = 0.25f * cret_mass(a), d = fmaxf(fminf(c->kg / m, 1 - a->food), 0), rest = c->kg - d * m;
        a->food += d; a->ev_food += d;
        if (rest >= 0.1f) item_spawn(s, CRET_ITEM_CRITTER, a->species, c->kind, c->x, c->z, c->yaw, rest);
    }
    s->stats.critters.eaten[c->kind]++;
    cret_quest_on_catch(s, i, c->kind);
    c->active = 0; s->ncrit--;
}

// ---------------------------------------------------------------- bots
bool cret_critter_bot(CretSim* s, int i, CretInput* in) {
    CretAgent* a = &s->agents[i]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    if (S->diet != CRET_DIET_CARNIVORE || a->growth >= 0.55f || a->food > 0.75f || a->latch || a->swimming) return false;
    // The nearest it can catch on foot: birds fly off and are not worth a stalk.
    int k = -1; float d = 45.0f;
    for (int q = 0; q < CRET_MAX_CRITTERS && s->ncrit; q++) {
        const CretCritter* c = &s->critters[q];
        if (!c->active || c->kind == CRET_CR_BIRD || c->kind == CRET_CR_FISH || c->alt > 0.5f) continue;
        float e = hypotf(c->x - a->x, c->z - a->z); if (e < d) { d = e; k = q; }
    }
    if (k < 0) return false;
    const CretCritter* c = &s->critters[k];
    // Downwind first: a bot in the prey's smell (cr_plume) circles wide, 15 m
    // to the side, to a point 9 m downwind of it before it closes in.
    float tx = c->x, tz = c->z, wx, wz, w; cret_wind(s, &wx, &wz, &w);
    bool bolted = c->state == CRET_CRS_FLEE;
    if (!bolted && d > 3 && cr_plume(s, c->x - a->x, c->z - a->z) > 0.3f) {
        float sx = c->x + wx * 9, sz = c->z + wz * 9, cross = (a->x - c->x) * wz - (a->z - c->z) * wx;
        float side = fabsf(cross) > 4 ? (cross > 0 ? 1.0f : -1.0f) : (k & 1) ? 1.0f : -1.0f;   // the side it is on; near the axis, one fixed per prey
        tx = c->x - wz * side * 15 + wx * 5; tz = c->z + wx * side * 15 + wz * 5;   // round the side it is on, wide of the plume
        if ((a->x - c->x) * wx + (a->z - c->z) * wz > 2) { tx = sx; tz = sz; }   // past the side: on to the downwind point
    }
    float want = wrapa(atan2f(tz - a->z, tx - a->x) - a->yaw);
    *in = (CretInput){.turn = clampf(want * 2, -1, 1), .aim = cret_aim_at(a, c->x, c->z)};
    // Stalk: walk to 9 m, then creep in while it forages (head down) and
    // stand still while it looks up (cr_detect); chase at a sprint once it bolts.
    bool circling = tx != c->x || tz != c->z;
    // Within 2.2 m a still hunter is seen anyway (cr_detect): it goes on in and snaps.
    in->forward = bolted || circling ? (bolted ? 1.0f : 0.5f) : d > 9 ? 0.5f : cret_critter_watch(s, k) > 0 && d > 2.2f ? 0.0f : 0.3f;
    in->sprint = bolted && a->stamina > 0.15f;
    if (fabsf(want) > 1.2f) in->forward *= 0.4f;
    float px, pz, g = cret_growth_scale(a->growth); cret_strike_point(a, &px, &pz);
    if (a->attack_cd == 0 && hypotf(c->x - px, c->z - pz) - cr_radius(c) < S->reach * g * 1.2f) in->action = CRET_ACT_ATTACK;   // the snap reaches 1.25x
    return true;
}
