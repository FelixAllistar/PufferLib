// v23 area of interest (net_priv.h, docs/anticheat.md): which entities each
// client is told about. Included by net.c. It reads the server's sim and
// writes only the net server's own state, so the sim, its determinism and
// training are untouched.
//
// The rule is a superset of what the sim's own senses use (eco.c: sight,
// hearing, calls; scent.c: smell), widened for a person at a screen: the
// renderer draws animals out to 900 m through the haze, the camera orbits
// up to 2.5 body lengths from the animal and above it, and what is drawn
// lags the server by the round trip and the interpolation delay. What the
// rule hides is what an unmodified client could not show: animals beyond
// sight for their size, light and weather, behind a ridge from wherever
// the camera could be, and not heard, smelled or kept for gameplay.
#include "progress.h"

// The highest the camera can look from (camera.h: the pivot over the
// animal, the boom at its longest, looking down as far as it goes).
static float aoi_eye_y(const CretAgent* a) {
    float L = CRET_SPECIES_TABLE[a->species % CRET_SPECIES].length * cret_growth_scale(a->growth);
    float boom = fmaxf(1.5f * L * 2.5f, 1.5f);   // CAM_BOOM_LENGTHS at CAM_ZOOM_MAX
    return a->y + a->height * 1.05f + boom * 0.53f;   // CAM_PIVOT_UP; sin(-CAM_PITCH_MIN)
}

// Whether the terrain leaves a line from the eye to the target's top clear,
// allowing it to rise `allow` m over the line. Samples every 25 m or so
// (small bumps between them are missed, which only lets more through).
static bool aoi_clear(const CretWorld* w, float x0, float y0, float z0, float x1, float y1, float z1, float allow) {
    if (!w) return true;
    float d = sqrtf((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0));
    int n = (int)(d / 25.0f); n = n < 4 ? 4 : n > 28 ? 28 : n;
    for (int k = 1; k < n; k++) {
        float f = (float)k / (float)n, x = x0 + (x1 - x0) * f, z = z0 + (z1 - z0) * f;
        if (cret_height(w, x, z) > y0 + (y1 - y0) * f + allow) return false;
    }
    return true;
}

// Sight range scale for viewer a: its species at night, the haze of
// overcast and rain (the renderer's haze thickens by 3.5 times, the same
// smoothstep of the cloud cover the wind uses), and a head wound.
static float aoi_sight_scale(const CretSim* s, const CretAgent* a) {
    float k = cret_eco_night(s) ? (cret_eco_diurnal(a->species) ? NET_AOI_NIGHT_DIURNAL : NET_AOI_NIGHT_NOCTURNAL) : 1.0f;
    float oc = clampf((s->cloud_cover - 0.68f) / (0.92f - 0.68f), 0, 1); oc = oc * oc * (3 - 2 * oc);
    return k * (1 - NET_AOI_HAZE * oc) * cret_wound_sense(a);
}

// Agent j dealt or took a blow in the last NET_AOI_FIGHT_S: a fight is heard.
static bool aoi_fighting(const CretSim* s, int j) {
    uint32_t k0 = s->hit_count > CRET_HIT_LOG ? s->hit_count - CRET_HIT_LOG : 0;
    for (uint32_t k = s->hit_count; k-- > k0;) {
        const CretHit* h = &s->hits[k % CRET_HIT_LOG];
        if (s->tick - h->tick > (int)(NET_AOI_FIGHT_S * CRET_TICK_HZ)) break;
        if (h->attacker == j || h->victim == j) return true;
    }
    return false;
}

