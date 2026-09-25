#include "validation.h"
#include "../../bridge.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "native spec check failed at %s:%d: %s\n",        \
                    __FILE__, __LINE__, #condition);                           \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static float f32_at(const uint32_t *row, unsigned at) {
    float value;
    memcpy(&value, row + at, sizeof value);
    return value;
}

static int close_float(float a, float b) {
    return fabsf(a - b) <= 1e-6f;
}

typedef enum {
    HIT_MISS = 0,
    HIT_ONE = 1,
    HIT_TWO = 2,
} Hit;

typedef struct {
    unsigned left;
    unsigned top;
} Rect;

typedef struct {
    Rect one;
    Rect two;
    unsigned clicks;
    Hit first;
    unsigned status; /* 0 running, 1 finished, 2 timeout */
    unsigned success;
} Sequence;

static int inside(Rect r, unsigned x, unsigned y) {
    return x >= r.left && x < r.left + 40u &&
           y >= r.top && y < r.top + 40u;
}

/* The second button is later in the source HTML, so it is the event target
 * when both rectangles contain the coordinate. */
static Hit hit_at(const Sequence *s, unsigned x, unsigned y) {
    if (inside(s->two, x, y)) return HIT_TWO;
    if (inside(s->one, x, y)) return HIT_ONE;
    return HIT_MISS;
}

static Sequence sequence_step(Sequence s, unsigned command, unsigned x,
                              unsigned y, unsigned elapsed) {
    if (command == 4u) {
        s.clicks = 0u;
        s.first = HIT_MISS;
        s.status = 0u;
        s.success = 0u;
        return s;
    }
    if (s.status != 0u) return s;
    if (elapsed >= 10000u) {
        s.status = 2u;
        s.success = 0u;
        return s;
    }
    if (command == 3u) {
        s.status = 2u;
        s.success = 0u;
        return s;
    }
    if (command != 1u) return s;
    Hit hit = hit_at(&s, x, y);
    if (hit == HIT_MISS) return s;
    if (s.clicks == 0u) {
        s.clicks = 1u;
        s.first = hit;
    } else {
        s.clicks = 2u;
        s.status = 1u;
        s.success = s.first == HIT_ONE && hit == HIT_TWO;
    }
    return s;
}

static double timed_reward(unsigned status, unsigned success,
                           unsigned elapsed) {
    if (status == 0u) return 0.0;
    if (status == 2u || !success) return -1.0;
    double reward = 1.0 - (double)elapsed / 10000.0;
    return reward > 0.0 ? reward : 0.0;
}

static float raw_reward(unsigned status, unsigned success) {
    if (status == 0u) return 0.0f;
    if (status == 2u || !success) return -1.0f;
    return 1.0f;
}

static unsigned hit_code(Hit hit) {
    return (unsigned)hit;
}

typedef struct {
    Sequence state;
    unsigned last_hit;
    unsigned saved;
    float raw;
    float timed;
} SequenceRef;

static void set_f32(uint32_t *row, unsigned at, float value) {
    memcpy(row + at, &value, sizeof value);
}

static void encode_sequence(uint32_t *row, const SequenceRef *ref,
                            unsigned command, unsigned x, unsigned y,
                            unsigned elapsed) {
    memset(row, 0, 256u * sizeof *row);
    row[0] = 7u;
    row[1] = ref->state.status;
    row[2] = ref->state.success;
    row[3] = command;
    row[4] = x;
    row[5] = y;
    row[6] = elapsed;
    row[7] = ref->saved;
    set_f32(row, 8u, ref->raw);
    set_f32(row, 9u, ref->timed);
    row[10] = ref->state.clicks;
    row[11] = hit_code(ref->state.first);
    row[12] = ref->last_hit;
    row[13] = ref->state.one.left;
    row[14] = ref->state.one.top;
    row[15] = ref->state.two.left;
    row[16] = ref->state.two.top;
    row[17] = 1u;
}

