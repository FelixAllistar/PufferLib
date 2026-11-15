// Developer cheats at the sim level (cheats.h).
#include "cheats.h"
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* const CRET_ACTOR_NAMES[CRET_ACTOR_KINDS] = {"none", "bot", "dummy", "passive", "hostile", "script"};

const CretCmdInfo CRET_CMDS[CMD_COUNT] = {
    [CMD_HELP] = {"help", "help [command]", "list commands, or one command's usage", false, false},
    [CMD_CLEAR] = {"clear", "clear", "clear the console", false, false},
    [CMD_SPAWN] = {"spawn", "spawn <species|random> [dummy|passive|hostile|bot] [n]", "spawn animals at the crosshair (default: one dummy)", true, false},
    [CMD_KILL] = {"kill", "kill [target|near|all]", "kill the animal under the crosshair, those within 100 m, or every animal", true, false},
    [CMD_HEAL] = {"heal", "heal", "full health, food, water and stamina; mends bones and bleeding", true, false},
    [CMD_GOD] = {"god", "god [on|off]", "take no harm, never hunger or thirst", true, false},
    [CMD_TP] = {"tp", "tp <x> <z> | tp nearest <species>", "teleport to a point, or next to the nearest animal of a species", true, false},
    [CMD_TIME] = {"time", "time <0..24>", "set the hour of day", true, false},
    [CMD_WEATHER] = {"weather", "weather <clear|overcast|rain|auto>", "fix the sky, or let the weather drift again", true, false},
    [CMD_SPEED] = {"speed", "speed <0.1..8>", "sim time scale", true, false},
    [CMD_FREEZE] = {"freeze", "freeze [on|off]", "animals stop thinking (the AI pauses)", true, false},
    [CMD_GROW] = {"grow", "grow <0..1>", "set your growth (0.35 hatchling .. 1 adult)", true, false},
    [CMD_SPECIES] = {"species", "species <name>", "become another species where you stand", true, false},
    [CMD_POP] = {"pop", "pop", "population: animals near you and what the director did", false, false},
    [CMD_PROP] = {"prop", "prop <boulder|stone|log> [kg]", "drop a movable boulder (2000 kg), stone (60) or log (150) at the crosshair; push it", true, false},
    [CMD_COLLIDERS] = {"colliders", "colliders [on|off]", "draw collision shapes (agent capsules, prop hulls) as wireframe", false, true},
    [CMD_LABELS] = {"labels", "labels [on|off]", "label every animal: species, state, target, health", false, true},
    [CMD_NAVDEBUG] = {"navdebug", "navdebug [on|off]", "bot goals, feelers, stuck escapes and unwalkable ground", false, true},
    [CMD_FPS] = {"fps", "fps [on|off]", "frame time graph", false, true},
    [CMD_PROFILE] = {"profile", "profile [on|off]", "GPU and CPU ms per render pass", false, true},
    [CMD_NEAR] = {"near", "near [on|off]", "nearest animals with distance and bearing", false, true},
    [CMD_PRESET] = {"preset", "preset <low|medium|high|ultra>", "graphics quality preset", false, true},
    [CMD_SHADOWS] = {"shadows", "shadows [off|low|high]", "sun shadow quality", false, true},
    [CMD_CLOUDS] = {"clouds", "clouds [on|off]", "volumetric clouds", false, true},
    [CMD_VEG] = {"veg", "veg [on|off]", "plants (trees, ferns, grass)", false, true},
    [CMD_GRASS] = {"grass", "grass [on|off]", "tall grass and its bending", false, true},
    [CMD_WATER] = {"water", "water [on|off]", "water surfaces", false, true},
    [CMD_FOG] = {"fog", "fog [on|off]", "aerial haze and valley mist (recompiles the shaders)", false, true},
    [CMD_WIREFRAME] = {"wireframe", "wireframe [on|off]", "wireframe terrain", false, true},
};

static const char* const ONOFF[] = {"on", "off", NULL};
static const char* const KILLW[] = {"target", "near", "all", NULL};
static const char* const WEATHERW[] = {"clear", "overcast", "rain", "auto", NULL};
static const char* const PRESETW[] = {"low", "medium", "high", "ultra", NULL};
static const char* const SHADOWW[] = {"off", "low", "high", NULL};
static const char* const ACTORW[] = {"dummy", "passive", "hostile", "bot", NULL};
static const char* const PROPW[] = {"boulder", "stone", "log", NULL};
static const char* const TPW[] = {"nearest", NULL};
static const char* const FULLNAME[CRET_SPECIES] = {"velociraptor", "stegosaurus", "tyrannosaurus", "pachycephalosaurus", "sauropelta", "gallimimus"};

