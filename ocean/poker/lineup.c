// Lineup evaluation as real cash sessions (2026-11-09; first version 2026-11-05). The user: "always memory, always
// simming real life cash games, walking in and playing real, not fake one hand and forget". Every table is a session:
// six players sit down with --stack BB (default 100) and play --hands hands (default 1000, a gen13 table life) in a
// row. Stacks carry over, models keep their recurrent memory across hands, and the seat tenure block counts the hands
// each occupant has been at the table, exactly as the gen13 env plays its lives (ocean/poker/poker.h). A player who
// busts (under 2 BB) or grows past the stack ceiling walks out and walks back in with a fresh buy-in: memory and
// tenure reset for that seat only (as a new player sitting down in training); everyone else keeps theirs.
//
// Card luck is taken out by DUPLICATE sessions: the session's deal sequence (decks and buttons) is replayed six times
// with the lineup rotated through the six seats, so each player holds each seat's cards once. One sample per table
// and player: its BB/100 averaged over the six rotations; the interval is over tables. Rake as in training (5%,
// capped at 3 BB, no flop no drop). Net counts every buy-in: result = chips taken home minus chips bought in.
//
// Players: bots by name (rules, fullhouse, range, tight, trapper; maniac and station were deleted 2026-11-09) or
// models NAME=PATH:HIDDEN:LAYERS (any layout policy.h reads), sampled from the masked softmax; the forward uses AVX2
// for speed, so logits match policy.h only to rounding.
//
// One table: exactly six players and no --tables. A pool: more players and --tables T: each table seats six distinct
// pool players drawn at random (seeded). Output per player: BB/100 with a 95% interval, play style (VPIP, PFR, all-in,
// flop, WTSD, W$SD, AF) and buy-ins per 1000 hands.
//
// Build (wide chips: winners' stacks grow without a 655 BB ceiling, as in gen12+ training):
//   gcc -O3 -march=native -std=gnu11 -fopenmp -DNDEBUG -DPOKER_WIDE_CHIPS
//     -DPOKER_FULLHOUSE_MODEL='"/path/fullhouse_weights.h"' -Iocean/poker ocean/poker/lineup.c -lm -o OUT
// Usage: lineup [--hands N] [--tables T] [--stack BB] [--seed S] PLAYER PLAYER ...
#include <omp.h>
#include "players.h"

#define LINEUP_CEILING (POKER_MAX_START / POKER_PLAYERS)  // poker.h's POKER_LIFE_CAP

typedef struct {
    double net, sample_sum, sample_sq;  // chips; per-table mean BB/100, summed and squared
    long hands, samples, buyins, vpip, pfr, allin, flop, wtsd, wsd, raises, calls;
} Stat;