static SequenceRef sequence_transition(SequenceRef ref, unsigned command,
                                        unsigned x, unsigned y,
                                        unsigned elapsed) {
    Sequence before = ref.state;
    ref.state = sequence_step(before, command, x, y, elapsed);
    if (command == 4u) {
        ref.last_hit = 0u;
        ref.saved = 0u;
        ref.raw = 0.0f;
        ref.timed = 0.0f;
    } else {
        if (command == 1u && before.status == 0u && elapsed < 10000u)
            ref.last_hit = hit_code(hit_at(&before, x, y));
        if (before.status == 0u) {
            ref.saved = elapsed;
            ref.raw = raw_reward(ref.state.status, ref.state.success);
            ref.timed = (float)timed_reward(ref.state.status,
                                            ref.state.success, elapsed);
        }
    }
    return ref;
}

static int check_unchanged_except(const uint32_t *actual,
                                  const uint32_t *input, const unsigned *skip,
                                  unsigned skip_count) {
    for (unsigned i = 0; i < 256u; ++i) {
        int ignored = 0;
        for (unsigned j = 0; j < skip_count; ++j)
            if (i == skip[j]) ignored = 1;
        if (!ignored && actual[i] != input[i]) return 0;
    }
    return 1;
}

static int run_sequence_batch(SequenceRef refs[WEBNAV_BATCH],
                               const unsigned command[WEBNAV_BATCH],
                               const unsigned x[WEBNAV_BATCH],
                               const unsigned y[WEBNAV_BATCH],
                               const unsigned elapsed[WEBNAV_BATCH]) {
    uint32_t words[WEBNAV_BATCH * WEBNAV_WORDS];
    uint32_t input_words[WEBNAV_BATCH * WEBNAV_WORDS];
    SequenceRef expected[WEBNAV_BATCH];
    unsigned skip[] = {1u, 2u, 7u, 8u, 9u, 10u, 11u, 12u};
    for (unsigned lane = 0; lane < WEBNAV_BATCH; ++lane) {
        uint32_t *row = words + lane * WEBNAV_WORDS;
        encode_sequence(row, refs + lane, command[lane], x[lane], y[lane],
                        elapsed[lane]);
        expected[lane] = sequence_transition(refs[lane], command[lane],
                                              x[lane], y[lane], elapsed[lane]);
    }
    memcpy(input_words, words, sizeof words);
    webnav_batch(words);
    for (unsigned lane = 0; lane < WEBNAV_BATCH; ++lane) {
        const uint32_t *row = words + lane * WEBNAV_WORDS;
        const SequenceRef *want = expected + lane;
        const uint32_t *input = input_words + lane * WEBNAV_WORDS;
        unsigned reset_skip[] = {1u, 2u, 6u, 7u, 8u, 9u, 10u, 11u, 12u};
        const unsigned *ignored = command[lane] == 4u ? reset_skip : skip;
        unsigned ignored_count = command[lane] == 4u
                                     ? sizeof reset_skip / sizeof reset_skip[0]
                                     : sizeof skip / sizeof skip[0];
        CHECK(row[0] == 7u);
        CHECK(row[1] == want->state.status);
        CHECK(row[2] == want->state.success);
        CHECK(row[10] == want->state.clicks);
        CHECK(row[11] == hit_code(want->state.first));
        CHECK(row[12] == want->last_hit);
        CHECK(row[7] == want->saved);
        CHECK(close_float(f32_at(row, 8u), want->raw));
        CHECK(close_float(f32_at(row, 9u), want->timed));
        if (!check_unchanged_except(row, input, ignored, ignored_count)) {
            for (unsigned i = 0; i < 256u; ++i)
                if (row[i] != input[i]) {
                    fprintf(stderr, "sequence lane %u changed word %u: %u -> %u\n",
                            lane, i, input[i], row[i]);
                    break;
                }
            return 1;
        }
        refs[lane] = *want;
    }
    return 0;
}

