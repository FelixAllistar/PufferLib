// Lineup evaluation (2026-11-05): who wins at a six-max table, and how, with card luck taken out. The user watched
// the web lineup table and saw whoever won the first big pots snowball; a few hundred hands of one table cannot rank
// players. This plays DUPLICATE deals: every deal (deck, board and button) is replayed six times with the players
// rotated through the six seats, so each player holds each seat's cards once. Stacks are fixed every hand (no
// snowball; --stack BB, default 100), with the training rake (5%, capped at 3 BB, no flop no drop).
//
// Players: bots by name (rules, fullhouse, range, maniac, station, tight, trapper) or models NAME=PATH:HIDDEN:LAYERS
// (any layout policy.h reads). Models act like the PufferLib league: poker_observe_public, recurrent memory cleared
// every hand, masked-softmax sampling; the forward uses AVX2 for speed, so logits match policy.h only to rounding.
//
// One table: exactly six players and no --tables. A pool: more players and --tables T: each table seats six distinct
// pool players drawn at random (seeded), --deals deals each; every player's numbers average over all its tables.
// Output per player: BB/100 hands with a 95% interval (one sample per deal: the mean over its rotations), and play
// style: VPIP (puts chips in preflop voluntarily), PFR (raises preflop), all-in (hands it ends all in), flop (sees a
// flop), WTSD (reaches showdown when it saw the flop), W$SD (wins at showdown), AF (raises per call).
//
// Build: clang -O3 -march=native -std=c11 -fopenmp -DNDEBUG -DPOKER_FULLHOUSE_MODEL='"/path/fullhouse_weights.h"'
//   -Iocean/poker ocean/poker/lineup.c -lm -o build/poker/lineup
// Usage: lineup [--deals D] [--tables T] [--stack BB] [--seed S] PLAYER PLAYER ...
#include <omp.h>
#include "players.h"

typedef struct {
    double net, deal_sum, deal_sq;  // chips; per-deal mean BB/hand, summed and squared
    long hands, deals, vpip, pfr, allin, flop, wtsd, wsd, raises, calls;
} Stat;

// One hand: who[s] is the player in seat s. Adds each seat's style counts to st[who[s]] and its net chips to net[s].
static void play(Poker* g, const int* who, int button, uint32_t stack, uint64_t deck, uint64_t seed, Stat* st,
    int32_t* net, float (*memory)[4 * POKER_POLICY_MAX_HIDDEN]) {
    memset(g, 0, sizeof(*g));
    g->rng[0] = deck;
    g->rake_permille = 50;
    g->rake_cap = 3 * POKER_BB;
    uint32_t stacks[POKER_PLAYERS];
    for (int s = 0; s < POKER_PLAYERS; s++) stacks[s] = stack;
    poker_deal(g, 0, button, stacks);
    uint64_t rng[POKER_PLAYERS];
    for (int s = 0; s < POKER_PLAYERS; s++) { rng[s] = mix(seed * 8 + s); memset(memory[s], 0, sizeof(memory[s])); }
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
        if (g->street[0] > 0 && g->street[0] != street) for (int q = 0; q < POKER_PLAYERS; q++) if (g->live[0] >> q & 1) flop[q] = 1;
        if (g->stack[s][0] == 0 && action >= 0) allin[s] = 1;
    }
    int showdown = __builtin_popcount(g->live[0]) > 1;
    for (int s = 0; s < POKER_PLAYERS; s++) {
        Stat* t = &st[who[s]];
        net[s] = (int32_t)g->stack[s][0] - (int32_t)stack;
        t->net += net[s];
        t->hands++;
        t->vpip += vpip[s]; t->pfr += pfr[s]; t->allin += allin[s]; t->flop += flop[s];
        if (flop[s] && showdown && (g->live[0] >> s & 1)) { t->wtsd++; t->wsd += net[s] > 0; }
    }
}

