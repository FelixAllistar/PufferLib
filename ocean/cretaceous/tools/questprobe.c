// Headless quest probe: are the quests a player gets doable, and how much of
// a session do they fill? For each species and growth stage, a scripted
// player follows its personal quest for --minutes of game time on the full
// island, with the population director on (as in solo play):
//   - it drinks and eats when it runs low (the scripted bot finds water and
//     plants; a predator stalks small prey or eats a carcass in reach);
//   - otherwise it trots to the quest's target and does what the quest asks:
//     drinks, grazes, chases and bites the prey species or small prey,
//     attacks the nearest predator (defend), stands still (rest, guard),
//     eats at the carcass or the nest, casts ability 1 near another animal,
//     builds a nest.
// A fresh character starts with its first steps; --veteran starts with them
// done, to see the ordinary quests at each stage.
// The player keeps its stage: after a death it comes back at the same growth.
// It reports per species and stage: quests offered and completed per hour,
// quests that were infeasible when offered (judged here by the probe's own
// rules, below, so before and after runs compare), time spent travelling,
// doing the quest, feeding and without a quest, deaths (and starvation
// deaths), and the distance of the quests offered.
// Infeasible, as the probe judges it when a quest is offered:
//   - hunt: no living animal of the prey species weighs at most 3 times the
//     quester (times the members of its group, for a group quest), or the
//     prey is a grown sauropelta and the hunter a raptor;
//   - defend: the quester is younger than 0.6 growth or a gallimimus;
//   - a place: trotting there takes longer than the stomach or the thirst
//     lasts (the water for a drinking quest, the food for a grazer's quest
//     do not count), or longer than the quest has left at a walk;
//   - catch: small prey are off;
//   - companion: no other animal of its species lives; nest: younger than
//     0.9 growth; guard: no nest of its own with eggs; scavenge: no carcass
//     with the meat asked for.
//   make -C ocean/cretaceous questprobe && ./build/cretaceous/questprobe
//   ./build/cretaceous/questprobe --species raptor --stage 0 --sessions 2 --minutes 30
// Run from the repository root (the world cache is build/cretaceous/worlds).
#include "../sim.h"
#include "../population.h"
#include "../progress.h"
#include "../abilities.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const float STAGE_GROWTH[3] = {0.35f, 0.65f, 1.0f};
static const char* STAGE_NAME[3] = {"hatchling", "juvenile", "adult"};

static float wrap(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static CretInput toward(const CretAgent* a, float x, float z, float fwd) {
    float d = wrap(atan2f(z - a->z, x - a->x) - a->yaw);
    CretInput in = {.turn = fmaxf(-1, fminf(1, d * 2)), .forward = fwd};
    if (fabsf(d) > 1.2f) in.forward *= 0.4f;
    return in;
}
static float mass(const CretAgent* a) { return cret_mass(a); }
static float dist2(float ax, float az, float bx, float bz) { return (ax - bx) * (ax - bx) + (az - bz) * (az - bz); }

// Chase and bite agent j (or a point, for small prey).
static CretInput attack_at(const CretSim* s, const CretAgent* a, float x, float z) {
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth);
    float d = sqrtf(dist2(a->x, a->z, x, z));
    CretInput in = toward(a, x, z, 1.0f);
    in.sprint = d < 25 && a->stamina > 0.15f; in.aim = cret_aim_at(a, x, z);
    float px, pz; cret_strike_point(a, &px, &pz);
    if (a->attack_cd == 0 && sqrtf(dist2(px, pz, x, z)) < S->reach * g + 1.0f) in.action = CRET_ACT_ATTACK;
    return in;
}
static int nearest(const CretSim* s, int me, int species, int diet, float range) {
    const CretAgent* a = &s->agents[me]; int best = -1; float bd = range * range;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j];
        if (j == me || !b->alive || (species >= 0 && b->species != species) || (diet >= 0 && CRET_SPECIES_TABLE[b->species].diet != diet)) continue;
        float d = dist2(a->x, a->z, b->x, b->z); if (d < bd) { bd = d; best = j; }
    }
    return best;
}

enum { M_IDLE, M_TRAVEL, M_DO, M_FEED, M_MODES };