typedef struct {
    const char *options[9];
    unsigned count;
    unsigned target;
    unsigned selected;
    unsigned status;
    unsigned success;
    unsigned saved;
    float raw;
    float timed;
} ChooseRef;

static void pack_ascii(uint32_t *row, unsigned offset, const char *text);

static ChooseRef choose_transition(ChooseRef ref, unsigned command,
                                   unsigned index, unsigned elapsed) {
    unsigned before_status = ref.status;
    if (command == 4u) {
        ref.selected = 0u;
        ref.status = 0u;
        ref.success = 0u;
        ref.saved = 0u;
        ref.raw = 0.0f;
        ref.timed = 0.0f;
        return ref;
    }
    if (before_status != 0u) return ref;
    if (elapsed >= 10000u) {
        ref.status = 2u;
        ref.success = 0u;
    } else if (command == 1u) {
        if (index < ref.count) ref.selected = index;
    } else if (command == 2u) {
        ref.status = 1u;
        ref.success = strcmp(ref.options[ref.selected],
                             ref.options[ref.target]) == 0;
    } else if (command == 3u) {
        ref.status = 2u;
        ref.success = 0u;
    }
    ref.saved = elapsed;
    ref.raw = raw_reward(ref.status, ref.success);
    ref.timed = (float)timed_reward(ref.status, ref.success, elapsed);
    return ref;
}

static void encode_choose(uint32_t *row, const ChooseRef *ref,
                          unsigned command, unsigned index,
                          unsigned elapsed) {
    memset(row, 0, 256u * sizeof *row);
    row[0] = 8u;
    row[1] = ref->status;
    row[2] = ref->success;
    row[3] = command;
    row[4] = index;
    row[6] = elapsed;
    row[7] = ref->saved;
    set_f32(row, 8u, ref->raw);
    set_f32(row, 9u, ref->timed);
    row[10] = ref->selected;
    row[11] = ref->target;
    row[12] = 1u;
    row[13] = ref->count;
    for (unsigned i = 0; i < ref->count; ++i) {
        unsigned length = (unsigned)strlen(ref->options[i]);
        row[17u + i] = length;
        pack_ascii(row, 32u + 16u * i, ref->options[i]);
    }
}

static int run_choose_batch(ChooseRef refs[WEBNAV_BATCH],
                            const unsigned command[WEBNAV_BATCH],
                            const unsigned index[WEBNAV_BATCH],
                            const unsigned elapsed[WEBNAV_BATCH]) {
    uint32_t words[WEBNAV_BATCH * WEBNAV_WORDS];
    uint32_t input_words[WEBNAV_BATCH * WEBNAV_WORDS];
    ChooseRef expected[WEBNAV_BATCH];
    unsigned skip[] = {1u, 2u, 7u, 8u, 9u, 10u};
    for (unsigned lane = 0; lane < WEBNAV_BATCH; ++lane) {
        uint32_t *row = words + lane * WEBNAV_WORDS;
        encode_choose(row, refs + lane, command[lane], index[lane],
                      elapsed[lane]);
        expected[lane] = choose_transition(refs[lane], command[lane],
                                           index[lane], elapsed[lane]);
    }
    memcpy(input_words, words, sizeof words);
    webnav_batch(words);
    for (unsigned lane = 0; lane < WEBNAV_BATCH; ++lane) {
        const uint32_t *row = words + lane * WEBNAV_WORDS;
        const uint32_t *input = input_words + lane * WEBNAV_WORDS;
        const ChooseRef *want = expected + lane;
        unsigned reset_skip[] = {1u, 2u, 6u, 7u, 8u, 9u, 10u};
        const unsigned *ignored = command[lane] == 4u ? reset_skip : skip;
        unsigned ignored_count = command[lane] == 4u
                                     ? sizeof reset_skip / sizeof reset_skip[0]
                                     : sizeof skip / sizeof skip[0];
        CHECK(row[0] == 8u);
        CHECK(row[1] == want->status);
        CHECK(row[2] == want->success);
        CHECK(row[10] == want->selected);
        CHECK(row[7] == want->saved);
        CHECK(close_float(f32_at(row, 8u), want->raw));
        CHECK(close_float(f32_at(row, 9u), want->timed));
        CHECK(check_unchanged_except(row, input, ignored, ignored_count));
        refs[lane] = *want;
    }
    return 0;
}

