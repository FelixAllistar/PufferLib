// Headless combat probe: how do fights feel to a person with a mouse and a
// keyboard? A scripted player with human limits fights or chases the game's
// bots in the pairings that matter, and every attack it throws is followed
// from the click to the blow, and every miss is given a reason.
//
// The player (one per scripted animal; a pack is several):
// - sees the world 0.2 s late (a reaction delay) and extrapolates what it
//   saw by its velocity, as a person tracks steady motion; a change of
//   direction is noticed 0.2 s late;
// - turns the camera toward the target with the mouse: at most 10 rad/s,
//   easing in over about 80 ms, with a wandering aim error of about 2.5
//   degrees; the attack aims where the camera looks (player_aim in
//   cretaceous.c) and the body turns toward the camera while a move key is
//   held (player_input), so standing still does not turn the body;
// - decides once a frame at 30 or 60 fps; presses wait for the next 30 Hz
//   tick, as in cretaceous.c; with --latency (default both 0 and 80 ms)
//   every decision reaches the game that much later;
// - clicks when the target, as seen, is within the bite's reach plus 0.3 m
//   of the jaws at the current aim, at most every 0.25 s, and not while its
//   own blow is visibly winding up;
// - closes at a sprint, at a trot within 3 m, and either keeps W held into
//   the target (style "press") or lets go of W once the target is in reach
//   and bites standing (style "stand"); a defending herbivore walks only to turn
//   toward its attacker (a stegosaur puts its tail to it and looks back);
//   a fleeing gallimimus runs from the predator at a sprint.
//
// Per pairing it prints: attacks thrown, attacks landed, swings knocked out
// before the blow (the attacker thrown or stunned), why the misses
// missed (passed: the target was alongside or behind, the attacker ran past
// it or circled; arc: in reach of another aim, the aim was off by so many
// degrees; moved: the blow would have landed where the target stood when
// the swing started; short: out of reach at every aim, by so many metres),
// clicks dropped (they reached the game while it could not attack, and it
// threw them away: nothing swung and nothing stayed queued), press to start
// and press to hit from the click the swing answered (the latest before it),
// the wait from the first click since the last swing (clicks in the
// cooldown) to the start, time spent closing and
// in reach, passes per minute and the share of time alongside, time to kill,
// wins, and for chases whether the prey was caught (first blow) or escaped
// (15 m further off than at the start), how long it took and how far the
// chaser ran.
//
//   make -C ocean/cretaceous combatprobe && ./build/cretaceous/combatprobe [--trials N] [--fps F] [--latency MS] [--style S] [--only NAME] [-v]
//   ./build/cretaceous/combatprobe --quick --check ocean/cretaceous/tests/combatprobe_budget.txt   # make test
//   ./build/cretaceous/combatprobe --quick --write ocean/cretaceous/tests/combatprobe_budget.txt   # after a deliberate change
// Runs from the repository root on the small test world (tests/test_util.h),
// on open ground: trees are walk-through (CRET_NO_TREE_COLLIDERS).
#include "../tests/test_util.h"
#include "../abilities.h"
#include <float.h>

#define REACT_TICKS 6                 // 0.2 s reaction delay
#define HIST 16                       // ticks of history kept (>= REACT_TICKS + 1)
#define MAXA 8                        // agents in a trial
#define CAM_RATE 10.0f                // rad/s: fastest mouse turn
#define CAM_K 12.0f                   // 1/s: how fast the camera settles on the target
#define AIM_SIGMA 0.045f              // rad: wandering aim error (about 2.5 degrees)
#define CLICK_GAP 0.25f               // s between deliberate clicks
#define CLICK_MARGIN 0.3f             // m: clicks when the target looks this close to reach
#define ESCAPE_M 15.0f                // a chase is lost once the prey is this much further off than at the start

static float wrapf(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }

enum { MISS_PASSED, MISS_ARC, MISS_MOVED, MISS_SELF, MISS_SHORT, MISS_OTHER, MISS_KINDS };
static const char* const MISS_NAME[MISS_KINDS] = {"passed", "arc", "moved", "self", "short", "other"};
enum { MODE_ATTACK, MODE_DEFEND, MODE_FLEE };

typedef struct Pairing {
    const char* name;
    int sp; float g; int n;           // the scripted players: species, growth, how many (a pack shares a group)
    int osp; float og; int on;        // the bots they meet
    int mode;                         // MODE_*: how the players play
    bool chase;                       // a chase: caught (first blow) or escaped
    float dist, limit;                // start distance (m), trial length (s)
} Pairing;

