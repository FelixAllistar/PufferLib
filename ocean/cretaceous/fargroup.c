// Far groups (fargroup.h; docs/ecosystem.md, Far groups), included by sim.c
// after bots.c: a folded record uses the sim's own helpers (survive_body,
// kill, overlaps, carcass_bite_kg, eco.c's grazing) and the bots' (walkable,
// find_water, bot_active and the urgencies of bot_choose), so a folded
// animal lives by the same equations as one in the full sim.
//
// Once a second (the last tick of each second): records near a person or a
// policy's animal (a herd: near any predator of the full sim) are unfolded; every record chooses and
// follows its drive and its members eat, drink and live a second; then calm
// units far from everyone are folded. Everything draws from CretFar.rng.

// Measured hunt odds (tools/ecoprobe.c --only world, the full sim with
// folding off: 6 game days on each of 8 seeds, 1011-1012, 1111-1112,
// 1211-1212 and 1311-1312, 48 animals, the director on; docs/ecosystem.md,
// Far groups, has the counts).
// FAR_KILL: of the hunts a predator species began on a prey species, the
// share that killed the prey (P_catch x P_kill as it happens): raptors on
// pachycephalosaurs 57 of 349, on gallimimus 71 of 321; the tyrannosaur on
// stegosaurs 59 of 254, pachycephalosaurs 17 of 24, sauropeltas 58 of 121,
// gallimimus 10 of 21. Pairings never hunted to a kill (a raptor on a
// stegosaur, 0 of 9, or a sauropelta) have none: a folded predator leaves
// them be, as the bots do.
// FAR_DEFEND: a herd's defenders. Predators die to herbivores mostly away
// from any hunt of their own (a stegosaur's tail on raptors passing its young:
// 3 of its 51 raptor kills fell within a minute of a hunt), and far groups
// meet only by hunting, so every herbivore kill of a predator species is
// spread over that species' hunts: per hunt begun, the chance a predator dies
// to a herbivore of each species (raptors: stegosaurs 51, pachycephalosaurs
// 23, sauropeltas 16 kills over 679 hunts; the tyrannosaur: stegosaurs 4 over 420).
static const float FAR_KILL[CRET_SPECIES][CRET_SPECIES] = {
    [CRET_SP_RAPTOR] = {[CRET_SP_PACHY] = 0.163f, [CRET_SP_GALLI] = 0.221f},
    [CRET_SP_TREX] = {[CRET_SP_STEGO] = 0.232f, [CRET_SP_PACHY] = 0.708f, [CRET_SP_SAUROPELTA] = 0.479f, [CRET_SP_GALLI] = 0.476f},
};
static const float FAR_DEFEND[CRET_SPECIES][CRET_SPECIES] = {
    [CRET_SP_RAPTOR] = {[CRET_SP_STEGO] = 0.075f, [CRET_SP_PACHY] = 0.034f, [CRET_SP_SAUROPELTA] = 0.024f},
    [CRET_SP_TREX] = {[CRET_SP_STEGO] = 0.0095f},
};

static inline float far_rand(CretSim* s) { return frand(&s->far.rng); }
bool cret_far_folded(const CretSim* s, int i) { return i >= 0 && i < CRET_MAX_AGENTS && s->far.of[i] != 0; }
bool cret_far_present(const CretSim* s, int i) { return i >= 0 && i < CRET_MAX_AGENTS && (s->agents[i].alive || s->far.of[i]); }
static bool far_herb(const CretAgent* a) { return CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_HERBIVORE; }

// Where member k of record g stands: the record turned by its heading, plus the member's place in the formation.
static void far_member_xz(const CretFarGroup* g, int k, float* x, float* z) {
    float c = cosf(g->heading), sn = sinf(g->heading);
    *x = g->x + c * g->fx[k] - sn * g->fz[k]; *z = g->z + sn * g->fx[k] + c * g->fz[k];
}
// Distance from (x, z) to the nearest living person.
static float far_person_dist(const CretSim* s, float x, float z) {
    float bd = 1e30f;
    for (int j = 0; j < s->cfg.num_agents; j++)
        if (s->agents[j].alive && cret_is_person(s, j)) bd = fminf(bd, hypotf(s->agents[j].x - x, s->agents[j].z - z));
    return bd;
}
// A nest task: a nest of its own or its group's, building one, carrying food
// home, raiding a nest, guarding one, or a quest to build, guard or raid one.
static bool far_nest_task(const CretSim* s, int i) {
    const CretAgent* a = &s->agents[i]; int st = a->bot_state;
    if (a->act == CRET_ACT_NEST || a->nest_ticks > 0 || cret_nest_of(s, i) >= 0) return true;
    if (st == BOT_NEST || st == BOT_CARRY || st == BOT_RAID || (st == BOT_GUARD && a->bot_target >= CRET_MAX_CARCASSES)) return true;
    for (int w = 0; w < 2; w++) {
        const CretQuest* q = cret_quest_of(s, i, w);
        if (q && (q->kind == CRET_Q_NEST || q->kind == CRET_Q_GUARD || q->kind == CRET_Q_RAID)) return true;
    }
    return false;
}
// An animal of the full sim within r of (x, z) that a unit of `diet` must
// meet in the full sim (not in group `group`): a person or a policy's slot
// (they never fold, and a policy observes its neighbours), and for a herd
// any predator of the full sim (a pack at its nest hunts what it sees, and
// it sees nothing folded). Such a unit does not fold, and such a record
// unfolds. A folded predator meets everything else by rule (far_strike).
static bool far_anchor_near(const CretSim* s, float x, float z, float r, int diet, int group) {
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (!b->alive || (group && b->group == group)) continue;
        bool must = cret_is_person(s, j) || s->far.learned[j] || (diet == CRET_DIET_HERBIVORE && CRET_SPECIES_TABLE[b->species].diet == CRET_DIET_CARNIVORE);
        if (!must) continue;
        float dx = b->x - x, dz = b->z - z; if (dx * dx + dz * dz < r * r) return true;
    }
    return false;
}
// An animal of the full sim is busy with agent i: hunting it, fleeing it or
// standing to it, alert to it as its attacker, or latched on to it.
static bool far_engaged(const CretSim* s, int i) {
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j]; if (!b->alive || j == i) continue;
        bool aim = b->bot_target == i && (b->bot_state == BOT_HUNT || b->bot_state == BOT_FLEE || b->bot_state == BOT_DEFEND);
        if (aim || (b->bot_alert > 0 && b->bot_foe == i + 1) || b->latch == i + 1) return true;
    }
    return false;
}