static int choose_submit(const char *const *options, unsigned count,
                         unsigned selected, const char *target) {
    if (selected >= count) return 0;
    return strcmp(options[selected], target) == 0;
}

static void pack_ascii(uint32_t *row, unsigned offset, const char *text) {
    unsigned length = (unsigned)strlen(text);
    for (unsigned i = 0; i < length; ++i)
        row[offset + i / 4u] |= (uint32_t)(unsigned char)text[i] << (8u * (i % 4u));
}

static int test_differential(void) {
    SequenceRef sequence[WEBNAV_BATCH] = {0};
    unsigned command[WEBNAV_BATCH], x[WEBNAV_BATCH], y[WEBNAV_BATCH];
    unsigned elapsed[WEBNAV_BATCH];
    for (unsigned lane = 0; lane < WEBNAV_BATCH; ++lane) {
        sequence[lane].state.one = (Rect){10u, 50u};
        sequence[lane].state.two = (Rect){60u, 50u};
        if (lane == 0u) sequence[lane].state.two = sequence[lane].state.one;
        sequence[lane].state.first = HIT_MISS;
    }

    for (unsigned lane = 0; lane < WEBNAV_BATCH; ++lane) {
        command[lane] = 0u;
        x[lane] = y[lane] = elapsed[lane] = 0u;
    }
    CHECK(run_sequence_batch(sequence, command, x, y, elapsed) == 0);

    for (unsigned lane = 0; lane < WEBNAV_BATCH; ++lane) {
        command[lane] = 1u;
        x[lane] = 200u;
        y[lane] = 200u;
        elapsed[lane] = 20u;
    }
    command[1] = 1u;
    x[1] = 20u;
    y[1] = 60u;
    elapsed[1] = 10u;
    command[2] = 1u;
    x[2] = 70u;
    y[2] = 60u;
    elapsed[2] = 11u;
    command[3] = 0u;
    command[4] = 3u;
    elapsed[4] = 25u;
    command[5] = 1u;
    x[5] = 20u;
    y[5] = 60u;
    elapsed[5] = 15u;
    CHECK(run_sequence_batch(sequence, command, x, y, elapsed) == 0);

    for (unsigned lane = 0; lane < WEBNAV_BATCH; ++lane) {
        command[lane] = 0u;
        x[lane] = y[lane] = 0u;
        elapsed[lane] = 30u;
    }
    command[0] = 1u;
    x[0] = 20u;
    y[0] = 60u;
    command[1] = 1u;
    x[1] = 70u;
    y[1] = 60u;
    command[2] = 1u;
    x[2] = 20u;
    y[2] = 60u;
    command[3] = 1u;
    x[3] = 20u;
    y[3] = 60u;
    command[4] = 1u;
    x[4] = 20u;
    y[4] = 60u;
    command[5] = 1u;
    x[5] = 70u;
    y[5] = 60u;
    CHECK(run_sequence_batch(sequence, command, x, y, elapsed) == 0);

    for (unsigned lane = 0; lane < WEBNAV_BATCH; ++lane) {
        command[lane] = 4u;
        x[lane] = y[lane] = 0u;
        elapsed[lane] = 12000u;
    }
    CHECK(run_sequence_batch(sequence, command, x, y, elapsed) == 0);

    for (unsigned lane = 0; lane < WEBNAV_BATCH; ++lane) {
        command[lane] = 1u;
        x[lane] = 20u;
        y[lane] = 60u;
        elapsed[lane] = 10000u;
    }
    command[0] = 1u;
    elapsed[0] = 1u;
    command[1] = 1u;
    elapsed[1] = 2u;
    CHECK(run_sequence_batch(sequence, command, x, y, elapsed) == 0);

    static const char *const alpha[] = {"Alpha", "Alpha", "Beta"};
    static const char *const countries[] = {
        "Canada", "Japan", "Brazil", "Kenya", "Norway",
    };
    static const char *const words[] = {"Red", "Green", "Blue", "Gold"};
    ChooseRef choose[WEBNAV_BATCH] = {0};
    for (unsigned lane = 0; lane < WEBNAV_BATCH; ++lane) {
        choose[lane].count = 3u;
        for (unsigned i = 0; i < choose[lane].count; ++i)
            choose[lane].options[i] = alpha[i];
        choose[lane].target = lane % 3u;
        if (lane == 1u) {
            choose[lane].count = 5u;
            for (unsigned i = 0; i < choose[lane].count; ++i)
                choose[lane].options[i] = countries[i];
            choose[lane].target = 2u;
        } else if (lane == 2u) {
            choose[lane].count = 4u;
            for (unsigned i = 0; i < choose[lane].count; ++i)
                choose[lane].options[i] = words[i];
            choose[lane].target = 3u;
        }
    }
    unsigned index[WEBNAV_BATCH];
    for (unsigned lane = 0; lane < WEBNAV_BATCH; ++lane) {
        command[lane] = 0u;
        index[lane] = 0u;
        elapsed[lane] = 0u;
    }
    command[0] = 1u;
    index[0] = 2u;
    elapsed[0] = 10u;
    command[1] = 1u;
    index[1] = 99u;
    elapsed[1] = 10u;
    command[2] = 2u;
    elapsed[2] = 10u;
    command[3] = 3u;
    elapsed[3] = 10u;
    command[4] = 1u;
    index[4] = 1u;
    elapsed[4] = 10u;
    CHECK(run_choose_batch(choose, command, index, elapsed) == 0);

    for (unsigned lane = 0; lane < WEBNAV_BATCH; ++lane) {
        command[lane] = 0u;
        index[lane] = 0u;
        elapsed[lane] = 20u;
    }
    command[0] = 1u;
    index[0] = 1u;
    command[1] = 2u;
    command[2] = 1u;
    index[2] = 0u;
    command[3] = 4u;
    elapsed[3] = 10001u;
    command[4] = 2u;
    command[5] = 1u;
    index[5] = 99u;
    CHECK(run_choose_batch(choose, command, index, elapsed) == 0);

    for (unsigned lane = 0; lane < WEBNAV_BATCH; ++lane) {
        command[lane] = 0u;
        index[lane] = 0u;
        elapsed[lane] = 30u;
    }
    command[0] = 2u;
    command[1] = 0u;
    command[2] = 4u;
    elapsed[2] = 30u;
    command[3] = 1u;
    index[3] = 2u;
    command[4] = 4u;
    elapsed[4] = 10002u;
    CHECK(run_choose_batch(choose, command, index, elapsed) == 0);

    for (unsigned lane = 0; lane < WEBNAV_BATCH; ++lane) {
        command[lane] = 0u;
        index[lane] = 0u;
        elapsed[lane] = 40u;
    }
    command[2] = 2u;
    command[3] = 2u;
    command[4] = 1u;
    index[4] = 1u;
    elapsed[4] = 10003u;
    CHECK(run_choose_batch(choose, command, index, elapsed) == 0);
    return 0;
}

