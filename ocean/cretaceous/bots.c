// Scripted bots (cret_bot_input), included by sim.c after the rest of the
// sim so they use its static helpers the way critters.c and scent.c do.

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
//   - a fed predator parent with young at its nest sometimes carries a small
//     kill (one it can lift) home and puts it down there (BOT_CARRY).
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
enum { BOT_WANDER, BOT_GRAZE, BOT_FLEE, BOT_HUNT, BOT_REST, BOT_DRINK, BOT_SEEK_WATER, BOT_FEED, BOT_DEFEND, BOT_NEST, BOT_RAID, BOT_CARRY };

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
// Carcass c is worth carrying home for agent i: on the ground, liftable, not
// already at the nest, and picked for about half of the (slot, carcass)
// pairs, so the choice is deterministic and draws nothing from the rng.
static bool worth_carrying(const CretSim* s, int i, int c) {
    const CretCarcass* k = &s->carcasses[c];
    return k->active && !k->carrier && k->meat > k->max_meat * 0.1f && cret_item_kg(k) <= cret_carry_limit(&s->agents[i]) &&
           !at_own_nest(s, i, k) && ((i + c) & 1) == 0;
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
        if (a->bot_state != BOT_HUNT && a->bot_state != BOT_FEED && a->bot_state != BOT_FLEE && a->bot_state != BOT_RAID && a->bot_state != BOT_CARRY && a->food < 0.75f) {
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
                a->bot_state == BOT_SEEK_WATER || a->bot_state == BOT_RAID || a->bot_state == BOT_NEST || a->bot_state == BOT_CARRY;
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
    // A fed predator parent looks once a second for small food within 60 m to carry home.
    if (calm && carn && a->food > 0.8f && (s->tick + i) % CRET_TICK_HZ == 0 && provide_nest(s, i) >= 0) {
        int c = nearest_carcass(s, a, 60.0f);
        if (c >= 0 && worth_carrying(s, i, c)) { bot_set(a, BOT_CARRY, 120 * CRET_TICK_HZ); a->bot_target = c; calm = false; }
    }
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
            if (a->food > 0.97f && worth_carrying(s, i, a->bot_target) && provide_nest(s, i) >= 0) { bot_set(a, BOT_CARRY, 120 * CRET_TICK_HZ); break; }   // the rest goes home
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
    case BOT_CARRY: {   // pick the food up, carry it to the nest, put it down there and stay a while
        int n = provide_nest(s, i);
        if (n < 0 || a->bot_timer == 0) { bot_set(a, BOT_REST, 300); break; }
        const CretNest* N = &s->nests[n];
        if (a->carry) {
            float mx, my, mz; cret_mouth_point(a, &mx, &my, &mz);
            if (hypotf(mx - N->x, mz - N->z) < cret_nest_radius(N->species) + 1.0f) { in.carry = 1; bot_set(a, BOT_REST, 600); break; }
            in.turn = steer(s, a, N->x, N->z, &dist); in.forward = dist > 12 ? 1.0f : 0.5f;
            break;
        }
        const CretCarcass* k = &s->carcasses[a->bot_target];
        if (!k->active || k->carrier || k->meat <= 0 || cret_item_kg(k) > cret_carry_limit(a)) { bot_set(a, BOT_REST, 300); break; }
        int what; if (cret_carry_state(s, i, &what) == 2 && what == a->bot_target) in.carry = 1;
        else { in.turn = steer(s, a, k->x, k->z, &dist); in.forward = dist > 12 ? 1.0f : 0.5f; }
        break;
    }
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
