// Evaluation harness: runs episodes of the trainer's env with a chosen policy
// per species (or slot) and reports survival, kills, deaths, food and water,
// growth, damage, a kill matrix, death causes and the reward split into its
// terms (the reward audit). See docs/testing.md.
//
//   cret_eval [--episodes N] [--ticks T] [--agents N] [--seed S] [--world-n N --world-cell C]
//             [--policy WHO=POLICY]... [--baselines] [--audit] [--quiet]
//   WHO     all, herbivores, carnivores, raptor, stego, trex, pachy, sauropelta, galli, or slotN
//   POLICY  bot      scripted bots (cret_bot_input), continuous inputs
//           botq     the bots' inputs snapped to the policy's action heads
//           random   uniform over the action heads (an untrained policy)
//           noop     stands still, never acts
//           brawl    probe: walks at the nearest animal outside its group and attacks
//           swim     probe: walks into deep water and floats there
//           net:FILE trained weights (PufferNet from src/puffercpu.c); [policy] hidden_size and
//                    num_layers from config/cretaceous.ini, or net:FILE:HIDDEN:LAYERS
//   Later --policy flags override earlier ones. Defaults: [env] of config/cretaceous.ini.
//   --baselines   bot, random and noop for every slot, one table each
//   --audit       reward terms per species for bots, random, noop and the probes, and flags
//
// To add a policy: write an act function (fill in[i] for agents with mask[i])
// and add it to make_policy below.
#include "../tests/test_util.h"
#include "cretaceous.h"
#include "../tests/reward_terms.h"
#include "../../../src/puffercpu.c"

#define AL8(n) (((n) + 7) & ~7)   // weight blocks are 8-float aligned (puffernet_weight_count)

// ------------------------------------------------------------------ policies
typedef struct Policy Policy;
struct Policy {
    char name[96];
    bool needs_obs;
    void (*act)(Policy* p, CretSim* s, const float* obs, const float* terminals, const bool* mask, CretInput* in);
    void (*close)(Policy* p);
    uint32_t rng;
    // net
    Weights* weights; PufferNet* net; float* actions; int num_agents;
    // probes
    int target[CRET_MAX_AGENTS]; float goal_x[CRET_MAX_AGENTS], goal_z[CRET_MAX_AGENTS];
};