int cret_species_by_name(const char* name) {
    if (!name || !*name) return -1;
    char lo[32]; int k = 0; for (; name[k] && k < 31; k++) lo[k] = (char)tolower((unsigned char)name[k]); lo[k] = 0;
    if (!strcmp(lo, "random")) return -2;
    for (int s = 0; s < CRET_SPECIES; s++) if (!strcmp(lo, CRET_SPECIES_TABLE[s].asset) || !strcmp(lo, FULLNAME[s])) return s;
    if (!strcmp(lo, "rex") || !strcmp(lo, "t-rex")) return CRET_SP_TREX;
    return -1;
}

static bool in_list(const char* w, const char* const* list) { for (; *list; list++) if (!strcmp(w, *list)) return true; return false; }
static bool number(const char* w, float* v) {
    char* e; float f = strtof(w, &e);
    if (e == w) return false;
    if (*e == 'x' && !e[1]) e++;   // "2x"
    if (*e || !isfinite(f)) return false;
    *v = f; return true;
}
static int find_cmd(const char* w) {
    for (int c = 0; c < CMD_COUNT; c++) if (!strcmp(w, CRET_CMDS[c].name)) return c;
    return -1;
}

#define ERR(...) do { if (err) snprintf(err, nerr, __VA_ARGS__); return false; } while (0)
bool cret_cmd_parse(const char* line, CretCmd* out, char* err, int nerr) {
    memset(out, 0, sizeof *out); out->species = -1; out->actor = CRET_ACTOR_DUMMY; out->count = 1; out->toggle = -1;
    char word[CRET_CMD_MAX_ARGS + 1][24]; int nw = 0;
    for (const char* p = line ? line : ""; *p;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (nw == CRET_CMD_MAX_ARGS + 1) ERR("too many arguments");
        int k = 0; while (*p && *p != ' ' && *p != '\t') { if (k < 23) word[nw][k++] = (char)tolower((unsigned char)*p); else ERR("word too long"); p++; }
        word[nw][k] = 0; nw++;
    }
    if (!nw) ERR("empty");
    int id = find_cmd(word[0]); if (id < 0) ERR("unknown command '%s' (help lists them)", word[0]);
    out->id = id; out->argc = nw - 1;
    for (int a = 0; a < out->argc; a++) { memcpy(out->arg[a], word[a + 1], 24); if (!number(out->arg[a], &out->num[a])) out->num[a] = NAN; }
    const CretCmdInfo* I = &CRET_CMDS[id]; const char* A0 = out->argc > 0 ? out->arg[0] : NULL;
    #define NEED(n) do { if (out->argc < (n)) ERR("usage: %s", I->usage); } while (0)
    #define MAXA(n) do { if (out->argc > (n)) ERR("usage: %s", I->usage); } while (0)
    switch (id) {
    case CMD_HELP: MAXA(1); if (A0 && find_cmd(A0) < 0) ERR("no command '%s'", A0); break;
    case CMD_CLEAR: case CMD_HEAL: case CMD_POP: MAXA(0); break;
    case CMD_PROP: NEED(1); MAXA(2); if (!in_list(A0, PROPW)) ERR("usage: %s", I->usage);
        if (out->argc == 2 && (isnan(out->num[1]) || out->num[1] < 1 || out->num[1] > 50000)) ERR("prop: kg 1..50000"); break;
    case CMD_SPAWN: {
        NEED(1); MAXA(3);
        out->species = cret_species_by_name(A0); if (out->species == -1) ERR("unknown species '%s' (raptor, stego, trex, pachy, sauropelta, galli, random)", A0);
        for (int a = 1; a < out->argc; a++) {
            const char* w = out->arg[a];
            if (in_list(w, ACTORW)) {
                out->actor = !strcmp(w, "dummy") ? CRET_ACTOR_DUMMY : !strcmp(w, "passive") ? CRET_ACTOR_PASSIVE : !strcmp(w, "hostile") ? CRET_ACTOR_HOSTILE : CRET_ACTOR_BOT;
            } else if (!isnan(out->num[a]) && out->num[a] == floorf(out->num[a])) {
                if (out->num[a] < 1 || out->num[a] > 16) ERR("spawn: n must be 1..16");
                out->count = (int)out->num[a];
            } else ERR("spawn: '%s' is neither dummy, passive, hostile, bot nor a count", w);
        }
        break;
    }
    case CMD_KILL: MAXA(1); if (A0 && !in_list(A0, KILLW)) ERR("usage: %s", I->usage); break;
    case CMD_TP:
        NEED(1); MAXA(2);
        if (!strcmp(A0, "nearest")) {
            NEED(2); out->species = cret_species_by_name(out->arg[1]);
            if (out->species < 0) ERR("unknown species '%s'", out->arg[1]);
        } else { NEED(2); if (isnan(out->num[0]) || isnan(out->num[1])) ERR("usage: %s", I->usage); }
        break;
    case CMD_TIME: NEED(1); MAXA(1); if (isnan(out->num[0]) || out->num[0] < 0 || out->num[0] > 24) ERR("time: an hour 0..24"); break;
    case CMD_WEATHER: NEED(1); MAXA(1); if (!in_list(A0, WEATHERW)) ERR("usage: %s", I->usage); break;
    case CMD_SPEED: NEED(1); MAXA(1); if (isnan(out->num[0]) || out->num[0] < 0.1f || out->num[0] > 8) ERR("speed: 0.1..8"); break;
    case CMD_GROW: NEED(1); MAXA(1); if (isnan(out->num[0]) || out->num[0] < 0 || out->num[0] > 1) ERR("grow: 0..1"); break;
    case CMD_SPECIES:
        NEED(1); MAXA(1); out->species = cret_species_by_name(A0);
        if (out->species < 0) ERR("unknown species '%s'", A0);
        break;
    case CMD_PRESET: NEED(1); MAXA(1); if (!in_list(A0, PRESETW)) ERR("usage: %s", I->usage); break;
    case CMD_SHADOWS: MAXA(1); if (A0 && !in_list(A0, SHADOWW)) ERR("usage: %s", I->usage); break;
    default:   // on/off toggles: god, freeze, overlays and graphics switches
        MAXA(1);
        if (A0) { if (!in_list(A0, ONOFF)) ERR("usage: %s", I->usage); out->toggle = !strcmp(A0, "on"); }
        break;
    }
    if (id == CMD_GOD || id == CMD_FREEZE) out->toggle = A0 ? !strcmp(A0, "on") : -1;
    return true;
    #undef NEED
    #undef MAXA
}
#undef ERR

