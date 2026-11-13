// Scripted bots (cret_bot_input), included by sim.c after the rest of the
// sim so they use its static helpers the way critters.c and scent.c do.

// Scripted bots until policies take over, using the same mechanics as any
// animal (docs/ecosystem.md):
//   - what a bot knows comes from its senses (eco.c): what it has seen, heard
//     or smelled, after a reaction delay that grows with its mass;
//   - every decision tick (3 Hz staggered by slot, 10 Hz with something
//     close, at once after an alarm or a hit) it scores each activity,
//     urgency x chance, and does the best (bot_choose): flee or stand, drink,
//     graze, hunt, scavenge, rest, go back to the herd or a parent, carry
//     food to hungry young, nest, guard a kill or a nest, explore;
//   - between decisions the motor steers it (steer: around trunks, rocks,
//     deep water and cliffs): grazing with its head coming up (vigilance),
//     stalking, charging and flanking with its pack, fleeing toward its herd;
//   - abilities when the moment fits (bot_abilities); an animal with raptors
//     on its back bucks to shake them off; its head (a stegosaur's tail)
//     turns to aim at a limited speed that falls with mass (bot_aim), so a
//     quick animal can bite a big one's flank and get clear before it turns;
//   - a hit leaves the bot alert to its attacker for 20-60 s (bot_perceive):
//     a herbivore keeps defending or keeps away, a predator turns on it if it
//     would win the fight and backs off otherwise. The alert runs down faster
//     with the attacker far off and ends once it is 120 m away or dead.
const char* const CRET_BOT_STATE_NAMES[BOT_STATES] = {"wander", "graze", "flee", "hunt", "rest", "drink", "seek water", "feed", "defend", "nest", "raid", "carry",
                                                      "guard", "group"};

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
        if (herb && t < 10 && cret_graze_at(s, gx, gz) < 0.25f) continue;   // grazers head for thick plants
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
    float d = wrapa(want - a->yaw);
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
// Food lying at the nest of agent i's own group: what a parent brought for the young.
static bool at_own_nest(const CretSim* s, int i, const CretCarcass* k) {
    int n = cret_nest_of(s, i); if (n < 0) return false;
    float r = cret_nest_radius(s->nests[n].species) + 3.0f, dx = k->x - s->nests[n].x, dz = k->z - s->nests[n].z;
    return dx * dx + dz * dz < r * r;
}
// Nearest carcass with meat within range (a grown animal that is not
// starving leaves the food at its own nest to the young).
static int nearest_carcass(const CretSim* s, const CretAgent* a, float range) {
    int best = -1, i = (int)(a - s->agents); float bd = range * range;
    for (int c = 0; c < CRET_MAX_CARCASSES; c++) {
        const CretCarcass* k = &s->carcasses[c]; if (!k->active || k->meat < k->max_meat * 0.05f || k->carrier) continue;
        if (a->growth >= 0.9f && a->food > 0.3f && at_own_nest(s, i, k)) continue;
        float dx = k->x - a->x, dz = k->z - a->z, d = dx * dx + dz * dz; if (d < bd) { bd = d; best = c; }
    }
    return best;
}
// The nest a fed predator parent would carry food to: its group's, within
// 400 m, with a young member (growth under 0.6) within 150 m of it (-1: none).
static int provide_nest(const CretSim* s, int i) {
    const CretAgent* a = &s->agents[i];
    if (!a->group || a->growth < 0.9f || CRET_SPECIES_TABLE[a->species].diet != CRET_DIET_CARNIVORE) return -1;
    int n = cret_nest_of(s, i); if (n < 0) return -1;
    const CretNest* N = &s->nests[n];
    if ((N->x - a->x) * (N->x - a->x) + (N->z - a->z) * (N->z - a->z) > 400.0f * 400.0f) return -1;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (j == i || !b->alive || b->group != a->group || b->growth >= 0.6f) continue;
        if ((N->x - b->x) * (N->x - b->x) + (N->z - b->z) * (N->z - b->z) < 150.0f * 150.0f) return n;
    }
    return -1;
}
// Carcass c is worth carrying home for agent i: on the ground, liftable (or a
// body it can tear a piece off), not already at the nest, and picked for
// about half of the (slot, carcass) pairs, so the choice is deterministic
// and draws nothing from the rng.
static bool worth_carrying(const CretSim* s, int i, int c) {
    const CretCarcass* k = &s->carcasses[c]; float limit = cret_carry_limit(&s->agents[i]);
    bool liftable = cret_item_kg(k) <= limit || (k->item == CRET_ITEM_BODY && k->meat > 2.0f && limit >= 1.0f);   // a body too heavy: a piece of it (cret_carry_tear)
    return k->active && !k->carrier && k->meat > k->max_meat * 0.1f && liftable && !at_own_nest(s, i, k) && ((i + c) & 1) == 0;
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


// Starts noticing agent j (a threat in view, or the one that just hit it).
// A cue already waiting keeps its time; a hit takes over from a threat that
// is still being noticed, without waiting longer.
static void bot_cue(CretSim* s, CretAgent* a, int j, bool hit) {
    if (a->bot_cue == j + 1) { a->bot_cue_hit |= hit; return; }
    if (a->bot_cue && !hit) return;
    int d = eco_react_ticks(s, a);
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

// Vigilance while feeding (eco.h): every few seconds the head comes up for
// ECO_VIG_UP s, less often in a bigger group (many eyes). True while it is up.
static bool bot_vigilant(CretSim* s, int i) {
    CretAgent* a = &s->agents[i];
    if (a->head_up > 0) { a->head_up -= CRET_DT; return true; }
    if ((a->vig_t -= CRET_DT) > 0) return false;
    int n = 1;
    for (int j = 0; j < s->cfg.num_agents && a->group; j++)
        n += j != i && s->agents[j].alive && s->agents[j].group == a->group && agent_dist(a, &s->agents[j]) < ECO_VIG_GROUP_M;
    a->head_up = ECO_VIG_UP; a->vig_t = cret_eco_vigil_interval(n) * (0.75f + 0.5f * frand(&s->rng));
    return true;
}

// ---------------------------------------------------------------- drives
// Every decision tick a bot scores each activity, U = urgency x chance (the
// table in docs/ecosystem.md section 5), the one it is doing x ECO_COMMIT,
// and does the best; ties go to the earlier activity in ORDER. Between
// decisions the motor below steers it toward that activity's target.
static const int ORDER[] = {BOT_FLEE, BOT_DEFEND, BOT_SEEK_WATER, BOT_GRAZE, BOT_HUNT, BOT_FEED, BOT_REST, BOT_GROUP, BOT_CARRY, BOT_NEST, BOT_GUARD, BOT_RAID, BOT_WANDER};
#define NORDER (int)(sizeof ORDER / sizeof *ORDER)
// The activity a state belongs to: drinking is the end of seeking water.
static int activity_of(int state) { return state == BOT_DRINK ? BOT_SEEK_WATER : state; }
static int group_count(const CretSim* s, int i, float* cx, float* cz) {
    const CretAgent* a = &s->agents[i]; int n = 0; float x = 0, z = 0;
    for (int j = 0; j < s->cfg.num_agents && a->group; j++) {
        const CretAgent* b = &s->agents[j]; if (j == i || !b->alive || b->group != a->group) continue;
        x += b->x; z += b->z; n++;
    }
    if (n) { *cx = x / n; *cz = z / n; }
    return n;
}
// The best ground to graze within ECO_GRAZE_M: intake there over c_max,
// less 0.6 x its risk, over (1 + d / 100 m); a herd member keeps within
// reach of its herd. Sets graze_x, graze_z and graze_val.
static void graze_pick(CretSim* s, int i) {
    CretAgent* a = &s->agents[i]; float cm = cret_eco_cmax(&s->cfg, cret_mass(a)), cx = a->x, cz = a->z;
    bool herd = group_count(s, i, &cx, &cz) > 0;
    float best = -1, bx = a->x, bz = a->z, bv = 0;
    for (int k = 0; k < 1 + 16 * 3; k++) {
        float r = k ? (k - 1) / 16 * 50.0f + 40.0f : 0, ang = (k - 1) % 16 * 0.3927f + (i & 3) * 0.1f;
        float x = a->x + cosf(ang) * r, z = a->z + sinf(ang) * r;
        if (k && !walkable(s->world, x, z)) continue;
        float v = cret_eco_intake(cm, cret_graze_at(s, x, z)) / cm, sc = v * (1 - 0.6f * fminf(cret_eco_risk(s, i, x, z), 1)) / (1 + r / 100.0f);
        if (herd) { float dc = hypotf(x - cx, z - cz); if (dc > ECO_HERD_M) sc /= 1 + (dc - ECO_HERD_M) / ECO_HERD_M; }
        if (sc > best) { best = sc; bx = x; bz = z; bv = v; }
    }
    a->graze_x = bx; a->graze_z = bz; a->graze_val = bv;
}
// The prey agent i would hunt now (-1: none worth it) and its value: the
// known herbivores (and, while it is alert to one, the animal that hurt it),
// best by rate, if the profit beats ECO_HUNT_MIN and waiting.
static int best_prey(CretSim* s, int i, CretHuntEval* out) {
    CretAgent* a = &s->agents[i]; int pack = pack_near(s, i, ECO_PACK_M), best = -1; CretHuntEval be = {0};
    for (int k = 0; k < CRET_KNOWN; k++) {
        const CretKnown* m = &a->known[k]; if (!m->who || m->since > s->tick) continue;
        int j = m->who - 1; const CretAgent* b = &s->agents[j];
        if (!b->alive || CRET_SPECIES_TABLE[b->species].diet != CRET_DIET_HERBIVORE || (a->lost == j + 1 && s->tick < a->lost_until)) continue;
        if (a->species != CRET_SP_TREX && CRET_SPECIES_TABLE[b->species].armor > 0.4f && b->growth > 0.6f) continue;   // armour puts raptors off
        CretHuntEval e = cret_eco_hunt_eval(s, i, j, pack, cret_eco_aware(s, j, i));
        if (best < 0 || e.rate > be.rate) { best = j; be = e; }
    }
    if (best >= 0 && be.profit <= fmaxf(ECO_HUNT_MIN, ECO_WAIT_VALUE * (1 - a->food))) best = -1;
    *out = be;
    return best;
}
// Starts activity `act` on `target`.
static void bot_start(CretSim* s, int i, int act, int target) {
    CretAgent* a = &s->agents[i]; int was = a->bot_state;
    a->bot_state = act; a->bot_target = target; a->bot_timer = 0;
    switch (act) {
    case BOT_HUNT: a->hunt_phase = 0; a->hunt_gap = 1e9f; a->hunt_tick = s->tick; break;
    case BOT_GRAZE: if (was != BOT_GRAZE || (a->graze_x == 0 && a->graze_z == 0)) graze_pick(s, i); break;
    case BOT_SEEK_WATER: {   // the water it knows, else a search (at most every 30 s: a failed one scans ~4000 points)
        float wx, wz;
        if (a->water_tick > 0) {
            float dx = a->water_x - a->x, dz = a->water_z - a->z, dl = fmaxf(hypotf(dx, dz), 1e-3f), back = fminf(6.0f, dl);
            a->goal_x = a->water_x - dx / dl * back; a->goal_z = a->water_z - dz / dl * back;
        } else if (a->water_tick == 0 || s->tick + a->water_tick > 30 * CRET_TICK_HZ) {
            if (find_water(s, a, &wx, &wz)) { a->water_x = wx; a->water_z = wz; a->water_tick = s->tick; }
            else {   // none within the search: a leg toward the nearest water on the map, then look again
                a->water_tick = -s->tick;
                if (!cret_world_toward_water(s->world, a->x, a->z, 320.0f, &a->goal_x, &a->goal_z)) a->goal_x = a->goal_z = 0;
            }
        }
        break; }
    case BOT_WANDER: pick_goal(s, a, 30, CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_CARNIVORE ? 160 : 70); a->bot_timer = 900; break;
    case BOT_NEST: a->bot_timer = 7 * CRET_TICK_HZ; break;
    case BOT_REST:   // midday: a big animal in the open looks for shade within 60 m
        a->goal_x = a->goal_z = 0;
        if (fabsf(s->time_of_day - 0.5f) * 24 < ECO_HEAT_HOURS * 0.5f && cret_mass(a) > ECO_SHADE_KG && cret_eco_canopy(s->world, a->x, a->z) < ECO_SHADE)
            for (int k = 0; k < 16; k++) {
                float r = k < 8 ? 30.0f : 60.0f, ang = k * 0.785f + (k >= 8) * 0.39f, x = a->x + cosf(ang) * r, z = a->z + sinf(ang) * r;
                if (walkable(s->world, x, z) && cret_eco_canopy(s->world, x, z) >= ECO_SHADE) { a->goal_x = x; a->goal_z = z; a->bot_timer = 60 * CRET_TICK_HZ; break; }
            }
        break;
    case BOT_CARRY: a->bot_timer = 120 * CRET_TICK_HZ; break;
    default: break;
    }
}
// The pack joins a hunt: the members within 150 m that are fit, not full and
// not busy with something urgent take the same prey. Silently: a call would
// tell the prey too (calls are heard at ECO_CALL_M).
static void pack_join(CretSim* s, int i, int prey) {
    CretAgent* a = &s->agents[i];
    if (!a->group) return;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        CretAgent* c = &s->agents[j];
        if (j == i || cret_is_person(s, j) || !c->alive || c->group != a->group || c->bot_state == BOT_HUNT || c->bot_state == BOT_FLEE || c->bot_state == BOT_FEED) continue;
        if (agent_dist(a, c) > 150.0f || c->health < 0.4f * c->max_health || c->broken > 0 || c->digest || c->food > ECO_HELP_FOOD) continue;
        if (cret_eco_hunt_eval(s, j, prey, pack_near(s, j, ECO_PACK_M), true).profit <= 0) continue;   // only if it pays for the helper too (bot_choose's rule)
        bot_start(s, j, BOT_HUNT, prey);
    }
}
// Time of day (docs/ecosystem.md section 7): how active a species is, at
// every third hour from midnight (0.25 of the day is sunrise, 6 h), linear
// in between. It multiplies hunting, grazing and exploring, and under
// ECO_REST_HOURS it adds ECO_REST_BONUS to resting. Rain takes it down to
// ECO_RAIN_ACTIVE.
static const float ACTIVE[CRET_SPECIES][8] = {
    //                        0 h   3 h   6 h   9 h   12 h  15 h  18 h  21 h
    [CRET_SP_RAPTOR]     = {0.35f, 0.4f, 1.2f, 0.7f, 0.6f, 0.7f, 1.2f, 0.5f},   // crepuscular: dawn and dusk
    [CRET_SP_STEGO]      = {0.3f, 0.3f, 0.8f, 1.0f, 0.6f, 1.0f, 0.8f, 0.3f},    // by day, a midday pause in the shade
    [CRET_SP_TREX]       = {0.8f, 0.4f, 0.5f, 0.6f, 0.4f, 0.6f, 1.0f, 1.2f},    // dusk into the night, low at midday
    [CRET_SP_PACHY]      = {0.3f, 0.3f, 1.0f, 1.2f, 0.7f, 1.2f, 0.9f, 0.3f},    // morning and late afternoon
    [CRET_SP_SAUROPELTA] = {0.3f, 0.3f, 0.8f, 1.0f, 0.6f, 1.0f, 0.8f, 0.3f},
    [CRET_SP_GALLI]      = {0.3f, 0.3f, 1.0f, 1.2f, 0.7f, 1.2f, 0.9f, 0.3f},
};
static float bot_active(const CretSim* s, const CretAgent* a) {
    float h = s->time_of_day * 8; int k = (int)h % 8; float f = h - floorf(h);
    float v = ACTIVE[a->species][k] + (ACTIVE[a->species][(k + 1) % 8] - ACTIVE[a->species][k]) * f;
    return s->cloud_cover > 0.9f ? v * ECO_RAIN_ACTIVE : v;
}
// Agent i's group: its nearest grown member (growth at least 0.9, within
// `range`, -1: none), and how many young (growth under ECO_YOUNG) it has.
static int group_adult(const CretSim* s, int i, float range) {
    const CretAgent* a = &s->agents[i]; int best = -1; float bd = range;
    for (int j = 0; j < s->cfg.num_agents && a->group; j++) {
        const CretAgent* b = &s->agents[j];
        if (j == i || !b->alive || b->group != a->group || b->growth < 0.9f) continue;
        float d = agent_dist(a, b); if (d < bd) { bd = d; best = j; }
    }
    return best;
}
// A young member of agent i's group within `range` that a known threat is
// within twice i's length of (-1: none); *threat the threat.
static int young_in_danger(const CretSim* s, int i, float range, int* threat) {
    const CretAgent* a = &s->agents[i]; float L2 = 2 * CRET_SPECIES_TABLE[a->species].length * cret_growth_scale(a->growth);
    for (int j = 0; j < s->cfg.num_agents && a->group; j++) {
        const CretAgent* y = &s->agents[j];
        if (j == i || !y->alive || y->group != a->group || y->growth >= ECO_YOUNG || agent_dist(a, y) > range) continue;
        for (int k = 0; k < CRET_KNOWN; k++) {
            const CretKnown* m = &a->known[k]; if (!m->who || m->since > s->tick) continue;
            const CretAgent* t = &s->agents[m->who - 1];
            if (cret_eco_threat_of(s, y, t) > 0 && hypotf(t->x - y->x, t->z - y->z) < L2) { *threat = m->who - 1; return j; }
        }
    }
    return -1;
}

static void bot_choose(CretSim* s, int i, CretInput* in) {
    CretAgent* a = &s->agents[i]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    bool carn = S->diet == CRET_DIET_CARNIVORE; float L = S->length * cret_growth_scale(a->growth), health = a->health / fmaxf(a->max_health, 1);
    float U[BOT_STATES] = {0}; int T[BOT_STATES]; for (int k = 0; k < BOT_STATES; k++) T[k] = -1;
    // The hour sets how active it is; hunger overrides it for feeding (a
    // starving animal feeds at any hour).
    float risk = cret_eco_risk(s, i, a->x, a->z), act = bot_active(s, a), feed_act = act + (1 - act) * (1 - a->food) * (1 - a->food);
    // Flee or stand: a threat coming inside the flight distance, the animal
    // that hurt it, or (at a walk) the risk of where it is.
    float fd; int p = cret_eco_flight(s, i, &fd), foe = a->bot_alert > 0 ? a->bot_foe - 1 : -1;
    if (foe >= 0) { float d = agent_dist(a, &s->agents[foe]); if (p < 0 || d < fd + 10) { p = foe; fd = d; } }
    int pt = p; float worst = 0;
    for (int k = 0; k < CRET_KNOWN && pt < 0; k++) {   // the riskiest threat it knows, for the direction to walk off in
        const CretKnown* m = &a->known[k]; if (!m->who || m->since > s->tick) continue;
        float D = cret_eco_threat_of(s, a, &s->agents[m->who - 1]); if (D > worst) { worst = D; pt = m->who - 1; }
    }
    // Walking off from mere risk: a hungry or thirsty animal bears more of it.
    float calm = ECO_RISK_CALM + ECO_RISK_NEED * fmaxf(1 - a->food, 1 - a->water);
    float uflee = p >= 0 ? 1 : sqrtf(clampf(risk - calm, 0, 1));
    if (!carn && a->st[CRET_ST_FEAR] <= 0 && pt >= 0) {
        bool engaged = false; herb_threat(s, i, pt, agent_dist(a, &s->agents[pt]), foe, &engaged);
        // The armoured (and a pachycephalosaur facing a lone predator its size) stand rather than run, once it comes.
        if ((engaged || p >= 0) && can_defend(s, a, &s->agents[pt])) { U[BOT_DEFEND] = uflee; T[BOT_DEFEND] = pt; }
        else { U[BOT_FLEE] = uflee; T[BOT_FLEE] = pt; }
    } else if (carn && pt >= 0) {
        // Turn on the attacker if it would win (P_kill over 0.5, its pack's bites counted) and is fit.
        bool take = foe == pt && health > 0.65f && a->broken <= 0 &&
                    (a->food < 0.75f || CRET_SPECIES_TABLE[s->agents[pt].species].diet == CRET_DIET_CARNIVORE) &&   // fed, it leaves a tail-swinging herbivore be (a predator it answers)
                    (a->species == CRET_SP_TREX || cret_eco_hunt_eval(s, i, pt, pack_near(s, i, ECO_PACK_M), true).p_kill > 0.5f);
        if (take && fd < 30.0f + L) { U[BOT_HUNT] = 1; T[BOT_HUNT] = pt; }   // turn on the attacker
        else if (p >= 0) { U[BOT_FLEE] = 1; T[BOT_FLEE] = p; }
        else if (cret_eco_threat_of(s, a, &s->agents[pt]) > 0) { U[BOT_FLEE] = uflee; T[BOT_FLEE] = pt; }
    }
    // A parent stands between its young and a threat close to them (section 6).
    int yt, young = !carn && a->growth >= 0.9f ? young_in_danger(s, i, 40.0f, &yt) : -1;
    if (young >= 0 && U[BOT_DEFEND] < ECO_DEFEND_YOUNG && (can_defend(s, a, &s->agents[yt]) || cret_eco_threat_of(s, a, &s->agents[yt]) == 0)) {
        U[BOT_DEFEND] = ECO_DEFEND_YOUNG; T[BOT_DEFEND] = yt;
    }
    if (a->st[CRET_ST_FEAR] > 0 && a->fear_src) { U[BOT_FLEE] = 1; T[BOT_FLEE] = a->fear_src - 1; }   // a roar
    if (carn && a->bot_state == BOT_HUNT && health < 0.45f) { U[BOT_FLEE] = 1; T[BOT_FLEE] = a->last_attacker >= 0 ? a->last_attacker : a->bot_target; }
    // Drink: thirst, and how far the water it knows is.
    float uw = a->water < 0.15f ? 1 : (1 - a->water) * (1 - a->water);
    float dw = a->water_tick > 0 ? hypotf(a->water_x - a->x, a->water_z - a->z) : ECO_WATER_GUESS_M;
    U[BOT_SEEK_WATER] = uw * expf(-dw / 400.0f);
    if (a->water < 0.15f || (a->bot_state == BOT_DRINK && a->water < 0.9f)) U[BOT_SEEK_WATER] = 1;   // parched, or drinking: water first
    // Leave in time: the water left lasts under twice the walk there (a slow animal far from water sets off early).
    if (a->water / fmaxf(cret_eco_thirst(s, a), 1e-6f) < 2 * dw / fmaxf(a->trot_speed, 0.5f) + ECO_WATER_MARGIN_S) U[BOT_SEEK_WATER] = fmaxf(U[BOT_SEEK_WATER], 0.9f);
    if (!carn) {   // graze: hunger (and an empty reserve once the gut is well filled), what the ground gives, its risk
        float ug = clampf((1 - a->food) + (a->food > ECO_FAT_ABOVE ? 0.5f * (1 - a->fat) : 0), 0, 1);
        float L2 = L * 0.45f, cm = cret_eco_cmax(&s->cfg, cret_mass(a));
        float here = cret_eco_intake(cm, cret_graze_at(s, a->x + cosf(a->yaw) * L2, a->z + sinf(a->yaw) * L2)) / cm;
        if (a->bot_state != BOT_GRAZE && (s->tick + i) % (10 * CRET_TICK_HZ) < 10) graze_pick(s, i);   // now and then: where it would go
        U[BOT_GRAZE] = ug * fmaxf(here, a->graze_val) * (1 - 0.6f * fminf(risk, 1)) * feed_act;
    } else {
        // Hunt: hunger past the digestion pause, the best prey's profit.
        float uh = a->digest ? 0 : a->food > 0.75f && a->fat >= 0.3f ? 0 : powf(1 - a->food, 1.5f);
        int helping = -1;   // a packmate hunting close by draws it in, unless it is full
        for (int j = 0; j < s->cfg.num_agents && a->group && !a->digest && a->food <= ECO_HELP_FOOD; j++) {
            const CretAgent* c = &s->agents[j];
            if (j != i && c->alive && c->group == a->group && c->bot_state == BOT_HUNT && c->bot_target >= 0 && agent_dist(a, c) < 150.0f) { helping = c->bot_target; break; }
        }
        CretHuntEval e; int q = a->bot_state == BOT_HUNT && a->bot_target >= 0 && s->agents[a->bot_target].alive ? a->bot_target : best_prey(s, i, &e);
        if (q >= 0 && q == a->bot_target) e = cret_eco_hunt_eval(s, i, q, pack_near(s, i, ECO_PACK_M), cret_eco_aware(s, q, i));
        if (q >= 0 && U[BOT_HUNT] < 1) { U[BOT_HUNT] = uh * clampf(e.profit / ECO_PROFIT_FULL, 0, 1) * feed_act; T[BOT_HUNT] = q; }
        if (helping >= 0 && U[BOT_HUNT] < ECO_HELP * act && (q < 0 || q == helping) && cret_eco_hunt_eval(s, i, helping, pack_near(s, i, ECO_PACK_M), true).profit > 0) {
            U[BOT_HUNT] = ECO_HELP * act; T[BOT_HUNT] = helping;
        }
        float cd;   // small prey (critters.c): a hatchling's food, and a small adult's staple between big kills
        if (U[BOT_HUNT] < 1 && cret_mass(a) < ECO_SMALL_PREY_KG && uh > 0 && cret_critter_nearest(s, i, 45.0f, &cd) >= 0 && uh * 0.6f * feed_act > U[BOT_HUNT]) { U[BOT_HUNT] = uh * 0.6f * feed_act; T[BOT_HUNT] = -1; }
        // Scavenge: a carcass within reach, by its meat less its risk; after a
        // kill it eats until 0.95 full.
        int c = nearest_carcass(s, a, 150.0f);
        if (c < 0 && a->kill > 0) {   // its own kill, remembered: it goes back to it from as far as a drink or a rest took it
            const CretCarcass* k = &s->carcasses[a->kill - 1];
            if (k->active && !k->carrier && k->meat >= 0.05f * k->max_meat && hypotf(k->x - a->x, k->z - a->z) < ECO_KILL_BACK_M) c = a->kill - 1;
        }
        if (c >= 0) {
            const CretCarcass* k = &s->carcasses[c];
            U[BOT_FEED] = (1 - a->food) * clampf(k->meat / fmaxf(cret_eco_gut_kg(a), 1e-3f) - cret_eco_risk(s, i, k->x, k->z), 0, 1); T[BOT_FEED] = c;
            if (a->bot_state == BOT_FEED && a->bot_target == c && a->food < 0.95f && k->meat > 0) U[BOT_FEED] = 1;
            // Meat lying within reach, enough to fill it: no new hunt (a kill is fed on for as long as it lasts).
            if (U[BOT_HUNT] < 1 && T[BOT_HUNT] >= 0 && k->meat >= 0.5f * (1 - a->food) * cret_eco_gut_kg(a)) U[BOT_HUNT] *= ECO_HUNT_FED;
        }
        int nk = raid_target(s, i);
        if (nk >= 0) { U[BOT_RAID] = (1 - a->food) * 0.4f; T[BOT_RAID] = nk; }
        // Provision: a fed parent with hungry young at the nest carries a liftable piece home.
        if (a->food > 0.6f && provide_nest(s, i) >= 0) {
            int k = a->bot_state == BOT_CARRY ? a->bot_target : nearest_carcass(s, a, 60.0f);
            // A load in the mouth goes home; just fed from a kill, a parent takes some home before it rests.
            if (k >= 0 && (a->carry || worth_carrying(s, i, k))) { U[BOT_CARRY] = a->carry ? 1.0f : a->food > 0.9f ? ECO_PROVIDE_FED : ECO_PROVIDE; T[BOT_CARRY] = k; }
        }
        // Guard the kill while there is meat on it and nothing bigger about.
        int kc = a->kill - 1;
        if (kc >= 0 && s->carcasses[kc].active && !s->carcasses[kc].carrier && s->carcasses[kc].meat > 0.2f * s->carcasses[kc].max_meat && risk < 0.5f) {
            U[BOT_GUARD] = 0.5f; T[BOT_GUARD] = kc;
        }
    }
    // Rest: fatigue, the species' rest hours, the digestion pause; only where it is safe.
    // Hurt and fed enough to heal: it lies up (resting heals four times as fast, sim.c).
    float lie_up = health < ECO_LIE_UP && a->food > 0.35f && a->water > 0.35f ? ECO_LIE_UP_URGE * (1 - health / ECO_LIE_UP) : 0;
    U[BOT_REST] = clampf(a->fatigue + (act < ECO_REST_HOURS ? ECO_REST_BONUS : 0) + (a->digest ? 0.6f : 0) + lie_up, 0, 1) * clampf(1 - risk, 0, 1);
    // Back to the herd: the farther, the more (packs keep together by their
    // wandering, pick_goal). The young follow a grown member of their group.
    // A starving animal eats where it stands before it keeps up (the young died following a herd on the move).
    float cx, cz, hungry = clampf(a->food / 0.3f, 0.3f, 1);
    if (!carn && S->group_max > 1 && group_count(s, i, &cx, &cz) > 0) {
        float d = hypotf(cx - a->x, cz - a->z);
        if (d > ECO_GROUP_NEAR + L) U[BOT_GROUP] = clampf(0.3f + 0.5f * d / ECO_HERD_M, 0, 1) * hungry;
    }
    int parent = a->growth < ECO_YOUNG ? group_adult(s, i, 400.0f) : -1;
    if (parent >= 0 && agent_dist(a, &s->agents[parent]) > ECO_FOLLOW_M) { U[BOT_GROUP] = fmaxf(U[BOT_GROUP], ECO_FOLLOW * hungry); T[BOT_GROUP] = parent; }
    // Guard the nest while it has eggs: more with a threat known within 80 m of it.
    int nest = cret_nest_of(s, i);
    if (nest >= 0 && s->nests[nest].eggs > 0 && a->growth >= 0.9f && hypotf(s->nests[nest].x - a->x, s->nests[nest].z - a->z) < 150.0f && U[BOT_GUARD] < ECO_GUARD_NEST) {
        bool near = false;
        for (int k = 0; k < CRET_KNOWN && !near; k++) {
            const CretKnown* m = &a->known[k];
            near = m->who && m->since <= s->tick && CRET_SPECIES_TABLE[s->agents[m->who - 1].species].diet == CRET_DIET_CARNIVORE &&
                   hypotf(m->x - s->nests[nest].x, m->z - s->nests[nest].z) < 80.0f;
        }
        U[BOT_GUARD] = near ? ECO_GUARD_THREAT : ECO_GUARD_NEST; T[BOT_GUARD] = CRET_MAX_CARCASSES + nest;
    }
    // A nest: a fed adult in a group with a mate in condition (fat over
    // ECO_BREED_FAT both), no nest yet, in the morning.
    bool morning = s->time_of_day > 0.25f && s->time_of_day < 0.42f;
    if (a->growth >= 0.95f && a->group && a->food > 0.6f && a->water > 0.5f && a->fat > ECO_BREED_FAT && cret_group_size(s, a->group) >= 2 && nest < 0 &&
        (a->bot_state == BOT_NEST || morning)) {
        bool mate = false;
        for (int j = 0; j < s->cfg.num_agents && !mate; j++) mate = j != i && s->agents[j].alive && s->agents[j].group == a->group && s->agents[j].growth >= 0.95f && s->agents[j].fat > ECO_BREED_FAT;
        if (mate) U[BOT_NEST] = ECO_NEST_URGE * clampf(1 - risk, 0, 1);
    }
    U[BOT_WANDER] = 0.15f * act;
    // A small predator's search is how it finds small prey (the forage below, or critters by a person):
    // hunger drives it like a hunt (unless small prey is in sight: then it hunts that), and an empty gut is not slept on.
    if (carn && !a->digest && cret_mass(a) < ECO_SMALL_PREY_KG && a->food < 0.75f && !(T[BOT_HUNT] == -1 && U[BOT_HUNT] > 0)) U[BOT_WANDER] = fmaxf(U[BOT_WANDER], powf(1 - a->food, 1.5f) * (a->food < 0.25f ? 1.0f : 0.6f) * feed_act);
    if (a->food < 0.15f) U[BOT_REST] *= 0.5f;
    // Choose, with the current activity's commitment. A hunt under way is
    // held more firmly while it still shows a profit: it is given up by the
    // chase going nowhere (bot_hunt), not by a passing better idea.
    int cur = activity_of(a->bot_state);
    U[cur] *= cur == BOT_HUNT && T[BOT_HUNT] == a->bot_target && U[BOT_HUNT] > 0 ? ECO_HUNT_COMMIT : ECO_COMMIT;
    if (a->water < 0.15f && a->hurt_ticks == 0) U[BOT_SEEK_WATER] = fmaxf(U[BOT_SEEK_WATER], 1.3f);   // parched: over anything it is committed to, short of a fight
    int best = ORDER[NORDER - 1];
    for (int k = NORDER - 1; k >= 0; k--) if (U[ORDER[k]] >= U[best]) best = ORDER[k];
    if (best == cur && T[best] == a->bot_target) return;
    if (best == cur && (best == BOT_GRAZE || best == BOT_WANDER || best == BOT_REST || best == BOT_SEEK_WATER || best == BOT_GROUP || best == BOT_NEST)) return;
    bot_start(s, i, best, T[best]);
    if (best == BOT_FLEE || best == BOT_DEFEND) a->bot_seen = T[best] + 1;
    if (best == BOT_HUNT && T[best] >= 0 && cur != BOT_HUNT) pack_join(s, i, T[best]);
    if (best == BOT_FLEE && !carn && p >= 0)   // the alarm: group members within ECO_ALARM_M learn of it, the farther ones later
        for (int j = 0; j < s->cfg.num_agents && a->group; j++) {
            CretAgent* c = &s->agents[j];
            if (j == i || !c->alive || c->group != a->group || c->bot_state == BOT_FLEE || c->bot_state == BOT_DEFEND) continue;
            float d = agent_dist(a, c); if (d <= ECO_ALARM_M) cret_eco_alarm(s, j, p, 1 + (int)(d * ECO_ALARM_DELAY * CRET_TICK_HZ));
        }
}

// ---------------------------------------------------------------- the motor
// Hunting: approach (a trot from far, a walk inside 90 m), stalk (inside
// 60 m: creep while the prey's head is down or it faces away, freeze while it
// looks this way and has noticed the stalker), charge (inside the charge distance, once the prey bolts,
// or when a packmate charges). A charge whose gap stops closing for 3 s, or
// that runs out of stamina, is given up: a decision, not a timer.
static void bot_hunt(CretSim* s, int i, CretInput* in, bool* aimed) {
    CretAgent* a = &s->agents[i]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth), dist;
    if (a->bot_target < 0) {   // small prey (critters.c)
        if (!cret_critter_bot(s, i, in)) a->bot_next = s->tick;
        else in->aim = bot_aim(a, in->aim), *aimed = true;
        return;
    }
    const CretAgent* b = &s->agents[a->bot_target];
    if (!b->alive) {   // killed: eat
        int c = nearest_carcass(s, a, 40.0f);
        if (c >= 0) { bot_start(s, i, BOT_FEED, c); a->kill = c + 1; } else a->bot_next = s->tick;
        return;
    }
    in->aim = bot_aim(a, cret_aim_at(a, b->x, b->z)); *aimed = true;
    float px, pz; strike_point_aim(a, in->aim, &px, &pz);
    float gap = body_dist(b, px, pz);
    bool payback = a->bot_alert > 0 && a->bot_foe == a->bot_target + 1;
    float ax = b->x - a->x, az = b->z - a->z, al = fmaxf(hypotf(ax, az), 1e-3f);
    bool bolting = b->speed > b->walk_speed * 1.3f && (b->vx * ax + b->vz * az) / al > 0;
    bool pack_charge = false;
    for (int j = 0; j < s->cfg.num_agents && a->group && !pack_charge; j++) {
        const CretAgent* c = &s->agents[j];
        pack_charge = j != i && c->alive && c->group == a->group && c->bot_state == BOT_HUNT && c->bot_target == a->bot_target && c->hunt_phase == 2;
    }
    float charge = cret_eco_charge_m(a);
    if (a->hunt_phase < 2 && (gap < charge || payback || (pack_charge && gap < 3 * charge) || (bolting && gap < 2 * charge))) { a->hunt_phase = 2; a->hunt_gap = gap; a->hunt_tick = s->tick; }
    else if (a->hunt_phase == 0 && gap < 60.0f) a->hunt_phase = 1;
    // Flank: packmates on the same prey spread round it, about 60 degrees apart by rank.
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
            float ang = atan2f(a->z - b->z, a->x - b->x), off = (rank - 0.5f * (n - 1)) * 1.1f, r = clampf(gap * 0.6f, 4.0f, 14.0f);
            tx = b->x + cosf(ang + off) * r; tz = b->z + sinf(ang + off) * r;
        }
    }
    in->turn = steer(s, a, tx, tz, &dist);
    if (a->hunt_phase == 2) {
        in->forward = gap > 0.5f ? 1.0f : 0.3f; in->sprint = a->stamina > 0.1f && (gap > 3 || b->speed > a->trot_speed * 0.8f);
        if (gap < a->hunt_gap - 0.5f) { a->hunt_gap = gap; a->hunt_tick = s->tick; }
        // A faster prey that tires first is still being run down: the gap it opens now closes once it
        // drops to a trot, if the hunter's own wind lasts (the arithmetic of cret_eco_hunt_eval).
        {
            const CretSpecies* SB = &CRET_SPECIES_TABLE[b->species];
            float tm = a->stamina / fmaxf(S->stamina_drain, 1e-3f), tq = b->stamina / fmaxf(SB->stamina_drain, 1e-3f);
            float end = gap + (b->sprint_speed - a->sprint_speed) * fminf(tm, tq) - (a->sprint_speed - b->trot_speed) * fmaxf(tm - tq, 0);
            if (tm > tq && end <= 0 && a->stamina >= ECO_GIVE_UP_STAMINA) a->hunt_tick = s->tick;
        }
        if (!payback && (s->tick - a->hunt_tick > ECO_GIVE_UP_S * CRET_TICK_HZ || a->stamina < ECO_GIVE_UP_STAMINA)) {   // given up
            a->lost = a->bot_target + 1; a->lost_until = s->tick + 30 * CRET_TICK_HZ;
            bot_start(s, i, BOT_REST, -1); a->bot_next = s->tick + CRET_TICK_HZ;
            return;
        }
    } else if (a->hunt_phase == 1) {
        float ang = wrapa(atan2f(a->z - b->z, a->x - b->x) - b->yaw);
        bool watched = b->act != CRET_ACT_EAT && b->act != CRET_ACT_DRINK && fabsf(ang) < ECO_CONE && cret_eco_aware(s, a->bot_target, i);
        in->forward = watched ? 0 : 0.25f;
        if (watched) in->turn = 0;
    } else in->forward = gap > 90.0f ? 1.0f : 0.5f;
    if (gap < S->reach * g * 0.9f) in->action = CRET_ACT_ATTACK;
}