// ---------------------------------------------------------------- folding
// Calm enough to fold: a bot (not a person, not a policy's slot) going
// about its day (wandering, grazing, resting, keeping up with its group,
// going to water or drinking), not hurt, bleeding, frightened, alert to an
// attacker, called to a rally, latched, leaping, carrying, swimming or down,
// and with no nest task: no nest of its own or its group's, not building one,
// no quest to build, guard or raid one.
static bool far_calm(const CretSim* s, int i) {
    const CretAgent* a = &s->agents[i]; int st = a->bot_state;
    if (!a->alive || cret_is_person(s, i) || s->far.learned[i] || a->ticks_alive < CRET_TICK_HZ || s->tick < s->far.hold[i]) return false;
    if (st != BOT_WANDER && st != BOT_GRAZE && st != BOT_REST && st != BOT_GROUP && st != BOT_SEEK_WATER && st != BOT_DRINK) return false;
    if (a->latch || a->latchers || a->leap || a->carry || a->hurt_ticks > 0 || a->swimming || !a->on_ground || cret_down(a)) return false;
    if (a->bot_alert > 0 || a->rally_ticks > 0 || a->st[CRET_ST_STUN] > 0 || a->st[CRET_ST_FEAR] > 0 || a->bleed >= CRET_WOUND_OPEN * a->max_health) return false;
    return !far_nest_task(s, i);
}
// The members of agent i's unit: its group's living members, or i alone.
static int far_unit_of(const CretSim* s, int i, int* out) {
    const CretAgent* a = &s->agents[i]; int n = 0;
    if (!a->group) { out[n++] = i; return n; }
    for (int j = 0; j < s->cfg.num_agents; j++) if (s->agents[j].alive && s->agents[j].group == a->group) out[n++] = j;
    return n;
}
// The drive a record takes over from its first member's activity.
static int far_drive_of(int bot_state) {
    switch (bot_state) {
    case BOT_GRAZE: return BOT_GRAZE;
    case BOT_REST: return BOT_REST;
    case BOT_SEEK_WATER: case BOT_DRINK: return BOT_SEEK_WATER;
    default: return BOT_WANDER;
    }
}
// Agent i leaves the full sim for record r: not alive (every loop of the
// full sim skips it), its body out of the physics world where the dead are parked.
static void far_park(CretSim* s, int i, int r) {
    CretAgent* a = &s->agents[i];
    s->far.of[i] = (uint8_t)(r + 1);
    a->alive = false; a->act = 0; a->vx = a->vy = a->vz = 0; a->speed = 0; a->sprinting = false;
    b3Body_SetTransform(a->body, (b3Pos){0, -1000.0f - i * 10, 0}, b3Quat_identity);
}
// Folds the unit mem[0..n) into a free record. Returns it, or -1.
static int far_fold_unit(CretSim* s, const int* mem, int n) {
    CretFar* F = &s->far; int r = -1;
    for (int k = 0; k < CRET_MAX_AGENTS && r < 0; k++) if (!F->g[k].active) r = k;
    if (r < 0 || n < 1 || n > CRET_FAR_MEMBERS) return -1;
    CretFarGroup* g = &F->g[r]; const CretAgent* lead = &s->agents[mem[0]];
    memset(g, 0, sizeof *g);
    float cx = 0, cz = 0;
    for (int k = 0; k < n; k++) { cx += s->agents[mem[k]].x / n; cz += s->agents[mem[k]].z / n; }
    g->active = 1; g->n = (uint8_t)n; g->diet = (uint8_t)CRET_SPECIES_TABLE[lead->species].diet; g->group = lead->group;
    g->x = cx; g->z = cz; g->heading = lead->yaw; g->since = s->tick;
    g->drive = (uint8_t)far_drive_of(lead->bot_state);
    g->tx = lead->goal_x != 0 || lead->goal_z != 0 ? lead->goal_x : cx; g->tz = lead->goal_x != 0 || lead->goal_z != 0 ? lead->goal_z : cz;
    if (g->drive == BOT_GRAZE) { g->tx = lead->graze_x; g->tz = lead->graze_z; }
    if (g->drive == BOT_REST) { g->tx = cx; g->tz = cz; }
    g->timer = 30;
    float c = cosf(g->heading), sn = sinf(g->heading);
    for (int k = 0; k < n; k++) {
        int i = mem[k]; CretAgent* a = &s->agents[i];
        float dx = a->x - cx, dz = a->z - cz, d = hypotf(dx, dz);
        if (d > CRET_FAR_FORM_M) { dx *= CRET_FAR_FORM_M / d; dz *= CRET_FAR_FORM_M / d; }   // the formation is kept close
        g->member[k] = (uint8_t)i; g->fx[k] = c * dx + sn * dz; g->fz[k] = -sn * dx + c * dz; g->fyaw[k] = wrapa(a->yaw - g->heading);
        far_park(s, i, r);
    }
    F->folds++;
    return r;
}
// A unit whose group already has a record (a newcomer that joined a folded
// herd) joins that record, in the formation where it stands, while there is
// room and it stands within CRET_FAR_SPREAD_M of it. Returns the record or -1.
static int far_join(CretSim* s, const int* mem, int n) {
    int group = s->agents[mem[0]].group; if (!group) return -1;
    for (int r = 0; r < CRET_MAX_AGENTS; r++) {
        CretFarGroup* g = &s->far.g[r];
        if (!g->active || g->group != group || g->n + n > CRET_FAR_MEMBERS) continue;
        for (int k = 0; k < n; k++) if (hypotf(s->agents[mem[k]].x - g->x, s->agents[mem[k]].z - g->z) > CRET_FAR_SPREAD_M) return -1;
        float c = cosf(g->heading), sn = sinf(g->heading);
        for (int k = 0; k < n; k++) {
            int i = mem[k], m = g->n++; CretAgent* a = &s->agents[i];
            float dx = a->x - g->x, dz = a->z - g->z, d = hypotf(dx, dz);
            if (d > CRET_FAR_FORM_M) { dx *= CRET_FAR_FORM_M / d; dz *= CRET_FAR_FORM_M / d; }
            g->member[m] = (uint8_t)i; g->fx[m] = c * dx + sn * dz; g->fz[m] = -sn * dx + c * dz; g->fyaw[m] = wrapa(a->yaw - g->heading);
            far_park(s, i, r);
        }
        s->far.folds++;
        return r;
    }
    return -1;
}
// Folds agent i's unit if every member is calm, together, far from every
// person, clear of the anchors it would meet and of anything in the full sim busy with it.
static int far_try_fold(CretSim* s, int i) {
    int mem[CRET_MAX_AGENTS], n = far_unit_of(s, i, mem);
    if (n > CRET_FAR_MEMBERS) return -1;
    float cx = 0, cz = 0;
    for (int k = 0; k < n; k++) { if (!far_calm(s, mem[k])) return -1; cx += s->agents[mem[k]].x / n; cz += s->agents[mem[k]].z / n; }
    for (int k = 0; k < n; k++) {
        const CretAgent* a = &s->agents[mem[k]];
        if (hypotf(a->x - cx, a->z - cz) > CRET_FAR_SPREAD_M || far_person_dist(s, a->x, a->z) < s->cfg.far_range) return -1;
        if (far_anchor_near(s, a->x, a->z, CRET_FAR_APART_M, CRET_SPECIES_TABLE[a->species].diet, a->group) || far_engaged(s, mem[k])) return -1;
    }
    int r = far_join(s, mem, n);
    return r >= 0 ? r : far_fold_unit(s, mem, n);
}
int cret_far_fold(CretSim* s, int i) {
    if (i < 0 || i >= s->cfg.num_agents || s->cfg.far_range <= 0) return -1;
    return far_try_fold(s, i);
}