// Candidate words for argument `pos` (0-based) of a command, given the earlier words.
static int candidates(int id, int pos, char words[][24], const char** out, int max) {
    int n = 0;
    #define ADD(w) do { if (n < max) out[n++] = (w); } while (0)
    #define ADDL(l) do { for (const char* const* q = (l); *q; q++) ADD(*q); } while (0)
    switch (id) {
    case CMD_HELP: if (pos == 0) for (int c = 0; c < CMD_COUNT; c++) ADD(CRET_CMDS[c].name); break;
    case CMD_SPAWN:
        if (pos == 0) { for (int s = 0; s < CRET_SPECIES; s++) ADD(CRET_SPECIES_TABLE[s].asset); ADD("random"); }
        else if (pos == 1) ADDL(ACTORW);
        break;
    case CMD_KILL: if (pos == 0) ADDL(KILLW); break;
    case CMD_PROP: if (pos == 0) ADDL(PROPW); break;
    case CMD_TP:
        if (pos == 0) ADDL(TPW);
        else if (pos == 1 && !strcmp(words[1], "nearest")) for (int s = 0; s < CRET_SPECIES; s++) ADD(CRET_SPECIES_TABLE[s].asset);
        break;
    case CMD_WEATHER: if (pos == 0) ADDL(WEATHERW); break;
    case CMD_SPECIES: if (pos == 0) for (int s = 0; s < CRET_SPECIES; s++) ADD(CRET_SPECIES_TABLE[s].asset); break;
    case CMD_PRESET: if (pos == 0) ADDL(PRESETW); break;
    case CMD_SHADOWS: if (pos == 0) ADDL(SHADOWW); break;
    case CMD_HEAL: case CMD_CLEAR: case CMD_POP: case CMD_TIME: case CMD_SPEED: case CMD_GROW: break;
    default: if (pos == 0) ADDL(ONOFF); break;
    }
    return n;
    #undef ADD
    #undef ADDL
}

