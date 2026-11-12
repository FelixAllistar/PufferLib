// Headless catching probe: how often does a hatchling raptor (growth 0.35)
// catch one small animal with a reasonable approach? Each trial puts one
// critter on open, flat ground near the middle of the island and the raptor
// 20 m from it, facing it, from a side of the wind:
//   downwind   the raptor downwind of the prey (the wind in its face)
//   across     across the wind
//   upwind     the raptor upwind of the prey (its smell blows onto it)
// and plays it like a person at a frame rate (--fps, the controller decides
// once a frame and sees the world 0.2 s late, like a reaction time; presses
// wait for the next 30 Hz tick, as in cretaceous.c):
//   careful    walk to 9 m, then creep in (forward 0.3, the sniffing gait) while the prey has
//              its head down, stand still while it looks up
//              (cret_critter_watch, as the player sees it), bite in reach
//   walk       walk straight in and bite in reach
//   sprint     sprint straight at it from the start and bite in reach
//   bot        the game's hunting bot (cret_critter_bot), every tick, no delay
// Whatever the style, prey that bolts is chased at a sprint (a trot when out
// of stamina) and bitten when in reach; a hiding animal is walked up to.
// A trial is a catch when the prey is eaten within 40 s. The bite is pressed
// when the prey (as seen) is within the bite's reach of the strike point
// plus 0.3 m. Prints the catch rate per kind, approach and style, how many
// prey bolted, and the mean time to the catch.
//   make -C ocean/cretaceous catchprobe && ./build/cretaceous/catchprobe [--fps 60] [--trials 40]
// Run from the repository root (the world cache is build/cretaceous/worlds).
#include "../sim.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DELAY_TICKS 6     // 0.2 s: what the player sees and reacts to
#define TRIAL_S 40.0f

static float wrap(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static uint32_t hash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }

enum { ST_CAREFUL, ST_WALK, ST_SPRINT, ST_BOT, ST_N };
static const char* const STYLE[ST_N] = {"careful", "walk", "sprint", "bot"};
static const char* const SIDE[3] = {"downwind", "across", "upwind"};

// A flat, dry, walkable spot with room around it (40 m), nearest the middle.
static void find_spot(const CretSim* s, float* ox, float* oz) {
    const CretWorld* w = s->world; float S = w->size, best = 1e30f; *ox = *oz = S * 0.5f;
    for (float z = S * 0.3f; z < S * 0.7f; z += 16) for (float x = S * 0.3f; x < S * 0.7f; x += 16) {
        float h0 = cret_height(w, x, z); bool ok = true;
        for (int k = 0; k < 24 && ok; k++) {
            float a = k * 0.2618f, r = (k % 3 + 1) * 12.0f, px = x + cosf(a) * r, pz = z + sinf(a) * r;
            ok = cret_walkable(w, px, pz) && cret_water(w, px, pz) < cret_height(w, px, pz) && fabsf(cret_height(w, px, pz) - h0) < 2.5f;
        }
        float d = hypotf(x - S * 0.5f, z - S * 0.5f);
        if (ok && d < best) { best = d; *ox = x; *oz = z; }
    }
}

typedef struct { int tries, caught, bolted; double t_sum; } Tally;