// ---------------------------------------------------------------- unfolding
// Counts the shapes an unfolding member's capsule overlaps, other than its own body's.
typedef struct FarOverlap { b3BodyId self; int n; } FarOverlap;
static bool far_overlap_hit(b3ShapeId shape, void* c) {
    FarOverlap* o = c;
    if (!B3_ID_EQUALS(b3Shape_GetBody(shape), o->self)) o->n++;
    return true;
}
// Dry, walkable ground at (x, z) where agent i's capsule touches no prop,
// movable prop (a small one it could kick aside too) or other animal: no
// water at its feet (spawn_point's rule).
static bool far_clear(CretSim* s, int i, float x, float z) {
    const CretWorld* w = s->world;
    if (!walkable(w, x, z)) return false;
    float h = cret_height(w, x, z); if (cret_water(w, x, z) > h - 0.2f) return false;
    const CretAgent* a = &s->agents[i];
    cret_prop_stream_touch(&s->props, w, s->phys, x, z, 40.0f, s->tick);
    b3Vec3 pts[2] = {{0, a->radius + 0.05f, 0}, {0, fmaxf(a->height - a->radius, a->radius + 0.01f) + 0.05f, 0}};
    b3ShapeProxy proxy = {pts, 2, a->radius};
    b3QueryFilter f = {.categoryBits = CAT_AGENT, .maskBits = CAT_AGENT | CAT_PROP | CRET_CAT_DYN, .id = 6, .name = "cret_far"};
    FarOverlap o = {a->body, 0};
    b3World_OverlapShape(s->phys, (b3Pos){x, h, z}, &proxy, f, far_overlap_hit, &o);
    return o.n == 0;
}
// A clear spot for agent i: its place in the formation, else the nearest
// clear ground around it, else around the record, farther out each try.
static bool far_spot(CretSim* s, int i, const CretFarGroup* g, float px, float pz, float* ox, float* oz) {
    if (far_clear(s, i, px, pz)) { *ox = px; *oz = pz; return true; }
    for (int t = 0; t < 24; t++) {
        float ang = 6.2831853f * far_rand(s), r = 1.5f + t * 1.1f + 2 * far_rand(s), x = px + cosf(ang) * r, z = pz + sinf(ang) * r;
        if (far_clear(s, i, x, z)) { *ox = x; *oz = z; return true; }
    }
    for (int t = 0; t < 200; t++) {
        float ang = 6.2831853f * far_rand(s), r = 3 + t * 1.5f, x = g->x + cosf(ang) * r, z = g->z + sinf(ang) * r;
        if (far_clear(s, i, x, z)) { *ox = x; *oz = z; return true; }
    }
    return false;
}
// Takes member slot k out of record g (it died, or the sim respawned it).
static void far_drop(CretSim* s, CretFarGroup* g, int k) {
    s->far.of[g->member[k]] = 0;
    for (int m = k; m + 1 < g->n; m++) { g->member[m] = g->member[m + 1]; g->fx[m] = g->fx[m + 1]; g->fz[m] = g->fz[m + 1]; g->fyaw[m] = g->fyaw[m + 1]; }
    if (--g->n == 0) g->active = 0;
}
// A member back in the full sim: what it was doing carries over as a calm
// activity it decides on at once (grazing where it stands, resting, else
// exploring); its memory of other animals is gone (they moved on).
static void far_wake(CretSim* s, int i, int drive) {
    CretAgent* a = &s->agents[i];
    a->bot_state = drive == BOT_GRAZE ? BOT_GRAZE : drive == BOT_REST ? BOT_REST : BOT_WANDER;
    a->bot_target = -1; a->bot_timer = 30; a->bot_next = s->tick; a->goal_x = a->goal_z = 0; a->hunt_phase = 0;
    a->graze_x = a->x; a->graze_z = a->z;
    a->stuck_ticks = a->escape_ticks = 0; a->stuck_x = a->x; a->stuck_z = a->z;
    a->bot_cue = a->bot_react = a->bot_cue_hit = a->bot_seen = 0; a->bot_hurt = a->hurt_ticks; a->eco_hurt = a->hurt_ticks;
    memset(a->known, 0, sizeof a->known);
    if (a->kill > 0 && !s->carcasses[a->kill - 1].active) a->kill = 0;
}
// Group id `id` is carried by nobody alive and by no folded record but r.
static bool far_group_free(const CretSim* s, int id, int r) {
    for (int j = 0; j < s->cfg.num_agents; j++) if (s->agents[j].alive && s->agents[j].group == id) return false;
    for (int q = 0; q < CRET_MAX_AGENTS; q++) if (q != r && s->far.g[q].active && s->far.g[q].group == id) return false;
    return true;
}
int cret_far_unfold(CretSim* s, int r) {
    if (r < 0 || r >= CRET_MAX_AGENTS || !s->far.g[r].active) return 0;
    CretFarGroup* g = &s->far.g[r]; int placed = 0;
    // The group's id may have been taken while it was away (an id is free once nobody alive carries it).
    uint8_t group = g->group;
    if (group && !far_group_free(s, group, r)) {
        for (int k = 0; k < 255; k++) { int id = 1 + (g->member[0] + k) % 255; if (far_group_free(s, id, r)) { group = (uint8_t)id; break; } }
    }
    for (int k = 0; k < g->n; k++) {
        int i = g->member[k]; CretAgent* a = &s->agents[i];
        float px, pz, x, z; far_member_xz(g, k, &px, &pz);
        s->far.of[i] = 0;
        if (!far_spot(s, i, g, px, pz, &x, &z)) {   // nowhere clear within ~300 m of the record (never seen): the sim's spawn point
            for (int t = 0; t < 50; t++) { spawn_point(s, &x, &z); if (far_clear(s, i, x, z)) break; }
        }
        a->alive = true; a->group = group; a->dead_ticks = 0;
        a->x = x; a->z = z; a->y = cret_height(s->world, x, z); a->yaw = wrapa(g->heading + g->fyaw[k]);
        a->vx = a->vy = a->vz = 0; a->speed = 0; a->on_ground = true; a->swimming = false; a->act = 0;
        far_wake(s, i, g->drive);
        update_shape(a);
        b3Body_SetTransform(a->body, (b3Pos){a->x, a->y + a->height * 0.5f, a->z}, b3Quat_identity);
        placed++;
    }
    g->active = 0; g->n = 0; s->far.unfolds++;
    return placed;
}
void cret_far_unfold_all(CretSim* s) { for (int r = 0; r < CRET_MAX_AGENTS; r++) if (s->far.g[r].active) cret_far_unfold(s, r); }