int cret_cmd_complete(const char* line, char* out, int nout, char list[][24], int maxlist) {
    if (!line) line = "";
    // Split into words; the last one (possibly empty, after a space) is completed.
    char words[8][24]; int nw = 0; const char* p = line; const char* last_start = line;
    while (*p && nw < 8) {
        while (*p == ' ') p++;
        if (!*p) break;
        last_start = p; int k = 0;
        while (*p && *p != ' ') { if (k < 23) words[nw][k++] = (char)tolower((unsigned char)*p); p++; }
        words[nw][k] = 0; nw++;
    }
    bool trailing = !*line || line[strlen(line) - 1] == ' ';
    if (trailing) { if (nw < 8) { words[nw][0] = 0; nw++; } last_start = line + strlen(line); }
    const char* cand[64]; int nc;
    if (nw <= 1) { nc = 0; for (int c = 0; c < CMD_COUNT && nc < 64; c++) cand[nc++] = CRET_CMDS[c].name; }
    else { int id = find_cmd(words[0]); nc = id < 0 ? 0 : candidates(id, nw - 2, words, cand, 64); }
    const char* pre = words[nw - 1]; size_t pl = strlen(pre);
    const char* match[64]; int nm = 0;
    for (int c = 0; c < nc; c++) if (!strncmp(cand[c], pre, pl)) match[nm++] = cand[c];
    if (list) for (int m = 0; m < nm && m < maxlist; m++) snprintf(list[m], 24, "%s", match[m]);
    size_t head = (size_t)(last_start - line);
    if (!nm) { snprintf(out, nout, "%s", line); return 0; }
    // Longest common prefix of the matches.
    size_t cp = strlen(match[0]);
    for (int m = 1; m < nm; m++) { size_t k = 0; while (k < cp && match[m][k] == match[0][k]) k++; cp = k; }
    char comp[24]; snprintf(comp, sizeof comp, "%.*s", (int)cp, match[0]);
    snprintf(out, nout, "%.*s%s%s", (int)head, line, comp, nm == 1 ? " " : "");
    return nm;
}