// One rotation of one session: who[s] sits in seat s for all hands. Adds style counts to st[who[s]] and returns each
// seat's net chips in net[s].
static void session(Poker* g, const int* who, long hands, uint32_t buyin, uint64_t table_seed, uint64_t rot_seed,
    Stat* st, double* net, float (*memory)[4 * POKER_POLICY_MAX_HIDDEN]) {
    memset(g, 0, sizeof(*g));
    g->rake_permille = 50;
    g->rake_cap = 3 * POKER_BB;
    uint32_t stacks[POKER_PLAYERS];
    uint64_t rng[POKER_PLAYERS];
    for (int s = 0; s < POKER_PLAYERS; s++) {
        stacks[s] = buyin; net[s] = -(double)buyin; st[who[s]].buyins++;
        memset(memory[s], 0, sizeof(memory[s]));
        g->tenure[s][0] = 0;
        rng[s] = mix(rot_seed * 8 + s);
    }
    int button = (int)(mix(table_seed) % POKER_PLAYERS);
    for (long h = 0; h < hands; h++) {
        g->rng[0] = mix(table_seed * 1000003 + (uint64_t)h);  // the same deck in every rotation
        poker_deal(g, 0, button, stacks);
        int vpip[POKER_PLAYERS] = {0}, pfr[POKER_PLAYERS] = {0}, allin[POKER_PLAYERS] = {0}, flop[POKER_PLAYERS] = {0};
        while (g->actor[0] != POKER_DONE) {
            int s = g->actor[0], street = g->street[0];
            const Player* pl = &players[who[s]];
            uint32_t call = g->high[0] - g->bet[s][0];
            int32_t action;
            if (pl->bot) {
                action = poker_decide(g, 0, &rng[s], pl->bot, 0, NULL);
            } else {
                float obs[POKER_PUB], logits[POKER_ACTIONS + 1];
                int32_t amounts[POKER_ACTIONS];
                unsigned mask = poker_actions(g, 0, amounts);
                poker_observe_public(g, 0, s, obs);
                forward(pl, memory[s], obs, logits);
                action = amounts[poker_policy_sample(logits, mask, poker_random(&rng[s]))];
            }
            Stat* t = &st[who[s]];
            if (action > 0) { t->raises++; if (!street) vpip[s] = pfr[s] = 1; }
            else if (action == 0 && call > 0) { t->calls++; if (!street) vpip[s] = 1; }
            poker_step(g, 0, action);
            if (g->street[0] > 0 && g->street[0] != street)
                for (int q = 0; q < POKER_PLAYERS; q++) if (g->live[0] >> q & 1) flop[q] = 1;
            if (g->stack[s][0] == 0 && action >= 0) allin[s] = 1;
        }
        int showdown = __builtin_popcount(g->live[0]) > 1;
        for (int s = 0; s < POKER_PLAYERS; s++) {
            Stat* t = &st[who[s]];
            int won = (int64_t)g->stack[s][0] > (int64_t)stacks[s];
            t->hands++;
            t->vpip += vpip[s]; t->pfr += pfr[s]; t->allin += allin[s]; t->flop += flop[s];
            if (flop[s] && showdown && (g->live[0] >> s & 1)) { t->wtsd++; t->wsd += won; }
            stacks[s] = g->stack[s][0];
            // Walk out (bust, or past the ceiling with the winnings) and walk back in with a fresh buy-in.
            if (h + 1 < hands && (stacks[s] < 2 * POKER_BB || stacks[s] > LINEUP_CEILING)) {
                net[s] += stacks[s];
                stacks[s] = buyin; net[s] -= buyin; t->buyins++;
                memset(memory[s], 0, sizeof(memory[s]));
                g->tenure[s][0] = 0;
            } else if (g->tenure[s][0] < 65535) {
                g->tenure[s][0]++;
            }
        }
        button = (button + 1) % POKER_PLAYERS;
    }
    for (int s = 0; s < POKER_PLAYERS; s++) { net[s] += stacks[s]; st[who[s]].net += net[s]; }
}