// One tick of the scripted player; *mode says what it spent the tick on.
static CretInput play(CretSim* s, int me, int* mode) {
    CretAgent* a = &s->agents[me]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    bool carn = S->diet == CRET_DIET_CARNIVORE; float g = cret_growth_scale(a->growth), d;
    const CretQuest* q = cret_quest_of(s, me, 0); float qx = 0, qz = 0, qr = 0;
    bool has_t = q && cret_quest_target(s, q, &qx, &qz, &qr), at_q = has_t && dist2(a->x, a->z, qx, qz) < qr * qr * 0.7f;
    int kind = q ? q->kind : 0;
    // Survival first: water, then food.
    if (a->water < 0.3f && !(kind == CRET_Q_DRINK && has_t)) { *mode = M_FEED; return cret_bot_input(s, me); }
    if (kind == CRET_Q_DRINK && at_q) { *mode = M_DO; return cret_bot_input(s, me); }   // the bot drinks at the water there
    float fd; int fs = carn ? cret_food_state(s, me, &fd) : 0;
    if (carn && fs == 1 && a->food < 0.95f) { *mode = M_FEED; return (CretInput){.action = CRET_ACT_EAT}; }   // a carcass at the jaws
    bool catching = kind == CRET_Q_CATCH;
    int k = carn && (a->food < 0.5f || catching) ? cret_critter_nearest(s, me, catching ? 40.0f : 25.0f, &d) : -1;
    if (k >= 0) { *mode = catching ? M_DO : M_FEED; return attack_at(s, a, s->critters[k].x, s->critters[k].z); }
    if (!carn) {
        float L = S->length * g * 0.45f, f = cret_forage(s->world, a->x + cosf(a->yaw) * L, a->z + sinf(a->yaw) * L);
        bool graze_here = kind == CRET_Q_GATHER && at_q;
        // Eat facing water drinks instead (sim.c), so turn from the water to graze, as a player does.
        CretInput graze = cret_water_ahead(s, a) ? (CretInput){.forward = 0.3f, .turn = 1} : (CretInput){.action = CRET_ACT_EAT};
        if (graze_here && f > 0.12f) { *mode = M_DO; return graze; }
        if (a->food < 0.5f && f > 0.25f) { *mode = M_FEED; return graze; }
        if (graze_here || a->food < 0.35f) {   // the thickest plants within 40 m (in the quest's place when grazing for it)
            float bx = 0, bz = 0, bf = graze_here ? 0.12f : 0.3f; uint32_t h = (uint32_t)s->tick / 30 * 2654435761u + 7;
            for (int t = 0; t < 12; t++) {
                float ang = t * 0.5236f + (h % 100) * 0.01f, r = 12 + 28 * ((t * 7 + h) % 10) / 10.0f, x = a->x + cosf(ang) * r, z = a->z + sinf(ang) * r;
                float v = cret_walkable(s->world, x, z) ? cret_forage(s->world, x, z) : 0;
                if (graze_here && dist2(x, z, qx, qz) > qr * qr) v = 0;
                if (v > bf) { bf = v; bx = x; bz = z; }
            }
            if (bf > (graze_here ? 0.12f : 0.3f)) { *mode = graze_here ? M_DO : M_FEED; return toward(a, bx, bz, 0.5f); }
        }
    } else if (a->food < 0.25f) { *mode = M_FEED; return cret_bot_input(s, me); }   // the bot hunts or scavenges
    if (!q) { *mode = M_IDLE; return (CretInput){.forward = 0.5f, .turn = 0.1f}; }
    // The quest itself.
    switch (kind) {
    case CRET_Q_HUNT: {
        int j = nearest(s, me, q->species, -1, 400);
        if (j >= 0) { *mode = M_DO; return attack_at(s, a, s->agents[j].x, s->agents[j].z); }
        break; }
    case CRET_Q_DEFEND: {
        int j = nearest(s, me, -1, CRET_DIET_CARNIVORE, 200);
        if (j >= 0) { *mode = M_DO; return attack_at(s, a, s->agents[j].x, s->agents[j].z); }
        break; }
    case CRET_Q_ABILITY: {   // cast ability 1 with another animal or small prey close by, else walk to the nearest animal
        int j = nearest(s, me, -1, -1, 400); float cd = 1e9f; int ck = cret_critter_nearest(s, me, 20, &cd);
        if ((j >= 0 && dist2(a->x, a->z, s->agents[j].x, s->agents[j].z) < 15 * 15) || ck >= 0) {
            *mode = M_DO; CretInput in = {0}; if (cret_ability_ready(s, me, 0)) in.ability = 1; return in;
        }
        if (j >= 0) { *mode = M_TRAVEL; return toward(a, s->agents[j].x, s->agents[j].z, 1.0f); }
        break; }
    case CRET_Q_SCAVENGE: case CRET_Q_RAID:   // eat at the carcass or the nest once there
        if (at_q || (has_t && dist2(a->x, a->z, qx, qz) < 15 * 15)) {
            if (kind == CRET_Q_SCAVENGE) {
                int best = -1; float bd = 30 * 30;
                for (int c = 0; c < CRET_MAX_CARCASSES; c++) if (s->carcasses[c].active && s->carcasses[c].meat > 0 && dist2(a->x, a->z, s->carcasses[c].x, s->carcasses[c].z) < bd) { bd = dist2(a->x, a->z, s->carcasses[c].x, s->carcasses[c].z); best = c; }
                if (fs == 1) { *mode = M_DO; return (CretInput){.action = CRET_ACT_EAT}; }
                if (best >= 0) { *mode = M_DO; return toward(a, s->carcasses[best].x, s->carcasses[best].z, 0.5f); }
            } else {
                float L = S->length * g * 0.45f, hx = a->x + cosf(a->yaw) * L, hz = a->z + sinf(a->yaw) * L;
                *mode = M_DO;
                if (dist2(hx, hz, qx, qz) < (2.0f + a->radius) * (2.0f + a->radius)) return (CretInput){.action = CRET_ACT_EAT};
                return toward(a, qx, qz, 0.5f);
            }
        }
        break;
    case CRET_Q_NEST: *mode = M_DO; return (CretInput){.action = CRET_ACT_NEST, .forward = a->nest_ticks == 0 && s->tick % 90 < 30 ? 1.0f : 0, .turn = 0.3f};
    default: break;
    }
    if (has_t && !at_q) { *mode = M_TRAVEL; return toward(a, qx, qz, 1.0f); }
    if (has_t) { *mode = M_DO; return (CretInput){0}; }   // there: stand (patrol steps on, the quest moves its target)
    *mode = M_IDLE;
    return (CretInput){.forward = 0.5f, .turn = 0.1f};   // a hunt with no prey in sight: amble
}