static const Pairing PAIRS[] = {
    {"raptor>galli",     CRET_SP_RAPTOR, 1, 1, CRET_SP_GALLI,      1.0f, 1, MODE_ATTACK, true,  25, 60},
    {"raptor>pachy",     CRET_SP_RAPTOR, 1, 1, CRET_SP_PACHY,      1.0f, 1, MODE_ATTACK, false, 14, 60},
    {"raptor4>stego",    CRET_SP_RAPTOR, 1, 4, CRET_SP_STEGO,      0.6f, 1, MODE_ATTACK, false, 16, 90},
    {"trex>raptor",      CRET_SP_TREX,   1, 1, CRET_SP_RAPTOR,     1.0f, 1, MODE_ATTACK, false, 14, 60},
    {"trex>stego",       CRET_SP_TREX,   1, 1, CRET_SP_STEGO,      1.0f, 1, MODE_ATTACK, false, 18, 60},
    {"trex>pachy",       CRET_SP_TREX,   1, 1, CRET_SP_PACHY,      1.0f, 1, MODE_ATTACK, true,  16, 60},
    {"trex>sauro",       CRET_SP_TREX,   1, 1, CRET_SP_SAUROPELTA, 1.0f, 1, MODE_ATTACK, false, 16, 60},
    {"trex>galli",       CRET_SP_TREX,   1, 1, CRET_SP_GALLI,      1.0f, 1, MODE_ATTACK, true,  20, 60},
    {"stego<trex",       CRET_SP_STEGO,  1, 1, CRET_SP_TREX,       1.0f, 1, MODE_DEFEND, false, 25, 60},
    {"stego<raptor3",    CRET_SP_STEGO,  0.6f, 1, CRET_SP_RAPTOR,  1.0f, 3, MODE_DEFEND, false, 25, 60},
    {"pachy<raptor",     CRET_SP_PACHY,  1, 1, CRET_SP_RAPTOR,     1.0f, 1, MODE_DEFEND, false, 25, 60},
    {"sauro<trex",       CRET_SP_SAUROPELTA, 1, 1, CRET_SP_TREX,   1.0f, 1, MODE_DEFEND, false, 25, 60},
    {"galli0.45<raptor", CRET_SP_GALLI,  0.45f, 1, CRET_SP_RAPTOR, 1.0f, 1, MODE_FLEE,   true,  25, 60},
    {"raptor=raptor",    CRET_SP_RAPTOR, 1, 1, CRET_SP_RAPTOR,     1.0f, 1, MODE_ATTACK, false, 12, 60},
    {"pachy=pachy",      CRET_SP_PACHY,  1, 1, CRET_SP_PACHY,      1.0f, 1, MODE_ATTACK, false, 12, 60},
    {"trex=trex",        CRET_SP_TREX,   1, 1, CRET_SP_TREX,       1.0f, 1, MODE_ATTACK, false, 16, 60},
};
#define NPAIRS ((int)(sizeof PAIRS / sizeof PAIRS[0]))

// Totals for one pairing (over every trial and setting) or one setting.
typedef struct Tally {
    int trials, presses, dropped, thrown, landed, interrupted, miss[MISS_KINDS];
    double miss_amt[MISS_KINDS];           // arc: degrees off; moved: m the target moved; self, short: m short at the best aim
    double p2s, p2h; int n_p2s, n_p2h;     // press to start, press to hit (s), from the click the swing answered (the latest)
    double wait; int n_wait;               // from the first click since the last swing to the start (clicks in the cooldown)
    int late;                              // attacks that started > 0.1 s after the press reached the game
    double t_close, t_reach, t_beside, t_total; int passes;
    double ttk[64]; int nttk;              // time to kill (s), first 64
    int wins, losses, draws;
    int caught, escaped, undecided; double t_decided, run_m; int n_decided;
} Tally;

// What the player saw of one agent on one tick.
typedef struct Seen { float x, z, yaw, vx, vz; bool alive; } Seen;

typedef struct Player {
    int slot; uint32_t rng;
    float cam, noise;                      // camera yaw; wandering aim error
    double last_click;
    // Decisions waiting out the input latency (a ring of the last frames).
    struct { double t; float cam, f, s; bool walk, sprint, press; } q[32]; int qn;
    bool eff_press_seen[32];
    double pending;                        // decision time of the first click not yet turned into an attack (-1: none)
    double last;                           // decision time of the latest such click
    double swing_click;                    // decision time of the click the current swing answers (-1: none)
    float run0x, run0z; double run_m;      // distance run
    Seen start_target; int start_target_i; // the target where it stood when the swing started
    float start_gap;                       // its gap from the jaws then (m; <= 0: in reach)
    bool fleeing;                          // MODE_FLEE: has started running
} Player;

static Seen hist[HIST][MAXA];
static int hist_n;

static float mass_of(const CretAgent* a) { return cret_mass(a); }
static float reach_of(const CretAgent* a) { return CRET_SPECIES_TABLE[a->species].reach * cret_growth_scale(a->growth); }
static float len_of(const CretAgent* a) { return CRET_SPECIES_TABLE[a->species].length * cret_growth_scale(a->growth); }
static float gauss(uint32_t* r) { float u = t_frand(r) + 1e-7f, v = t_frand(r); return sqrtf(-2 * logf(u)) * cosf(6.2831853f * v); }

// The camera aim of player_aim (cretaceous.c): the neck arc in front, or a
// stegosaur's tail behind, fading to straight back while the camera looks ahead.
static float player_aim(float cam, const CretAgent* a) {
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    float rel = wrapf(cam - a->yaw);
    if (!S->tail) return clampf(rel / S->neck_arc, -1, 1);
    float t = clampf((fabsf(rel) - 1.5707963f) / 0.5f, 0, 1); t = t * t * (3 - 2 * t);
    return clampf(wrapf(rel - 3.14159265f) / S->neck_arc, -1, 1) * t;
}

