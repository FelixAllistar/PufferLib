// Behavior fingerprints (2026-11-05): describe a policy by what it does in fixed situations, not by its weights.
//
// bank: plays HANDS hands at random six-seat tables drawn from the given players (100 BB, training rake) and keeps a
//   stratified sample of decision points: street (4) x what the actor faces (unopened, a bet, a raise after its own
//   bet or raise) x position from the button (6) x hand-strength bucket (5: preflop by the hand-class score, postflop
//   by the made-hand category), at most CAP per stratum. A probe stores the deck seed, button and every action before
//   the decision, so any player can be put in that exact spot with the hand's real history.
// probe: for each player, its action distribution over the 8 engine actions (fold, check/call, min raise, +1/3 pot,
//   +2/3 pot, +pot, +2 pots, all in) at every probe. Models: the hand is replayed with the probed seat's model run at
//   each of its earlier decisions (its recurrent memory follows the real history; the recorded actions are played),
//   then the masked softmax of its logits (no sampling noise). Bots: SAMPLES decisions on copies of the state, each
//   chip amount mapped to the nearest legal action.
//
// Files: bank = probes (binary, struct Probe); probe output = text, one line per player: name then 8 x probes floats.
// fingerprint.py turns them into distances, a map and per-situation summaries.
//
// Build: gcc -O3 -march=native -std=gnu11 -fopenmp -DNDEBUG -DPOKER_FULLHOUSE_MODEL='"/path/fullhouse_weights.h"'
//   -Iocean/poker ocean/poker/fingerprint.c -lm -o build/poker/fingerprint
// Usage: fingerprint bank BANK HANDS CAP PLAYER...   |   fingerprint probe BANK OUT SAMPLES PLAYER...
#include <omp.h>
#include "players.h"

#define PROBE_MAX_ACTIONS 96
typedef struct {
    uint64_t deck;
    uint8_t button, seat, count, street, facing, position, strength, pad;
    int32_t actions[PROBE_MAX_ACTIONS];
} Probe;
#define STRATA (4 * 3 * POKER_PLAYERS * 5)

static int strength_bucket(Poker* g, int p) {
    uint8_t h0 = g->hole[p][0][0], h1 = g->hole[p][1][0];
    if (!g->street[0]) {
        int r0 = h0 >> 2, r1 = h1 >> 2, hi = r0 > r1 ? r0 : r1, lo = r0 < r1 ? r0 : r1, suited = (h0 & 3) == (h1 & 3);
        float score = (r0 == r1 ? 30 + 3 * hi : 2 * hi + lo + 4 * suited + 4 * (hi - lo <= 2)) / 66.0f;
        return score < .3f ? 0 : score < .4f ? 1 : score < .5f ? 2 : score < .6f ? 3 : 4;
    }
    uint8_t cards[7] = {h0, h1};
    int n = g->street[0] + 2;
    for (int i = 0; i < n; i++) cards[2 + i] = g->board[i][0];
    int made = poker_rank(cards, n + 2) >> 24;  // 0 high card, 1 pair, 2 two pair, 3 trips, 4+ straight or better
    return made < 4 ? made : 4;
}

static void deal(Poker* g, uint64_t deck, int button) {
    memset(g, 0, sizeof(*g));
    g->rng[0] = deck;
    g->rake_permille = 50;
    g->rake_cap = 3 * POKER_BB;
    uint32_t stacks[POKER_PLAYERS];
    for (int s = 0; s < POKER_PLAYERS; s++) stacks[s] = 100 * POKER_BB;
    poker_deal(g, 0, button, stacks);
}

static int32_t decide(const Player* pl, Poker* g, uint64_t* rng, float* memory) {
    if (pl->bot) return poker_decide(g, 0, rng, pl->bot, 0, NULL);
    float obs[POKER_PUB], logits[POKER_ACTIONS + 1];
    int32_t amounts[POKER_ACTIONS];
    unsigned mask = poker_actions(g, 0, amounts);
    poker_observe_public(g, 0, g->actor[0], obs);
    forward(pl, memory, obs, logits);
    return amounts[poker_policy_sample(logits, mask, poker_random(rng))];
}