int cret_far_pick(const CretSim* s, int diet, int species, float x, float z) {
    int best = -1; float bs = 1e30f;
    for (int r = 0; r < CRET_MAX_AGENTS; r++) {
        const CretFarGroup* g = &s->far.g[r];
        if (!g->active || g->diet != diet || (species >= 0 && s->agents[g->member[0]].species != species)) continue;
        if (g->drive == BOT_HUNT || g->drive == BOT_FEED || g->drive == BOT_FLEE) continue;   // busy, as far_unit (population.c) skips the busy
        float sc = hypotf(g->x - x, g->z - z) / (g->n >= 2 ? 3.0f : 1.0f);   // a group counts as a third as far (population.c, far_unit)
        if (sc < bs) { bs = sc; best = r; }
    }
    return best;
}
int cret_far_count(const CretSim* s, int* members) {
    int n = 0, m = 0;
    for (int r = 0; r < CRET_MAX_AGENTS; r++) if (s->far.g[r].active) { n++; m += s->far.g[r].n; }
    if (members) *members = m;
    return n;
}

// ---------------------------------------------------------------- moving
// Ground a record may cross: land, or water shallow enough to wade (an
// animal of the full sim walks any slope its mover climbs; only the sea and
// deep water stop it), inside the island's margin. Stricter ground (walkable,
// dry, clear) is asked of where the members are put back (far_clear).
static bool far_passable(const CretWorld* w, float x, float z) {
    if (x < 30 || z < 30 || x > w->size - 30 || z > w->size - 30) return false;
    float h = cret_height(w, x, z); return h > 0.5f && cret_water(w, x, z) < h + 0.5f;
}
// One second toward (tx, tz) at v m/s over passable ground, bending up to
// 70 degrees round what blocks it. Returns true once within `reach` of it,
// or blocked within CRET_FAR_NEAR_M of it (a carcass or a shore at the
// water's edge). Blocked farther away it counts the seconds in g->stuck.
static bool far_go(CretSim* s, CretFarGroup* g, float v, float reach) {
    float dx = g->tx - g->x, dz = g->tz - g->z, d = hypotf(dx, dz);
    g->speed = 0;
    if (d <= reach) { g->stuck = 0; return true; }
    float step = fminf(v, d), want = atan2f(dz, dx);
    static const float BEND[5] = {0, 0.6f, -0.6f, 1.2f, -1.2f};
    for (int k = 0; k < 5; k++) {
        float h = want + BEND[k], nx = g->x + cosf(h) * step, nz = g->z + sinf(h) * step;
        if (!far_passable(s->world, nx, nz)) continue;
        g->x = nx; g->z = nz; g->heading = h; g->speed = step; g->stuck = 0;
        return hypotf(g->tx - nx, g->tz - nz) <= reach;
    }
    if (d < CRET_FAR_NEAR_M) return true;
    if (g->stuck < 255) g->stuck++;
    return false;
}
// The slowest member's walk and trot, a leg wound slowing it as in the full sim's mover (cret_wound_speed).
static void far_gaits(const CretSim* s, const CretFarGroup* g, float* walk, float* trot) {
    *walk = *trot = 1e9f;
    for (int k = 0; k < g->n; k++) {
        const CretAgent* a = &s->agents[g->member[k]]; float slow = cret_wound_speed(a);
        *walk = fminf(*walk, a->walk_speed * slow); *trot = fminf(*trot, a->trot_speed * slow);
    }
}
// Mean position of the herbivores in the world (alive or folded), as pick_goal's herd_center.
static bool far_herd_center(const CretSim* s, float* cx, float* cz) {
    int n = 0; *cx = *cz = 0;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (!cret_far_present(s, j) || !far_herb(b)) continue;
        *cx += b->x; *cz += b->z; n++;
    }
    if (n) { *cx /= n; *cz /= n; }
    return n > 0;
}
// A new wander leg, as pick_goal: toward the group's quest place in longer
// legs when fed, else 30-70 m (herbivores, preferring thick plants) or
// 30-160 m (predators, often toward the herbivores). Returns true on a quest leg.
static bool far_pick_goal(CretSim* s, CretFarGroup* g) {
    const CretAgent* lead = &s->agents[g->member[0]]; bool herb = g->diet == CRET_DIET_HERBIVORE;
    float lo = 30, hi = herb ? 70.0f : 160.0f, qx, qz, base = g->heading;
    if (cret_quest_target(s, cret_quest_of(s, g->member[0], g->group ? 1 : 0), &qx, &qz, NULL) && lead->food > 0.5f && lead->water > 0.45f && far_rand(s) < 0.85f) {
        float dx = qx - g->x, dz = qz - g->z; lo = hi * 0.8f; hi *= 2;
        if (dx * dx + dz * dz < hi * hi * 2.25f && walkable(s->world, qx, qz)) { g->tx = qx; g->tz = qz; return true; }
        base = atan2f(dz, dx);
        for (int t = 0; t < 12; t++) {
            float ang = base + (far_rand(s) - 0.5f) * (t < 8 ? 0.6f : 1.5f), d = lo + (hi - lo) * far_rand(s);
            float x = g->x + cosf(ang) * d, z = g->z + sinf(ang) * d;
            if (walkable(s->world, x, z)) { g->tx = x; g->tz = z; return true; }
        }
        lo *= 0.5f; hi *= 0.5f; base = g->heading;
    }
    float hx, hz;
    if (!herb && lead->food < 0.8f && far_herd_center(s, &hx, &hz) && hypotf(hx - g->x, hz - g->z) > 100.0f && far_rand(s) < 0.6f) base = atan2f(hz - g->z, hx - g->x);
    for (int t = 0; t < 16; t++) {
        float ang = base + (far_rand(s) - 0.5f) * (t < 8 ? 1.5f : 3.0f), d = lo + (hi - lo) * far_rand(s);
        float x = g->x + cosf(ang) * d, z = g->z + sinf(ang) * d;
        if (!walkable(s->world, x, z) || (herb && t < 10 && cret_graze_at(s, x, z) < 0.25f)) continue;
        g->tx = x; g->tz = z; return false;
    }
    g->tx = g->x - cosf(g->heading) * lo; g->tz = g->z - sinf(g->heading) * lo;   // turn back
    return false;
}
// The best ground to graze within ECO_GRAZE_M of the record, as graze_pick:
// intake there over c_max (the same for every member: it depends on the
// plants only), less 0.6 x the landscape of fear, over (1 + d / 100 m).
// Returns the value there.
static float far_graze_pick(CretSim* s, CretFarGroup* g, int salt) {
    float best = -1, bx = g->x, bz = g->z, bv = 0;
    for (int k = 0; k < 1 + 16 * 3; k++) {
        float r = k ? (k - 1) / 16 * 50.0f + 40.0f : 0, ang = (k - 1) % 16 * 0.3927f + (salt & 3) * 0.1f;
        float x = g->x + cosf(ang) * r, z = g->z + sinf(ang) * r;
        if (k && !walkable(s->world, x, z)) continue;
        float bk = cret_graze_at(s, x, z), v = bk / (bk + ECO_B_HALF), sc = v * (1 - 0.6f * fminf(cret_eco_fear_at(s, x, z), 1)) / (1 + r / 100.0f);
        if (sc > best) { best = sc; bx = x; bz = z; bv = v; }
    }
    g->tx = bx; g->tz = bz;
    return bv;
}