// The gap from agent a's jaws (or tail) at `aim` to the body of an agent seen at s.
static float gap_at(const CretAgent* a, float aim, const CretAgent* b, const Seen* s) {
    CretAgent q = *a; q.aim = aim; float px, pz; cret_strike_point(&q, &px, &pz);
    CretAgent v = *b; v.x = s->x; v.z = s->z; v.yaw = s->yaw;
    return cret_body_dist(&v, px, pz) - reach_of(a);
}
// The smallest gap over every aim across the neck arc, and the aim that gives it.
static float best_gap(const CretAgent* a, const CretAgent* b, const Seen* s, float* aim) {
    float best = FLT_MAX;
    for (int k = 0; k <= 40; k++) { float m = -1 + k * 0.05f, d = gap_at(a, m, b, s); if (d < best) { best = d; if (aim) *aim = m; } }
    return best;
}
static Seen seen_now(const CretAgent* b) { return (Seen){b->x, b->z, b->yaw, b->vx, b->vz, b->alive}; }

// A flat, dry, walkable spot with open ground round it, nearest the middle.
static bool find_spot(const CretSim* s, float* ox, float* oz) {
    const CretWorld* w = s->world; float S = w->size, best = 1e30f; bool found = false;
    for (float z = S * 0.2f; z < S * 0.8f; z += 12) for (float x = S * 0.2f; x < S * 0.8f; x += 12) {
        float h0 = cret_height(w, x, z); bool ok = true;
        for (int k = 0; k < 60 && ok; k++) {
            float a = k * 0.6283f + (k / 10) * 0.3f, r = (k / 10 + 1) * 8.0f, px = x + cosf(a) * r, pz = z + sinf(a) * r;
            ok = cret_walkable(w, px, pz) && cret_water(w, px, pz) < cret_height(w, px, pz) && fabsf(cret_height(w, px, pz) - h0) < 0.2f * r + 1;
        }
        float d = hypotf(x - S * 0.5f, z - S * 0.5f);
        if (ok && d < best) { best = d; *ox = x; *oz = z; found = true; }
    }
    return found;
}

static void place(CretSim* s, int i, int sp, float g, float x, float z, float yaw, int group, bool bot) {
    cret_sim_set_species(s, i, sp);
    CretAgent* a = &s->agents[i];
    a->growth = g; a->max_health = 0; cret_apply_growth(a); a->health = a->max_health;
    a->x = x; a->z = z; a->y = cret_height(s->world, x, z); a->yaw = yaw; a->vx = a->vy = a->vz = 0; a->speed = 0;
    a->stamina = 1; a->water = 1; a->food = bot && CRET_SPECIES_TABLE[sp].diet == CRET_DIET_CARNIVORE ? 0.5f : 0.9f;
    a->group = (uint8_t)group; a->bot_state = 0; a->bot_timer = 0; a->goal_x = x; a->goal_z = z;
    cret_sim_place(s, i);
}

// The nearest living enemy of player slot i as seen (pack mates are not enemies).
static int pick_target(const CretSim* s, int i, int n, const Seen* view) {
    const CretAgent* a = &s->agents[i]; int best = -1; float bd = FLT_MAX;
    for (int j = 0; j < n; j++) {
        if (j == i || !view[j].alive || (a->group && s->agents[j].group == a->group)) continue;
        float d = hypotf(view[j].x - a->x, view[j].z - a->z); if (d < bd) { bd = d; best = j; }
    }
    return best;
}

enum { STYLE_PRESS, STYLE_STAND, STYLES };
static const char* const STYLE_NAME[STYLES] = {"press", "stand"};
typedef struct Setting { float fps, latency; int style; } Setting;