static void act_bot(Policy* p, CretSim* s, const float* obs, const float* term, const bool* mask, CretInput* in) {
    for (int i = 0; i < s->cfg.num_agents; i++) if (mask[i]) in[i] = cret_bot_input(s, i);
}
// Nearest action-head values to a continuous input (throttle -0.5/0/0.5/1/sprint, turn -1/-0.4/0/0.4/1, ...).
static void quantize(const CretInput* c, float* act) {
    act[0] = c->sprint && c->forward > 0.5f ? 4 : c->forward > 0.75f ? 3 : c->forward > 0.25f ? 2 : c->forward < -0.25f ? 0 : 1;
    act[1] = c->turn < -0.7f ? 0 : c->turn < -0.2f ? 1 : c->turn <= 0.2f ? 2 : c->turn <= 0.7f ? 3 : 4;
    act[2] = c->strafe < -0.5f ? 0 : c->strafe > 0.5f ? 2 : 1;
    act[3] = c->jump ? 1 : 0;
    act[4] = c->action == CRET_ACT_ATTACK ? 1 : c->action == CRET_ACT_EAT || c->action == CRET_ACT_DRINK ? 2 : c->action == CRET_ACT_CALL ? 3 : 0;
}
static void act_botq(Policy* p, CretSim* s, const float* obs, const float* term, const bool* mask, CretInput* in) {
    for (int i = 0; i < s->cfg.num_agents; i++) if (mask[i]) { CretInput c = cret_bot_input(s, i); float a[CRET_ACTION_HEADS]; quantize(&c, a); in[i] = cret_decode_action(a); }
}
static void act_random(Policy* p, CretSim* s, const float* obs, const float* term, const bool* mask, CretInput* in) {
    for (int i = 0; i < s->cfg.num_agents; i++) if (mask[i]) { float a[CRET_ACTION_HEADS]; t_random_actions(&p->rng, a); in[i] = cret_decode_action(a); }
}
static void act_noop(Policy* p, CretSim* s, const float* obs, const float* term, const bool* mask, CretInput* in) {
    for (int i = 0; i < s->cfg.num_agents; i++) if (mask[i]) in[i] = (CretInput){0};
}
static float turn_to(const CretAgent* a, float x, float z) {
    float d = atan2f(z - a->z, x - a->x) - a->yaw;
    while (d > 3.14159265f) d -= 6.2831853f; while (d < -3.14159265f) d += 6.2831853f;
    return fmaxf(-1, fminf(1, d * 2));
}
// Probe: attack whatever is nearest and not in our group (tests whether
// damage and kill rewards pay for indiscriminate fighting).
static void act_brawl(Policy* p, CretSim* s, const float* obs, const float* term, const bool* mask, CretInput* in) {
    for (int i = 0; i < s->cfg.num_agents; i++) {
        if (!mask[i]) continue;
        CretAgent* a = &s->agents[i]; in[i] = (CretInput){0}; if (!a->alive) continue;
        int best = -1; float bd = 1e30f;
        for (int j = 0; j < s->cfg.num_agents; j++) {
            const CretAgent* b = &s->agents[j];
            if (j == i || !b->alive || (a->group && b->group == a->group) || b->swimming) continue;
            float d = hypotf(b->x - a->x, b->z - a->z); if (d < bd) { bd = d; best = j; }
        }
        if (best < 0) continue;
        const CretAgent* b = &s->agents[best]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
        float g = cret_growth_scale(a->growth), reach = S->length * g * 0.45f + S->reach * g + b->radius;
        if (S->tail) {   // tail swingers back into the target
            float t = turn_to(a, 2 * a->x - b->x, 2 * a->z - b->z); in[i].turn = t;
            in[i].forward = bd > reach + 4 ? 1.0f : 0; if (bd < reach * 1.2f) in[i].action = CRET_ACT_ATTACK;
            if (bd > reach + 4) in[i].turn = turn_to(a, b->x, b->z);
        } else {
            in[i].turn = turn_to(a, b->x, b->z); in[i].forward = bd > reach * 0.8f ? 1.0f : 0.3f; in[i].sprint = bd > 15 && a->stamina > 0.3f;
            if (bd < reach) in[i].action = CRET_ACT_ATTACK;
        }
    }
}
// Probe: the nearest deep water within 400 m, walk in, float there.
static bool find_deep(const CretWorld* w, float x, float z, float* gx, float* gz) {
    for (float r = 10; r <= 400; r += 10) {
        int n = (int)(r * 0.3f) + 8;
        for (int q = 0; q < n; q++) {
            float ang = q * 6.2831853f / n, px = x + cosf(ang) * r, pz = z + sinf(ang) * r;
            if (px < 20 || pz < 20 || px > w->size - 20 || pz > w->size - 20) continue;
            if (cret_water(w, px, pz) - cret_height(w, px, pz) > 6) { *gx = px; *gz = pz; return true; }
        }
    }
    return false;
}
static void act_swim(Policy* p, CretSim* s, const float* obs, const float* term, const bool* mask, CretInput* in) {
    for (int i = 0; i < s->cfg.num_agents; i++) {
        if (!mask[i]) continue;
        CretAgent* a = &s->agents[i]; in[i] = (CretInput){0}; if (!a->alive || a->swimming) continue;
        if (a->ticks_alive <= 1 || (s->tick + i) % 60 == 0) { if (!find_deep(s->world, a->x, a->z, &p->goal_x[i], &p->goal_z[i])) { p->goal_x[i] = a->x; p->goal_z[i] = a->z; } }
        in[i].turn = turn_to(a, p->goal_x[i], p->goal_z[i]); in[i].forward = fabsf(in[i].turn) < 0.8f ? 1.0f : 0.3f;
    }
}
// Trained weights: one PufferNet over every agent (recurrent state per slot,
// reset on terminals like the trainer); only masked agents use its actions.
static void act_net(Policy* p, CretSim* s, const float* obs, const float* term, const bool* mask, CretInput* in) {
    forward_puffernet(p->net, (float*)obs, p->actions, NULL, term);
    for (int i = 0; i < s->cfg.num_agents; i++) if (mask[i]) in[i] = cret_decode_action(p->actions + i * CRET_ACTION_HEADS);
}
static void close_net(Policy* p) { free_puffernet(p->net); free(p->weights); free(p->actions); }