// ---------------------------------------------------------------- the members' second
// What each member of a record does this second (act, speed, the activity
// the probes read), then a second of its life by the full sim's rules:
// grazing or eating or drinking first, then survive_body (sim.c). Members
// that die leave the record as ordinary dead animals (kill() left their
// carcass and counted them), and respawn as any dead bot does.
static void far_live(CretSim* s, CretFarGroup* g, int act, int state, int carcass) {
    const float dt = 1.0f;
    float eat_share = cret_eco_vigil_interval(g->n) / (cret_eco_vigil_interval(g->n) + ECO_VIG_UP);   // a grazer's head is up ECO_VIG_UP s each look
    for (int k = 0; k < g->n; k++) {
        int i = g->member[k]; CretAgent* a = &s->agents[i];
        float x, z; far_member_xz(g, k, &x, &z);
        x = clampf(x, 1, s->world->size - 1); z = clampf(z, 1, s->world->size - 1);
        a->distance += hypotf(x - a->x, z - a->z);
        a->x = x; a->z = z; a->y = cret_height(s->world, x, z); a->yaw = wrapa(g->heading + g->fyaw[k]);
        a->speed = g->speed; a->sprinting = false; a->swimming = false; a->on_ground = true;
        a->bot_state = state; a->act = (uint8_t)act;
        a->alive = true;   // while it lives its second (kill() needs it)
        a->ev_food = a->ev_water = 0;
        const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float G = cret_eco_gut_kg(a), L = S->length * cret_growth_scale(a->growth) * 0.45f;
        if (act == CRET_ACT_EAT && carcass < 0 && S->diet == CRET_DIET_HERBIVORE && a->food < 1) {
            float mx = x + cosf(a->yaw) * L, mz = z + sinf(a->yaw) * L;
            float d = eco_graze_dt(s, a, mx, mz, (1 - a->food) * G, dt * eat_share) / G; a->food += d; a->ev_food += d;
            float v = cret_eco_intake(cret_eco_cmax(&s->cfg, cret_mass(a)), cret_graze_at(s, mx, mz));
            a->intake_avg += (v - a->intake_avg) * 24.0f / s->cfg.day_seconds;   // bot_graze's average over a game hour
        } else if (act == CRET_ACT_EAT && carcass >= 0 && s->carcasses[carcass].active) {
            float kg = carcass_bite_kg(a, s->carcasses[carcass].meat, dt);
            s->carcasses[carcass].meat -= kg; a->food += kg / G; a->ev_food += kg / G; a->kill = carcass + 1;
        } else if (act == CRET_ACT_DRINK && a->water < 0.97f) {
            float d = fminf(dt / 8.0f, 1 - a->water); a->water += d; a->ev_water += d;
        } else if (act == CRET_ACT_EAT || act == CRET_ACT_DRINK) a->act = 0;   // full: it stands by
        if (S->diet == CRET_DIET_CARNIVORE) bot_forage(s, a, dt);
        bot_fatigue(s, a, dt);
        for (int t = 0; t < CRET_TICK_HZ; t++) agent_timers(a);
        for (int t = 0; t < CRET_ST_COUNT; t++) a->st[t] = fmaxf(a->st[t] - dt, 0);
        for (int t = 0; t < CRET_AB_SLOTS; t++) a->ab_cd[t] = fmaxf(a->ab_cd[t] - dt, 0);
        a->ticks_alive += CRET_TICK_HZ;
        if (!survive_body(s, i, dt)) { s->far.deaths++; far_drop(s, g, k); k--; continue; }   // starved or died of thirst (kill() made it an ordinary dead animal)
        a->alive = false;
    }
}