// One trial. Players are slots 0..P->n-1, bots after them, so a player's blow
// resolves before the bots move on that tick (the miss analysis uses the
// state before the step).
static void trial(const Pairing* P, Setting st, uint32_t seed, float ox, float oz, Tally* T, bool verbose) {
    int np = P->n, n = P->n + P->on;
    CretConfig c = t_config(n); c.respawn_ticks = 1 << 28; c.hunger_seconds = c.thirst_seconds = 1e6f; c.seed = seed;
    CretSim* s = calloc(1, sizeof *s);
    if (cret_sim_init(s, c) != 0) { fprintf(stderr, "combatprobe: world failed\n"); exit(2); }
    uint32_t rng = seed * 2654435761u + 17;
    float ang = t_frand(&rng) * 6.2831853f;
    for (int k = 0; k < np; k++) {   // the players side by side, facing the bots
        float side = (k - 0.5f * (np - 1)) * 4.0f, px = ox - sinf(ang) * side, pz = oz + cosf(ang) * side;
        place(s, k, P->sp, P->g, px, pz, ang + (P->mode == MODE_FLEE ? 3.14159265f : 0), np > 1 ? 1 : 0, false);
    }
    for (int k = 0; k < P->on; k++) {
        float side = (k - 0.5f * (P->on - 1)) * 5.0f, bx = ox + cosf(ang) * P->dist - sinf(ang) * side, bz = oz + sinf(ang) * P->dist + cosf(ang) * side;
        place(s, np + k, P->osp, P->og, bx, bz, ang + (t_frand(&rng) - 0.5f) * 2.0f, P->on > 1 ? np + 1 : 0, true);
    }
    for (int k = 0; k < CRET_MAX_AGENTS; k++) s->prog[k].quest.kind = 0;
    uint8_t group0[MAXA]; for (int k = 0; k < n; k++) group0[k] = s->agents[k].group;
    Player pl[MAXA]; memset(pl, 0, sizeof pl);
    for (int k = 0; k < np; k++) {
        CretAgent* a = &s->agents[k];
        pl[k].slot = k; pl[k].rng = seed * 747796405u + k * 2891336453u + 1; pl[k].cam = a->yaw; pl[k].last_click = -1; pl[k].pending = pl[k].swing_click = -1;
        pl[k].run0x = a->x; pl[k].run0z = a->z; pl[k].start_target_i = -1;
    }
    for (int h = 0; h < HIST; h++) for (int j = 0; j < n; j++) hist[h][j] = seen_now(&s->agents[j]);
    hist_n = 0;
    CretAgent pre[MAXA]; CretInput in[MAXA];
    double clock = 0, acc = 0, frame = 1.0 / st.fps; int ticks = (int)(P->limit * CRET_TICK_HZ), t = 0;
    uint8_t press[MAXA] = {0};
    CretInput mine[MAXA]; memset(mine, 0, sizeof mine);
    float hp0 = 0; for (int k = np; k < n; k++) hp0 += s->agents[k].health;
    int outcome = 0; double decided = -1; bool first_blow = false;
    float prev_bear[MAXA]; for (int k = 0; k < MAXA; k++) prev_bear[k] = 0;
    while (t < ticks && !outcome) {
        // A frame: each player looks, moves the mouse and the keys, maybe clicks.
        for (int k = 0; k < np; k++) {
            Player* p = &pl[k]; CretAgent* a = &s->agents[k];
            if (!a->alive) continue;
            const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
            Seen view[MAXA];
            for (int j = 0; j < n; j++) {   // the world REACT_TICKS ago, carried on by its velocity then
                Seen v = hist[(hist_n + HIST - REACT_TICKS) % HIST][j];
                float lag = REACT_TICKS * CRET_DT; v.x += v.vx * lag; v.z += v.vz * lag; view[j] = v;
            }
            int tg = pick_target(s, k, n, view);
            float want = p->cam, f = 0, sd = 0; bool walk = false, sprint = false, click = false;
            if (tg >= 0) {
                const CretAgent* b = &s->agents[tg]; const Seen* v = &view[tg];
                float dx = v->x - a->x, dz = v->z - a->z, d = hypotf(dx, dz), bear = atan2f(dz, dx);
                float gap = gap_at(a, player_aim(p->cam, a), b, v);
                if (P->mode == MODE_FLEE) {
                    // Runs once the predator commits: within 18 m, or coming at more than a walk.
                    float closing = -(v->vx * dx + v->vz * dz) / fmaxf(d, 1e-3f);
                    if (p->fleeing || d < 18 || closing > b->walk_speed * 1.3f) {
                        p->fleeing = true; want = bear + 3.14159265f; f = 1; sprint = true;
                        if (d > 8) want = cret_avoid_heading(s, k, want, 20);
                    }
                } else if (P->mode == MODE_ATTACK) {
                    want = bear; f = 1; sprint = gap > 3.0f;
                    if (d > 8 + len_of(a)) want = cret_avoid_heading(s, k, want, d);
                    if (st.style == STYLE_STAND && gap < 0) f = 0;
                } else {   // defend: face the attacker (a stegosaur its tail), walking only to turn
                    float face = S->tail ? bear + 3.14159265f : bear, off = wrapf(face - a->yaw);
                    want = bear;
                    if (fabsf(off) > 0.5f * S->neck_arc) {
                        if (S->tail) want = face;   // looks away to turn the tail round, then back at the attacker
                        f = 1; walk = true;
                    }
                }
                // Click: the target looks within reach (plus a margin) of the jaws at
                // this aim, the last click was a moment ago and no blow is winding up.
                bool winding = a->act == CRET_ACT_ATTACK && a->act_ticks < (int)(S->hit_time * CRET_TICK_HZ);
                if (P->mode != MODE_FLEE && v->alive && gap < CLICK_MARGIN && clock - p->last_click >= CLICK_GAP && !winding) click = true;
            }
            // The mouse: eases toward the wanted yaw plus a wandering error, never faster than CAM_RATE.
            p->noise += (-p->noise * frame / 0.3f) + AIM_SIGMA * sqrtf(2 * frame / 0.3f) * gauss(&p->rng);
            float err = wrapf(want + p->noise - p->cam), step = err * (1 - expf(-CAM_K * (float)frame));
            p->cam = wrapf(p->cam + clampf(step, -CAM_RATE * (float)frame, CAM_RATE * (float)frame));
            if (click) { p->last_click = clock; if (p->pending < 0) p->pending = clock; p->last = clock; T->presses++; }
            int qi = p->qn++ % 32;
            p->q[qi].t = clock; p->q[qi].cam = p->cam; p->q[qi].f = f; p->q[qi].s = sd; p->q[qi].walk = walk; p->q[qi].sprint = sprint; p->q[qi].press = click;
            p->eff_press_seen[qi] = false;
            // What reaches the game now: the newest decision at least `latency` old; its
            // clicks (and any skipped over) are latched for the next tick.
            int eff = -1;
            for (int b = 0; b < 32 && b < p->qn; b++) {
                int j = (p->qn - 1 - b) % 32;
                if (p->q[j].t <= clock - st.latency / 1000.0 + 1e-9) { if (eff < 0) eff = j; if (p->q[j].press && !p->eff_press_seen[j]) { p->eff_press_seen[j] = true; press[k] = CRET_ACT_ATTACK; } }
            }
            CretInput m = {0};
            if (eff >= 0) {   // player_input (cretaceous.c)
                float cam = p->q[eff].cam, ff = p->q[eff].f, ss = p->q[eff].s;
                if (ff != 0 || ss != 0) {
                    float wy = cam + atan2f(ss, ff), diff = wrapf(wy - a->yaw);
                    m.turn = clampf(diff * 2.5f, -1, 1);
                    m.forward = (p->q[eff].walk ? 0.5f : 1.0f) * clampf(1.2f - fabsf(diff) / 1.6f, 0.15f, 1.0f);
                }
                m.sprint = p->q[eff].sprint; m.aim = player_aim(cam, a);
            }
            mine[k] = m;
        }
        acc += frame; clock += frame;
        while (acc >= CRET_DT - 1e-9 && t < ticks && !outcome) {
            acc -= CRET_DT;
            for (int k = 0; k < n; k++) in[k] = k < np ? mine[k] : cret_bot_input(s, k);
            for (int k = 0; k < np; k++) if (press[k]) in[k].action = press[k];
            memcpy(pre, s->agents, sizeof(CretAgent) * n);
            uint32_t h0 = s->hit_count;
            cret_sim_step(s, in); t++;
            // This tick's blows: which players landed one; the first blow of a chase.
            bool landed_now[MAXA] = {0};
            for (uint32_t h = h0; h < s->hit_count; h++) {
                const CretHit* e = &s->hits[h % CRET_HIT_LOG];
                if (e->kind == CRET_HIT_THORNS) continue;
                if (e->attacker < np) landed_now[e->attacker] = true;
                if (P->mode == MODE_FLEE ? e->victim < np : e->attacker < np) first_blow = true;
            }
            for (int k = 0; k < np; k++) press[k] = 0;
            // Groups stay as set up: a bot's call would otherwise take a lone player of
            // its species into its group, and neither would bite the other.
            for (int k = 0; k < n; k++) s->agents[k].group = group0[k];
            hist_n++; for (int j = 0; j < n; j++) hist[hist_n % HIST][j] = seen_now(&s->agents[j]);
            for (int k = 0; k < np; k++) {
                Player* p = &pl[k]; CretAgent* a = &s->agents[k]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
                if (!pre[k].alive) continue;
                int hit = (int)(S->hit_time * CRET_TICK_HZ);
                Seen pv[MAXA]; for (int j = 0; j < n; j++) pv[j] = seen_now(&pre[j]);
                int tg = pick_target(s, k, n, pv);
                // A swing started: the click it answers.
                // A swing that ended before its blow: knocked out of it (thrown or stunned).
                if (pre[k].act == CRET_ACT_ATTACK && pre[k].act_ticks + 1 < hit && !(a->act == CRET_ACT_ATTACK && a->act_ticks == pre[k].act_ticks + 1)) { T->interrupted++; p->swing_click = -1; }
                if (a->act == CRET_ACT_ATTACK && a->act_ticks == 0) {
                    T->thrown++;
                    if (p->pending >= 0) {
                        double d = clock - p->last; T->p2s += d; T->n_p2s++; T->wait += clock - p->pending; T->n_wait++;
                        if (d > st.latency / 1000.0 + CRET_DT + 0.1) T->late++;
                    }
                    p->start_target_i = tg;
                    if (tg >= 0) { p->start_target = pv[tg]; CretAgent A = pre[k]; A.yaw = a->yaw; p->start_gap = gap_at(&A, a->aim, &pre[tg], &pv[tg]); }
                    p->swing_click = p->pending >= 0 ? p->last : -1; p->pending = -1;
                }
                // The blow resolved this tick: landed (a hit logged by this
                // player) or missed, and why.
                if (a->act == CRET_ACT_ATTACK && a->act_ticks == hit) {
                    if (landed_now[k]) { T->landed++; if (p->swing_click >= 0) { T->p2h += clock - p->swing_click; T->n_p2h++; } }
                    else if (tg >= 0) {
                        CretAgent A = pre[k]; A.yaw = a->yaw; A.aim = a->aim;
                        const CretAgent* b = &pre[tg]; Seen bs = pv[tg];
                        float need, g = gap_at(&A, A.aim, b, &bs), gb = best_gap(&A, b, &bs, &need);
                        float L = len_of(&A), cy = cosf(A.yaw), sy = sinf(A.yaw), dx = bs.x - A.x, dz = bs.z - A.z;
                        float along = (dx * cy + dz * sy) * (S->tail ? -1 : 1), bear = wrapf(atan2f(dz, dx) - A.yaw - (S->tail ? 3.14159265f : 0));
                        int why; float amt = 0;
                        if (g <= 0) why = MISS_OTHER;
                        else if (gb <= 0) { why = MISS_ARC; amt = fabsf(need - A.aim) * S->neck_arc * 57.2958f; }
                        else if (fabsf(bear) > S->neck_arc + 0.35f && along < 0.18f * L + len_of(b) * 0.4f) why = MISS_PASSED;
                        else if (p->start_target_i != tg || p->start_gap > 0) { why = MISS_SHORT; amt = gb; }
                        else if (gap_at(&A, A.aim, b, &p->start_target) <= 0) { why = MISS_MOVED; amt = hypotf(bs.x - p->start_target.x, bs.z - p->start_target.z); }
                        else { why = MISS_SELF; amt = gb; }
                        T->miss[why]++; T->miss_amt[why] += amt;
                        if (verbose) printf("    miss %-6s t %5.2f gap %.2f best %.2f start gap %.2f bear %4.0f along %5.2f amt %.2f v %.1f tv %.1f\n", MISS_NAME[why], clock, g, gb,
                                            p->start_gap, bear * 57.3f, along, amt, pre[k].speed, b->speed);
                    }
                    p->swing_click = -1;
                }
                // Dropped: the click reached the game, nothing swings and nothing is queued.
                if (p->pending >= 0 && a->act != CRET_ACT_ATTACK && !a->buf_ticks && clock - p->pending > st.latency / 1000.0 + 2 * CRET_DT) { T->dropped++; p->pending = -1; }
                // Closing or in reach; passes; alongside.
                if (tg >= 0 && pre[tg].alive) {
                    const CretAgent* b = &pre[tg];
                    float gb = best_gap(a, b, &pv[tg], NULL), d = hypotf(b->x - a->x, b->z - a->z);
                    float bear = wrapf(atan2f(b->z - a->z, b->x - a->x) - a->yaw - (S->tail ? 3.14159265f : 0));
                    T->t_total += CRET_DT;
                    if (gb <= 0) T->t_reach += CRET_DT; else T->t_close += CRET_DT;
                    float near = 0.5f * (len_of(a) + len_of(b)) + reach_of(a);
                    if (gb > 0 && d < near) T->t_beside += CRET_DT;
                    if (fabsf(prev_bear[k]) < S->neck_arc && fabsf(bear) > 1.5707963f && d < near) T->passes++;
                    prev_bear[k] = bear;
                }
                p->run_m += hypotf(a->x - pre[k].x, a->z - pre[k].z);
            }
            if (verbose && t % CRET_TICK_HZ == 0) {
                printf("    t %4.1f", t * CRET_DT);
                for (int k = 0; k < n; k++) { const CretAgent* a = &s->agents[k]; printf("  [%d %.0f,%.0f v %.1f st %.2f hp %.0f act %d bot %d]", k, a->x, a->z, a->speed, a->stamina, a->health, a->act, a->bot_state); }
                printf("\n");
            }
            // Outcome.
            bool foes = false, mine_alive = false;
            for (int k = np; k < n; k++) foes |= s->agents[k].alive;
            for (int k = 0; k < np; k++) mine_alive |= s->agents[k].alive;
            if (!foes) outcome = 1; else if (!mine_alive) outcome = 2;
            if (P->chase && decided < 0) {
                float sep = FLT_MAX;
                for (int k = 0; k < np; k++) for (int j = np; j < n; j++) sep = fminf(sep, hypotf(s->agents[k].x - s->agents[j].x, s->agents[k].z - s->agents[j].z));
                if (first_blow) { decided = clock; T->caught++; outcome = 3; }
                else if (sep > P->dist + ESCAPE_M) { decided = clock; T->escaped++; outcome = 3; }
            }
        }
    }
    T->trials++;
    if (P->chase) {
        if (decided < 0) T->undecided++;
        else { T->t_decided += decided; T->n_decided++; for (int k = 0; k < np; k++) T->run_m += pl[k].run_m / np; }
    }
    if (outcome == 1) { T->wins++; if (T->nttk < 64) T->ttk[T->nttk++] = clock; }
    else if (outcome == 2) T->losses++;
    else if (!P->chase) T->draws++;
    if (verbose) {
        float hp = 0; for (int k = np; k < n; k++) hp += s->agents[k].alive ? s->agents[k].health : 0;
        printf("  %s %s fps %.0f lat %.0f seed %u: %s at %.1f s, foe hp %.0f%%\n", P->name, STYLE_NAME[st.style], st.fps, st.latency, seed,
               outcome == 1 ? "won" : outcome == 2 ? "lost" : outcome == 3 ? "decided" : "time", clock, 100 * hp / fmaxf(hp0, 1));
    }
    cret_sim_close(s); free(s);
}