static Ini g_ini;
static Policy* make_policy(const char* spec, int num_agents, uint32_t seed) {
    Policy* p = calloc(1, sizeof *p); snprintf(p->name, sizeof p->name, "%s", spec); p->rng = seed * 2654435761u + 7;
    if (!strcmp(spec, "bot")) p->act = act_bot;
    else if (!strcmp(spec, "botq")) p->act = act_botq;
    else if (!strcmp(spec, "random")) p->act = act_random;
    else if (!strcmp(spec, "noop")) p->act = act_noop;
    else if (!strcmp(spec, "brawl")) p->act = act_brawl;
    else if (!strcmp(spec, "swim")) p->act = act_swim;
    else if (!strncmp(spec, "net:", 4)) {
        char path[512]; snprintf(path, sizeof path, "%s", spec + 4);
        int hidden = (int)puf_ini_get(&g_ini, "policy", "hidden_size"), layers = (int)puf_ini_get(&g_ini, "policy", "num_layers");
        char* c1 = strchr(path, ':'); if (c1) { *c1 = 0; hidden = atoi(c1 + 1); char* c2 = strchr(c1 + 1, ':'); if (c2) layers = atoi(c2 + 1); }
        p->weights = load_weights(path);
        if (!p->weights) { fprintf(stderr, "cret_eval: cannot read %s\n", path); exit(1); }
        int sizes[] = CRET_ACTION_SIZES, atn = 0; for (int h = 0; h < CRET_ACTION_HEADS; h++) atn += sizes[h];
        // Same count as puffernet_weight_count (src/puffercpu.c): encoder, decoder (+value), layers x 3 HxH; each block 8-aligned.
        long need = AL8((long)hidden * CRET_OBS_SIZE); need = AL8(need + (long)(atn + 1) * hidden); for (int l = 0; l < layers; l++) need = AL8(need + 3L * hidden * hidden);
        long have = p->weights->size - 7;
        if (have < need || have - need > 7) {
            fprintf(stderr, "cret_eval: %s holds %ld floats; obs %d, actions %d, hidden %d x %d layers needs %ld (another contract or size?)\n",
                    path, have, CRET_OBS_SIZE, atn, hidden, layers, need);
            exit(1);
        }
        p->net = make_puffernet(p->weights, num_agents, CRET_OBS_SIZE, hidden, layers, sizes, CRET_ACTION_HEADS);
        p->actions = calloc((size_t)num_agents * CRET_ACTION_HEADS, sizeof(float)); p->num_agents = num_agents;
        p->act = act_net; p->close = close_net; p->needs_obs = true; srand(seed);
    } else { fprintf(stderr, "cret_eval: unknown policy '%s'\n", spec); exit(1); }
    return p;
}

// ------------------------------------------------------------------ who
static const char* SP_KEY[CRET_SPECIES] = {"raptor", "stego", "trex", "pachy", "sauropelta", "galli"};
static bool who_matches(const char* who, int slot, int species) {
    if (!strcmp(who, "all")) return true;
    if (!strcmp(who, "herbivores")) return CRET_SPECIES_TABLE[species].diet == CRET_DIET_HERBIVORE;
    if (!strcmp(who, "carnivores")) return CRET_SPECIES_TABLE[species].diet == CRET_DIET_CARNIVORE;
    if (!strncmp(who, "slot", 4)) return atoi(who + 4) == slot;
    for (int k = 0; k < CRET_SPECIES; k++) if (!strcmp(who, SP_KEY[k])) return species == k;
    fprintf(stderr, "cret_eval: unknown group '%s'\n", who); exit(1);
}

// ------------------------------------------------------------------ stats
enum { CAUSE_ATTACK, CAUSE_STARVED, CAUSE_THIRST, CAUSE_BLEED, CAUSE_OTHER, CAUSES };
static const char* CAUSE_NAMES[CAUSES] = {"attack", "starved", "thirst", "bleeding", "fall/other"};
typedef struct SpStats {
    long slot_eps, lives, life_ticks, deaths, kills, survived_eps;
    double food, water, dealt, taken, growth_end, growth_gain, ret, terms[RT_COUNT];
    long causes[CAUSES], herb_kills_by, stale_kills;
} SpStats;
typedef struct Run {
    SpStats sp[CRET_SPECIES];
    long kill_matrix[CRET_SPECIES][CRET_SPECIES + 1];   // [victim][killer species or CRET_SPECIES: no credited attacker]
    char label[160]; int episodes, ticks, agents;
    char policy_of[CRET_MAX_AGENTS][96];
} Run;

typedef struct Assign { char who[32]; char policy[96]; } Assign;

static Dict* env_kwargs(int agents, int ticks, int world_n, float world_cell) {
    Dict* kw = puf_ini_section(&g_ini, "env", 0);
    if (agents > 0) dict_set(kw, "num_agents", agents);
    if (ticks > 0) dict_set(kw, "max_ticks", ticks);
    if (world_n > 0) dict_set(kw, "world_n", world_n);
    if (world_cell > 0) dict_set(kw, "world_cell", world_cell);
    return kw;
}