// One trial; returns the time to the catch (s), or -1.
static float trial(CretSim* s, int kind, int side, int style, float fps, uint32_t seed, float ox, float oz, bool* bolted) {
    // The prey: one critter, idle, at a jittered spot; the pool otherwise empty.
    memset(s->critters, 0, sizeof s->critters); cret_scent_clear(s); s->ncrit = 1;
    const CretCritterKind* K = &CRET_CRITTER_KINDS[kind];
    uint32_t h = hash(seed);
    float cx = ox + ((h & 255) / 255.0f - 0.5f) * 10, cz = oz + (((h >> 8) & 255) / 255.0f - 0.5f) * 10;
    CretCritter* c = &s->critters[0];
    *c = (CretCritter){.active = 1, .kind = (uint8_t)kind, .state = CRET_CRS_IDLE, .owner = 0, .threat = -1,
                       .x = cx, .z = cz, .y = cret_height(s->world, cx, cz), .yaw = ((h >> 16) & 1023) / 1023.0f * 6.2831853f,
                       .kg = K->kg_min + (K->kg_max - K->kg_min) / 3, .hp = K->hp};
    c->timer = (uint16_t)(CRET_TICK_HZ * (0.5f + 3 * ((h >> 26) & 63) / 63.0f));
    // The raptor 20 m away on the chosen side of the wind, facing the prey.
    float wx, wz, ws; cret_wind(s, &wx, &wz, &ws);
    float ang = atan2f(wz, wx) + (side == 0 ? 0 : side == 1 ? 1.5708f : 3.14159f) + (((h >> 4) & 255) / 255.0f - 0.5f) * 0.5f;
    CretAgent* a = &s->agents[0];
    a->x = cx + cosf(ang) * 20; a->z = cz + sinf(ang) * 20; a->y = cret_height(s->world, a->x, a->z);
    a->yaw = atan2f(cz - a->z, cx - a->x); a->vx = a->vy = a->vz = 0; a->speed = 0; a->stamina = 1; a->food = 0.3f;
    a->growth = 0.35f; cret_apply_growth(a); a->water = 1;
    a->act = 0; a->act_ticks = 0; a->attack_cd = 0; a->buf_act = 0; a->buf_ticks = 0; a->health = a->max_health;
    cret_sim_place(s, 0);
    int eaten0 = 0; for (int k = 0; k < CRET_CR_KINDS; k++) eaten0 += s->stats.critters.eaten[k];
    float hx[DELAY_TICKS + 1], hz[DELAY_TICKS + 1]; for (int q = 0; q <= DELAY_TICKS; q++) { hx[q] = cx; hz[q] = cz; }
    int hstate[DELAY_TICKS + 1] = {0}; bool halert[DELAY_TICKS + 1] = {0};
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    CretInput in = {0}; uint8_t press = 0; double acc = 0, frame = 1.0 / fps; *bolted = false;
    int ticks = (int)(TRIAL_S * CRET_TICK_HZ);
    for (int t = 0; t < ticks; ) {
        // A frame: the controller looks at the world as it was DELAY_TICKS ago.
        float sx = hx[DELAY_TICKS], sz = hz[DELAY_TICKS]; int sst = hstate[DELAY_TICKS];
        float d = hypotf(sx - a->x, sz - a->z), want = wrap(atan2f(sz - a->z, sx - a->x) - a->yaw);
        bool chase = style == ST_SPRINT || sst == CRET_CRS_FLEE;
        in = (CretInput){.turn = fmaxf(-1, fminf(1, want * 2.5f)), .aim = cret_aim_at(a, sx, sz)};
        in.forward = chase ? 1.0f : style == ST_CAREFUL && d < 9 ? (halert[DELAY_TICKS] && d > 1.2f ? 0.0f : 0.3f) : 0.5f;
        in.sprint = chase && a->stamina > 0.05f;
        if (fabsf(want) > 1.2f) in.forward *= 0.4f;
        float px, pz; cret_strike_point(a, &px, &pz);
        if (hypotf(sx - px, sz - pz) < S->reach * g + 0.3f && a->attack_cd == 0) press = CRET_ACT_ATTACK;
        (void)d;
        if (style == ST_BOT) { press = 0; if (!cret_critter_bot(s, 0, &in)) in = (CretInput){0}; if (cret_act_is_attack(in.action)) press = in.action; in.action = 0; }
        acc += frame;
        while (acc >= 1.0 / CRET_TICK_HZ && t < ticks) {
            acc -= 1.0 / CRET_TICK_HZ;
            CretInput tick_in = in; if (press) tick_in.action = press;
            cret_sim_step(s, &tick_in); t++; press = 0;
            for (int q = DELAY_TICKS; q > 0; q--) { hx[q] = hx[q - 1]; hz[q] = hz[q - 1]; hstate[q] = hstate[q - 1]; }
            for (int q = DELAY_TICKS; q > 0; q--) halert[q] = halert[q - 1];
            hx[0] = c->x; hz[0] = c->z; hstate[0] = c->active ? c->state : 0; halert[0] = c->active && cret_critter_watch(s, 0) > 0;
            if (c->active && c->state == CRET_CRS_FLEE) *bolted = true;
            int eaten = 0; for (int k = 0; k < CRET_CR_KINDS; k++) eaten += s->stats.critters.eaten[k];
            if (eaten > eaten0) return t * CRET_DT;
            if (!c->active) return -1;
        }
    }
    return -1;
}