static void add(Tally* a, const Tally* b) {
    a->trials += b->trials; a->presses += b->presses; a->dropped += b->dropped; a->thrown += b->thrown; a->landed += b->landed; a->interrupted += b->interrupted;
    for (int k = 0; k < MISS_KINDS; k++) { a->miss[k] += b->miss[k]; a->miss_amt[k] += b->miss_amt[k]; }
    a->p2s += b->p2s; a->p2h += b->p2h; a->n_p2s += b->n_p2s; a->n_p2h += b->n_p2h; a->late += b->late; a->wait += b->wait; a->n_wait += b->n_wait;
    a->t_close += b->t_close; a->t_reach += b->t_reach; a->t_beside += b->t_beside; a->t_total += b->t_total; a->passes += b->passes;
    for (int k = 0; k < b->nttk && a->nttk < 64; k++) a->ttk[a->nttk++] = b->ttk[k];
    a->wins += b->wins; a->losses += b->losses; a->draws += b->draws;
    a->caught += b->caught; a->escaped += b->escaped; a->undecided += b->undecided; a->t_decided += b->t_decided; a->run_m += b->run_m; a->n_decided += b->n_decided;
}
static int cmpd(const void* x, const void* y) { double a = *(const double*)x, b = *(const double*)y; return a < b ? -1 : a > b; }
static double median(const Tally* T) {
    if (!T->nttk) return -1;
    double v[64]; memcpy(v, T->ttk, sizeof(double) * T->nttk); qsort(v, T->nttk, sizeof(double), cmpd);
    return T->nttk % 2 ? v[T->nttk / 2] : 0.5 * (v[T->nttk / 2 - 1] + v[T->nttk / 2]);
}
static float landed_pct(const Tally* T) { return T->thrown ? 100.0f * T->landed / T->thrown : 0; }
static float p2h_ms(const Tally* T) { return T->n_p2h ? 1000.0f * (float)(T->p2h / T->n_p2h) : 0; }