// Grazing: walk to the chosen ground, eat with the head coming up now and
// then (vigilance); move on once the intake here falls under 0.85 of what it
// has averaged (the marginal value theorem) or the plants run out.
static void bot_graze(CretSim* s, int i, CretInput* in) {
    CretAgent* a = &s->agents[i]; float L = CRET_SPECIES_TABLE[a->species].length * cret_growth_scale(a->growth) * 0.45f, dist;
    float gx = a->graze_x, gz = a->graze_z, d = hypotf(gx - a->x, gz - a->z);
    if (d > 8.0f + a->radius) { in->turn = steer(s, a, gx, gz, &dist); in->forward = dist > 60 ? 1.0f : 0.5f; return; }
    if ((s->tick + i) % CRET_TICK_HZ == 0) {   // once a second: the intake here against its average over a game hour
        float cm = cret_eco_cmax(&s->cfg, cret_mass(a)), v = cret_eco_intake(cm, cret_graze_at(s, a->x + cosf(a->yaw) * L, a->z + sinf(a->yaw) * L));
        if (a->act == CRET_ACT_EAT) a->intake_avg += (v - a->intake_avg) * 24.0f / s->cfg.day_seconds;
        if (v < 0.85f * a->intake_avg || v < 0.05f * cm) { graze_pick(s, i); if (hypotf(a->graze_x - a->x, a->graze_z - a->z) < 4) a->intake_avg = v; return; }
    }
    if (water_ahead(s, a)) { in->turn = 1; in->forward = 0.3f; return; }   // at the shore eating would drink: turn to the plants
    in->action = bot_vigilant(s, i) ? 0 : CRET_ACT_EAT;
    if (a->food > 0.99f) a->bot_next = s->tick;
}