// ---------------------------------------------------------------- hunting
// Predator species p hunts prey species q (FAR_KILL has odds for the pairing).
static bool far_hunts(int p, int q) { return FAR_KILL[p][q] > 0; }
// The herbivore of species sp credited with killing a hunter at (x, z): one
// of the prey's own record, else the nearest of that species anywhere (-1:
// none alive). The full sim's island lives as one crowd, where a stegosaur
// is always near the pachycephalosaurs a pack hunts; far groups spread out,
// so the measured rate is kept by crediting the nearest rather than
// requiring one at hand (it does not move; only the kill is its).
static int far_defender(const CretSim* s, const CretFarGroup* h, int sp, float x, float z) {
    for (int k = 0; h && k < h->n; k++) if (s->agents[h->member[k]].species == sp) return h->member[k];
    int best = -1; float bd = 1e30f;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (!cret_far_present(s, j) || b->species != sp || cret_is_person(s, j) || s->far.learned[j]) continue;
        float d = hypotf(b->x - x, b->z - z); if (d < bd) { bd = d; best = j; }
    }
    return best;
}
// What predator `pred` (with its record's n - 1 packmates) makes of prey j
// (best_prey's rule: cret_eco_hunt_eval, armour putting all but a
// tyrannosaur off the grown; a pairing never hunted is not hunted). False: not a prey.
static bool far_eval(CretSim* s, int pred, int pack, int j, CretHuntEval* e) {
    const CretAgent* a = &s->agents[pred]; const CretAgent* b = &s->agents[j];
    if (!far_hunts(a->species, b->species)) return false;
    if (a->species != CRET_SP_TREX && CRET_SPECIES_TABLE[b->species].armor > 0.4f && b->growth > 0.6f) return false;
    *e = cret_eco_hunt_eval(s, pred, j, pack, false);
    return true;
}
// The prey record g's predator `pred` would hunt within range m (-1: none):
// any herbivore in the world that is folded, or in the full sim far from
// every person and busy with nobody (a herd at its nest), not the group it
// gave up on lately; the best by rate, with a profit over the value of
// waiting (best_prey's rule). The prey `keep` it hunts is kept while it still pays.
static int far_prey(CretSim* s, const CretFarGroup* g, int pred, float m, int keep, CretHuntEval* out) {
    const CretAgent* a = &s->agents[pred]; int best = -1; CretHuntEval be = {0};
    float floor = fmaxf(ECO_HUNT_MIN, ECO_WAIT_VALUE * (1 - a->food));
    if (keep >= 0 && cret_far_present(s, keep) && hypotf(s->agents[keep].x - g->x, s->agents[keep].z - g->z) <= 2 * m &&
        far_eval(s, pred, g->n - 1, keep, out) && out->profit > floor) return keep;   // a hunt under way is held while it pays (bot_choose holds it too)
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (!cret_far_present(s, j) || !far_herb(b) || cret_is_person(s, j) || s->far.learned[j]) continue;
        if (s->tick < g->lost_until && (b->group ? b->group == g->lost_group : j + 1 == g->lost)) continue;
        float d = hypotf(b->x - g->x, b->z - g->z); if (d > (j == keep ? 2 * m : m)) continue;
        if (b->alive && (far_person_dist(s, b->x, b->z) < s->cfg.far_range || far_engaged(s, j))) continue;
        CretHuntEval e; if (!far_eval(s, pred, g->n - 1, j, &e)) continue;
        if (best < 0 || e.rate > be.rate) { best = j; be = e; }
    }
    if (best >= 0 && be.profit <= floor) best = -1;
    *out = be;
    return best;
}
// Kills folded agent i of record g (slot k), or agent i of the full sim (g
// NULL), as the full sim would: kill() counts it, credits the killer and
// leaves its carcass where it stood.
static void far_kill(CretSim* s, CretFarGroup* g, int k, int i, int killer) {
    CretAgent* a = &s->agents[i];
    if (g) far_drop(s, g, k);
    a->alive = true;
    if (killer >= 0) { a->last_attacker = killer; a->last_hit_tick = s->tick; a->hurt_ticks = 1; }
    kill(s, i);
    s->far.deaths++;
}
// A herd record trots away from (fx, fz) for CRET_FAR_FLEE_S seconds.
static void far_flee(CretSim* s, CretFarGroup* h, float fx, float fz, int from) {
    float base = atan2f(h->z - fz, h->x - fx);
    h->drive = BOT_FLEE; h->timer = CRET_FAR_FLEE_S; h->target = from + 1; h->tx = h->x; h->tz = h->z;
    for (int t = 0; t < 12; t++) {
        float ang = base + (far_rand(s) - 0.5f) * (t < 6 ? 0.8f : 2.4f), x = h->x + cosf(ang) * 150.0f, z = h->z + sinf(ang) * 150.0f;
        if (walkable(s->world, x, z)) { h->tx = x; h->tz = z; break; }
    }
}
// The member of record g that leads a hunt: the hungriest.
static int far_lead(const CretSim* s, const CretFarGroup* g) {
    int lead = g->member[0];
    for (int k = 1; k < g->n; k++) if (s->agents[g->member[k]].food < s->agents[lead].food) lead = g->member[k];
    return lead;
}
// Predator record r reached prey j: the chase costs the predators their
// sprint (cret_eco_burn at a sprint, for the planned chase), then the roll:
// the prey dies (FAR_KILL), and, rolled apart, the lead predator dies to a
// herd's defender of the species FAR_DEFEND says (far_defender); a folded herd runs
// either way, and the predators leave that group alone a while.
static void far_strike(CretSim* s, int r, int j) {
    CretFarGroup* g = &s->far.g[r]; int lead = far_lead(s, g); CretAgent* b = &s->agents[j];
    CretHuntEval e;
    g->lost = j + 1; g->lost_group = b->group; g->lost_until = s->tick + CRET_FAR_GIVE_UP_S * CRET_TICK_HZ;
    g->drive = BOT_WANDER; g->target = 0;
    if (!far_eval(s, lead, g->n - 1, j, &e)) return;
    s->far.hunts++;
    for (int k = 0; k < g->n; k++) {
        CretAgent* a = &s->agents[g->member[k]]; float v = a->speed; bool sp = a->sprinting;
        a->speed = a->sprint_speed; a->sprinting = true;
        s->stats.burned_kg[a->species] += cret_eco_spend(a, cret_eco_burn(&s->cfg, a) * e.chase_s);
        a->speed = v; a->sprinting = sp;
    }
    int ps = s->agents[lead].species, hr = s->far.of[j] - 1;   // the prey's record (-1: it is in the full sim)
    CretFarGroup* h = hr >= 0 ? &s->far.g[hr] : NULL;
    float u = far_rand(s), fx = g->x, fz = g->z, bx = b->x, bz = b->z;
    if (u < FAR_KILL[ps][b->species]) {
        int jk = 0; while (h && jk < h->n && h->member[jk] != j) jk++;
        far_kill(s, h, jk, j, lead);
        s->far.kills++;
        int c = -1;
        for (int k = 0; k < CRET_MAX_CARCASSES; k++) if (s->carcasses[k].active && s->carcasses[k].agent == j && s->carcasses[k].age == 0) c = k;
        if (c >= 0) { g->drive = BOT_FEED; g->target = c + 1; g->tx = bx; g->tz = bz; }
    }
    float v = far_rand(s), acc = 0;   // a defender, of the species the odds say
    for (int sp = 0; sp < CRET_SPECIES && g->active; sp++) {
        if (FAR_DEFEND[ps][sp] <= 0 || v >= (acc += FAR_DEFEND[ps][sp])) continue;
        int killer = far_defender(s, h && h->active ? h : NULL, sp, fx, fz);
        int lk = 0; while (lk < g->n && g->member[lk] != lead) lk++;
        if (killer >= 0 && lk < g->n) far_kill(s, g, lk, lead, killer);
        break;
    }
    if (h && h->active) far_flee(s, h, fx, fz, r);
}