int main(int argc, char** argv) {
    long deals = 1000, tables = 0; uint32_t stack_bb = 100; uint64_t seed = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--deals") && i + 1 < argc) deals = atol(argv[++i]);
        else if (!strcmp(argv[i], "--tables") && i + 1 < argc) tables = atol(argv[++i]);
        else if (!strcmp(argv[i], "--stack") && i + 1 < argc) stack_bb = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 10);
        else if (nplayers >= MAX_PLAYERS || !add_player(argv[i])) return 2;
    }
    if (nplayers < POKER_PLAYERS || (!tables && nplayers != POKER_PLAYERS)) {
        fprintf(stderr, "usage: lineup [--deals D] [--tables T] [--stack BB] [--seed S] PLAYER x6 (or a pool with --tables)\n");
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
    Stat total[MAX_PLAYERS] = {0};
    long jobs = tables * deals, done = 0;
    double t0 = omp_get_wtime();
    #pragma omp parallel
    {
        Stat st[MAX_PLAYERS] = {0};
        static __thread Poker g;
        static __thread float memory[POKER_PLAYERS][4 * POKER_POLICY_MAX_HIDDEN];
        #pragma omp for schedule(dynamic, 4)
        for (long j = 0; j < jobs; j++) {
            const int* lineup = lineups[j / deals];
            uint64_t deck = mix(seed * 7919 + (uint64_t)j);
            int button = (int)(mix(deck) % POKER_PLAYERS);
            double per[MAX_PLAYERS] = {0};
            int count[MAX_PLAYERS] = {0};
            for (int rot = 0; rot < POKER_PLAYERS; rot++) {
                int who[POKER_PLAYERS];
                int32_t net[POKER_PLAYERS];
                for (int s = 0; s < POKER_PLAYERS; s++) who[s] = lineup[(s + rot) % POKER_PLAYERS];
                play(&g, who, button, stack_bb * POKER_BB, deck, mix(deck + 31 * rot), st, net, memory);
                for (int s = 0; s < POKER_PLAYERS; s++) { per[who[s]] += net[s] / 100.0; count[who[s]]++; }
            }
            for (int p = 0; p < nplayers; p++) if (count[p]) {
                double m = per[p] / count[p];
                st[p].deal_sum += m; st[p].deal_sq += m * m; st[p].deals++;
            }
            #pragma omp atomic
            done++;
            if (omp_get_thread_num() == 0 && done % 200 == 0)
                fprintf(stderr, "\r%ld / %ld deals (%.0f s)", done, jobs, omp_get_wtime() - t0);
        }
        #pragma omp critical
        for (int p = 0; p < nplayers; p++) {
            Stat* a = &total[p]; const Stat* b = &st[p];
            a->net += b->net; a->deal_sum += b->deal_sum; a->deal_sq += b->deal_sq;
            a->hands += b->hands; a->deals += b->deals; a->vpip += b->vpip; a->pfr += b->pfr; a->allin += b->allin;
            a->flop += b->flop; a->wtsd += b->wtsd; a->wsd += b->wsd; a->raises += b->raises; a->calls += b->calls;
        }
    }
    fprintf(stderr, "\r%ld deals x 6 rotations in %.0f s%20s\n", jobs, omp_get_wtime() - t0, "");
    printf("%ld table%s x %ld deals x 6 rotations, %u BB stacks, rake 5%% capped at 3 BB, seed %llu\n",
        tables, tables > 1 ? "s" : "", deals, stack_bb, (unsigned long long)seed);
    printf("%-14s %9s %7s %6s %5s %6s %5s %5s %5s %5s %8s\n", "player", "BB/100", "+/-95%", "VPIP", "PFR", "all-in",
        "flop", "WTSD", "W$SD", "AF", "hands");
    int order[MAX_PLAYERS];
    for (int i = 0; i < nplayers; i++) order[i] = i;
    for (int i = 0; i < nplayers; i++) for (int j = i + 1; j < nplayers; j++) {
        const Stat *a = &total[order[i]], *b = &total[order[j]];
        if ((b->deals ? b->deal_sum / b->deals : -1e9) > (a->deals ? a->deal_sum / a->deals : -1e9)) {
            int x = order[i]; order[i] = order[j]; order[j] = x;
        }
    }
    for (int k = 0; k < nplayers; k++) {
        int p = order[k]; const Stat* s = &total[p];
        if (!s->deals) continue;
        double mean = s->deal_sum / s->deals, var = s->deal_sq / s->deals - mean * mean;
        double ci = 1.96 * sqrt(var > 0 ? var : 0) / sqrt((double)s->deals);
        double h = (double)s->hands;
        printf("%-14s %+9.1f %7.1f %5.0f%% %4.0f%% %5.1f%% %4.0f%% %4.0f%% %4.0f%% %5.2f %8ld\n", players[p].name,
            100 * mean, 100 * ci, 100 * s->vpip / h, 100 * s->pfr / h, 100 * s->allin / h, 100 * s->flop / h,
            s->flop ? 100.0 * s->wtsd / s->flop : 0, s->wtsd ? 100.0 * s->wsd / s->wtsd : 0,
            s->calls ? (double)s->raises / s->calls : 0, s->hands);
    }
    return 0;
}