static int test_sequence_reference(void) {
    Sequence initial = {{10u, 50u}, {60u, 50u}, 0u, HIT_MISS, 0u, 0u};
    Sequence s = sequence_step(initial, 1u, 200u, 200u, 10u);
    CHECK(memcmp(&s, &initial, sizeof s) == 0); /* background clicks do nothing */

    Sequence overlap = {{10u, 50u}, {10u, 50u}, 0u, HIT_MISS, 0u, 0u};
    s = sequence_step(overlap, 1u, 20u, 60u, 10u);
    CHECK(s.clicks == 1u && s.first == HIT_TWO && s.status == 0u);
    s = sequence_step(initial, 1u, 20u, 60u, 10u);
    CHECK(s.clicks == 1u && s.first == HIT_ONE && s.status == 0u);
    s = sequence_step(s, 1u, 70u, 60u, 20u);
    CHECK(s.status == 1u && s.success == 1u && s.clicks == 2u);

    s = sequence_step(initial, 1u, 70u, 60u, 10u);
    CHECK(s.clicks == 1u && s.first == HIT_TWO && s.status == 0u);
    s = sequence_step(s, 1u, 20u, 60u, 20u);
    CHECK(s.status == 1u && s.success == 0u && s.clicks == 2u);
    s = sequence_step(s, 1u, 20u, 60u, 30u);
    CHECK(s.status == 1u && s.success == 0u); /* terminal absorption */

    s = sequence_step(initial, 1u, 20u, 60u, 10000u);
    CHECK(s.status == 2u && s.success == 0u);
    s = sequence_step(s, 4u, 0u, 0u, 10001u);
    CHECK(s.status == 0u && s.clicks == 0u && s.first == HIT_MISS);
    CHECK(timed_reward(1u, 1u, 2500u) == 0.75);
    CHECK(timed_reward(1u, 0u, 1u) == -1.0);
    return 0;
}