static int bank(const char* path, long hands, int cap, uint64_t seed) {
    Probe* kept = calloc((size_t)STRATA * cap, sizeof(Probe));
    long seen[STRATA] = {0};
    static float memory[POKER_PLAYERS][4 * POKER_POLICY_MAX_HIDDEN];
    Poker g;
    for (long h = 0; h < hands; h++) {
        uint64_t deck = mix(seed * 7919 + h), r = mix(deck);
        int who[POKER_PLAYERS], order[MAX_PLAYERS];
        for (int i = 0; i < nplayers; i++) order[i] = i;
        for (int s = 0; s < POKER_PLAYERS; s++) {  // six distinct players when the pool allows, else with repeats
            r = mix(r);
            if (nplayers >= POKER_PLAYERS) { int j = s + (int)(r % (uint64_t)(nplayers - s)); int x = order[s]; order[s] = order[j]; order[j] = x; who[s] = order[s]; }
            else who[s] = (int)(r % (uint64_t)nplayers);
        }
        int button = (int)(mix(r) % POKER_PLAYERS);
        deal(&g, deck, button);
        uint64_t rng[POKER_PLAYERS];
        for (int s = 0; s < POKER_PLAYERS; s++) { rng[s] = mix(deck * 8 + s); memset(memory[s], 0, sizeof(memory[s])); }
        int32_t actions[PROBE_MAX_ACTIONS];
        int count = 0, aggressed[POKER_PLAYERS] = {0};
        while (g.actor[0] != POKER_DONE && count < PROBE_MAX_ACTIONS) {
            int s = g.actor[0];
            uint32_t call = g.high[0] - g.bet[s][0];
            // Preflop the blinds are not a bet: unopened until someone raises.
            int opened = g.street[0] ? call > 0 : g.high[0] > POKER_BB;
            int facing = !opened ? 0 : aggressed[s] && call ? 2 : 1;
            int position = (s - g.button[0] + POKER_PLAYERS) % POKER_PLAYERS;
            int stratum = ((g.street[0] * 3 + facing) * POKER_PLAYERS + position) * 5 + strength_bucket(&g, s);
            // Reservoir sample per stratum.
            long n = seen[stratum]++;
            long slot = n < cap ? n : (long)(mix(deck + 977 * count) % (uint64_t)(n + 1));
            if (slot < cap) {
                Probe* p = &kept[(size_t)stratum * cap + slot];
                p->deck = deck; p->button = (uint8_t)button; p->seat = (uint8_t)s; p->count = (uint8_t)count;
                p->street = g.street[0]; p->facing = (uint8_t)facing; p->position = (uint8_t)position;
                p->strength = (uint8_t)strength_bucket(&g, s);
                memcpy(p->actions, actions, count * sizeof(int32_t));
            }
            int32_t a = decide(&players[who[s]], &g, &rng[s], memory[s]);
            int street = g.street[0];
            poker_step(&g, 0, a);
            actions[count++] = a;
            // Own aggression this street; a new street resets it.
            if (g.street[0] != street) memset(aggressed, 0, sizeof(aggressed));
            else if (a > 0) aggressed[s] = 1;
        }
    }
    FILE* f = fopen(path, "wb");
    long total = 0, filled = 0;
    for (int k = 0; k < STRATA; k++) {
        long n = seen[k] < cap ? seen[k] : cap;
        if (n) filled++;
        fwrite(&kept[(size_t)k * cap], sizeof(Probe), n, f);
        total += n;
    }
    fclose(f);
    printf("bank %s: %ld probes from %ld hands, %ld of %d strata filled\n", path, total, hands, filled, STRATA);
    return 0;
}