bool netp_aoi_judge(const CretSim* s, int i, int e, bool keep, bool* seen, bool* far) {
    const CretAgent* a = &s->agents[i]; *seen = false; *far = false;
    float x, z, top, len, speed, hear = 0, smell = 0;
    float R[3] = {0, 0, 0}; bool carn = CRET_SPECIES_TABLE[a->species % CRET_SPECIES].diet == CRET_DIET_CARNIVORE;
    if (carn) cret_scent_ranges(s, i, true, R);
    if (e < NET_CARCASS0) {
        const CretAgent* b = &s->agents[e];
        x = b->x; z = b->z; top = b->y + b->height; speed = b->alive ? b->speed : 0;
        len = CRET_SPECIES_TABLE[b->species % CRET_SPECIES].length * cret_growth_scale(b->growth);
        if (b->alive) {
            hear = cret_eco_hear_m(s, b->speed, cret_mass(b)) * NET_AOI_HEAR;
            if (b->act == CRET_ACT_CALL) hear = fmaxf(hear, ECO_CALL_M * NET_AOI_HEAR);
            if (aoi_fighting(s, e)) hear = fmaxf(hear, NET_HIT_RANGE);
            hear *= cret_wound_sense(a);
            smell = R[CRET_SCENT_ANIMAL];
        }
    } else {
        const CretCarcass* k = &s->carcasses[e - NET_CARCASS0];
        x = k->x; z = k->z; top = k->y + 0.5f; speed = 0;
        len = CRET_SPECIES_TABLE[k->species % CRET_SPECIES].length * cret_growth_scale(k->growth) * (k->item == CRET_ITEM_BODY ? 1.0f : 0.2f);
        smell = R[CRET_SCENT_CARCASS] * 1.5f;   // a big body smells further (scent.c, carcass_size: at most 1.5)
    }
    float dx = x - a->x, dz = z - a->z, d = sqrtf(dx * dx + dz * dz);
    float lead = (a->speed + speed) * NET_AOI_LEAD_S, grow = keep ? NET_AOI_KEEP : 1.0f, add = keep ? NET_AOI_KEEP_M : 0.0f;
    float sight = fminf(NET_AOI_SIGHT_MIN + NET_AOI_SIGHT_PER_L * len, NET_AOI_SIGHT_MAX) * aoi_sight_scale(s, a);
    float eye = aoi_eye_y(a);
    if (d <= sight && (d <= 20 || aoi_clear(s->world, a->x, eye, a->z, x, top, z, 0))) *seen = true;
    float nose = smell > 0 ? smell * cret_scent_lobe(s, -dx, -dz) : 0;   // carried downwind to the viewer
    if (d <= fmaxf(fmaxf(NET_AOI_NEAR, hear), nose) * grow + lead + add) return true;
    // Twice past every range: it went somewhere (a respawn, a teleport), not across the edge.
    *far = d > 2 * (fmaxf(fmaxf(NET_AOI_NEAR, hear), fmaxf(nose, sight)) * grow + lead + add);
    if (d > sight * grow + lead + add) return false;
    return *seen || aoi_clear(s->world, a->x, eye, a->z, x, top + 1.0f, z, keep ? NET_AOI_LOS_KEEP : NET_AOI_LOS_ENTER);
}

// What gameplay shows whatever the senses say: the viewer's group (compass,
// map, nameplates), the animals marked for it (the map's red dots, hud.c's
// marked_for), its quest companion, what it latches onto and what latches
// onto it.
static bool aoi_always(const CretSim* s, int i, int j, int companion) {
    const CretAgent* a = &s->agents[i]; const CretAgent* b = &s->agents[j];
    if (j == i || j == companion) return true;
    if (a->group && b->group == a->group) return true;
    if (b->st[CRET_ST_MARKED] > 0 && b->mark_by == (a->group ? a->group : 100 + i + 1)) return true;
    return a->latch == j + 1 || b->latch == i + 1;
}

void netp_aoi_reset(CretNet* n, int i) {
    if (i < 0 || i >= CRET_MAX_AGENTS) return;
    NetAoi* A = &n->aoi[i]; memset(A->in, 0, sizeof A->in); memset(A->seen, 0, sizeof A->seen);
    for (int e = 0; e < NET_NENT; e++) A->out_tick[e] = -1;
    A->tick = -1;
}

void netp_aoi_update(CretNet* n, const CretSim* s, int i) {
    if (i < 0 || i >= s->cfg.num_agents) return;
    NetAoi* A = &n->aoi[i];
    if (A->tick == s->tick) return;
    A->tick = s->tick;
    if (!n->aoi_on && !n->tl) {   // the rule off and nothing to log: everything is relevant
        memset(A->in, 1, sizeof A->in); return;
    }
    int na = s->cfg.num_agents, companion = cret_quest_companion(s, i), linger = (int)(NET_AOI_LINGER * CRET_TICK_HZ);
    for (int e = 0; e < NET_NENT; e++) {
        if (e >= na && e < NET_CARCASS0) continue;
        bool seen, now, far = false;
        if (e >= NET_CARCASS0 && s->carcasses[e - NET_CARCASS0].carrier) {   // carried food goes with its carrier (agents come first)
            int c = s->carcasses[e - NET_CARCASS0].carrier - 1; seen = c < na && A->seen[c]; now = c < na && A->in[c];
        } else {
            now = netp_aoi_judge(s, i, e, A->in[e], &seen, &far);
            if (e < NET_CARCASS0 && aoi_always(s, i, e, companion)) now = true;
        }
        A->seen[e] = seen;
        if (!n->aoi_on) now = true;
        if (now) { A->in[e] = 1; A->out_tick[e] = -1; continue; }
        if (!A->in[e]) continue;
        if (A->out_tick[e] < 0) A->out_tick[e] = s->tick;
        if (far || s->tick - A->out_tick[e] >= linger) { A->in[e] = 0; A->out_tick[e] = -1; }
    }
}

void cret_net_set_aoi(CretNet* n, bool on) { if (n) n->aoi_on = on; }