int main(int argc, char** argv) {
    int trials = 6; bool quick = false, verbose = false; const char* only = NULL; const char* check = NULL; const char* write = NULL;
    float fps_one = 0, lat_one = -1; int style_one = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--trials") && i + 1 < argc) trials = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fps") && i + 1 < argc) fps_one = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--latency") && i + 1 < argc) lat_one = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
        else if (!strcmp(argv[i], "--style") && i + 1 < argc) { i++; style_one = !strcmp(argv[i], "stand") ? STYLE_STAND : STYLE_PRESS; }
        else if (!strcmp(argv[i], "--check") && i + 1 < argc) check = argv[++i];
        else if (!strcmp(argv[i], "--write") && i + 1 < argc) write = argv[++i];
        else if (!strcmp(argv[i], "--quick")) quick = true;
        else if (!strcmp(argv[i], "-v")) verbose = true;
        else { fprintf(stderr, "usage: combatprobe [--trials N] [--fps F] [--latency MS] [--only NAME] [--style press|stand] [--quick] [--check FILE] [--write FILE] [-v]\n"); return 2; }
    }
    if (quick) trials = 3;
    double t0 = t_now();
    // Trees are walk-through: the test world is wooded nearly everywhere, and a
    // trunk in the way measures the scripted player's path finding, not the fight.
    setenv("CRET_NO_TREE_COLLIDERS", "1", 1);
    t_hold_world();
    float ox, oz;
    {
        CretSim* s = calloc(1, sizeof *s); CretConfig c = t_config(1);
        if (cret_sim_init(s, c) != 0 || !find_spot(s, &ox, &oz)) { fprintf(stderr, "combatprobe: no open flat ground in the test world\n"); return 2; }
        cret_sim_close(s); free(s);
    }
    Setting sets[8]; int nsets = 0;
    for (int y = 0; y < STYLES; y++) for (int f = 0; f < 2; f++) for (int l = 0; l < 2; l++) {
        Setting e = {f ? 60.0f : 30.0f, l ? 80.0f : 0.0f, y};
        if ((fps_one <= 0 || e.fps == fps_one) && (lat_one < 0 || e.latency == lat_one) && (style_one < 0 || e.style == style_one)) sets[nsets++] = e;
    }
    Tally all[NPAIRS], byset[8], bystyle[NPAIRS][STYLES]; memset(all, 0, sizeof all); memset(byset, 0, sizeof byset); memset(bystyle, 0, sizeof bystyle);
    printf("combatprobe: %d trials per pairing and setting (fps x input latency), reaction 0.2 s, spot %.0f %.0f\n\n", trials, ox, oz);
    for (int p = 0; p < NPAIRS; p++) {
        if (only && !strstr(PAIRS[p].name, only)) continue;
        for (int k = 0; k < nsets; k++) for (int r = 0; r < trials; r++) {
            Tally T = {0};
            trial(&PAIRS[p], sets[k], (uint32_t)(p * 1000 + r * 7 + 1), ox, oz, &T, verbose);
            add(&all[p], &T); add(&byset[k], &T); add(&bystyle[p][sets[k].style], &T);
        }
    }
    printf("| pairing | trials | thrown | landed | knocked out | passed | arc (deg) | moved (m) | self (m) | short (m) | dropped | late | press>start | first click>start | press>hit | closing | in reach | alongside | passes/min | TTK | won/lost/draw | chase: caught/escaped/open, s, m run |\n");
    printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (int p = 0; p < NPAIRS; p++) {
        const Tally* T = &all[p]; if (!T->trials) continue;
        printf("| %s | %d | %d | %.0f%% | %d | %d | %d (%.0f) | %d (%.1f) | %d (%.1f) | %d (%.1f) | %d of %d | %d | %.0f ms | %.0f ms | %.0f ms | %.1f s | %.1f s | %.0f%% | %.1f | %.1f s | %d/%d/%d |",
               PAIRS[p].name, T->trials, T->thrown, landed_pct(T), T->interrupted, T->miss[MISS_PASSED],
               T->miss[MISS_ARC], T->miss[MISS_ARC] ? T->miss_amt[MISS_ARC] / T->miss[MISS_ARC] : 0,
               T->miss[MISS_MOVED], T->miss[MISS_MOVED] ? T->miss_amt[MISS_MOVED] / T->miss[MISS_MOVED] : 0,
               T->miss[MISS_SELF], T->miss[MISS_SELF] ? T->miss_amt[MISS_SELF] / T->miss[MISS_SELF] : 0,
               T->miss[MISS_SHORT], T->miss[MISS_SHORT] ? T->miss_amt[MISS_SHORT] / T->miss[MISS_SHORT] : 0,
               T->dropped, T->presses, T->late,
               T->n_p2s ? 1000 * T->p2s / T->n_p2s : 0, T->n_wait ? 1000 * T->wait / T->n_wait : 0, p2h_ms(T),
               T->t_close / T->trials, T->t_reach / T->trials, T->t_total > 0 ? 100 * T->t_beside / T->t_total : 0,
               T->t_total > 0 ? 60 * T->passes / T->t_total : 0, median(T), T->wins, T->losses, T->draws);
        if (PAIRS[p].chase) printf(" %d/%d/%d, %.1f s, %.0f m |\n", T->caught, T->escaped, T->undecided, T->n_decided ? T->t_decided / T->n_decided : 0, T->n_decided ? T->run_m / T->n_decided : 0);
        else printf(" |\n");
    }
    printf("\nBy style (landed, press>hit, time to kill):\n| pairing | press | stand |\n|---|---|---|\n");
    for (int p = 0; p < NPAIRS; p++) {
        if (!all[p].trials) continue;
        printf("| %s |", PAIRS[p].name);
        for (int y = 0; y < STYLES; y++) { const Tally* T = &bystyle[p][y]; printf(" %.0f%%, %.0f ms, %.1f s |", landed_pct(T), p2h_ms(T), median(T)); }
        printf("\n");
    }
    printf("\nBy setting (all pairings):\n| style | fps | latency | thrown | landed | dropped | press>start | first click>start | press>hit |\n|---|---|---|---|---|---|---|---|---|\n");
    for (int k = 0; k < nsets; k++) {
        const Tally* T = &byset[k];
        printf("| %s | %.0f | %.0f ms | %d | %.0f%% | %d of %d | %.0f ms | %.0f ms | %.0f ms |\n", STYLE_NAME[sets[k].style], sets[k].fps, sets[k].latency, T->thrown, landed_pct(T), T->dropped, T->presses,
               T->n_p2s ? 1000 * T->p2s / T->n_p2s : 0, T->n_wait ? 1000 * T->wait / T->n_wait : 0, p2h_ms(T));
    }
    // The regression budget: per pairing, the share of attacks that land must
    // not fall more than 5 points below the stored value, nor press to hit
    // rise more than 40 ms above it.
    if (write) {
        FILE* f = fopen(write, "w"); if (!f) { perror(write); return 2; }
        fprintf(f, "# combatprobe --quick budget: pairing, landed %% (floor = value - 5), press to hit ms (ceiling = value + 40). Written by --write.\n");
        for (int p = 0; p < NPAIRS; p++) if (all[p].trials && all[p].thrown) fprintf(f, "%s %.1f %.0f\n", PAIRS[p].name, landed_pct(&all[p]), p2h_ms(&all[p]));
        fclose(f); printf("\nwrote %s\n", write);
    }
    int fail = 0;
    if (check) {
        FILE* f = fopen(check, "r"); if (!f) { perror(check); return 2; }
        char line[256], name[64]; float lp, ph;
        while (fgets(line, sizeof line, f)) {
            if (line[0] == '#' || sscanf(line, "%63s %f %f", name, &lp, &ph) != 3) continue;
            for (int p = 0; p < NPAIRS; p++) {
                if (strcmp(PAIRS[p].name, name) || !all[p].trials) continue;
                float l = landed_pct(&all[p]), h = p2h_ms(&all[p]);
                if (l < lp - 5) { printf("  FAIL %s: %.1f%% of attacks land, budget %.1f%%\n", name, l, lp); fail++; }
                if (h > ph + 40) { printf("  FAIL %s: press to hit %.0f ms, budget %.0f ms\n", name, h, ph); fail++; }
            }
        }
        fclose(f);
    }
    fprintf(stderr, "combatprobe: %s (%.1f s)\n", fail ? "FAILED" : "ok", t_now() - t0);
    return fail ? 1 : 0;
}
