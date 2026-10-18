#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* A control and a candidate each play the SAME frozen opponent library.
 * Separate synchronized states avoid relying on private Poker struct layout.
 * All symbol lookup is local; no policy or game code is compiled into this file.
 * Evaluation has no table history. Independent duplicate-deal blocks rotate
 * hero through every seat and are paired across arms.
 */
typedef struct {
    void *handle, *state;
    size_t (*bytes)(void);
    void (*seed)(void *, uint64_t);
    void (*reset)(void *, int, int, const uint32_t *, const uint8_t *);
    void (*step)(void *, int, int32_t);
    void (*snapshot)(void *, int, uint32_t *);
    int32_t (*decide)(void *, int, uint64_t *, int, int, void *);
    unsigned (*styles)(void);
} Library;

static void fail(const char *message) {
    fprintf(stderr, "dual_bench: %s\n", message);
    exit(2);
}

static Library open_library(const char *path) {
    Library x = {0};
    x.handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!x.handle) fail(dlerror());
    #define GET(field, symbol) do { \
        *(void **)(&x.field) = dlsym(x.handle, symbol); \
        if (!x.field) fail("missing native symbol " symbol); \
    } while (0)
    GET(bytes, "poker_bytes");
    GET(seed, "poker_seed");
    GET(reset, "poker_reset");
    GET(step, "poker_step");
    GET(snapshot, "poker_snapshot");
    GET(decide, "poker_decide");
    GET(styles, "poker_bot_styles");
    #undef GET
    size_t size = x.bytes();
    if (!size || size > (1u << 28)) fail("unexpected native state size");
    x.state = calloc(1, size);
    if (!x.state) fail("state allocation failed");
    x.seed(x.state, 1);
    return x;
}

static void close_library(Library *x) {
    free(x->state);
    dlclose(x->handle);
}