int main(int argc, char** argv) {
    int trials = 40; float fps_list[2] = {15, 60}; int nfps = 2;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--trials") && i + 1 < argc) trials = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fps") && i + 1 < argc) { fps_list[0] = (float)atof(argv[++i]); nfps = 1; }
        else { fprintf(stderr, "usage: catchprobe [--trials N] [--fps F]\n"); return 2; }
    }
    CretConfig cfg = cret_default_config(); cfg.num_agents = 1; cfg.player_species = CRET_SP_RAPTOR; cfg.player_growth = 0.35f;
    cfg.weather = 0; cfg.cloud_cover = 0.3f; cfg.seed = 7;
    CretSim* s = calloc(1, sizeof *s);
    if (cret_sim_init(s, cfg) != 0) { fprintf(stderr, "catchprobe: world failed\n"); return 1; }
    cret_sim_reset(s);
    s->crit_cfg.per_person = s->crit_cfg.fresh_per_person = 0;   // no spawning: only the trial's prey
    float ox, oz; find_spot(s, &ox, &oz);
    printf("catchprobe: hatchling raptor (growth %.2f, walk %.1f, sprint %.1f m/s, reach %.2f m) at %.0f %.0f, %d trials a cell\n",
           s->agents[0].growth, s->agents[0].walk_speed, s->agents[0].sprint_speed, CRET_SPECIES_TABLE[CRET_SP_RAPTOR].reach * cret_growth_scale(s->agents[0].growth), ox, oz, trials);
    static const int KINDS[3] = {CRET_CR_MOUSE, CRET_CR_LIZARD, CRET_CR_BADGER};
    printf("| fps | prey | approach | style | caught | bolted | escaped | mean time to catch |\n|---|---|---|---|---|---|---|---|\n");
    for (int f = 0; f < nfps; f++) for (int ki = 0; ki < 3; ki++) for (int side = 0; side < 3; side++) for (int st = 0; st < ST_N; st++) {
        Tally T = {0}; int esc0 = s->stats.critters.escaped;
        for (int n = 0; n < trials; n++) {
            uint32_t seed = (uint32_t)(n * 7919 + ki * 104729 + side * 15485863 + st * 32452843);
            CretAgent* a = &s->agents[0]; a->alive = true;
            bool bolted; float tc = trial(s, KINDS[ki], side, st, fps_list[f], seed, ox, oz, &bolted);
            T.tries++; T.bolted += bolted;
            if (tc >= 0) { T.caught++; T.t_sum += tc; }
            if (!s->agents[0].alive) { cret_sim_respawn(s, 0); s->agents[0].growth = 0.35f; cret_apply_growth(&s->agents[0]); }
        }
        char mt[32] = "-"; if (T.caught) snprintf(mt, sizeof mt, "%.1f s", T.t_sum / T.caught);
        printf("| %.0f | %s | %s | %s | %d%% (%d/%d) | %d%% | %d%% | %s |\n", fps_list[f], CRET_CRITTER_KINDS[KINDS[ki]].name, SIDE[side], STYLE[st],
               100 * T.caught / T.tries, T.caught, T.tries, 100 * T.bolted / T.tries, 100 * (s->stats.critters.escaped - esc0) / T.tries, mt);
    }
    cret_sim_close(s); free(s);
    return 0;
}