// The probe's own judgement of a quest when it is offered (see the top).
static const char* infeasible(const CretSim* s, int me, const CretQuest* q) {
    const CretAgent* a = &s->agents[me]; bool carn = CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_CARNIVORE;
    if (q->kind == CRET_Q_HUNT) {
        int members = 1;
        if (q->group) { members = 0; for (int j = 0; j < s->cfg.num_agents; j++) members += s->agents[j].alive && s->agents[j].group == q->group; }
        for (int j = 0; j < s->cfg.num_agents; j++) {
            const CretAgent* b = &s->agents[j];
            if (j == me || !b->alive || b->species != q->species) continue;
            if (a->species == CRET_SP_RAPTOR && b->species == CRET_SP_SAUROPELTA && b->growth > 0.6f) continue;
            if (mass(b) <= 3 * mass(a) * members) return NULL;
        }
        return "hunt: no prey of that species within 3x the hunter's mass";
    }
    if (q->kind == CRET_Q_DEFEND) return a->growth < 0.6f || a->species == CRET_SP_GALLI ? "defend: too young or a gallimimus" : NULL;
    if (q->kind == CRET_Q_CATCH && !s->crit_cfg.enabled) return "catch: small prey are off";
    if (q->kind == CRET_Q_COMPANION && nearest(s, me, a->species, -1, 1e5f) < 0) return "companion: none of its kind lives";
    if (q->kind == CRET_Q_NEST && a->growth < 0.9f) return "nest: not grown";
    if (q->kind == CRET_Q_GUARD) { int k = cret_nest_of(s, me); if (k < 0 || s->nests[k].eggs <= 0) return "guard: no nest with eggs"; }
    if (q->kind == CRET_Q_SCAVENGE) {
        bool any = false; for (int c = 0; c < CRET_MAX_CARCASSES; c++) any |= s->carcasses[c].active && s->carcasses[c].meat >= q->goal;
        if (!any) return "scavenge: no carcass with that much meat";
    }
    float x, z;
    if (cret_quest_target(s, q, &x, &z, NULL)) {
        float d = sqrtf(dist2(a->x, a->z, x, z)), trot = d / fmaxf(a->trot_speed, 0.5f), walk = d / fmaxf(a->walk_speed, 0.3f);
        float food_s = carn || q->kind != CRET_Q_GATHER ? a->food * s->cfg.hunger_seconds : 1e9f;
        float water_s = q->kind != CRET_Q_DRINK ? a->water * s->cfg.thirst_seconds : 1e9f;
        if (!carn && q->kind != CRET_Q_GATHER) food_s = 1e9f;   // a grazer eats on the way
        if (trot > fminf(food_s, water_s)) return "place: the trip outlasts the stomach";
        if (walk > (q->expires - s->tick) / (float)CRET_TICK_HZ) return "place: too far to walk before it expires";
    }
    return NULL;
}