// ---------------------------------------------------------------- choosing
// What a record does next, by bot_choose's urgencies over its members:
// water by the neediest member (the herd goes with it), grazing, rest and
// wandering by the members' mean, a hunt by the hungriest predator's profit
// on the best prey it senses (*hunt: agent + 1), feeding on a
// carcass within 150 m (*feed: slot + 1); the drive under way x ECO_COMMIT,
// a hunt x ECO_HUNT_COMMIT. Returns -1 when a pair of its members would start
// a nest (a fed pair in condition, in the morning, with that urge the
// strongest): the record unfolds and the full sim builds it.
static int far_choose(CretSim* s, int r, int* hunt, int* feed) {
    CretFarGroup* g = &s->far.g[r]; bool carn = g->diet == CRET_DIET_CARNIVORE; int n = g->n;
    float U[BOT_STATES] = {0};
    float act = 0, food = 0, uw = 0, ug = 0, ur = 0, us = 0, hunger = 0; int hungriest = g->member[0], breeders = 0;
    float gb = cret_graze_at(s, g->tx, g->tz), gv = g->drive == BOT_GRAZE ? gb / (gb + ECO_B_HALF) : 0;
    *hunt = *feed = 0;
    for (int k = 0; k < n; k++) {
        CretAgent* a = &s->agents[g->member[k]]; float ak = bot_active(s, a), fa = bot_feed_act(a, ak);
        act += ak / n; food += a->food / n;
        uw = fmaxf(uw, bot_water_urge(s, a, g->drive == BOT_DRINK));
        if (a->water < 0.15f) uw = fmaxf(uw, 1.3f);   // parched: over anything it is committed to
        ur += bot_rest_urge(a, ak) / n;
        if (!carn) { float bk = cret_graze_at(s, a->x, a->z); ug += bot_graze_hunger(a) * fmaxf(bk / (bk + ECO_B_HALF), gv) * fa / n; }
        else {
            float uh = bot_hunt_hunger(a) * fa; if (uh > hunger) { hunger = uh; hungriest = g->member[k]; }
            us = fmaxf(us, bot_search_urge(a, fa));
        }
        breeders += a->growth >= 0.95f && a->food > 0.6f && a->water > 0.5f && a->fat > ECO_BREED_FAT;
    }
    U[BOT_SEEK_WATER] = uw; U[BOT_REST] = ur * (food < 0.15f ? 0.5f : 1); U[BOT_WANDER] = 0.15f * act;
    if (!carn) U[BOT_GRAZE] = ug;
    else {
        U[BOT_WANDER] = fmaxf(U[BOT_WANDER], us);
        if (hunger > 0) {   // the prey it senses (the one it hunts, twice as far)
            CretHuntEval e; int j = far_prey(s, g, hungriest, CRET_FAR_SENSE_M, g->drive == BOT_HUNT ? g->target - 1 : -1, &e);
            if (j >= 0) { U[BOT_HUNT] = hunger * clampf(e.profit / ECO_PROFIT_FULL, 0, 1); *hunt = j + 1; }
        }
        int c = g->drive == BOT_FEED && g->target > 0 ? g->target - 1 : -1; float cd = 150.0f;
        if (c >= 0 && (!s->carcasses[c].active || s->carcasses[c].carrier || s->carcasses[c].meat <= 0)) c = -1;
        for (int k = 0; k < CRET_MAX_CARCASSES && c < 0; k++) {
            const CretCarcass* kc = &s->carcasses[k];
            if (!kc->active || kc->carrier || kc->meat <= 0) continue;
            float d = hypotf(kc->x - g->x, kc->z - g->z); if (d < cd) { cd = d; c = k; }
        }
        if (c >= 0) {   // bot_choose's scavenging: the meat against the gut; a kill is fed on to 0.95 full, and no new hunt beside enough meat
            const CretCarcass* kc = &s->carcasses[c]; float G = fmaxf(cret_eco_gut_kg(&s->agents[hungriest]), 1e-3f);
            U[BOT_FEED] = (1 - food) * clampf(kc->meat / G, 0, 1);
            if (g->drive == BOT_FEED && g->target == c + 1 && food < 0.95f) U[BOT_FEED] = 1;
            if (U[BOT_HUNT] < 1 && kc->meat >= 0.5f * (1 - food) * G) U[BOT_HUNT] *= ECO_HUNT_FED;
            *feed = c + 1;
        }
    }
    int cur = g->drive == BOT_DRINK ? BOT_SEEK_WATER : g->drive == BOT_FLEE ? BOT_WANDER : g->drive;
    U[cur] *= cur == BOT_HUNT && *hunt == g->target ? ECO_HUNT_COMMIT : ECO_COMMIT;
    static const int ORDER_FAR[] = {BOT_SEEK_WATER, BOT_GRAZE, BOT_HUNT, BOT_FEED, BOT_REST, BOT_WANDER};
    int best = BOT_WANDER;
    for (int k = 5; k >= 0; k--) if (U[ORDER_FAR[k]] >= U[best]) best = ORDER_FAR[k];
    if (g->group && breeders >= 2 && s->time_of_day > 0.25f && s->time_of_day < 0.42f && ECO_NEST_URGE > U[best]) return -1;
    return best;
}

// Water to drink where the record stands or a few metres off (it waded in,
// or stands on the bank): the full sim drinks facing water (water_ahead).
static bool far_water_at_hand(const CretSim* s, const CretFarGroup* g) {
    for (int k = 0; k < 5; k++) {
        float x = g->x + (k ? 6.0f * cosf(k * 1.5708f) : 0), z = g->z + (k ? 6.0f * sinf(k * 1.5708f) : 0);
        if (cret_water(s->world, x, z) > cret_height(s->world, x, z) + 0.1f) return true;
    }
    return false;
}