static uint64_t random_u64(uint64_t *state) {
    uint64_t x = (*state += UINT64_C(0x9e3779b97f4a7c15));
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

static uint64_t bounded(uint64_t *rng, uint64_t bound) {
    uint64_t threshold = -bound % bound;
    for (;;) {
        uint64_t value = random_u64(rng);
        if (value >= threshold) return value % bound;
    }
}

static void shuffled_deck(uint8_t *deck, uint64_t *rng) {
    for (int i = 0; i < 52; ++i) deck[i] = (uint8_t)i;
    for (int i = 0; i < 51; ++i) {
        int j = i + (int)bounded(rng, (uint64_t)(52 - i));
        uint8_t tmp = deck[i]; deck[i] = deck[j]; deck[j] = tmp;
    }
}

static double hand(Library *hero_lib, Library *opponent_lib, int players,
                   int hero_seat, int hero_style, const int *opponents, int count,
                   int hero_samples, int opponent_samples, uint32_t stack,
                   const uint8_t *deck, uint64_t seed, uint64_t block,
                   uint64_t *decisions) {
    uint32_t stacks[9], state[78] = {0}, other[78] = {0}, before[78] = {0};
    uint64_t rng[9];
    for (int i = 0; i < 9; ++i) stacks[i] = stack;
    for (int p = 0; p < players; ++p) {
        uint64_t key = (seed ^ UINT64_C(0xd1b54a32d192ed03)) + block * 1000003
                     + (uint64_t)hero_seat * 101 + (uint64_t)p + 1;
        rng[p] = random_u64(&key);
        if (!rng[p]) rng[p] = 1;
    }
    const int words = 15 + 7 * players;
    hero_lib->reset(hero_lib->state, 0, 0, stacks, deck);
    opponent_lib->reset(opponent_lib->state, 0, 0, stacks, deck);
    hero_lib->snapshot(hero_lib->state, 0, state);
    opponent_lib->snapshot(opponent_lib->state, 0, other);
    if (state[5] != (1u << players) - 1 || other[5] != (1u << players) - 1)
        fail("library player count differs from --players");
    if (memcmp(state, other, (size_t)words * sizeof(uint32_t)))
        fail("hero and opponent engines disagree after reset");
    unsigned steps = 0;
    while (state[0] != 255) {
        int actor = (int)state[0];
        if (actor >= players) fail("bad native actor");
        int offset = (actor + players - hero_seat) % players;
        int style = offset ? opponents[(offset - 1) % count] : hero_style;
        Library *acting = offset ? opponent_lib : hero_lib;
        int samples = offset ? opponent_samples : hero_samples;
        memcpy(before, state, (size_t)words * sizeof(uint32_t));
        int32_t action = acting->decide(acting->state, 0, &rng[actor], style, samples, NULL);
        acting->snapshot(acting->state, 0, other);
        if (memcmp(before, other, (size_t)words * sizeof(uint32_t)))
            fail("policy changed game state during a decision");
        hero_lib->step(hero_lib->state, 0, action);
        opponent_lib->step(opponent_lib->state, 0, action);
        hero_lib->snapshot(hero_lib->state, 0, state);
        opponent_lib->snapshot(opponent_lib->state, 0, other);
        if (memcmp(state, other, (size_t)words * sizeof(uint32_t)))
            fail("hero and opponent engines disagree after action");
        ++*decisions;
        if (++steps > 4096) fail("hand exceeded 4096 decisions");
    }
    uint64_t total = 0;
    for (int p = 0; p < players; ++p) total += state[10 + 7 * p];
    if (total != (uint64_t)stack * (uint64_t)players) fail("chip conservation failed");
    return ((double)state[10 + 7 * hero_seat] - stack) / 100.0;
}

static FILE *new_csv(const char *prefix, const char *suffix) {
    char path[4096];
    if (snprintf(path, sizeof(path), "%s.%s.csv", prefix, suffix) >= (int)sizeof(path))
        fail("output path too long");
    FILE *file = fopen(path, "wx");
    if (!file) fail("cannot create CSV; path must exist and output must be new");
    fprintf(file, "block,mean_bb\n");
    return file;
}

int main(int argc, char **argv) {
    if (argc != 15) {
        fprintf(stderr, "usage: %s control.so candidate.so opponent.so players hero_style "
                "opponents_csv blocks seed stack_chips control_samples candidate_samples "
                "opponent_samples output_prefix label\n", argv[0]);
        return 2;
    }
    int players = atoi(argv[4]), hero_style = atoi(argv[5]), opponents[9], count = 0;
    if (players != 2 && players != 6 && players != 9) fail("players must be 2, 6, or 9");
    char *csv = strdup(argv[6]);
    if (!csv) fail("allocation failed");
    for (char *tok = strtok(csv, ","); tok; tok = strtok(NULL, ",")) {
        if (count >= 9) fail("too many opponent policies");
        opponents[count++] = atoi(tok);
    }
    free(csv);
    if (!count) fail("need opponent policies");
    uint64_t blocks = strtoull(argv[7], NULL, 10), seed = strtoull(argv[8], NULL, 10);
    uint32_t stack = (uint32_t)strtoul(argv[9], NULL, 10);
    int control_samples = atoi(argv[10]), candidate_samples = atoi(argv[11]), opponent_samples = atoi(argv[12]);
    if (blocks < 2 || stack < 100 || stack > 65535) fail("invalid blocks or stack");
    Library control = open_library(argv[1]), candidate = open_library(argv[2]);
    Library opponent_control = open_library(argv[3]), opponent_candidate = open_library(argv[3]);
    if (hero_style < 0 || hero_style >= 32 || !(control.styles() & (1u << hero_style)) ||
        !(candidate.styles() & (1u << hero_style))) fail("hero policy unavailable");
    for (int p = 0; p < count; ++p)
        if (opponents[p] < 0 || opponents[p] >= 32 || !(opponent_control.styles() & (1u << opponents[p])))
            fail("opponent policy unavailable");
    FILE *control_out = new_csv(argv[13], "control");
    FILE *candidate_out = new_csv(argv[13], "candidate");
    FILE *difference_out = new_csv(argv[13], "difference");
    uint64_t deal_rng = seed ^ UINT64_C(0xa0761d6478bd642f), decisions[2] = {0};
    double sums[3] = {0}, sums_sq[3] = {0};
    uint64_t equal_blocks = 0;
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (uint64_t block = 0; block < blocks; ++block) {
        uint8_t deck[52];
        shuffled_deck(deck, &deal_rng);
        double means[3] = {0};
        for (int hero = 0; hero < players; ++hero) {
            means[0] += hand(&control, &opponent_control, players, hero, hero_style,
                             opponents, count, control_samples, opponent_samples, stack,
                             deck, seed, block, &decisions[0]) / players;
            means[1] += hand(&candidate, &opponent_candidate, players, hero, hero_style,
                             opponents, count, candidate_samples, opponent_samples, stack,
                             deck, seed, block, &decisions[1]) / players;
        }
        means[2] = means[1] - means[0];
        equal_blocks += means[2] == 0;
        fprintf(control_out, "%" PRIu64 ",%.12f\n", block, means[0]);
        fprintf(candidate_out, "%" PRIu64 ",%.12f\n", block, means[1]);
        fprintf(difference_out, "%" PRIu64 ",%.12f\n", block, means[2]);
        for (int i = 0; i < 3; ++i) { sums[i] += means[i]; sums_sq[i] += means[i] * means[i]; }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    fclose(control_out); fclose(candidate_out); fclose(difference_out);
    double seconds = end.tv_sec - start.tv_sec + 1e-9 * (end.tv_nsec - start.tv_nsec);
    const char *names[] = {"control", "candidate", "paired_gain"};
    printf("{\"players\":%d,\"hands_per_arm\":%" PRIu64 ",\"independent_duplicate_blocks\":%" PRIu64
           ",\"seed\":%" PRIu64 ",\"stack_bb\":%.2f,\"native_history\":false,"
           "\"control_samples\":%d,\"candidate_samples\":%d,\"opponent_samples\":%d,"
           "\"seconds\":%.6f,\"decisions_control\":%" PRIu64 ",\"decisions_candidate\":%" PRIu64
           ",\"identical_blocks\":%" PRIu64 ",", players, blocks * players, blocks, seed, stack / 100.0,
           control_samples, candidate_samples, opponent_samples, seconds, decisions[0], decisions[1], equal_blocks);
    for (int i = 0; i < 3; ++i) {
        double mean = sums[i] / blocks;
        double variance = fmax(0, (sums_sq[i] - sums[i] * mean) / (blocks - 1));
        printf("%s\"%s\":{\"bb100\":%.9f,\"ci95_half_width_bb100\":%.9f}",
               i ? "," : "", names[i], 100 * mean, 196 * sqrt(variance / blocks));
    }
    printf("}\n");
    close_library(&control); close_library(&candidate);
    close_library(&opponent_control); close_library(&opponent_candidate);
    return 0;
}