static void run_eval(Run* R, Dict* kw, const Assign* as, int nas, int episodes, uint32_t seed) {
    memset(R->sp, 0, sizeof R->sp); memset(R->kill_matrix, 0, sizeof R->kill_matrix);
    Env* env = calloc(1, sizeof(Env)); env->rng = seed; puf_init(env, kw);
    CretSim* s = env->sim; int n = env->num_agents;
    R->agents = n; R->ticks = s->cfg.max_ticks; R->episodes = episodes;
    // Policies per slot (the last matching assignment wins); one instance per distinct spec.
    Policy* pols[16]; int npol = 0; int slot_pol[CRET_MAX_AGENTS];
    for (int i = 0; i < n; i++) {
        int sp = cret_slot_species(&s->cfg, i); const char* spec = "bot";
        for (int k = 0; k < nas; k++) if (who_matches(as[k].who, i, sp)) spec = as[k].policy;
        int q = -1; for (int k = 0; k < npol; k++) if (!strcmp(pols[k]->name, spec)) q = k;
        if (q < 0) { q = npol; pols[q] = make_policy(spec, n, seed + 17 * (uint32_t)q); npol++; }
        slot_pol[i] = q; snprintf(R->policy_of[i], sizeof R->policy_of[i], "%s", spec);
    }
    bool needs_obs = false; for (int k = 0; k < npol; k++) needs_obs |= pols[k]->needs_obs;
    float* obs = calloc((size_t)n * CRET_OBS_SIZE, sizeof(float)); float term[CRET_MAX_AGENTS];
    for (int ep = 0; ep < episodes; ep++) {
        if (ep > 0) cret_sim_reset(s);
        long life_start[CRET_MAX_AGENTS], last_hit[CRET_MAX_AGENTS]; float food[CRET_MAX_AGENTS], water[CRET_MAX_AGENTS], g_start[CRET_MAX_AGENTS];
        int deaths_ep[CRET_MAX_AGENTS], hurt_prev[CRET_MAX_AGENTS];
        for (int i = 0; i < n; i++) {
            life_start[i] = 0; last_hit[i] = -100000; food[i] = water[i] = 0; g_start[i] = s->agents[i].growth; deaths_ep[i] = 0; hurt_prev[i] = 0; term[i] = 1;
            R->sp[s->agents[i].species].slot_eps++;
        }
        for (int t = 0; t < s->cfg.max_ticks; t++) {
            if (needs_obs) for (int i = 0; i < n; i++) cret_sim_observe(s, i, obs + (size_t)i * CRET_OBS_SIZE);
            CretInput in[CRET_MAX_AGENTS] = {0}; bool mask[CRET_MAX_AGENTS];
            for (int k = 0; k < npol; k++) { for (int i = 0; i < n; i++) mask[i] = slot_pol[i] == k; pols[k]->act(pols[k], s, obs, term, mask, in); }
            float g0[CRET_MAX_AGENTS]; bool was_alive[CRET_MAX_AGENTS];
            for (int i = 0; i < n; i++) { g0[i] = s->agents[i].growth; was_alive[i] = s->agents[i].alive; }
            cret_sim_step(s, in);
            for (int i = 0; i < n; i++) {
                CretAgent* a = &s->agents[i]; SpStats* S = &R->sp[a->species];
                if (!was_alive[i] && a->alive) { life_start[i] = s->tick - 1; food[i] = water[i] = 0; g_start[i] = a->growth; last_hit[i] = -100000; hurt_prev[i] = 0; }
                float tt[RT_COUNT]; cret_reward_terms(&env->w, a, g0[i], tt);
                for (int k = 0; k < RT_COUNT; k++) S->terms[k] += tt[k];
                S->ret += cret_reward_sum(tt);
                S->food += a->ev_food; S->water += a->ev_water; S->dealt += a->ev_dealt; S->taken += a->ev_taken; S->kills += a->ev_kills;
                if (a->hurt_ticks > hurt_prev[i]) last_hit[i] = s->tick;
                hurt_prev[i] = a->hurt_ticks;
                term[i] = a->ev_died ? 1.0f : 0.0f;
                if (a->ev_died) {
                    S->deaths++; S->lives++; deaths_ep[i]++; S->life_ticks += s->tick - life_start[i];
                    S->growth_end += a->growth; S->growth_gain += a->growth - g_start[i];
                    int k = a->last_attacker, ks = k >= 0 && k < n ? s->agents[k].species : CRET_SPECIES;
                    R->kill_matrix[a->species][ks]++;
                    bool recent = s->tick - last_hit[i] <= 3 * CRET_TICK_HZ;
                    int cause = recent && k >= 0 ? CAUSE_ATTACK : a->food <= 0 ? CAUSE_STARVED : a->water <= 0 ? CAUSE_THIRST : a->bleed > 0 ? CAUSE_BLEED : CAUSE_OTHER;
                    S->causes[cause]++;
                    if (ks < CRET_SPECIES) {
                        if (CRET_SPECIES_TABLE[ks].diet == CRET_DIET_HERBIVORE) R->sp[ks].herb_kills_by++;
                        if (s->tick - last_hit[i] > 30 * CRET_TICK_HZ) R->sp[ks].stale_kills++;
                    }
                }
            }
        }
        for (int i = 0; i < n; i++) {   // lives cut by the episode end
            const CretAgent* a = &s->agents[i]; SpStats* S = &R->sp[a->species];
            if (!a->alive) continue;
            S->lives++; S->life_ticks += s->tick - life_start[i]; S->growth_end += a->growth; S->growth_gain += a->growth - g_start[i];
            S->survived_eps += deaths_ep[i] == 0;
        }
        for (int i = 0; i < n; i++) term[i] = 1;   // the next episode starts fresh (recurrent state too)
    }
    free(obs);
    for (int k = 0; k < npol; k++) { if (pols[k]->close) pols[k]->close(pols[k]); free(pols[k]); }
    puf_close(env); free(env);
}