// ---------------------------------------------------------------- a record's second
// Records step once a second: the drive is chosen (a flight runs its time
// out first), the record moves at its slowest member's walk or trot, and
// its members live the second (far_live) doing what the drive says.
static void far_tick(CretSim* s, int r) {
    CretFarGroup* g = &s->far.g[r]; float walk, trot; far_gaits(s, g, &walk, &trot);
    int prev = g->drive, hunt, feed;
    bool stuck = g->stuck >= CRET_FAR_STUCK_S;   // a target it cannot get to: given up below
    if (g->drive == BOT_FLEE && --g->timer > 0 && !stuck) {
        if (far_go(s, g, trot, 4.0f)) g->timer = 0;
        far_live(s, g, 0, BOT_FLEE, -1);
        return;
    }
    int d = far_choose(s, r, &hunt, &feed);
    if (d < 0) {   // a nest to build: the full sim's job, for a while
        for (int k = 0; k < g->n; k++) s->far.hold[g->member[k]] = s->tick + CRET_FAR_NEST_HOLD_S * CRET_TICK_HZ;
        cret_far_unfold(s, r);
        return;
    }
    if (stuck) {   // the target is out of reach: forget it (a hunt is given up, water searched for again, a new place)
        g->stuck = 0;
        if (g->drive == BOT_HUNT && g->target > 0) { const CretAgent* b = &s->agents[g->target - 1]; g->lost = g->target; g->lost_group = b->group; g->lost_until = s->tick + CRET_FAR_GIVE_UP_S * CRET_TICK_HZ; }
        if (g->drive == BOT_SEEK_WATER) for (int k = 0; k < g->n; k++) s->agents[g->member[k]].water_tick = 0;
        prev = -1;
    }
    if (d != prev && !(d == BOT_SEEK_WATER && prev == BOT_DRINK)) {
        g->drive = (uint8_t)d; g->timer = 30;
        if (d == BOT_GRAZE) far_graze_pick(s, g, r + s->tick);
        if (d == BOT_WANDER) far_pick_goal(s, g);
        if (d == BOT_SEEK_WATER) g->target = 0;
    }
    if (d == BOT_HUNT) g->target = hunt;
    if (d == BOT_FEED) g->target = feed;
    switch (g->drive) {
    case BOT_WANDER: {
        float qx, qz; bool quest = cret_quest_target(s, cret_quest_of(s, g->member[0], g->group ? 1 : 0), &qx, &qz, NULL) && hypotf(qx - g->tx, qz - g->tz) < 400.0f;
        if (far_go(s, g, quest ? trot : walk, 4.0f) || --g->timer <= 0) { far_pick_goal(s, g); g->timer = 30; }
        far_live(s, g, 0, BOT_WANDER, -1);
        break; }
    case BOT_GRAZE: {
        float d0 = hypotf(g->tx - g->x, g->tz - g->z);
        if (!far_go(s, g, d0 > 60 ? trot : walk, 8.0f)) { far_live(s, g, 0, BOT_GRAZE, -1); break; }
        g->speed = 0;
        far_live(s, g, CRET_ACT_EAT, BOT_GRAZE, -1);
        // bot_graze's rule: move on once the intake here falls under 0.85 of its average (or to nothing).
        float v = 0, avg = 0; int m = 0;
        for (int k = 0; k < g->n; k++) {
            const CretAgent* a = &s->agents[g->member[k]]; float cm = cret_eco_cmax(&s->cfg, cret_mass(a));
            v += cret_eco_intake(cm, cret_graze_at(s, a->x, a->z)) / cm; avg += a->intake_avg / cm; m++;
        }
        if (m && (v < 0.85f * avg || v < 0.05f * m)) far_graze_pick(s, g, r + s->tick);
        break; }
    case BOT_SEEK_WATER: case BOT_DRINK: {
        if (far_water_at_hand(s, g)) { g->speed = 0; g->drive = BOT_DRINK; g->target = 1; far_live(s, g, CRET_ACT_DRINK, BOT_DRINK, -1); break; }
        if (g->target == 0) {   // the water a member knows, else a search (find_water from the record), else a leg toward the map's water
            int best = -1; float bd = 1e30f;
            for (int k = 0; k < g->n; k++) {
                const CretAgent* a = &s->agents[g->member[k]];
                if (a->water_tick > 0) { float dd = hypotf(a->water_x - g->x, a->water_z - g->z); if (dd < bd) { bd = dd; best = g->member[k]; } }
            }
            CretAgent* lead = &s->agents[g->member[0]]; float wx, wz;
            if (best >= 0) { wx = s->agents[best].water_x; wz = s->agents[best].water_z; }
            else {
                float sx = lead->x, sz = lead->z; lead->x = g->x; lead->z = g->z;
                bool found = find_water(s, lead, &wx, &wz);
                lead->x = sx; lead->z = sz;
                if (!found) {   // a leg toward the map's water, else anywhere (the search starts again from there)
                    if (!cret_world_toward_water(s->world, g->x, g->z, 320.0f, &g->tx, &g->tz)) far_pick_goal(s, g);
                    g->target = -1;
                    if (far_go(s, g, trot, 4.0f)) g->target = 0;
                    far_live(s, g, 0, BOT_SEEK_WATER, -1);
                    break;
                }
                for (int k = 0; k < g->n; k++) { CretAgent* a = &s->agents[g->member[k]]; a->water_x = wx; a->water_z = wz; a->water_tick = s->tick; }
            }
            float dx = wx - g->x, dz = wz - g->z, dl = fmaxf(hypotf(dx, dz), 1e-3f), back = fminf(6.0f, dl);
            g->tx = wx - dx / dl * back; g->tz = wz - dz / dl * back; g->target = 1;
        } else if (g->target < 0) {   // on the leg toward the map's water: search again at its end
            if (far_go(s, g, trot, 4.0f)) g->target = 0;
            far_live(s, g, 0, BOT_SEEK_WATER, -1);
            break;
        }
        if (!far_go(s, g, trot, 8.0f)) { far_live(s, g, 0, BOT_SEEK_WATER, -1); break; }
        g->drive = BOT_DRINK;
        far_live(s, g, CRET_ACT_DRINK, BOT_DRINK, -1);
        for (int k = 0; k < g->n; k++) { CretAgent* a = &s->agents[g->member[k]]; a->water_tick = s->tick; }
        break; }
    case BOT_REST:
        g->speed = 0;
        far_live(s, g, CRET_ACT_REST, BOT_REST, -1);
        break;
    case BOT_HUNT: {   // approach the prey at a trot, a walk inside 90 m; strike from CRET_FAR_STRIKE_M
        int j = g->target - 1;
        if (j < 0 || !cret_far_present(s, j)) { g->drive = BOT_WANDER; far_pick_goal(s, g); far_live(s, g, 0, BOT_WANDER, -1); break; }
        const CretAgent* b = &s->agents[j];
        g->tx = b->x; g->tz = b->z;
        bool there = far_go(s, g, hypotf(b->x - g->x, b->z - g->z) > 90 ? trot : walk, CRET_FAR_STRIKE_M);
        for (int k = 0; k < g->n; k++) s->agents[g->member[k]].bot_target = j;   // what the probes read as the hunt's target
        far_live(s, g, 0, BOT_HUNT, -1);
        if (g->active && there) far_strike(s, r, j);
        break; }
    case BOT_FEED: {
        int c = g->target - 1;
        if (c < 0 || !s->carcasses[c].active || s->carcasses[c].meat <= 0) { g->drive = BOT_REST; far_live(s, g, CRET_ACT_REST, BOT_REST, -1); break; }
        g->tx = s->carcasses[c].x; g->tz = s->carcasses[c].z;
        if (!far_go(s, g, trot, 6.0f)) { far_live(s, g, 0, BOT_FEED, -1); break; }
        far_live(s, g, CRET_ACT_EAT, BOT_FEED, c);
        break; }
    default: g->drive = BOT_WANDER; far_live(s, g, 0, BOT_WANDER, -1); break;
    }
}

// ---------------------------------------------------------------- the step
void cret_far_step(CretSim* s) {
    CretFar* F = &s->far;
    if (s->tick % CRET_TICK_HZ != CRET_TICK_HZ - 1) return;
    if (s->cfg.far_range <= 0) { cret_far_unfold_all(s); return; }   // off (or turned off): everyone in the full sim
    float near = s->cfg.far_range - CRET_FAR_HYST;
    for (int r = 0; r < CRET_MAX_AGENTS; r++) {   // members the sim brought back itself (a respawn) leave; records near a person or the full sim unfold
        CretFarGroup* g = &F->g[r]; if (!g->active) continue;
        for (int k = 0; k < g->n; k++) if (s->agents[g->member[k]].alive) { far_drop(s, g, k); k--; }
        if (!g->active) continue;
        if (far_person_dist(s, g->x, g->z) - CRET_FAR_FORM_M < near || far_anchor_near(s, g->x, g->z, CRET_FAR_MEET_M + CRET_FAR_FORM_M, g->diet, 0)) cret_far_unfold(s, r);
    }
    for (int r = 0; r < CRET_MAX_AGENTS; r++) if (F->g[r].active) far_tick(s, r);
    uint8_t tried[CRET_MAX_AGENTS + 1] = {0};
    for (int i = 0; i < s->cfg.num_agents; i++) {
        const CretAgent* a = &s->agents[i]; if (!a->alive || F->of[i]) continue;
        if (a->group) { if (tried[a->group]) continue; tried[a->group] = 1; }
        far_try_fold(s, i);
    }
}