// ---------------------------------------------------------------- actors
static inline uint32_t ch_xs(uint32_t* s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x ? x : 0x9e3779b9u; }
static inline float ch_rand(uint32_t* s) { return (ch_xs(s) >> 8) * (1.0f / 16777216.0f); }
static inline float ch_wrap(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static inline float ch_clamp(float x, float a, float b) { return x < a ? a : x > b ? b : x; }

void cret_cheats_init(CretCheats* c, CretPop* pop, uint32_t seed) {
    memset(c, 0, sizeof *c); c->speed = 1; c->pop = pop; c->rng = seed * 747796405u + 1;
    for (int i = 0; i < CRET_MAX_AGENTS; i++) c->target[i] = -1;
}

void cret_cheat_release(CretCheats* c, CretSim* s, int i) {
    if (i < 0 || i >= CRET_MAX_AGENTS) return;
    c->actor[i] = CRET_ACTOR_NONE; c->target[i] = -1; c->flee_t[i] = 0;
    if (!cret_is_person(s, i)) s->species_pick[i] = 0;
    if (c->pop) c->pop->exempt[i] = 0;
}

int cret_cheat_spawn(CretCheats* c, CretSim* s, int species, int actor, float x, float z, float yaw, int me) {
    if (species == -2) species = (int)(ch_rand(&c->rng) * CRET_SPECIES) % CRET_SPECIES;
    if (species < 0 || species >= CRET_SPECIES) return -1;
    // A free slot: dead and waiting, else the bot farthest from every person.
    int best = -1; float bs = -1;
    for (int i = 0; i < s->cfg.num_agents; i++) {
        if (i == me || cret_is_person(s, i) || (c->actor[i] != CRET_ACTOR_NONE && s->agents[i].alive)) continue;   // a dead debug actor's slot is free
        const CretAgent* a = &s->agents[i];
        if (a->alive && (a->latch || a->latchers)) continue;
        float d = 2e6f;   // a slot waiting to respawn is the best
        if (a->alive) {
            d = 1e6f;
            for (int j = 0; j < s->cfg.num_agents; j++)
                if (cret_is_person(s, j) && s->agents[j].alive) d = fminf(d, hypotf(s->agents[j].x - a->x, s->agents[j].z - a->z));
        }
        if (d > bs) { bs = d; best = i; }
    }
    if (best < 0) return -1;
    int i = best;
    s->species_pick[i] = (uint8_t)(species + 1);
    cret_sim_respawn(s, i);
    CretAgent* a = &s->agents[i];
    a->growth = 1; a->max_health = 0; cret_apply_growth(a); a->health = a->max_health; a->food = a->water = a->stamina = 1;
    a->group = 0;
    if (cret_pop_move_group(s, i, x, z, false) <= 0) {   // blocked there: as close as the ground allows
        a->x = x; a->z = z; a->y = cret_height(s->world, x, z);
    }
    a->yaw = yaw; a->bot_state = 0; a->bot_timer = 60; a->goal_x = a->goal_z = 0;
    cret_sim_place(s, i);
    c->actor[i] = (uint8_t)actor; c->target[i] = -1; c->flee_t[i] = 0; c->wander[i] = yaw; c->last_hurt[i] = 0;
    if (c->pop) { c->pop->exempt[i] = 1; c->pop->moved[i] = s->tick; }
    c->spawned++;
    return i;
}

// Toward agent t: steer, close in, and strike in reach (primary; a tail swipe at something behind).
CretInput cret_actor_attack_input(CretSim* s, int i, int t) {
    CretInput in = {0}; CretAgent* a = &s->agents[i];
    if (t < 0 || t >= s->cfg.num_agents || t == i || !s->agents[t].alive || !a->alive) return in;
    const CretAgent* b = &s->agents[t]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    float dx = b->x - a->x, dz = b->z - a->z, d = hypotf(dx, dz);
    float want = S->tail ? atan2f(-dz, -dx) : atan2f(dz, dx);   // a stegosaur backs its tail toward the foe
    in.turn = ch_clamp(ch_wrap(want - a->yaw) * 2.0f, -1, 1);
    in.aim = cret_aim_at(a, b->x, b->z);
    float saved = a->aim; a->aim = in.aim; float px, pz; cret_strike_point(a, &px, &pz); a->aim = saved;
    float gap = cret_body_dist(b, px, pz), reach = S->reach * g;
    if (!S->tail) { in.forward = gap > reach * 0.6f ? 1.0f : 0.2f; in.sprint = gap > 12 && a->stamina > 0.2f; }
    else in.forward = d > S->length * g + 6 ? -0.5f : 0;
    if (!S->tail && gap > reach + 3) in.turn = ch_clamp(ch_wrap(cret_avoid_heading(s, i, want, gap) - a->yaw) * 2.0f, -1, 1);   // round trunks on the way in
    if (gap < reach * 0.9f) in.action = CRET_ACT_ATTACK;
    else if (S->alt.kind == CRET_ALT_TAIL && a->attack2_cd == 0 && d < S->alt.reach * g && fabsf(ch_wrap(atan2f(dz, dx) - a->yaw)) > 1.5f) in.action = CRET_ACT_ATTACK2;
    return in;
}

static int nearest_person_slot(const CretSim* s, int i) {
    int best = -1; float bd = FLT_MAX; const CretAgent* a = &s->agents[i];
    for (int j = 0; j < s->cfg.num_agents; j++) {
        if (j == i || !s->agents[j].alive || !cret_is_person(s, j)) continue;
        float d = hypotf(s->agents[j].x - a->x, s->agents[j].z - a->z); if (d < bd) { bd = d; best = j; }
    }
    return best;
}

void cret_cheats_inputs(CretCheats* c, CretSim* s, CretInput* inputs, int me) {
    for (int i = 0; i < s->cfg.num_agents; i++) {
        CretAgent* a = &s->agents[i];
        if (!a->alive) continue;
        bool person = cret_is_person(s, i) || i == me;
        if (c->freeze && !person && c->actor[i] != CRET_ACTOR_SCRIPT) { inputs[i] = (CretInput){0}; continue; }
        bool hurt = a->hurt_ticks > 0 && a->hurt_ticks > c->last_hurt[i];   // a new blow
        c->last_hurt[i] = a->hurt_ticks;
        switch (c->actor[i]) {
        case CRET_ACTOR_DUMMY: {   // stands; turns slowly toward whoever hit it
            CretInput in = {0}; int t = a->last_attacker;
            if (a->hurt_ticks > 0 && t >= 0 && t < s->cfg.num_agents && s->agents[t].alive) {
                float d = ch_wrap(atan2f(s->agents[t].z - a->z, s->agents[t].x - a->x) - a->yaw);
                in.turn = ch_clamp(d * 0.6f, -0.3f, 0.3f);
            }
            inputs[i] = in; break;
        }
        case CRET_ACTOR_PASSIVE: {   // ambles; runs from a blow for a few seconds
            CretInput in = {0};
            if (hurt) c->flee_t[i] = 6.0f;
            int t = a->last_attacker;
            if (c->flee_t[i] > 0 && t >= 0 && t < s->cfg.num_agents && s->agents[t].alive) {
                c->flee_t[i] -= CRET_DT;
                float away = atan2f(a->z - s->agents[t].z, a->x - s->agents[t].x);
                in.turn = ch_clamp(ch_wrap(away - a->yaw) * 2, -1, 1); in.forward = 1; in.sprint = a->stamina > 0.1f;
            } else {
                c->flee_t[i] = 0;
                if (ch_rand(&c->rng) < 0.01f) c->wander[i] = a->yaw + (ch_rand(&c->rng) - 0.5f) * 2.0f;
                float ahead = 8 + a->radius * 4;
                if (!cret_walkable(s->world, a->x + cosf(a->yaw) * ahead, a->z + sinf(a->yaw) * ahead)) c->wander[i] = a->yaw + 2.0f;
                in.turn = ch_clamp(ch_wrap(c->wander[i] - a->yaw) * 1.5f, -0.5f, 0.5f); in.forward = 0.35f;
            }
            inputs[i] = in; break;
        }
        case CRET_ACTOR_HOSTILE: {
            int t = c->target[i] >= 0 && c->target[i] < s->cfg.num_agents && s->agents[c->target[i]].alive ? c->target[i] : nearest_person_slot(s, i);
            inputs[i] = cret_actor_attack_input(s, i, t); break;
        }
        default: break;   // bot, script, none: as given
        }
    }
}

void cret_cheats_after_step(CretCheats* c, CretSim* s, int me) {
    if (c->god && me >= 0 && me < s->cfg.num_agents && s->agents[me].alive) {
        CretAgent* a = &s->agents[me];
        a->health = a->max_health; a->bleed = 0; a->broken = 0; a->food = a->water = 1; a->stamina = fmaxf(a->stamina, 0.5f);
        a->st[CRET_ST_STUN] = a->st[CRET_ST_SLOW] = a->st[CRET_ST_FEAR] = 0;
    }
    for (int i = 0; i < s->cfg.num_agents; i++) {
        if (c->actor[i] == CRET_ACTOR_NONE || c->actor[i] == CRET_ACTOR_SCRIPT) continue;
        CretAgent* a = &s->agents[i];
        if (!a->alive) { cret_cheat_release(c, s, i); continue; }
        a->food = fmaxf(a->food, 0.9f); a->water = fmaxf(a->water, 0.9f);
    }
}

// ---------------------------------------------------------------- sim commands
static int nearest_of_species(const CretSim* s, int me, int species) {
    const CretAgent* a = &s->agents[me]; int best = -1; float bd = FLT_MAX;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (j == me || !b->alive || (species >= 0 && b->species != species)) continue;
        float d = hypotf(b->x - a->x, b->z - a->z); if (d < bd) { bd = d; best = j; }
    }
    return best;
}