int main(int argc, char** argv) {
    float minutes = 30, title_s = 60; int sessions = 2, only_species = -1, only_stage = -1, verbose = 0, veteran = 0; uint32_t seed0 = 3000;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--minutes") && i + 1 < argc) minutes = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--sessions") && i + 1 < argc) sessions = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed0 = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--stage") && i + 1 < argc) only_stage = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "--veteran")) veteran = 1;
        else if (!strcmp(argv[i], "--species") && i + 1 < argc) {
            const char* sp = argv[++i]; for (int k = 0; k < CRET_SPECIES; k++) if (!strcmp(sp, CRET_SPECIES_TABLE[k].asset)) only_species = k;
        } else { fprintf(stderr, "usage: questprobe [--species raptor|stego|trex|pachy|sauropelta|galli] [--stage 0|1|2] [--minutes M] [--sessions N] [--seed N] [--veteran] [-v]\n"); return 2; }
    }
    CretSim* s = calloc(1, sizeof *s);
    int ticks = (int)(minutes * 60 * CRET_TICK_HZ);
    printf("questprobe: %.0f min per session, %d sessions per species and stage, %s\n", minutes, sessions, veteran ? "first steps done" : "new characters");
    printf("%-19s %-9s %6s %6s %6s %6s %5s %5s %5s %5s %6s %6s %6s %9s %5s %8s\n", "species", "stage", "offer/h", "done/h", "expir", "infeas", "trav%", "do%", "feed%", "idle%", "deaths", "starve", "dist", "dist min-max", "steps", "steps by");
    long tot_offer = 0, tot_done = 0, tot_inf = 0, tot_starve = 0; double tot_hours = 0;
    for (int sp = 0; sp < CRET_SPECIES; sp++) for (int st = 0; st < 3; st++) {
        if ((only_species >= 0 && sp != only_species) || (only_stage >= 0 && st != only_stage)) continue;
        long offered = 0, done = 0, expired = 0, inf = 0, mode_t[M_MODES] = {0}, deaths = 0, starved = 0; double dsum = 0; float dmin = 1e9f, dmax = 0; int nd = 0;
        int kinds[CRET_Q_COUNT] = {0}, steps = 0; double steps_t = 0; int steps_n = 0;
        for (int ss = 0; ss < sessions; ss++) {
            CretConfig c = cret_default_config(); c.seed = seed0 + (uint32_t)(ss * 7919 + sp * 131 + st * 17); c.player_species = sp;
            cret_pop_config(&c);
            if (cret_sim_init(s, c) != 0) { fprintf(stderr, "world failed\n"); return 1; }
            CretPop P; cret_pop_init(&P, s, true);
            CretInput in[CRET_MAX_AGENTS];
            for (int t = 0; t < (int)(title_s * CRET_TICK_HZ); t++) {   // the title screen: everyone is a bot
                for (int i = 0; i < s->cfg.num_agents; i++) in[i] = cret_bot_input(s, i);
                cret_sim_step(s, in); cret_pop_step(&P, s);
            }
            cret_sim_set_species(s, 0, sp);   // Play: a fresh animal at the stage
            const CretAgent* a = &s->agents[0];
            if (veteran) s->prog[0].chain = CRET_CHAIN_STEPS;
            int chain0 = s->prog[0].chain; float t_steps = -1;
            cret_sim_restore(s, 0, STAGE_GROWTH[st], 1, 1, 1, false, 0, 0);
            CretQuest last = {0}; int done0 = s->prog[0].quests_done, mode = 0;
            for (int t = 0; t < ticks; t++) {
                for (int i = 1; i < s->cfg.num_agents; i++) in[i] = cret_bot_input(s, i);
                in[0] = a->alive ? play(s, 0, &mode) : (CretInput){0};
                cret_sim_step(s, in); cret_pop_step(&P, s);
                if (!a->alive) {
                    if (a->dead_ticks == 0) {
                        deaths++; if (a->last_attacker < 0 && a->food <= 0) starved++;
                        if (verbose) printf("    died at %.0f s: %s\n", t / 30.0f, a->last_attacker >= 0 ? CRET_SPECIES_TABLE[s->agents[a->last_attacker].species].name : a->food <= 0 ? "starved" : a->water <= 0 ? "thirst" : "other");
                    }
                    if (a->dead_ticks > 60) { cret_sim_respawn(s, 0); cret_sim_restore(s, 0, STAGE_GROWTH[st], 1, 1, 1, false, 0, 0); }
                    continue;
                }
                mode_t[mode]++;
                const CretQuest* q = cret_quest_of(s, 0, 0);
                if (q && (q->kind != last.kind || q->expires != last.expires)) {   // a new quest
                    offered++; kinds[q->kind]++;
                    const char* why = infeasible(s, 0, q);
                    float x, z; char title[96], prog[48]; cret_quest_text(s, q, title, sizeof title, prog, sizeof prog);
                    if (cret_quest_target(s, q, &x, &z, NULL)) { float d = sqrtf(dist2(a->x, a->z, x, z)); dsum += d; nd++; dmin = fminf(dmin, d); dmax = fmaxf(dmax, d); }
                    if (why) { inf++; if (verbose) printf("    INFEASIBLE at %.0f s (growth %.2f): %s: %s\n", t / 30.0f, a->growth, title, why); }
                    else if (verbose) printf("    %5.0f s  growth %.2f food %.2f  %s\n", t / 30.0f, a->growth, a->food, title);
                }
                if (!q && last.kind && s->tick > last.expires) expired++;
                if (q) last = *q; else memset(&last, 0, sizeof last);
                if (t_steps < 0 && s->prog[0].chain >= (st == 2 ? CRET_CHAIN_STEPS : CRET_CHAIN_STEPS - 1) && chain0 < s->prog[0].chain) t_steps = t / 30.0f;
                // Keep the stage: quests and time grow the animal; put it back (the young stay young).
                if (a->growth > STAGE_GROWTH[st] + 0.1f && st < 2) cret_sim_restore(s, 0, STAGE_GROWTH[st], a->health / a->max_health, a->food, a->water, false, 0, 0);
            }
            done += s->prog[0].quests_done - done0; steps += s->prog[0].chain - chain0;
            if (t_steps >= 0) { steps_t += t_steps; steps_n++; }
            cret_sim_close(s);
        }
        double hours = sessions * minutes / 60.0; long all = 0; for (int m = 0; m < M_MODES; m++) all += mode_t[m];
        char mm[32]; snprintf(mm, sizeof mm, nd ? "%.0f-%.0f" : "-", dmin, dmax);
        char sb[16] = "-"; if (steps_n) snprintf(sb, sizeof sb, "%.0f s", steps_t / steps_n);
        printf("%-19s %-9s %6.1f %6.1f %6ld %6ld %5.0f %5.0f %5.0f %5.0f %6ld %6ld %6.0f %9s %5d %8s\n", CRET_SPECIES_TABLE[sp].name, STAGE_NAME[st],
               offered / hours, done / hours, expired, inf, 100.0 * mode_t[M_TRAVEL] / all, 100.0 * mode_t[M_DO] / all, 100.0 * mode_t[M_FEED] / all,
               100.0 * mode_t[M_IDLE] / all, deaths, starved, nd ? dsum / nd : 0, mm, steps, sb);
        if (verbose) { printf("    kinds offered:"); for (int k = 1; k < CRET_Q_COUNT; k++) if (kinds[k]) printf(" %s %d", cret_quest_kind_name(k), kinds[k]); printf("\n"); }
        tot_offer += offered; tot_done += done; tot_inf += inf; tot_starve += starved; tot_hours += hours;
    }
    printf("total: %.1f quests offered and %.1f completed per hour, %ld infeasible, %ld starvation deaths\n", tot_offer / tot_hours, tot_done / tot_hours, tot_inf, tot_starve);
    free(s);
    return 0;
}
