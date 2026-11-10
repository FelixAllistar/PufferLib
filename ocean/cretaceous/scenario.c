// Scripted test scenarios (scenario.h).
#include "scenario.h"
#include "abilities.h"
#include <ctype.h>
#include <dirent.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* const CRET_SCN_CTRL_NAMES[SCN_CTRL_KINDS] = {"idle", "bot", "walk", "attack", "circle", "flee", "swipe", "hitrun"};
const char* const CRET_SCN_CAM_NAMES[SCN_CAM_KINDS] = {"follow", "side", "orbit", "high"};

static inline float sc_clamp(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float sc_wrap(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }

// ---------------------------------------------------------------- parsing
static void actor_defaults(CretScnActor* a, const char* name) {
    memset(a, 0, sizeof *a); snprintf(a->name, sizeof a->name, "%s", name);
    a->species = CRET_SP_RAPTOR; a->count = 1; a->behaviour = CRET_ACTOR_BOT; a->growth = 1; a->facing = NAN; a->group = true;
    a->ctrl = SCN_CTRL_IDLE; a->spread = 8;
}
static char* trim(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    char* e = s + strlen(s); while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}
static bool on_off(const char* v, bool* out) {
    if (!strcmp(v, "on") || !strcmp(v, "yes") || !strcmp(v, "1") || !strcmp(v, "true")) { *out = true; return true; }
    if (!strcmp(v, "off") || !strcmp(v, "no") || !strcmp(v, "0") || !strcmp(v, "false")) { *out = false; return true; }
    return false;
}
static bool parse_num(const char* v, float* out) { char* e; float f = strtof(v, &e); if (e == v || *trim(e) || !isfinite(f)) return false; *out = f; return true; }

// "attack stego", "walk 10 -4", "circle pack 9", "hitrun stego 3"
static bool parse_ctrl(const char* v, CretScnActor* a, char* err, int nerr, int line) {
    char w[4][24] = {{0}}; int n = sscanf(v, "%23s %23s %23s %23s", w[0], w[1], w[2], w[3]);
    int c = -1; for (int k = 0; k < SCN_CTRL_KINDS; k++) if (n >= 1 && !strcmp(w[0], CRET_SCN_CTRL_NAMES[k])) c = k;
    if (c < 0) { snprintf(err, nerr, "line %d: unknown controller '%s' (idle, bot, walk, attack, circle, flee, swipe, hitrun)", line, n >= 1 ? w[0] : ""); return false; }
    a->ctrl = c; a->ctrl_target[0] = 0; a->p0 = a->p1 = 0;
    if (c == SCN_CTRL_WALK) {
        if (n < 3 || !parse_num(w[1], &a->p0) || !parse_num(w[2], &a->p1)) { snprintf(err, nerr, "line %d: walk X Z [run]", line); return false; }
        a->run = n >= 4 && !strcmp(w[3], "run");
    } else if (c >= SCN_CTRL_ATTACK) {
        if (n < 2) { snprintf(err, nerr, "line %d: %s needs a target name", line, w[0]); return false; }
        snprintf(a->ctrl_target, sizeof a->ctrl_target, "%s", w[1]);
        a->p0 = c == SCN_CTRL_CIRCLE ? 8.0f : c == SCN_CTRL_HITRUN ? 3.0f : 0;
        if (n >= 3 && !parse_num(w[2], &a->p0)) { snprintf(err, nerr, "line %d: bad number '%s'", line, w[2]); return false; }
        if (c == SCN_CTRL_CIRCLE && (a->p0 < 2 || a->p0 > 60)) { snprintf(err, nerr, "line %d: circle radius 2..60", line); return false; }
        if (c == SCN_CTRL_HITRUN && (a->p0 < 0.5f || a->p0 > 30)) { snprintf(err, nerr, "line %d: hitrun away seconds 0.5..30", line); return false; }
    } else if (n > 1) { snprintf(err, nerr, "line %d: %s takes no arguments", line, w[0]); return false; }
    return true;
}

static int find_actor(const CretScenario* sc, const char* name) {
    if (!strcmp(name, "player")) return 0;
    for (int k = 0; k < sc->nactor; k++) if (!strcmp(sc->actor[k].name, name)) return k + 1;
    return -1;
}

bool cret_scenario_parse(const char* text, const char* name, CretScenario* sc, char* err, int nerr) {
    char dummy[8]; if (!err) { err = dummy; nerr = sizeof dummy; }
    memset(sc, 0, sizeof *sc); err[0] = 0;
    snprintf(sc->name, sizeof sc->name, "%s", name ? name : "scenario"); snprintf(sc->title, sizeof sc->title, "%s", sc->name);
    sc->seconds = 30; sc->hour = 11; sc->weather = 0; sc->open = true; sc->camera = SCN_CAM_SIDE; sc->end = SCN_END_TIME;
    actor_defaults(&sc->player, "player"); sc->player.ctrl = SCN_CTRL_IDLE; sc->player.behaviour = CRET_ACTOR_SCRIPT;
    sc->player.x = -25; sc->player.facing = 0;
    enum { SEC_NONE, SEC_SCN, SEC_PLAYER, SEC_ACTOR } sec = SEC_NONE; CretScnActor* cur = NULL;
    int line = 0; const char* p = text ? text : "";
    while (*p) {
        const char* e = strchr(p, '\n'); size_t L = e ? (size_t)(e - p) : strlen(p); line++;
        char buf[256]; if (L >= sizeof buf) { snprintf(err, nerr, "line %d: too long", line); return false; }
        memcpy(buf, p, L); buf[L] = 0; p += L + (e ? 1 : 0);
        for (char* c = buf; *c; c++) if (*c == '#' || *c == ';') { *c = 0; break; }
        char* s = trim(buf); if (!*s) continue;
        for (char* c = s; *c; c++) *c = (char)tolower((unsigned char)*c);
        if (*s == '[') {
            char* close = strchr(s, ']'); if (!close || close[1]) { snprintf(err, nerr, "line %d: bad section", line); return false; }
            *close = 0; char* inner = trim(s + 1);
            if (!strcmp(inner, "scenario")) sec = SEC_SCN;
            else if (!strcmp(inner, "player")) { sec = SEC_PLAYER; cur = &sc->player; sc->has_player = true; }
            else if (!strncmp(inner, "actor", 5) && (!inner[5] || inner[5] == ' ' || inner[5] == '\t')) {
                char* nm = trim(inner + 5);
                if (!*nm || strlen(nm) >= sizeof cur->name || strchr(nm, ' ')) { snprintf(err, nerr, "line %d: an actor needs a one-word name", line); return false; }
                if (!strcmp(nm, "player") || find_actor(sc, nm) > 0) { snprintf(err, nerr, "line %d: actor '%s' defined twice", line, nm); return false; }
                if (sc->nactor == CRET_SCN_MAX_ACTORS) { snprintf(err, nerr, "line %d: at most %d actors", line, CRET_SCN_MAX_ACTORS); return false; }
                cur = &sc->actor[sc->nactor++]; actor_defaults(cur, nm); sec = SEC_ACTOR;
            } else { snprintf(err, nerr, "line %d: unknown section [%s]", line, inner); return false; }
            continue;
        }
        char* eq = strchr(s, '='); if (!eq) { snprintf(err, nerr, "line %d: expected key = value", line); return false; }
        *eq = 0; char* k = trim(s); char* v = trim(eq + 1);
        if (sec == SEC_NONE) { snprintf(err, nerr, "line %d: '%s' outside a section", line, k); return false; }
        if (sec == SEC_SCN) {
            if (!strcmp(k, "title")) {   // keep the case of the title as written
                const char* raw = strstr(text, "title"); (void)raw; snprintf(sc->title, sizeof sc->title, "%s", v);
            }
            else if (!strcmp(k, "seconds")) { if (!parse_num(v, &sc->seconds) || sc->seconds < 1 || sc->seconds > 3600) { snprintf(err, nerr, "line %d: seconds 1..3600", line); return false; } }
            else if (!strcmp(k, "hour")) { if (!parse_num(v, &sc->hour) || sc->hour < 0 || sc->hour > 24) { snprintf(err, nerr, "line %d: hour 0..24", line); return false; } }
            else if (!strcmp(k, "weather")) {
                sc->weather = !strcmp(v, "clear") ? 0 : !strcmp(v, "overcast") ? 1 : !strcmp(v, "rain") ? 2 : -2;
                if (sc->weather == -2) { snprintf(err, nerr, "line %d: weather clear, overcast or rain", line); return false; }
            } else if (!strcmp(k, "place")) {
                if (!strcmp(v, "open")) sc->open = true;
                else if (sscanf(v, "%f %f", &sc->px, &sc->pz) == 2 && isfinite(sc->px) && isfinite(sc->pz)) sc->open = false;
                else { snprintf(err, nerr, "line %d: place open, or X Z", line); return false; }
            } else if (!strcmp(k, "camera")) {
                sc->camera = -1; for (int c = 0; c < SCN_CAM_KINDS; c++) if (!strcmp(v, CRET_SCN_CAM_NAMES[c])) sc->camera = c;
                if (sc->camera < 0) { snprintf(err, nerr, "line %d: camera follow, side, orbit or high", line); return false; }
            } else if (!strcmp(k, "end")) {
                char w0[16] = {0}, w1[24] = {0}; int n = sscanf(v, "%15s %23s", w0, w1);
                if (n >= 1 && !strcmp(w0, "time")) sc->end = SCN_END_TIME;
                else if (n == 2 && (!strcmp(w0, "death") || !strcmp(w0, "wipe"))) { sc->end = !strcmp(w0, "death") ? SCN_END_DEATH : SCN_END_WIPE; snprintf(sc->end_target, sizeof sc->end_target, "%s", w1); }
                else { snprintf(err, nerr, "line %d: end time | death NAME | wipe NAME", line); return false; }
            } else if (!strcmp(k, "you")) {
                if (!on_off(v, &sc->you)) { snprintf(err, nerr, "line %d: you on or off", line); return false; }
            } else if (!strcmp(k, "prop")) {
                char kw[16] = {0}; float x, z, kg = -1, yaw = 0; int n = sscanf(v, "%15s %f %f %f %f", kw, &x, &z, &kg, &yaw);
                int kind = !strcmp(kw, "boulder") ? PROP_BOULDER : !strcmp(kw, "stone") ? PROP_STONE : !strcmp(kw, "log") ? PROP_LOG : -1;
                if (n < 3 || kind < 0 || !isfinite(x) || !isfinite(z) || sc->nprop == 8) { snprintf(err, nerr, "line %d: prop boulder|stone|log X Z [KG] [YAW] (at most 8)", line); return false; }
                if (kg <= 0) kg = kind == PROP_LOG ? 150.0f : kind == PROP_STONE ? 60.0f : 2000.0f;
                sc->prop[sc->nprop].kind = kind; sc->prop[sc->nprop].x = x; sc->prop[sc->nprop].z = z; sc->prop[sc->nprop].kg = kg; sc->prop[sc->nprop].yaw = yaw; sc->nprop++;
            } else { snprintf(err, nerr, "line %d: unknown key '%s' in [scenario]", line, k); return false; }
            continue;
        }
        CretScnActor* a = cur;
        if (!strcmp(k, "species")) { a->species = cret_species_by_name(v); if (a->species < 0) { snprintf(err, nerr, "line %d: unknown species '%s'", line, v); return false; } }
        else if (!strcmp(k, "count")) { float f; if (sec == SEC_PLAYER || !parse_num(v, &f) || f < 1 || f > CRET_SCN_MAX_COUNT || f != floorf(f)) { snprintf(err, nerr, "line %d: count 1..%d (actors only)", line, CRET_SCN_MAX_COUNT); return false; } a->count = (int)f; }
        else if (!strcmp(k, "at")) { if (sscanf(v, "%f %f", &a->x, &a->z) != 2 || !isfinite(a->x) || !isfinite(a->z) || fabsf(a->x) > 2000 || fabsf(a->z) > 2000) { snprintf(err, nerr, "line %d: at X Z (within 2 km)", line); return false; } }
        else if (!strcmp(k, "spread")) { if (!parse_num(v, &a->spread) || a->spread < 0 || a->spread > 200) { snprintf(err, nerr, "line %d: spread 0..200", line); return false; } }
        else if (!strcmp(k, "growth")) { if (!parse_num(v, &a->growth) || a->growth < 0.35f || a->growth > 1) { snprintf(err, nerr, "line %d: growth 0.35..1", line); return false; } }
        else if (!strcmp(k, "facing")) { float f; if (parse_num(v, &f)) { a->facing = f; a->face[0] = 0; } else { a->facing = NAN; snprintf(a->face, sizeof a->face, "%s", v); } }
        else if (!strcmp(k, "behaviour") || !strcmp(k, "behavior")) {
            int b = -1; for (int q = 1; q < CRET_ACTOR_KINDS; q++) if (!strcmp(v, CRET_ACTOR_NAMES[q])) b = q;
            if (b < 0) { snprintf(err, nerr, "line %d: behaviour bot, dummy, passive, hostile or script", line); return false; }
            a->behaviour = b;
        }
        else if (!strcmp(k, "target")) snprintf(a->target, sizeof a->target, "%s", v);
        else if (!strcmp(k, "control")) { if (!parse_ctrl(v, a, err, nerr, line)) return false; if (sec == SEC_ACTOR) a->behaviour = CRET_ACTOR_SCRIPT; }
        else if (!strcmp(k, "abilities")) { if (!on_off(v, &a->abilities)) { snprintf(err, nerr, "line %d: abilities on or off", line); return false; } }
        else if (!strcmp(k, "god")) { if (!on_off(v, &a->god)) { snprintf(err, nerr, "line %d: god on or off", line); return false; } }
        else if (!strcmp(k, "group")) { if (!on_off(v, &a->group)) { snprintf(err, nerr, "line %d: group on or off", line); return false; } }
        else { snprintf(err, nerr, "line %d: unknown key '%s'", line, k); return false; }
    }
    // Names used by controllers, targets, facing and the end condition must exist.
    CretScnActor* all[CRET_SCN_MAX_ACTORS + 1]; int na = 0; all[na++] = &sc->player; for (int k = 0; k < sc->nactor; k++) all[na++] = &sc->actor[k];
    for (int k = 0; k < na; k++) {
        const CretScnActor* a = all[k];
        if (a->ctrl_target[0] && find_actor(sc, a->ctrl_target) < 0) { snprintf(err, nerr, "%s: control target '%s' is not an actor", a->name, a->ctrl_target); return false; }
        if (a->target[0] && find_actor(sc, a->target) < 0) { snprintf(err, nerr, "%s: target '%s' is not an actor", a->name, a->target); return false; }
        if (a->face[0] && find_actor(sc, a->face) < 0) { snprintf(err, nerr, "%s: facing '%s' is neither degrees nor an actor", a->name, a->face); return false; }
        if (a->behaviour == CRET_ACTOR_SCRIPT && k > 0 && a->ctrl == SCN_CTRL_IDLE && !a->ctrl_target[0]) { /* a scripted idler: fine */ }
    }
    if (sc->end != SCN_END_TIME && find_actor(sc, sc->end_target) < 0) { snprintf(err, nerr, "end: '%s' is not an actor", sc->end_target); return false; }
    int total = 1; for (int k = 0; k < sc->nactor; k++) total += sc->actor[k].count;
    if (total > CRET_MAX_AGENTS) { snprintf(err, nerr, "too many animals (%d)", total); return false; }
    return true;
}

bool cret_scenario_load(const char* name, CretScenario* sc, char* err, int nerr) {
    char path[600]; size_t L = strlen(name);
    if (L > 4 && !strcmp(name + L - 4, ".ini")) snprintf(path, sizeof path, "%s", name);
    else snprintf(path, sizeof path, "%s/%s.ini", CRET_SCN_DIR, name);
    FILE* f = fopen(path, "rb"); if (!f) { snprintf(err, nerr, "cannot open %s", path); return false; }
    char* text = malloc(65536); size_t n = fread(text, 1, 65535, f); text[n] = 0; fclose(f);
    // The scenario's name: the file name without directory or .ini.
    const char* base = strrchr(path, '/'); base = base ? base + 1 : path;
    char nm[48]; snprintf(nm, sizeof nm, "%s", base); char* dot = strrchr(nm, '.'); if (dot) *dot = 0;
    bool ok = cret_scenario_parse(text, nm, sc, err, nerr);
    // Titles keep their case: reread the raw title line.
    if (ok) for (char* l = text; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
        char* t = l; while (*t == ' ') t++;
        if (strncmp(t, "title", 5)) continue;
        char* eq = strchr(t, '='); char* nl = strchr(t, '\n'); if (!eq || (nl && eq > nl)) continue;
        char tb[96]; size_t tl = nl ? (size_t)(nl - eq - 1) : strlen(eq + 1); if (tl >= sizeof tb) tl = sizeof tb - 1;
        memcpy(tb, eq + 1, tl); tb[tl] = 0; char* hash = strpbrk(tb, "#;"); if (hash) *hash = 0;
        snprintf(sc->title, sizeof sc->title, "%s", trim(tb)); break;
    }
    free(text);
    return ok;
}

static int cmp48(const void* a, const void* b) { return strcmp((const char*)a, (const char*)b); }
int cret_scenario_list(char names[][48], int max) {
    DIR* d = opendir(CRET_SCN_DIR); if (!d) return 0;
    int n = 0; struct dirent* e;
    while ((e = readdir(d)) && n < max) {
        size_t L = strlen(e->d_name); if (L < 5 || L > 47 || strcmp(e->d_name + L - 4, ".ini")) continue;
        memcpy(names[n], e->d_name, L - 4); names[n][L - 4] = 0; n++;
    }
    closedir(d); qsort(names, (size_t)n, 48, cmp48);
    return n;
}

// ---------------------------------------------------------------- setup
CretConfig cret_scenario_config(const CretScenario* sc, CretConfig base) {
    CretConfig c = base;
    int total = 1; for (int k = 0; k < sc->nactor; k++) total += sc->actor[k].count;
    c.num_agents = total; c.player_species = sc->player.species; c.player_growth = sc->player.growth;
    c.respawn_ticks = 1 << 28; c.weather = 0; c.max_ticks = 1 << 30;
    c.cloud_cover = sc->weather == 2 ? 0.97f : sc->weather == 1 ? 0.8f : 0.25f;
    return c;
}

bool cret_scenario_find_open(const CretWorld* w, float* ox, float* oz) {
    const TerraMap* m = &w->map; int n = m->n; float cell = m->cell; int best = -1; float bd = FLT_MAX;
    // Strict first; on hilly islands a second pass takes gentler slopes and
    // twice the relief (never the centre fallback, which may be water).
    for (int pass = 0; pass < 2 && best < 0; pass++) {
    const float flat = pass ? 0.9f : 0.95f, side = pass ? 0.85f : 0.9f, relief = pass ? 0.24f : 0.12f;
    for (int z = n / 4; z < 3 * n / 4; z += 3) for (int x = n / 4; x < 3 * n / 4; x += 3) {
        size_t i = (size_t)z * n + x; const uint8_t* vg = m->veg + i * TERRA_VEG_COUNT;
        float px = x * cell, pz = z * cell, nn[3]; terra_normal(m, px, pz, nn);
        if (m->height[i] < 4 || m->water_kind[i] != TERRA_WATER_NONE || nn[1] < flat) continue;
        bool open = true;
        for (int q = 0; q < 24 && open; q++) {
            float a = q * 0.2618f, r = q < 12 ? 20.0f : 40.0f; int gx = (int)((px + cosf(a) * r) / cell + 0.5f), gz = (int)((pz + sinf(a) * r) / cell + 0.5f);
            if (gx < 0 || gz < 0 || gx >= n || gz >= n) { open = false; break; }
            size_t j = (size_t)gz * n + gx; float qn[3]; terra_normal(m, gx * cell, gz * cell, qn);
            if (m->water_kind[j] != TERRA_WATER_NONE || fabsf(m->height[j] - m->height[i]) > r * relief || qn[1] < side) open = false;
        }
        if (!open) continue;
        float d = (vg[TERRA_VEG_CONIFER] + vg[TERRA_VEG_BROADLEAF] + 0.6f * vg[TERRA_VEG_TREEFERN] + 0.3f * vg[TERRA_VEG_PALM]) + 0.08f * (fabsf(x - n * 0.5f) + fabsf(z - n * 0.52f));
        if (d < bd) { bd = d; best = (int)i; }
    }
    }
    if (best < 0) return false;
    *ox = (best % n) * cell; *oz = (best / n) * cell;
    return true;
}

// A predator played by the game AI (it hunts only when hungry).
static bool hunts(const CretScnActor* A, const CretAgent* a) {
    bool bot = A->behaviour == CRET_ACTOR_BOT || ((A->behaviour == CRET_ACTOR_SCRIPT || !strcmp(A->name, "player")) && A->ctrl == SCN_CTRL_BOT);
    return bot && CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_CARNIVORE;
}

// Nearest live member of scenario group g (0 player, k + 1 actor k) to agent i; -1 if none.
static int nearest_member(const CretScnRun* r, const CretSim* s, int g, int i) {
    if (g < 0) return -1;
    int best = -1; float bd = FLT_MAX; const CretAgent* a = &s->agents[i];
    for (int k = 0; k < r->nslot[g]; k++) {
        int j = r->slot[g][k]; if (j == i || !s->agents[j].alive) continue;
        float d = hypotf(s->agents[j].x - a->x, s->agents[j].z - a->z); if (d < bd) { bd = d; best = j; }
    }
    return best;
}

static void place_agent(CretSim* s, int i, int species, float growth, float x, float z, float yaw) {
    s->species_pick[i] = (uint8_t)(species + 1);
    cret_sim_respawn(s, i);
    CretAgent* a = &s->agents[i];
    a->growth = growth; a->max_health = 0; cret_apply_growth(a); a->health = a->max_health; a->food = a->water = a->stamina = 1;
    a->group = 0;
    if (cret_pop_move_group(s, i, x, z, false) <= 0) { a->x = x; a->z = z; a->y = cret_height(s->world, x, z); }
    a->yaw = yaw; a->vx = a->vz = 0; a->speed = 0; a->bot_state = 0; a->bot_timer = 30; a->goal_x = a->goal_z = 0;
    cret_sim_place(s, i);
}

bool cret_scenario_start(CretScnRun* r, const CretScenario* sc, CretSim* s) {
    memset(r, 0, sizeof *r); r->sc = *sc;
    cret_cheats_init(&r->cheats, NULL, s->cfg.seed);
    for (int i = 0; i < CRET_MAX_AGENTS; i++) r->group_of[i] = -1;
    if (sc->open) { if (!cret_scenario_find_open(s->world, &r->ox, &r->oz)) { r->ox = s->world->size * 0.5f; r->oz = s->world->size * 0.5f; } }
    else { r->ox = sc->px; r->oz = sc->pz; }
    s->time_of_day = fmodf(sc->hour / 24.0f, 1.0f);
    if (sc->weather >= 0) { s->cfg.weather = 0; s->cloud_cover = s->cloud_target = s->cfg.cloud_cover = sc->weather == 2 ? 0.97f : sc->weather == 1 ? 0.8f : 0.25f; }
    for (int i = 0; i < s->cfg.num_agents; i++) { s->agents[i].alive = false; }
    for (int k = 0; k < sc->nprop; k++)
    {
        int id = cret_dyn_place(s, sc->prop[k].kind, sc->prop[k].kg, r->ox + sc->prop[k].x, r->oz + sc->prop[k].z, sc->prop[k].yaw * 3.14159265f / 180.0f);
        if (getenv("CRET_SCN_DEBUG")) { int q = cret_dyn_find(s, id); fprintf(stderr, "scenario: prop %d -> id %d rec %d body %d at %.1f %.1f %.1f\n", k, id, q, q >= 0 ? (int)B3_IS_NON_NULL(s->dyn[q].body) : -1, q >= 0 ? s->dyn[q].p[0] - r->ox : 0, q >= 0 ? s->dyn[q].p[1] : 0, q >= 0 ? s->dyn[q].p[2] - r->oz : 0); }
    }
    // Slots: 0 the player, then each actor's animals in order.
    const CretScnActor* all[CRET_SCN_MAX_ACTORS + 1]; int na = 0; all[na++] = &sc->player; for (int k = 0; k < sc->nactor; k++) all[na++] = &sc->actor[k];
    int next = 1; uint32_t rng = 0x5eed1234u;
    for (int g = 0; g < na; g++) {
        const CretScnActor* A = all[g]; int cnt = g == 0 ? 1 : A->count;
        for (int c = 0; c < cnt; c++) {
            int i = g == 0 ? 0 : next++;
            if (i >= s->cfg.num_agents) return false;
            float x = r->ox + A->x, z = r->oz + A->z;
            if (cnt > 1) {   // scattered within the spread (a sunflower pattern, deterministic)
                float ang = c * 2.39996f, rr = A->spread * sqrtf((c + 0.5f) / cnt);
                x += cosf(ang) * rr; z += sinf(ang) * rr;
            }
            float yaw = isnan(A->facing) ? 0 : A->facing * 3.14159265f / 180.0f;
            place_agent(s, i, A->species, A->growth, x, z, yaw);
            r->slot[g][r->nslot[g]++] = i; r->group_of[i] = g;
        }
        r->st[g].n = cnt;
    }
    // Facing an actor, groups, behaviours.
    for (int g = 0; g < na; g++) {
        const CretScnActor* A = all[g];
        for (int c = 0; c < r->nslot[g]; c++) {
            int i = r->slot[g][c]; CretAgent* a = &s->agents[i];
            if (A->face[0]) {
                int tg = find_actor(sc, A->face), t = tg >= 0 && r->nslot[tg] ? r->slot[tg][0] : -1;
                if (t >= 0) a->yaw = atan2f(s->agents[t].z - a->z, s->agents[t].x - a->x);
            }
            if (g > 0 && A->group && r->nslot[g] > 1 && CRET_SPECIES_TABLE[A->species].group_max > 1) a->group = (uint8_t)(r->slot[g][0] + 1);
            r->cheats.actor[i] = (uint8_t)(g == 0 ? CRET_ACTOR_SCRIPT : A->behaviour);
            if (A->behaviour == CRET_ACTOR_HOSTILE) {
                int tg = A->target[0] ? find_actor(sc, A->target) : 0;
                r->cheats.target[i] = tg >= 0 && r->nslot[tg] ? r->slot[tg][0] : 0;
            }
            r->cheats.wander[i] = a->yaw; r->was_alive[i] = 1;
            if (hunts(A, a)) a->food = 0.5f;
            for (int k = 0; k < CRET_AB_SLOTS; k++) r->prev_cd[i][k] = a->ab_cd[k];
            cret_sim_place(s, i);
        }
    }
    (void)rng;
    r->hits_seen = s->hit_count;
    r->end_tick = (int)(sc->seconds * CRET_TICK_HZ);
    return true;
}

// ---------------------------------------------------------------- controllers
static CretInput controller(CretScnRun* r, CretSim* s, int i, const CretScnActor* A) {
    CretAgent* a = &s->agents[i]; CretInput in = {0};
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    int tg = A->ctrl_target[0] ? find_actor(&r->sc, A->ctrl_target) : -1;
    int t = nearest_member(r, s, tg, i);
    const CretAgent* b = t >= 0 ? &s->agents[t] : NULL;
    float dx = b ? b->x - a->x : 0, dz = b ? b->z - a->z : 0, d = hypotf(dx, dz), to = atan2f(dz, dx);
    switch (A->ctrl) {
    case SCN_CTRL_BOT: return cret_bot_input(s, i);
    case SCN_CTRL_WALK: {
        float tx = r->ox + A->p0, tz = r->oz + A->p1, wd = hypotf(tx - a->x, tz - a->z);
        if (wd > 2.5f) { in.turn = sc_clamp(sc_wrap(cret_avoid_heading(s, i, atan2f(tz - a->z, tx - a->x), wd) - a->yaw) * 2, -1, 1); in.forward = A->run ? 1.0f : wd > 8 ? 0.5f : 0.3f; in.sprint = A->run && a->stamina > 0.1f; }
        break;
    }
    case SCN_CTRL_ATTACK: if (b) in = cret_actor_attack_input(s, i, t); break;
    case SCN_CTRL_CIRCLE:
        if (b) {
            // Face the target, strafe round it at the radius, close in to strike when the attack is ready.
            float rad = A->p0 + CRET_SPECIES_TABLE[b->species].length * cret_growth_scale(b->growth) * 0.4f;
            bool strike = a->attack_cd == 0 && d < rad + 6;
            in.turn = sc_clamp(sc_wrap(to - a->yaw) * 2.5f, -1, 1);
            in.strafe = 1; in.forward = strike ? 1.0f : sc_clamp((d - rad) * 0.25f, -0.6f, 1);
            in.aim = cret_aim_at(a, b->x, b->z);
            float px, pz, sv = a->aim; a->aim = in.aim; cret_strike_point(a, &px, &pz); a->aim = sv;
            if (cret_body_dist(b, px, pz) < S->reach * g * 0.9f) in.action = CRET_ACT_ATTACK;
        }
        break;
    case SCN_CTRL_FLEE:
        if (b) { in.turn = sc_clamp(sc_wrap(cret_avoid_heading(s, i, to + 3.14159265f, 20) - a->yaw) * 2, -1, 1); in.forward = 1; in.sprint = a->stamina > 0.1f; }
        break;
    case SCN_CTRL_SWIPE:
        if (b) {
            // Keep the target off the flank-rear (about 120 degrees round) and sweep it with the secondary attack.
            float side = sc_wrap(to - a->yaw) >= 0 ? 1.0f : -1.0f, want = to - side * 2.1f;
            in.turn = sc_clamp(sc_wrap(want - a->yaw) * 2, -1, 1);
            in.forward = d > S->alt.reach * g * 0.9f ? -0.4f : 0;
            if (a->attack2_cd == 0 && d < S->alt.reach * g + CRET_SPECIES_TABLE[b->species].length * 0.4f) in.action = CRET_ACT_ATTACK2;
        }
        break;
    case SCN_CTRL_HITRUN:
        if (b) {
            if (r->hr_phase[i] == 0) {   // in: strike until a blow lands
                in = cret_actor_attack_input(s, i, t);
                if (r->st[r->group_of[i]].hits > r->hr_hits[i]) { r->hr_phase[i] = 1; r->hr_t[i] = A->p0; }
            } else {                     // out: run off, then come back
                in.turn = sc_clamp(sc_wrap(cret_avoid_heading(s, i, to + 3.14159265f, 20) - a->yaw) * 2, -1, 1); in.forward = 1; in.sprint = a->stamina > 0.2f;
                if ((r->hr_t[i] -= CRET_DT) <= 0) { r->hr_phase[i] = 0; r->hr_hits[i] = r->st[r->group_of[i]].hits; }
            }
            if (r->hr_phase[i] == 0) r->hr_hits[i] = r->hr_hits[i] > r->st[r->group_of[i]].hits ? r->st[r->group_of[i]].hits : r->hr_hits[i];
        }
        break;
    default: break;
    }
    // Unstick: pushing forward without moving (wedged between trunks, a rock
    // in the way) for 0.6 s backs out for 0.6 s, then sidesteps for 1 s, the
    // other side each time, then resumes.
    if (r->side_t[i] > 0) {
        r->side_t[i] -= CRET_DT; float side = r->side_n[i] & 1 ? 1.0f : -1.0f;
        in.strafe = side; in.forward = r->side_t[i] > 1.0f ? -0.8f : 0.4f; in.turn = r->side_t[i] > 1.0f ? side * 0.5f : 0; in.sprint = 0;
        if (in.action == CRET_ACT_ATTACK) in.action = 0;
    } else if (in.forward > 0.5f && hypotf(a->vx, a->vz) < 0.3f && !cret_act_is_attack(a->act) && in.action == 0) {
        if ((r->stuck_t[i] += CRET_DT) > 0.6f) { r->stuck_t[i] = 0; r->side_t[i] = 1.6f; r->side_n[i]++; }
    } else r->stuck_t[i] = 0;
    // Abilities: slot 1 when the target is a few metres ahead (pounce, charge, leap), slot 2 in reach.
    if (A->abilities && b && !in.ability && (A->ctrl == SCN_CTRL_ATTACK || A->ctrl == SCN_CTRL_CIRCLE || (A->ctrl == SCN_CTRL_HITRUN && r->hr_phase[i] == 0))) {
        float px, pz; cret_strike_point(a, &px, &pz); float gap = cret_body_dist(b, px, pz);
        if (cret_ability_ready(s, i, 0) && gap > 2.5f && gap < 9 && fabsf(sc_wrap(to - a->yaw)) < 0.35f) in.ability = 1;
        else if (cret_ability_ready(s, i, 1) && gap < S->reach * g * 0.9f && !cret_act_is_attack(a->act)) in.ability = 2;
    }
    return in;
}

void cret_scenario_inputs(CretScnRun* r, CretSim* s, CretInput* inputs) {
    const CretScnActor* all[CRET_SCN_MAX_ACTORS + 1]; int na = 0; all[na++] = &r->sc.player; for (int k = 0; k < r->sc.nactor; k++) all[na++] = &r->sc.actor[k];
    for (int i = 0; i < s->cfg.num_agents; i++) {
        int g = r->group_of[i]; if (g < 0 || !s->agents[i].alive) { inputs[i] = (CretInput){0}; continue; }
        if (g == 0 && r->manual) continue;   // a person plays it
        const CretScnActor* A = all[g];
        if (g == 0 || A->behaviour == CRET_ACTOR_SCRIPT) inputs[i] = controller(r, s, i, A);
        else if (A->behaviour == CRET_ACTOR_BOT) inputs[i] = cret_bot_input(s, i);
        else inputs[i] = (CretInput){0};   // dummy, passive, hostile: cret_cheats_inputs below
    }
    // Hostile actors go for the nearest live member of their target group (the first one may be dead).
    for (int i = 0; i < s->cfg.num_agents; i++) {
        int g = r->group_of[i]; if (g <= 0 || all[g]->behaviour != CRET_ACTOR_HOSTILE || !s->agents[i].alive) continue;
        int tg = all[g]->target[0] ? find_actor(&r->sc, all[g]->target) : 0, t = nearest_member(r, s, tg, i);
        r->cheats.target[i] = t >= 0 ? t : (tg == 0 ? 0 : i);   // nobody left: stand (a target of itself is no target)
    }
    CretInput keep = inputs[0];
    cret_cheats_inputs(&r->cheats, s, inputs, 0);
    inputs[0] = keep;
}

void cret_scenario_after_step(CretScnRun* r, CretSim* s) {
    const CretScnActor* all[CRET_SCN_MAX_ACTORS + 1]; int na = 0; all[na++] = &r->sc.player; for (int k = 0; k < r->sc.nactor; k++) all[na++] = &r->sc.actor[k];
    // God mode per actor; debug actors stay fed (no respawns here, so nothing to release).
    for (int i = 0; i < s->cfg.num_agents; i++) {
        int g = r->group_of[i]; CretAgent* a = &s->agents[i]; if (g < 0 || !a->alive) continue;
        if (all[g]->god) { a->health = a->max_health; a->bleed = 0; a->broken = 0; }
        // Hunger: predators the game AI plays stay hungry (bots hunt below 0.75 food), everyone else stays fed.
        a->water = fmaxf(a->water, 0.9f);
        if (hunts(all[g], a)) a->food = sc_clamp(a->food, 0.4f, 0.6f); else a->food = fmaxf(a->food, 0.9f);
    }
    // Hits from the log (attacker, victim, damage as a fraction of the victim's health).
    for (; r->hits_seen < s->hit_count; r->hits_seen++) {
        if (s->hit_count - r->hits_seen > CRET_HIT_LOG) r->hits_seen = s->hit_count - CRET_HIT_LOG;
        const CretHit* h = &s->hits[r->hits_seen % CRET_HIT_LOG];
        if (h->attacker >= s->cfg.num_agents || h->victim >= s->cfg.num_agents) continue;
        int ga = r->group_of[h->attacker]; if (ga < 0) continue;
        r->st[ga].hits++; r->st[ga].dealt += h->dmg / 255.0f * s->agents[h->victim].max_health;
    }
    for (int i = 0; i < s->cfg.num_agents; i++) {
        int g = r->group_of[i]; if (g < 0) continue;
        CretAgent* a = &s->agents[i];
        r->st[g].taken += a->ev_taken * a->max_health;
        r->st[g].kills += a->ev_kills;
        if (r->was_alive[i] && !a->alive) r->st[g].deaths++;
        r->was_alive[i] = a->alive;
        for (int k = 0; k < CRET_AB_SLOTS; k++) { if (a->ab_cd[k] > r->prev_cd[i][k] + 0.5f) r->st[g].abilities++; r->prev_cd[i][k] = a->ab_cd[k]; }
        int cd = a->attack_cd + a->attack2_cd; if (cd > r->prev_atk_cd[i] + 2) r->st[g].attacks++; r->prev_atk_cd[i] = cd;
    }
    r->tick++;
    if (r->done) return;
    if (!r->ended[0]) {
        if (r->sc.end != SCN_END_TIME) {
            int g = find_actor(&r->sc, r->sc.end_target), dead = 0;
            for (int k = 0; g >= 0 && k < r->nslot[g]; k++) dead += !s->agents[r->slot[g][k]].alive;
            if (g >= 0 && ((r->sc.end == SCN_END_DEATH && dead > 0) || (r->sc.end == SCN_END_WIPE && dead == r->nslot[g]))) {
                snprintf(r->ended, sizeof r->ended, "%s %s", r->sc.end_target, r->sc.end == SCN_END_DEATH ? "lost an animal" : "all dead");
                r->end_tick = r->tick + 2 * CRET_TICK_HZ;   // two seconds more to see it
            }
        }
        if (r->tick >= (int)(r->sc.seconds * CRET_TICK_HZ) && !r->ended[0]) snprintf(r->ended, sizeof r->ended, "time");
        if (!s->agents[0].alive && !r->ended[0] && r->sc.end == SCN_END_TIME) { snprintf(r->ended, sizeof r->ended, "the player died"); r->end_tick = r->tick + 2 * CRET_TICK_HZ; }
    }
    if (r->tick >= r->end_tick) r->done = true;
}

int cret_scenario_summary(const CretScnRun* r, char* out, int n) {
    int k = snprintf(out, n, "scenario %s: %.1f s (ended: %s)\n  %-12s %-10s %2s %5s %5s %8s %8s %6s %5s %9s\n", r->sc.name, r->tick / (float)CRET_TICK_HZ,
                     r->ended[0] ? r->ended : "running", "who", "species", "n", "swings", "hits", "dealt", "taken", "deaths", "kills", "abilities");
    for (int g = 0; g <= r->sc.nactor && k < n; g++) {
        const CretScnActor* A = g ? &r->sc.actor[g - 1] : &r->sc.player; const CretScnStats* t = &r->st[g];
        k += snprintf(out + k, n - k, "  %-12s %-10s %2d %5d %5d %6.0fhp %6.0fhp %6d %5d %9d\n", A->name, CRET_SPECIES_TABLE[A->species].asset, t->n, t->attacks, t->hits,
                      t->dealt, t->taken, t->deaths, t->kills, t->abilities);
    }
    return k;
}

bool cret_scenario_centre(const CretScnRun* r, const CretSim* s, float* x, float* z, float* radius) {
    float sx = 0, sz = 0; int n = 0;
    for (int i = 0; i < s->cfg.num_agents; i++) if (r->group_of[i] >= 0 && s->agents[i].alive) { sx += s->agents[i].x; sz += s->agents[i].z; n++; }
    if (!n) return false;
    *x = sx / n; *z = sz / n; float rr = 0;
    for (int i = 0; i < s->cfg.num_agents; i++) if (r->group_of[i] >= 0 && s->agents[i].alive) rr = fmaxf(rr, hypotf(s->agents[i].x - *x, s->agents[i].z - *z) + CRET_SPECIES_TABLE[s->agents[i].species].length * 0.5f);
    if (radius) *radius = rr;
    return true;
}