int main(int argc, char** argv) {
    long hands = 1000, tables = 0; uint32_t stack_bb = 100; uint64_t seed = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--hands") && i + 1 < argc) hands = atol(argv[++i]);
        else if (!strcmp(argv[i], "--tables") && i + 1 < argc) tables = atol(argv[++i]);
        else if (!strcmp(argv[i], "--stack") && i + 1 < argc) stack_bb = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 10);
        else if (nplayers >= MAX_PLAYERS || !add_player(argv[i])) return 2;
    }
    if (nplayers < POKER_PLAYERS || (!tables && nplayers != POKER_PLAYERS) || hands < 1
        || stack_bb * POKER_BB > LINEUP_CEILING) {
        fprintf(stderr, "usage: lineup [--hands N] [--tables T] [--stack BB] [--seed S] PLAYER x6 (or a pool with --tables)\n");
        return 2;
    }
    if (!tables) tables = 1;
    // Table lineups: one table as given, or six distinct pool players per table.
    int (*lineups)[POKER_PLAYERS] = malloc(sizeof(*lineups) * tables);
    for (long t = 0; t < tables; t++) {
        int order[MAX_PLAYERS];
        for (int i = 0; i < nplayers; i++) order[i] = i;
        uint64_t r = mix(seed * 1000003 + t);
        if (nplayers > POKER_PLAYERS) for (int i = 0; i < POKER_PLAYERS; i++) {
            r = mix(r); int j = i + (int)(r % (uint64_t)(nplayers - i));
            int x = order[i]; order[i] = order[j]; order[j] = x;
        }
        memcpy(lineups[t], order, sizeof(lineups[t]));
    }
    // Net chips per (table, rotation, seat); aggregated per table after all sessions finish.
    double (*nets)[POKER_PLAYERS][POKER_PLAYERS] = calloc(tables, sizeof(*nets));
    Stat total[MAX_PLAYERS] = {0};
    long jobs = tables * POKER_PLAYERS, done = 0;
    double t0 = omp_get_wtime();
    #pragma omp parallel
    {
        Stat st[MAX_PLAYERS] = {0};
        static __thread Poker g;
        static __thread float memory[POKER_PLAYERS][4 * POKER_POLICY_MAX_HIDDEN];
        #pragma omp for schedule(dynamic, 1)
        for (long j = 0; j < jobs; j++) {
            long t = j / POKER_PLAYERS; int rot = (int)(j % POKER_PLAYERS);
            int who[POKER_PLAYERS];
            for (int s = 0; s < POKER_PLAYERS; s++) who[s] = lineups[t][(s + rot) % POKER_PLAYERS];
            uint64_t table_seed = mix(seed * 7919 + (uint64_t)t);
            session(&g, who, hands, stack_bb * POKER_BB, table_seed, mix(table_seed + 31 * (uint64_t)rot), st,
                nets[t][rot], memory);
            #pragma omp atomic
            done++;
            if (omp_get_thread_num() == 0)
                fprintf(stderr, "\r%ld / %ld sessions (%.0f s)", done, jobs, omp_get_wtime() - t0);
        }
        #pragma omp critical
        for (int p = 0; p < nplayers; p++) {
            Stat* a = &total[p]; const Stat* b = &st[p];
            a->net += b->net; a->hands += b->hands; a->buyins += b->buyins; a->vpip += b->vpip; a->pfr += b->pfr;
            a->allin += b->allin; a->flop += b->flop; a->wtsd += b->wtsd; a->wsd += b->wsd;
            a->raises += b->raises; a->calls += b->calls;
        }
    }
    for (long t = 0; t < tables; t++) {
        double per[MAX_PLAYERS] = {0};
        for (int rot = 0; rot < POKER_PLAYERS; rot++)
            for (int s = 0; s < POKER_PLAYERS; s++) per[lineups[t][(s + rot) % POKER_PLAYERS]] += nets[t][rot][s];
        for (int k = 0; k < POKER_PLAYERS; k++) {
            int p = lineups[t][k];
            double m = per[p] / POKER_PLAYERS / POKER_BB / hands * 100;  // BB/100 over the six rotations
            total[p].sample_sum += m; total[p].sample_sq += m * m; total[p].samples++;
        }
    }
    fprintf(stderr, "\r%ld sessions in %.0f s%30s\n", jobs, omp_get_wtime() - t0, "");
    printf("%ld table%s x 6 rotations x %ld-hand sessions (stacks and memory carry), %u BB buy-ins, rake 5%% capped at "
        "3 BB, seed %llu\n", tables, tables > 1 ? "s" : "", hands, stack_bb, (unsigned long long)seed);
    printf("%-14s %9s %7s %6s %5s %6s %5s %5s %5s %5s %8s %9s\n", "player", "BB/100", "+/-95%", "VPIP", "PFR",
        "all-in", "flop", "WTSD", "W$SD", "AF", "buyins/k", "hands");
    int order[MAX_PLAYERS];
    for (int i = 0; i < nplayers; i++) order[i] = i;
    for (int i = 0; i < nplayers; i++) for (int j = i + 1; j < nplayers; j++) {
        const Stat *a = &total[order[i]], *b = &total[order[j]];
        if ((b->samples ? b->sample_sum / b->samples : -1e9) > (a->samples ? a->sample_sum / a->samples : -1e9)) {
            int x = order[i]; order[i] = order[j]; order[j] = x;
        }
    }
    for (int k = 0; k < nplayers; k++) {
        int p = order[k]; const Stat* s = &total[p];
        if (!s->samples) continue;
        double mean = s->sample_sum / s->samples, var = s->sample_sq / s->samples - mean * mean;
        double ci = s->samples > 1 ? 1.96 * sqrt(var > 0 ? var * s->samples / (s->samples - 1) : 0) / sqrt((double)s->samples) : 0;
        double h = (double)s->hands;
        printf("%-14s %+9.1f %7.1f %5.0f%% %4.0f%% %5.1f%% %4.0f%% %4.0f%% %4.0f%% %5.2f %8.1f %9ld\n", players[p].name,
            mean, ci, 100 * s->vpip / h, 100 * s->pfr / h, 100 * s->allin / h, 100 * s->flop / h,
            s->flop ? 100.0 * s->wtsd / s->flop : 0, s->wtsd ? 100.0 * s->wsd / s->wtsd : 0,
            s->calls ? (double)s->raises / s->calls : 0, 1000.0 * s->buyins / h, s->hands);
    }
    return 0;
}