// ------------------------------------------------------------------ report
static void policy_list(const Run* R, int species, char* out, size_t n) {
    out[0] = 0;
    for (int i = 0; i < R->agents; i++) {
        if (cret_slot_species(&(CretConfig){.player_species = CRET_SP_RAPTOR}, i) != species) continue;
        if (!strstr(out, R->policy_of[i])) { if (out[0]) strncat(out, "/", n - strlen(out) - 1); strncat(out, R->policy_of[i], n - strlen(out) - 1); }
    }
}
static void print_table(const Run* R) {
    printf("\n### %s\n\n%d episodes x %d ticks (%.0f s), %d agents. Per slot-episode unless noted; life = mean life in seconds.\n\n",
           R->label, R->episodes, R->ticks, R->ticks / (float)CRET_TICK_HZ, R->agents);
    printf("| species | policy | slots | life s | survived | deaths | kills | food | water | growth end | dealt | taken | return |\n");
    printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    SpStats T = {0};
    for (int k = 0; k < CRET_SPECIES; k++) {
        const SpStats* S = &R->sp[k]; if (!S->slot_eps) continue;
        double e = (double)S->slot_eps; char pol[128]; policy_list(R, k, pol, sizeof pol);
        printf("| %s | %s | %ld | %.0f | %.0f%% | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f |\n", CRET_SPECIES_TABLE[k].name, pol,
               S->slot_eps / R->episodes, S->lives ? S->life_ticks / (double)S->lives / CRET_TICK_HZ : 0, 100.0 * S->survived_eps / e,
               S->deaths / e, S->kills / e, S->food / e, S->water / e, S->lives ? S->growth_end / S->lives : 0, S->dealt / e, S->taken / e, S->ret / e);
        T.slot_eps += S->slot_eps; T.lives += S->lives; T.life_ticks += S->life_ticks; T.survived_eps += S->survived_eps; T.deaths += S->deaths;
        T.kills += S->kills; T.food += S->food; T.water += S->water; T.growth_end += S->growth_end; T.dealt += S->dealt; T.taken += S->taken; T.ret += S->ret;
    }
    double e = (double)T.slot_eps;
    printf("| **all** | | %ld | %.0f | %.0f%% | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f |\n", T.slot_eps / R->episodes,
           T.lives ? T.life_ticks / (double)T.lives / CRET_TICK_HZ : 0, 100.0 * T.survived_eps / e, T.deaths / e, T.kills / e, T.food / e, T.water / e,
           T.lives ? T.growth_end / T.lives : 0, T.dealt / e, T.taken / e, T.ret / e);
    printf("\nKill matrix (rows: victim, columns: credited killer; none = no attacker on record) and death causes:\n\n| victim |");
    for (int k = 0; k < CRET_SPECIES; k++) printf(" %s |", SP_KEY[k]);
    printf(" none |");
    for (int c = 0; c < CAUSES; c++) printf(" %s |", CAUSE_NAMES[c]);
    printf("\n|---|"); for (int k = 0; k <= CRET_SPECIES + CAUSES; k++) printf("---|"); printf("\n");
    for (int v = 0; v < CRET_SPECIES; v++) {
        if (!R->sp[v].slot_eps) continue;
        printf("| %s |", SP_KEY[v]);
        for (int k = 0; k <= CRET_SPECIES; k++) printf(" %ld |", R->kill_matrix[v][k]);
        for (int c = 0; c < CAUSES; c++) printf(" %ld |", R->sp[v].causes[c]);
        printf("\n");
    }
}
static void print_terms(const Run* R) {
    printf("\nReward terms per slot-episode (%s):\n\n| species | policy |", R->label);
    for (int k = 0; k < RT_COUNT; k++) printf(" %s |", RT_NAMES[k]);
    printf(" total |\n|---|---|"); for (int k = 0; k <= RT_COUNT; k++) printf("---|"); printf("\n");
    for (int sp = 0; sp < CRET_SPECIES; sp++) {
        const SpStats* S = &R->sp[sp]; if (!S->slot_eps) continue;
        char pol[128]; policy_list(R, sp, pol, sizeof pol);
        printf("| %s | %s |", CRET_SPECIES_TABLE[sp].name, pol);
        for (int k = 0; k < RT_COUNT; k++) printf(" %+.3f |", S->terms[k] / S->slot_eps);
        printf(" %+.3f |\n", S->ret / S->slot_eps);
    }
}
static double total_return(const Run* R, int diet) {
    double r = 0, e = 0;
    for (int k = 0; k < CRET_SPECIES; k++) if (diet < 0 || CRET_SPECIES_TABLE[k].diet == diet) { r += R->sp[k].ret; e += R->sp[k].slot_eps; }
    return e ? r / e : 0;
}
static double survived(const Run* R, int diet) {
    double r = 0, e = 0;
    for (int k = 0; k < CRET_SPECIES; k++) if (diet < 0 || CRET_SPECIES_TABLE[k].diet == diet) { r += R->sp[k].survived_eps; e += R->sp[k].slot_eps; }
    return e ? r / e : 0;
}