#define REPLY(...) snprintf(out, nout, __VA_ARGS__)
bool cret_cheat_exec(CretCheats* c, CretSim* s, CretCheatEnv* env, const CretCmd* cmd, char* out, int nout) {
    const CretCmdInfo* I = &CRET_CMDS[cmd->id];
    if (I->view || cmd->id == CMD_HELP || cmd->id == CMD_CLEAR) return false;
    if (I->cheat && !env->authority) { REPLY("not allowed: only the host or a solo player can use '%s'", I->name); return false; }
    int me = env->me; CretAgent* a = me >= 0 && me < s->cfg.num_agents ? &s->agents[me] : NULL;
    switch (cmd->id) {
    case CMD_SPAWN: {
        if (!a) { REPLY("spawn: no player"); return false; }
        float fx = cosf(env->cam_yaw), fz = sinf(env->cam_yaw), cx, cz;
        if (env->has_aim && hypotf(env->aim[0] - a->x, env->aim[2] - a->z) < 60) { cx = env->aim[0]; cz = env->aim[2]; }
        else { cx = a->x + fx * 8; cz = a->z + fz * 8; }
        int made = 0, first = -1;
        for (int k = 0; k < cmd->count; k++) {
            int sp = cmd->species == -2 ? (int)(ch_rand(&c->rng) * CRET_SPECIES) % CRET_SPECIES : cmd->species;
            float L = CRET_SPECIES_TABLE[sp].length, ang = k * 2.39996f, r = k ? (L * 0.8f + 2) * sqrtf((float)k) : 0;
            float x = cx + cosf(ang) * r, z = cz + sinf(ang) * r;
            int i = cret_cheat_spawn(c, s, sp, cmd->actor, x, z, atan2f(a->z - z, a->x - x), me);
            if (i < 0) break;
            if (first < 0) first = i;
            made++;
        }
        if (!made) { REPLY("spawn: no free slot"); return false; }
        REPLY("spawned %d %s %s%s (slot %d%s)", made, CRET_ACTOR_NAMES[cmd->actor], cmd->species == -2 ? "random" : CRET_SPECIES_TABLE[cmd->species].asset,
              made > 1 ? "s" : "", first, made > 1 ? "..." : "");
        return true;
    }
    case CMD_KILL: {
        const char* w = cmd->argc ? cmd->arg[0] : "target"; int n = 0;
        if (!strcmp(w, "target")) {
            int t = env->target;
            if (t < 0 && a) { float bd = 40; for (int j = 0; j < s->cfg.num_agents; j++) { const CretAgent* b = &s->agents[j]; if (j == me || !b->alive) continue; float d = hypotf(b->x - a->x, b->z - a->z); if (d < bd) { bd = d; t = j; } } }
            if (t < 0 || t == me || t >= s->cfg.num_agents || !s->agents[t].alive) { REPLY("kill: nothing under the crosshair or within 40 m"); return false; }
            cret_kill(s, t); REPLY("killed %s (slot %d)", CRET_SPECIES_TABLE[s->agents[t].species].asset, t); return true;
        }
        for (int j = 0; j < s->cfg.num_agents; j++) {
            const CretAgent* b = &s->agents[j];
            if (j == me || !b->alive || cret_is_person(s, j)) continue;
            if (!strcmp(w, "near") && a && hypotf(b->x - a->x, b->z - a->z) > 100) continue;
            cret_kill(s, j); n++;
        }
        REPLY("killed %d animal%s", n, n == 1 ? "" : "s"); return true;
    }
    case CMD_HEAL:
        if (!a || !a->alive) { REPLY("heal: you are dead (Enter respawns)"); return false; }
        a->health = a->max_health; a->bleed = 0; a->broken = 0; a->food = a->water = a->stamina = 1;
        for (int k = 0; k < CRET_ST_COUNT; k++) if (k != CRET_ST_LATCHED && k != CRET_ST_LATCHING) a->st[k] = 0;
        REPLY("healed"); return true;
    case CMD_GOD: c->god = cmd->toggle < 0 ? !c->god : cmd->toggle; REPLY("god mode %s", c->god ? "on" : "off"); return true;
    case CMD_FREEZE: c->freeze = cmd->toggle < 0 ? !c->freeze : cmd->toggle; REPLY("AI %s", c->freeze ? "frozen" : "running"); return true;
    case CMD_TP: {
        if (!a || !a->alive) { REPLY("tp: you are dead"); return false; }
        float x, z;
        if (cmd->species >= 0) {
            int t = nearest_of_species(s, me, cmd->species);
            if (t < 0) { REPLY("tp: no live %s", CRET_SPECIES_TABLE[cmd->species].asset); return false; }
            const CretAgent* b = &s->agents[t]; float back = CRET_SPECIES_TABLE[b->species].length * 1.2f + 8;
            float ang = atan2f(a->z - b->z, a->x - b->x);
            x = b->x + cosf(ang) * back; z = b->z + sinf(ang) * back;
        } else { x = cmd->num[0]; z = cmd->num[1]; }
        float S = s->world->size; x = ch_clamp(x, 30, S - 30); z = ch_clamp(z, 30, S - 30);
        if (cret_pop_move_group(s, me, x, z, false) <= 0) {   // water or a cliff: put it there anyway (it swims or slides)
            a->x = x; a->z = z; a->y = fmaxf(cret_height(s->world, x, z), cret_water(s->world, x, z) - a->height * 0.5f); cret_sim_place(s, me);
        }
        if (c->pop) c->pop->keep[me] = 0;
        env->cut = true;
        REPLY("teleported to %.0f %.0f", a->x, a->z); return true;
    }
    case CMD_TIME: s->time_of_day = fmodf(cmd->num[0] / 24.0f, 1.0f); REPLY("time %05.2f", s->time_of_day * 24); return true;
    case CMD_WEATHER: {
        const char* w = cmd->arg[0];
        if (!strcmp(w, "auto")) { s->cfg.weather = 1; REPLY("weather drifts again"); return true; }
        float cov = !strcmp(w, "clear") ? 0.25f : !strcmp(w, "overcast") ? 0.8f : 0.97f;
        s->cfg.weather = 0; s->cfg.cloud_cover = s->cloud_cover = s->cloud_target = cov;
        REPLY("weather %s (cloud cover %.2f)", w, cov); return true;
    }
    case CMD_SPEED: c->speed = cmd->num[0]; REPLY("sim speed %.2fx", c->speed); return true;
    case CMD_GROW:
        if (!a || !a->alive) { REPLY("grow: you are dead"); return false; }
        a->growth = ch_clamp(cmd->num[0], 0.35f, 1.0f); cret_apply_growth(a); cret_sim_place(s, me);
        REPLY("growth %.2f", a->growth); return true;
    case CMD_SPECIES: {
        if (!a) return false;
        float x = a->x, z = a->z, yaw = a->yaw; bool alive = a->alive;
        cret_sim_set_species(s, me, cmd->species);
        if (alive) {
            a = &s->agents[me];
            if (cret_pop_move_group(s, me, x, z, false) <= 0) { a->x = x; a->z = z; a->y = cret_height(s->world, x, z); cret_sim_place(s, me); }   // a bigger body: as close as the ground allows
            a->yaw = yaw;
        }
        if (c->pop) c->pop->keep[me] = 1;
        REPLY("you are a %s", CRET_SPECIES_TABLE[cmd->species].name); return true;
    }
    case CMD_PROP: {
        if (!a) { REPLY("prop: no player"); return false; }
        float fx = cosf(env->cam_yaw), fz = sinf(env->cam_yaw), cx, cz;
        if (env->has_aim && hypotf(env->aim[0] - a->x, env->aim[2] - a->z) < 60) { cx = env->aim[0]; cz = env->aim[2]; }
        else { cx = a->x + fx * 8; cz = a->z + fz * 8; }
        const char* w = cmd->arg[0]; int kind = !strcmp(w, "log") ? PROP_LOG : !strcmp(w, "stone") ? PROP_STONE : PROP_BOULDER;
        float kg = cmd->argc == 2 ? cmd->num[1] : kind == PROP_LOG ? 150.0f : kind == PROP_STONE ? 60.0f : 2000.0f;
        int id = cret_dyn_place(s, kind, kg, cx, cz, env->cam_yaw + 1.5708f);
        if (id < 0) { REPLY("prop: no free movable %s", w); return false; }
        REPLY("dropped a %.0f kg %s (prop %d)", cret_prop_mass(s->world, id), w, id);
        return true;
    }
    case CMD_POP: {
        int n150 = 0, n300 = 0, alive = 0;
        for (int j = 0; j < s->cfg.num_agents; j++) {
            const CretAgent* b = &s->agents[j]; if (j == me || !b->alive) continue; alive++;
            if (!a) continue;
            float d = hypotf(b->x - a->x, b->z - a->z); n150 += d < 150; n300 += d < 300;
        }
        REPLY("%d animals alive (%d slots); %d within 150 m, %d within 300 m; director %s: %d herds brought, %d respawns rehomed, %d placements",
              alive, s->cfg.num_agents, n150, n300, c->pop && c->pop->on ? "on" : "off", c->pop ? c->pop->pulls : 0, c->pop ? c->pop->joins : 0, c->pop ? c->pop->placed : 0);
        return true;
    }
    default: return false;
    }
}
#undef REPLY