// Replays probe pr with player pl in its seat and writes its 8-action distribution to dist.
static void probe_one(const Player* pl, const Probe* pr, int samples, float* dist) {
    Poker g;
    float memory[4 * POKER_POLICY_MAX_HIDDEN] = {0};
    deal(&g, pr->deck, pr->button);
    for (int i = 0; i < pr->count; i++) {
        if (!pl->bot && g.actor[0] == pr->seat) {  // keep the model's memory on the real history
            float obs[POKER_PUB], logits[POKER_ACTIONS + 1];
            poker_observe_public(&g, 0, pr->seat, obs);
            forward(pl, memory, obs, logits);
        }
        poker_step(&g, 0, pr->actions[i]);
    }
    memset(dist, 0, POKER_ACTIONS * sizeof(float));
    int32_t amounts[POKER_ACTIONS];
    unsigned mask = poker_actions(&g, 0, amounts);
    if (g.actor[0] != pr->seat) return;  // should not happen: the bank recorded this seat to act
    if (!pl->bot) {
        float obs[POKER_PUB], logits[POKER_ACTIONS + 1], max = -INFINITY, total = 0;
        poker_observe_public(&g, 0, pr->seat, obs);
        forward(pl, memory, obs, logits);
        for (int a = 0; a < POKER_ACTIONS; a++) if (mask >> a & 1 && logits[a] > max) max = logits[a];
        for (int a = 0; a < POKER_ACTIONS; a++) { dist[a] = mask >> a & 1 ? expf(logits[a] - max) : 0; total += dist[a]; }
        for (int a = 0; a < POKER_ACTIONS; a++) dist[a] /= total;
        return;
    }
    for (int k = 0; k < samples; k++) {
        Poker copy = g;
        uint64_t rng = mix(pr->deck * 131 + k);
        int32_t chips = poker_decide(&copy, 0, &rng, pl->bot, 0, NULL);
        int best = chips < 0 ? 0 : chips == 0 ? 1 : -1;
        if (best < 0) {
            long gap = -1;
            for (int a = 2; a < POKER_ACTIONS; a++) if (mask >> a & 1) {
                long d = labs((long)amounts[a] - chips);
                if (gap < 0 || d < gap) { gap = d; best = a; }
            }
            if (best < 0) best = 1;
        }
        dist[best] += 1.0f / samples;
    }
}

static int probe(const char* bank_path, const char* out_path, int samples) {
    FILE* f = fopen(bank_path, "rb");
    if (!f) { perror(bank_path); return 1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f) / (long)sizeof(Probe);
    fseek(f, 0, SEEK_SET);
    Probe* probes = malloc(n * sizeof(Probe));
    if (fread(probes, sizeof(Probe), n, f) != (size_t)n) return 1;
    fclose(f);
    FILE* out = fopen(out_path, "w");
    fprintf(out, "probes %ld\n", n);
    for (long i = 0; i < n; i++) fprintf(out, "%s%d %d %d %d %d", i ? " " : "situations ", probes[i].street,
        probes[i].facing, probes[i].position, probes[i].strength, probes[i].count);
    fprintf(out, "\n");
    float* dist = malloc(n * POKER_ACTIONS * sizeof(float));
    for (int p = 0; p < nplayers; p++) {
        double t0 = omp_get_wtime();
        #pragma omp parallel for schedule(dynamic, 16)
        for (long i = 0; i < n; i++) probe_one(&players[p], &probes[i], samples, dist + i * POKER_ACTIONS);
        fprintf(out, "%s", players[p].name);
        for (long i = 0; i < n * POKER_ACTIONS; i++) fprintf(out, " %.4g", dist[i]);
        fprintf(out, "\n");
        fflush(out);
        fprintf(stderr, "%s: %ld probes in %.0f s\n", players[p].name, n, omp_get_wtime() - t0);
    }
    fclose(out);
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 6 && !strcmp(argv[1], "bank")) {
        for (int i = 5; i < argc; i++) if (nplayers >= MAX_PLAYERS || !add_player(argv[i])) return 2;
        return bank(argv[2], atol(argv[3]), atoi(argv[4]), 1);
    }
    if (argc >= 6 && !strcmp(argv[1], "probe")) {
        for (int i = 5; i < argc; i++) if (nplayers >= MAX_PLAYERS || !add_player(argv[i])) return 2;
        return probe(argv[2], argv[3], atoi(argv[4]));
    }
    fprintf(stderr, "usage: fingerprint bank BANK HANDS CAP PLAYER...  |  fingerprint probe BANK OUT SAMPLES PLAYER...\n");
    return 2;
}