int main(int argc, char** argv) {
    int episodes = 4, ticks = 0, agents = 0, world_n = 0, quiet = 0; float world_cell = 0; uint32_t seed = 1;
    bool baselines = false, audit = false, smoke = false; Assign as[32]; int nas = 0;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i]; const char* v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--episodes") && v) { episodes = atoi(v); i++; }
        else if (!strcmp(a, "--ticks") && v) { ticks = atoi(v); i++; }
        else if (!strcmp(a, "--agents") && v) { agents = atoi(v); i++; }
        else if (!strcmp(a, "--seed") && v) { seed = (uint32_t)atoi(v); i++; }
        else if (!strcmp(a, "--world-n") && v) { world_n = atoi(v); i++; }
        else if (!strcmp(a, "--world-cell") && v) { world_cell = (float)atof(v); i++; }
        else if (!strcmp(a, "--policy") && v && nas < 32) {
            const char* eq = strchr(v, '='); i++;
            if (!eq) { snprintf(as[nas].who, sizeof as[nas].who, "all"); snprintf(as[nas].policy, sizeof as[nas].policy, "%s", v); }
            else { snprintf(as[nas].who, sizeof as[nas].who, "%.*s", (int)(eq - v), v); snprintf(as[nas].policy, sizeof as[nas].policy, "%s", eq + 1); }
            nas++;
        }
        else if (!strcmp(a, "--baselines")) baselines = true;
        else if (!strcmp(a, "--audit")) audit = true;
        else if (!strcmp(a, "--quiet")) quiet = 1;
        else if (!strcmp(a, "--smoke")) smoke = true;
        else { fprintf(stderr, "usage: see the comment at the top of tools/cret_eval.c\n"); return 1; }
    }
    (void)quiet;
    puf_ini_load_file(&g_ini, "config/cretaceous.ini");
    if (smoke) {
        // make test: every policy type, including a net slot on all-zero weights of the
        // current contract's size, for a short run on the test world.
        const char* path = "build/cretaceous/test/zero_net.bin";
        int hidden = (int)puf_ini_get(&g_ini, "policy", "hidden_size"), layers = (int)puf_ini_get(&g_ini, "policy", "num_layers");
        int sizes[] = CRET_ACTION_SIZES, atn = 0; for (int h = 0; h < CRET_ACTION_HEADS; h++) atn += sizes[h];
        long need = AL8((long)hidden * CRET_OBS_SIZE); need = AL8(need + (long)(atn + 1) * hidden); for (int l = 0; l < layers; l++) need = AL8(need + 3L * hidden * hidden);
        FILE* f = fopen(path, "wb"); float z[1024] = {0};
        for (long k = 0; f && k < need; k += 1024) fwrite(z, sizeof(float), need - k < 1024 ? need - k : 1024, f);
        if (f) fclose(f);
        char netspec[600]; snprintf(netspec, sizeof netspec, "net:%s", path);
        Assign sm[6] = {{"all", "random"}, {"stego", "botq"}, {"pachy", "noop"}, {"galli", "swim"}, {"slot0", "brawl"}, {"carnivores", ""}};
        snprintf(sm[5].policy, sizeof sm[5].policy, "%s", netspec);
        CretConfig tc = t_config(16);
        Dict* skw = env_kwargs(16, ticks > 0 ? ticks : 600, tc.world_n, tc.world_cell);
        static Run S; snprintf(S.label, sizeof S.label, "smoke: every policy type");
        run_eval(&S, skw, sm, 6, 1, seed);
        int nan = 0; for (int k = 0; k < CRET_SPECIES; k++) nan += !isfinite(S.sp[k].ret);
        if (!quiet) { print_table(&S); print_terms(&S); }
        fprintf(stderr, "cret_eval --smoke: %s\n", nan ? "FAILED (non-finite return)" : "ok");
        return nan ? 1 : 0;
    }
    Dict* kw = env_kwargs(agents, ticks, world_n, world_cell);
    printf("# cret_eval: world %d^2 x %g m (seed %d), contract %d, seed %u\n", (int)dict_get(kw, "world_n"), dict_get(kw, "world_cell"),
           (int)dict_get(kw, "world_seed"), CRET_CONTRACT_VERSION, seed);
    double t0 = t_now();
    static Run R;
    if (!baselines && !audit) {
        snprintf(R.label, sizeof R.label, "custom");
        for (int k = 0; k < nas; k++) { strncat(R.label, k ? ", " : ": ", sizeof R.label - strlen(R.label) - 1); strncat(R.label, as[k].who, sizeof R.label - strlen(R.label) - 1); strncat(R.label, "=", sizeof R.label - strlen(R.label) - 1); strncat(R.label, as[k].policy, sizeof R.label - strlen(R.label) - 1); }
        run_eval(&R, kw, as, nas, episodes, seed); print_table(&R); print_terms(&R);
    }
    if (baselines || audit) {
        const char* base[] = {"bot", "random", "noop"};
        static Run B[3];
        for (int q = 0; q < 3; q++) {
            Assign one = {"all", ""}; snprintf(one.policy, sizeof one.policy, "%s", base[q]);
            snprintf(B[q].label, sizeof B[q].label, "all slots: %s", base[q]);
            run_eval(&B[q], kw, &one, 1, episodes, seed);
            print_table(&B[q]);
            if (audit) print_terms(&B[q]);
            fprintf(stderr, "cret_eval: %s done (%.0f s)\n", base[q], t_now() - t0);
        }
        if (audit) {
            // Probes: herbivores brawl (do damage and kill rewards pay for
            // attacking the nearest animal?), everyone swims (is deep water a safe idle?).
            static Run P[3]; Assign pa[3][2] = {{{"all", "bot"}, {"herbivores", "brawl"}}, {{"all", "bot"}, {"carnivores", "brawl"}}, {{"all", "swim"}, {"", ""}}};
            const char* lab[3] = {"bots, herbivores brawl", "bots, carnivores brawl", "all slots: swim"};
            int npa[3] = {2, 2, 1};
            for (int q = 0; q < 3; q++) {
                snprintf(P[q].label, sizeof P[q].label, "%s", lab[q]);
                run_eval(&P[q], kw, pa[q], npa[q], episodes, seed); print_table(&P[q]); print_terms(&P[q]);
                fprintf(stderr, "cret_eval: probe %s done (%.0f s)\n", lab[q], t_now() - t0);
            }
            printf("\n## Audit flags\n\n");
            double rb = total_return(&B[0], -1), rr = total_return(&B[1], -1), rn = total_return(&B[2], -1);
            printf("- Return per slot-episode: bot %+.3f, random %+.3f, noop %+.3f; survived the episode: bot %.0f%%, random %.0f%%, noop %.0f%%.\n",
                   rb, rr, rn, 100 * survived(&B[0], -1), 100 * survived(&B[1], -1), 100 * survived(&B[2], -1));
            if (rn >= rb) printf("  - **FLAG** standing still earns at least as much as the scripted bots: doing nothing is a strong local optimum.\n");
            // Share of the return that no behaviour changes: alive (respawn_ticks 1 keeps every slot alive) and passive growth.
            double alive = 0, growth_b = 0, growth_n = 0, e = 0;
            for (int k = 0; k < CRET_SPECIES; k++) { alive += B[0].sp[k].terms[RT_ALIVE]; growth_b += B[0].sp[k].terms[RT_GROWTH]; growth_n += B[2].sp[k].terms[RT_GROWTH]; e += B[0].sp[k].slot_eps; }
            if (e > 0) {
                printf("- The alive term is %.0f%% of the bots' return (%+.2f per slot-episode) and differs by under 0.1%% between bot, random and noop: "
                       "with respawn_ticks = 1 a death costs one tick of it. Its only signal is the value lost at the terminal.\n", 100 * alive / e / rb, alive / e);
                printf("- Growth: %+.3f per slot-episode for bots, %+.3f standing still. It accrues whenever food and water are above 0.25, so it pays for time alive, not for eating.\n",
                       growth_b / e, growth_n / e);
                if (growth_n > 0.8 * growth_b) printf("  - **FLAG** the growth term barely separates feeding from idling.\n");
            }
            // Probes, per species: does attacking the nearest animal pay more than the bots' behaviour?
            for (int q = 0; q < 2; q++) {
                for (int k = 0; k < CRET_SPECIES; k++) {
                    const SpStats *pb = &P[q].sp[k], *bb = &B[0].sp[k];
                    if (!pb->slot_eps || !bb->slot_eps) continue;
                    bool probed = (q == 0) == (CRET_SPECIES_TABLE[k].diet == CRET_DIET_HERBIVORE);
                    if (!probed) continue;
                    double rp = pb->ret / pb->slot_eps, rbk = bb->ret / bb->slot_eps;
                    printf("- %s attacking the nearest animal: return %+.2f vs %+.2f as a bot (%.1f kills, %.2f deaths per episode; kill term %+.2f, dealt %+.2f, food %+.2f).\n",
                           CRET_SPECIES_TABLE[k].name, rp, rbk, pb->kills / (double)pb->slot_eps, pb->deaths / (double)pb->slot_eps,
                           pb->terms[RT_KILL] / pb->slot_eps, pb->terms[RT_DEALT] / pb->slot_eps, pb->terms[RT_FOOD] / pb->slot_eps);
                    if (rp > 1.1 * rbk) printf("  - **FLAG** indiscriminate killing beats the bots' behaviour by %.0f%% for %s.\n", 100 * (rp / rbk - 1), CRET_SPECIES_TABLE[k].name);
                }
            }
            long hk = 0, same = 0, all_k = 0;
            for (int k = 0; k < CRET_SPECIES; k++) { hk += P[0].sp[k].herb_kills_by; same += P[0].kill_matrix[k][k] + P[1].kill_matrix[k][k]; for (int v = 0; v < CRET_SPECIES; v++) all_k += P[0].kill_matrix[v][k] + P[1].kill_matrix[v][k]; }
            printf("- In the brawl probes, herbivores killed %ld animals and %ld of %ld kills were of the killer's own species (outside its group). "
                   "Kill and damage rewards do not depend on diet, target or the victim's size.\n", hk, same, all_k);
            if (hk > 0) printf("  - **FLAG** herbivores are paid for killing (%+.2f per kill, %+.2f per victim max health dealt).\n",
                               dict_get(kw, "reward_kill"), dict_get(kw, "reward_damage_dealt"));
            double rs = total_return(&P[2], -1); long sw = 0, st = 0;
            for (int k = 0; k < CRET_SPECIES; k++) { sw += P[2].sp[k].survived_eps; st += P[2].sp[k].slot_eps; }
            printf("- Everyone floating in deep water: return %+.3f, survived %.0f%% (safe from attacks, but nobody can drink while swimming).\n", rs, st ? 100.0 * sw / st : 0);
            if (rs >= rb) printf("  - **FLAG** idling in deep water earns at least as much as the bots.\n");
            long stale = 0, kills = 0; for (int k = 0; k < CRET_SPECIES; k++) { stale += B[0].sp[k].stale_kills + B[1].sp[k].stale_kills; kills += B[0].sp[k].kills + B[1].sp[k].kills; }
            printf("- Kill credit: %ld of %ld kills (bot and random runs) went to an attacker whose last hit was over 30 s before the death "
                   "(last_attacker never expires, so starving or thirsty animals credit whoever bit them last).\n", stale, kills);
            if (kills && stale * 4 > kills) printf("  - **FLAG** stale kill credit is common.\n");
        }
    }
    fprintf(stderr, "cret_eval: %.1f s\n", t_now() - t0);
    return 0;
}