static int test_choose_reference(void) {
    const char *options[] = {"Alpha", "Alpha", "Beta"};
    CHECK(choose_submit(options, 3u, 0u, "Alpha") == 1);
    CHECK(choose_submit(options, 3u, 1u, "Alpha") == 1); /* text, not index */
    CHECK(choose_submit(options, 3u, 2u, "Alpha") == 0);
    CHECK(choose_submit(options, 3u, 3u, "Alpha") == 0); /* invalid selection */

    unsigned selected = 0u;
    if (2u < 3u) selected = 2u;
    CHECK(selected == 2u);
    if (99u < 3u) selected = 99u;
    CHECK(selected == 2u); /* invalid select is identity */
    selected = 1u;
    CHECK(choose_submit(options, 3u, selected, "Alpha") == 1);
    return 0;
}

static int test_validator(void) {
    uint32_t row[256] = {0};
    row[0] = 7u;
    row[13] = 0u;
    row[14] = 50u;
    row[15] = 60u;
    row[16] = 50u;
    row[17] = 1u;
    CHECK(webnav_sequence_select_valid(row));
    row[15] = 118u;
    CHECK(!webnav_sequence_select_valid(row));
    row[15] = 60u;
    row[10] = 1u;
    CHECK(!webnav_sequence_select_valid(row)); /* first hit required */
    row[11] = 1u;
    CHECK(webnav_sequence_select_valid(row));
    row[3] = 4u;
    row[6] = 0u;
    row[7] = 100u;
    CHECK(webnav_sequence_select_valid(row)); /* reset may clear old timing */
    row[3] = 0u;
    row[6] = 99u;
    CHECK(!webnav_sequence_select_valid(row));

    memset(row, 0, sizeof row);
    row[0] = 8u;
    row[10] = 0u;
    row[11] = 1u;
    row[12] = 1u;
    row[13] = 3u;
    const char *labels[] = {"Alpha", "Alpha", "Beta"};
    for (unsigned i = 0; i < 3u; ++i) {
        row[17u + i] = (uint32_t)strlen(labels[i]);
        pack_ascii(row, 32u + 16u * i, labels[i]);
    }
    CHECK(webnav_sequence_select_valid(row));
    row[11] = 3u;
    CHECK(!webnav_sequence_select_valid(row));
    row[11] = 1u;
    row[33u + 16u * 2u] |= 0x1fu;
    CHECK(!webnav_sequence_select_valid(row)); /* nonzero packed padding */
    return 0;
}

int main(void) {
    CHECK(test_sequence_reference() == 0);
    CHECK(test_choose_reference() == 0);
    CHECK(test_validator() == 0);
    CHECK(test_differential() == 0);
    puts("sequence-select independent native spec and Bend differential: PASS");
    return 0;
}
