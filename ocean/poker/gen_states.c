// State set generator (2026-10-28, states.h): plays 6-max cash sessions between every bot and policy we have and
// captures decision points as training start states, biased to the deep spots shove-or-fold self-play never
// reaches (turn and river, facing a bet, big pots with chips behind). Each session seats a fresh mix: every seat is
// one of our policies with probability --policy-share (default 0.1), otherwise a uniformly drawn bot (rules,
// Fullhouse when built with it, range, maniac, station, tight, trapper). Seats keep their style for the session;
// a seat that busts or passes the cap is replaced by a new player of a new style. Stacks: 20-250 BB, cap 500 BB.
// Build: clang -O2 -march=native -ffp-contract=off -std=c11 -DNDEBUG
//   -DPOKER_FULLHOUSE_MODEL='"/path/fullhouse_weights.h"' ocean/poker/gen_states.c -lm -o build/poker/gen_states
// Usage: gen_states OUT STATES [--seed S] [--policy-share F] [--policy WEIGHTS HIDDEN LAYERS ...]
#include <stdio.h>
#include "policy.h"
#include "states.h"

#define GEN_MAX_POLICIES 8
#define GEN_SESSION 200

static PokerPolicy policies[GEN_MAX_POLICIES];
static int policy_count;
static float memory[POKER_PLAYERS][4 * POKER_POLICY_MAX_HIDDEN];
static uint64_t rng;
static float policy_share = 0.1f;
static int bots[POKER_BOT_COUNT], bot_count;

static float uniform(void) { return (poker_random(&rng) >> 8) * 0x1p-24f; }

static uint8_t new_style(void) {
    if (policy_count && uniform() < policy_share)
        return POKER_STATE_POLICY + poker_random(&rng) % policy_count;
    return bots[poker_random(&rng) % bot_count];
}

static uint32_t sit_down(void) { return (20 + poker_random(&rng) % 231) * POKER_BB; }

static int32_t move(Poker* g, int p, uint8_t style) {
    if (style < POKER_STATE_POLICY) return poker_decide(g, 0, &rng, style, 0, NULL);
    float obs[POKER_OBS], logits[POKER_ACTIONS + 1];
    int32_t amounts[POKER_ACTIONS];
    unsigned mask = poker_actions(g, 0, amounts);
    poker_observe(g, 0, p, obs);
    poker_policy_forward(&policies[style - POKER_STATE_POLICY], memory[p], obs, logits);
    return amounts[poker_policy_sample(logits, mask, poker_random(&rng))];
}

// Capture probability of the current decision. Preflop and shallow spots are kept rarely: the learner already sees
// plenty of them; turn/river decisions facing a bet with chips behind are what it lacks.
static float keep(Poker* g) {
    static const float street[4] = {0.004f, 0.05f, 0.15f, 0.2f};
    int p = g->actor[0];
    uint32_t call = g->high[0] - g->bet[p][0];
    float w = street[g->street[0]];
    if (call) w *= 2;
    if (g->pot[0] >= 20 * POKER_BB) w *= 2;
    if (g->stack[p][0] < g->pot[0] / 2) w *= 0.25f;  // little behind: a push/fold spot
    return w < 1 ? w : 1;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: gen_states OUT STATES [--seed S] [--policy-share F] [--policy WEIGHTS HIDDEN LAYERS ...]\n");
        return 2;
    }
    long target = atol(argv[2]);
    rng = 0x9e3779b97f4a7c15ull;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) rng ^= (uint64_t)atoll(argv[++i]) * 0xbf58476d1ce4e5b9ull;
        else if (!strcmp(argv[i], "--policy-share") && i + 1 < argc) policy_share = atof(argv[++i]);
        else if (!strcmp(argv[i], "--policy") && i + 3 < argc && policy_count < GEN_MAX_POLICIES) {
            int h = atoi(argv[i + 2]), l = atoi(argv[i + 3]);
            size_t n = poker_policy_size(h, l);
            float* w = malloc(n * sizeof(float));
            FILE* f = fopen(argv[i + 1], "rb");
            if (!f || fread(w, sizeof(float), n, f) != n || fgetc(f) != EOF) {
                fprintf(stderr, "cannot read %s as %dx%d\n", argv[i + 1], h, l);
                return 2;
            }
            fclose(f);
            policies[policy_count++] = (PokerPolicy){w, h, l};
            i += 3;
        } else {
            fprintf(stderr, "unknown argument %s\n", argv[i]);
            return 2;
        }
    }
    for (int b = 0; b < POKER_BOT_COUNT; b++) if (poker_bot_supported(b)) bots[bot_count++] = b;
    FILE* out = fopen(argv[1], "wb");
    if (!out) { fprintf(stderr, "cannot write %s\n", argv[1]); return 2; }
    static Poker g;
    g.rng[0] = rng ^ 0x5851f42d4c957f2dull;
    uint8_t styles[POKER_PLAYERS];
    uint32_t stacks[POKER_PLAYERS];
    long saved = 0, hands = 0, by_street[4] = {0}, by_actor[256] = {0};
    double pot_bb = 0;
    while (saved < target) {
        if (hands % GEN_SESSION == 0)
            for (int p = 0; p < POKER_PLAYERS; p++) { styles[p] = new_style(); stacks[p] = sit_down(); }
        poker_deal(&g, 0, hands % POKER_PLAYERS, stacks);
        memset(memory, 0, sizeof(memory));
        while (g.actor[0] != POKER_DONE) {
            int p = g.actor[0];
            if (saved < target && uniform() < keep(&g)) {
                PokerState s;
                poker_state_capture(&g, 0, styles, &s);
                fwrite(&s, sizeof(s), 1, out);
                saved++;
                by_street[g.street[0]]++;
                by_actor[styles[p]]++;
                pot_bb += g.pot[0] / (double)POKER_BB;
            }
            poker_step(&g, 0, move(&g, p, styles[p]));
        }
        hands++;
        for (int p = 0; p < POKER_PLAYERS; p++) {
            stacks[p] = g.stack[p][0];
            if (stacks[p] < POKER_BB || stacks[p] > 500 * POKER_BB) { styles[p] = new_style(); stacks[p] = sit_down(); }
        }
    }
    fclose(out);
    printf("%ld states from %ld hands (%zu bytes each); streets pre %ld flop %ld turn %ld river %ld; mean pot %.1f BB\n",
        saved, hands, sizeof(PokerState), by_street[0], by_street[1], by_street[2], by_street[3], pot_bb / (saved ? saved : 1));
    printf("deciding seat creator:");
    for (int c = 0; c < 256; c++) if (by_actor[c]) printf(" %d:%ld", c, by_actor[c]);
    printf("\n");
}