// Fleeing: away from the threat, bending toward the herd (the selfish herd:
// its members within ECO_HERD_M); a sprint from a threat inside the flight
// distance or the animal that hurt it, a trot or a walk from mere risk.
static void bot_flee(CretSim* s, int i, CretInput* in) {
    CretAgent* a = &s->agents[i]; bool carn = CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_CARNIVORE; float dist;
    float L = CRET_SPECIES_TABLE[a->species].length * cret_growth_scale(a->growth);
    if (a->bot_target < 0 || a->bot_target >= s->cfg.num_agents) { a->bot_next = s->tick; return; }
    const CretAgent* b = &s->agents[a->bot_target];
    float ax = a->x - b->x, az = a->z - b->z, al = fmaxf(sqrtf(ax * ax + az * az), 1e-3f);
    ax /= al; az /= al;
    if (!carn) {
        float hx = 0, hz = 0; int n = 0;
        for (int j = 0; j < s->cfg.num_agents && a->group; j++) {
            const CretAgent* c = &s->agents[j];
            if (j != i && c->alive && c->group == a->group && agent_dist(a, c) < ECO_HERD_M) { hx += c->x; hz += c->z; n++; }
        }
        if (n) { float dx = hx / n - a->x, dz = hz / n - a->z, dl = sqrtf(dx * dx + dz * dz); if (dl > 6) { ax += 0.6f * dx / dl; az += 0.6f * dz / dl; } }
    }
    int parent = !carn && a->growth < ECO_YOUNG ? group_adult(s, i, 60.0f) : -1;   // the young run to a grown member
    if (parent >= 0) { const CretAgent* p = &s->agents[parent]; float dx = p->x - a->x, dz = p->z - a->z, dl = fmaxf(hypotf(dx, dz), 1e-3f); ax += 1.2f * dx / dl; az += 1.2f * dz / dl; }
    in->turn = steer(s, a, a->x + ax * 20, a->z + az * 20, &dist);
    if ((s->tick + i) % 3 == 0) { float fd; a->flee_run = cret_eco_flight(s, i, &fd) >= 0; }   // 10 Hz: something coming inside the flight distance
    bool coming = a->flee_run || a->st[CRET_ST_FEAR] > 0 || (a->bot_alert > 0 && a->bot_foe == a->bot_target + 1 && al < 45.0f + L);
    in->forward = coming || al < 25.0f + L ? 1.0f : 0.6f;
    in->sprint = coming && a->stamina > 0.15f;
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
    // Fatigue: a day awake is 1 / ECO_FATIGUE_DAY, rest takes it off three times as fast.
    a->fatigue = clampf(a->fatigue + (a->act == CRET_ACT_REST ? -3.0f : 1.0f) * CRET_DT / (ECO_FATIGUE_DAY * s->cfg.day_seconds), 0, 1);
    // Small prey away from people. Critters are simulated only round a person
    // (critters.c), so a small predator searching elsewhere finds its share by
    // the habitat it walks through: ECO_FORAGE x basal on the best ground,
    // enough to live on and not enough to grow fat (a big kill does that).
    if (carn && a->bot_state == BOT_WANDER && a->food < 0.75f && !a->digest && a->speed > 0.5f && cret_mass(a) < ECO_SMALL_PREY_KG) {
        float near = 1e9f, ds = s->crit_cfg.despawn;
        for (int j = 0; j < s->cfg.num_agents && near > ds; j++) if (s->agents[j].alive && cret_is_person(s, j)) near = fminf(near, agent_dist(a, &s->agents[j]));
        if (near > ds) {
            float hab = fmaxf(cret_critter_habitat(s, CRET_CR_MOUSE, a->x, a->z), cret_critter_habitat(s, CRET_CR_LIZARD, a->x, a->z));
            float d = fminf(ECO_FORAGE * hab * cret_eco_basal(s->cfg.hunger_seconds, cret_mass(a)) * CRET_DT / fmaxf(cret_eco_gut_kg(a), 1e-3f), 1 - a->food);
            a->food += d; a->ev_food += d;
        }
    }
    // Holding on to prey: let go when badly hurt (Space); stunned: nothing. The head stays where it was.
    in.aim = isfinite(a->aim) ? a->aim : 0;
    if (a->latch) { in.jump = health < 0.3f; return in; }
    if (a->st[CRET_ST_STUN] > 0) return in;
    in.aim = 0;
    // Decisions are staggered at 3 Hz, 10 Hz with a known animal close by or
    // while hurt, and at once when an alarm or the motor calls for one
    // (docs/ecosystem.md section 9).
    bool decide = (s->tick + i) % 10 == 0 || (a->bot_next && s->tick >= a->bot_next) || (a->hurt_ticks > 0 && (s->tick + i) % 3 == 0);
    if (!decide && (s->tick + i) % 3 == 0)
        for (int k = 0; k < CRET_KNOWN && !decide; k++) decide = a->known[k].who && hypotf(a->known[k].x - a->x, a->known[k].z - a->z) < 60.0f;
    if (decide) {
        if (a->bot_next && s->tick >= a->bot_next) a->bot_next = 0;
        cret_eco_perceive(s, i);
        bot_choose(s, i, &in);
    }
    // A group member called: come to it (unless busy with something urgent).
    bool calm = a->bot_state == BOT_WANDER || a->bot_state == BOT_GRAZE || a->bot_state == BOT_REST || a->bot_state == BOT_GROUP;
    if (a->rally_ticks > 0 && calm) {
        float dx = a->rally_x - a->x, dz = a->rally_z - a->z;
        if (dx * dx + dz * dz > (12.0f + L) * (12.0f + L)) {
            in.turn = steer(s, a, a->rally_x, a->rally_z, &dist); in.forward = dist > 40 ? 1.0f : 0.5f;
            in.aim = bot_aim(a, 0);
            return in;
        }
    }
    bool aimed = false;   // defending and hunting aim at the target; otherwise the head comes back to centre
    switch (a->bot_state) {
    case BOT_FLEE: bot_flee(s, i, &in); break;
    case BOT_DEFEND: {
        // A tail to the predator (stegosaur), or face it (headbutt, bash); strike in reach.
        if (a->bot_target < 0) { a->bot_next = s->tick; break; }
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
        if (!b->alive || agent_dist(a, b) > 25.0f + L) a->bot_next = s->tick;
        break;
    }
    case BOT_HUNT: bot_hunt(s, i, &in, &aimed); break;
    case BOT_FEED: {
        CretCarcass* k = &s->carcasses[a->bot_target < 0 ? 0 : a->bot_target];
        if (a->bot_target < 0 || !k->active || k->meat <= 0 || k->carrier || a->food > 0.97f) { a->bot_next = s->tick; break; }
        float hx = a->x + cosf(a->yaw) * L * 0.45f, hz = a->z + sinf(a->yaw) * L * 0.45f;
        if (carcass_near(s, hx, hz, 1.0f + S->reach * g) == a->bot_target) { in.action = CRET_ACT_EAT; a->kill = a->bot_target + 1; }
        else { in.turn = steer(s, a, k->x, k->z, &dist); in.forward = dist > 12 ? 1.0f : 0.5f; }
        break;
    }
    case BOT_SEEK_WATER: {
        if (water_ahead(s, a)) { a->bot_state = BOT_DRINK; in.action = CRET_ACT_DRINK; break; }
        if (a->water_tick <= 0) {   // none found nearby: the leg toward the map's nearest water, then search again
            if (a->goal_x == 0 && a->goal_z == 0) { a->bot_next = s->tick + CRET_TICK_HZ; pick_goal(s, a, 60, 150); a->bot_state = BOT_WANDER; a->bot_timer = 900; break; }
            in.turn = steer(s, a, a->goal_x, a->goal_z, &dist); in.forward = 1.0f;
            if (dist < 30 || s->tick + a->water_tick > 60 * CRET_TICK_HZ) bot_start(s, i, BOT_SEEK_WATER, -1);
            break;
        }
        in.turn = steer(s, a, a->goal_x, a->goal_z, &dist); in.forward = dist > 20 ? 1.0f : 0.5f;
        if (dist < 8 + a->radius) {   // at the shore: turn to the water and step in
            float dx = a->water_x - a->x, dz = a->water_z - a->z, d = wrapa(atan2f(dz, dx) - a->yaw);
            in.turn = clampf(d * 2, -1, 1); in.forward = fabsf(d) < 0.4f ? 0.4f : 0.0f;
            if (++a->bot_timer > 15 * CRET_TICK_HZ) { a->water_tick = 0; a->bot_next = s->tick; }   // cannot get at it: forget it
        }
        break;
    }
    case BOT_DRINK:
        in.action = CRET_ACT_DRINK;
        if ((a->water > 0.97f && !at_drink_quest(s, i)) || !water_ahead(s, a)) { a->bot_state = BOT_SEEK_WATER; a->bot_next = s->tick; }
        else { a->water_x = a->x + cosf(a->yaw) * 6; a->water_z = a->z + sinf(a->yaw) * 6; a->water_tick = s->tick; }
        break;
    case BOT_GRAZE: bot_graze(s, i, &in); break;
    case BOT_REST:
        // Midday heat: a big animal (over ECO_SHADE_KG) in the open walks to shade first (bot_start found it).
        if (a->goal_x != 0 && hypotf(a->goal_x - a->x, a->goal_z - a->z) > 3 + a->radius && a->bot_timer > 0) {
            in.turn = steer(s, a, a->goal_x, a->goal_z, &dist); in.forward = 0.5f; break;
        }
        in.action = CRET_ACT_REST;
        if ((s->tick + i) % 600 == 60 && frand(&s->rng) < 0.5f) in.action = CRET_ACT_CALL;
        break;
    case BOT_GUARD: {   // by the kill or the nest (target CRET_MAX_CARCASSES + nest): lie close to it, come back when it strays
        int t = a->bot_target; float gx, gz;
        if (t >= CRET_MAX_CARCASSES && t < CRET_MAX_CARCASSES + CRET_MAX_NESTS && s->nests[t - CRET_MAX_CARCASSES].active) { gx = s->nests[t - CRET_MAX_CARCASSES].x; gz = s->nests[t - CRET_MAX_CARCASSES].z; }
        else if (t >= 0 && t < CRET_MAX_CARCASSES && s->carcasses[t].active) { gx = s->carcasses[t].x; gz = s->carcasses[t].z; }
        else { a->bot_next = s->tick; break; }
        if (hypotf(gx - a->x, gz - a->z) > 12.0f + L) { in.turn = steer(s, a, gx, gz, &dist); in.forward = 0.5f; }
        else if (!carn && water_ahead(s, a) && cret_food_state(s, i, NULL) != 1) { in.turn = 1; in.forward = 0.3f; }   // facing bare water: turn to the plants (eating here would only drink)
        else in.action = carn ? CRET_ACT_REST : bot_vigilant(s, i) ? 0 : CRET_ACT_EAT;   // a herbivore grazes by its nest, looking up
        break;
    }
    case BOT_GROUP: {   // back to the herd, or after the parent (target: a grown member)
        float cx, cz; int t = a->bot_target;
        if (t >= 0 && t < s->cfg.num_agents && s->agents[t].alive) { cx = s->agents[t].x; cz = s->agents[t].z; }
        else if (!group_count(s, i, &cx, &cz)) { a->bot_next = s->tick; break; }
        in.turn = steer(s, a, cx, cz, &dist); in.forward = dist > 60 ? 1.0f : 0.5f;
        if (dist < (t >= 0 ? 0.5f * ECO_FOLLOW_M : 0.5f * ECO_GROUP_NEAR) + L) { in.forward = 0; a->bot_next = s->tick; }
        break;
    }
    case BOT_NEST:   // hold still and build (progress.c); done once the nest stands
        in.action = CRET_ACT_NEST;
        if (cret_nest_of(s, i) >= 0 || a->bot_timer == 0) { in.action = 0; bot_start(s, i, BOT_REST, -1); a->bot_next = s->tick; }
        break;
    case BOT_CARRY: {   // pick the food up, carry it to the nest, put it down there and stay a while
        int n = provide_nest(s, i);
        if (n < 0 || a->bot_timer == 0) { a->bot_next = s->tick; break; }
        const CretNest* N = &s->nests[n];
        if (a->carry) {
            float mx, my, mz; cret_mouth_point(a, &mx, &my, &mz);
            if (hypotf(mx - N->x, mz - N->z) < cret_nest_radius(N->species) + 1.0f) { in.carry = 1; bot_start(s, i, BOT_REST, -1); break; }
            in.turn = steer(s, a, N->x, N->z, &dist); in.forward = dist > 12 ? 1.0f : 0.5f;
            break;
        }
        const CretCarcass* k = &s->carcasses[a->bot_target < 0 ? 0 : a->bot_target];
        if (a->bot_target < 0 || !k->active || k->carrier || k->meat <= 0) { a->bot_next = s->tick; break; }
        int what, st = cret_carry_state(s, i, &what);
        if (st == 2 && what == a->bot_target) in.carry = 1;
        else if (cret_item_kg(k) > cret_carry_limit(a) && cret_carry_tear(s, i, a->bot_target, cret_carry_limit(a)) >= 0) {}   // a piece of the body in the mouth
        else if (cret_item_kg(k) > cret_carry_limit(a) && k->item != CRET_ITEM_BODY) { a->bot_next = s->tick; break; }
        else { in.turn = steer(s, a, k->x, k->z, &dist); in.forward = dist > 12 ? 1.0f : 0.5f; }
        break;
    }
    case BOT_RAID: {   // walk up to the nest and eat the eggs
        const CretNest* n = &s->nests[a->bot_target < 0 ? 0 : a->bot_target % CRET_MAX_NESTS];
        if (a->bot_target < 0 || !n->active || n->eggs <= 0 || a->food > 0.95f) { a->bot_next = s->tick; break; }
        float hx = a->x + cosf(a->yaw) * L * 0.45f, hz = a->z + sinf(a->yaw) * L * 0.45f;
        float dx = n->x - hx, dz = n->z - hz;
        if (dx * dx + dz * dz < (2.0f + a->radius) * (2.0f + a->radius)) in.action = CRET_ACT_EAT;
        else { in.turn = steer(s, a, n->x, n->z, &dist); in.forward = dist > 12 ? 1.0f : 0.4f; }
        break;
    }
    default: {   // explore: walk to the goal (sometimes a trot; a trot on a quest), then on to the next
        float qx, qz; bool questing = a->food > 0.6f && a->water > 0.5f && quest_goal(s, i, &qx, &qz);
        in.turn = steer(s, a, a->goal_x, a->goal_z, &dist);
        in.forward = questing || (i + a->ticks_alive / 600) % 4 == 0 ? 1.0f : 0.5f;
        if (fabsf(in.turn) > 0.8f) in.forward *= 0.6f;
        if (a->growth >= 0.9f && a->group) {   // a parent leads its young at their pace
            for (int j = 0; j < s->cfg.num_agents; j++)
                if (j != i && s->agents[j].alive && s->agents[j].group == a->group && s->agents[j].growth < ECO_YOUNG && agent_dist(a, &s->agents[j]) < 60.0f) { in.forward = fminf(in.forward, 0.5f); break; }
        }
        if (dist < 4 + a->radius || a->bot_timer == 0 || (a->goal_x == 0 && a->goal_z == 0)) { pick_goal(s, a, 30, carn ? 160 : 70); a->bot_timer = 900; }
        if (water_ahead(s, a) && at_drink_quest(s, i)) { a->bot_state = BOT_DRINK; in.action = CRET_ACT_DRINK; }
    }
    }
    if (!aimed) in.aim = bot_aim(a, 0);
    // Raptors on its back: buck and twist to throw them off.
    if (a->latchers > 0) { in.turn = (a->ticks_alive / 12) % 2 ? 1.0f : -1.0f; in.forward = 1; in.sprint = a->stamina > 0.1f; }
    bot_attack2(s, i, &in);
    bot_abilities(s, i, &in);
    if (a->carry && a->bot_state != BOT_CARRY) in.carry = 1;   // anything else to do (a hunt, a flight): put the load down
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
