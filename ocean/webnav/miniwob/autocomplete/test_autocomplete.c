#include "validation.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

void autocomplete_batch(uint32_t words[32u * 256u]);
static int runtime_warmed;

static float f32_at(const uint32_t *r, unsigned at) {
    float value;
    memcpy(&value, r + at, sizeof value);
    return value;
}

static void put_ascii(uint32_t *r, unsigned at, const char *s) {
    unsigned n = (unsigned)strlen(s);
    for (unsigned i = 0; i < n; ++i) r[at + i] = (unsigned char)s[i];
}

static void set_task(uint32_t *r, const char *prefix, const char *suffix,
                     unsigned match_end) {
    unsigned np = (unsigned)strlen(prefix), ns = (unsigned)strlen(suffix);
    assert(np >= 2 && np <= 5 && ns >= 2 && ns <= 5);
    r[12] = match_end;
    r[13] = np;
    r[14] = ns;
    memset(r + 160, 0, 32u * sizeof *r);
    memset(r + 192, 0, 32u * sizeof *r);
    put_ascii(r, 160, prefix);
    put_ascii(r, 192, suffix);
}

static void set_value(uint32_t *r, const char *value) {
    unsigned n = (unsigned)strlen(value);
    assert(n <= 64);
    r[16] = n;
    r[17] = n;
    r[18] = n;
    memset(r + 32, 0, 64u * sizeof *r);
    put_ascii(r, 32, value);
}

static void empty_row(uint32_t *r, const char *prefix, const char *suffix,
                      unsigned match_end) {
    memset(r, 0, 256u * sizeof *r);
    r[0] = 10;
    r[1] = 1;
    r[5] = 0;
    r[6] = 0;
    set_task(r, prefix, suffix, match_end);
}

static void set_payload(uint32_t *r, const char *payload) {
    unsigned n = (unsigned)strlen(payload);
    assert(n <= 32);
    r[11] = n;
    memset(r + 224, 0, 32u * sizeof *r);
    put_ascii(r, 224, payload);
}

static void run_one(uint32_t *r, unsigned command, unsigned argument,
                    unsigned elapsed, const char *payload) {
    uint32_t words[32u * 256u];
    r[5] = elapsed;
    r[7] = command;
    r[8] = argument;
    if (command == 2) set_payload(r, payload);
    else set_payload(r, "");
    assert(webnav_autocomplete_valid(r));
    for (unsigned lane = 0; lane < 32; ++lane)
        memcpy(words + lane * 256u, r, 256u * sizeof *r);
    autocomplete_batch(words);
    if (!runtime_warmed) {
        /* The generated standalone CLI installs a diagnostic SIGABRT handler
         * that waits for a debugger. Native tests should fail immediately. */
        signal(SIGABRT, SIG_DFL);
        runtime_warmed = 1;
    }
    memcpy(r, words, 256u * sizeof *r);
    assert(webnav_autocomplete_valid(r));
}

static void test_validator(void) {
    uint32_t r[256];
    empty_row(r, "Ca", "da", 1);
    assert(webnav_autocomplete_valid(r));

    r[0] = 9;
    assert(!webnav_autocomplete_valid(r));
    r[0] = 10;
    r[5] = 3;
    r[6] = 4;
    assert(!webnav_autocomplete_valid(r));
    r[5] = 4;
    r[6] = 3;
    r[7] = 0;
    r[8] = 1;
    assert(!webnav_autocomplete_valid(r));
    r[8] = 0;
    r[7] = 2;
    r[8] = 1;
    assert(!webnav_autocomplete_valid(r));
    r[7] = 0;
    r[8] = 0;
    r[15] = 1;
    assert(!webnav_autocomplete_valid(r));
    r[15] = 0;
    r[16] = 1;
    r[32] = 31;
    assert(!webnav_autocomplete_valid(r));
    r[32] = 0;
    r[16] = 0;
    assert(webnav_autocomplete_valid(r));
}

static void test_empty_and_closed_menu_search(void) {
    uint32_t r[256];
    empty_row(r, "Ca", "da", 1);
    set_value(r, "ca");
    run_one(r, 12, 0, 1, "");         /* body keys do not drive the widget */
    assert(r[2] == 0 && r[15] == 0 && r[16] == 2);

    empty_row(r, "Ca", "da", 1);
    run_one(r, 1, 0, 1, "");              /* focus/end */
    run_one(r, 12, 0, 2, "");             /* empty query stays closed */
    assert(r[15] == 0 && r[16] == 0 && r[21] == 0);

    run_one(r, 2, 0, 3, "ca");
    assert(r[15] == 1 && r[19] == 2 && r[20] > 0 && r[21] == 0);
    /* The first arrow after a closed popup opens it without selecting. */
    run_one(r, 15, 0, 4, "");           /* no active item: no-op */
    assert(r[15] == 1 && r[16] == 2);
    run_one(r, 12, 0, 5, "");
    assert(r[15] == 2 && r[21] == 1 && r[16] > 0);
    run_one(r, 15, 0, 6, "");            /* click/enter selected item */
    assert(r[15] == 0 && r[16] > 2);

    /* A new arrow on the now-closed selected value searches again, leaving
     * focus on the input and retaining the value until a second arrow. */
    unsigned n = r[16];
    run_one(r, 12, 0, 7, "");
    assert(r[15] == 1 && r[21] == 0 && r[16] == n);
}

static void test_menu_boundaries(void) {
    uint32_t r[256];
    empty_row(r, "Ca", "da", 1);
    run_one(r, 1, 0, 1, "");
    run_one(r, 2, 0, 2, "ca");
    assert(r[15] == 1 && r[20] > 1);
    unsigned count = r[20];
    run_one(r, 12, 0, 3, "");       /* open -> first */
    for (unsigned i = 1; i < count; ++i)
        run_one(r, 12, 0, 3 + i, "");
    assert(r[15] == 2 && r[21] == count);
    run_one(r, 12, 0, 4 + count, "");
    assert(r[15] == 1 && r[21] == 0 && r[16] == 2);

    /* With the menu visible, Up moves to the last item. */
    run_one(r, 13, 0, 5 + count, "");
    assert(r[15] == 2 && r[21] == count);
    run_one(r, 13, 0, 6 + count, "");
    assert(r[15] == 2 && r[21] == count - 1);
}

static void test_matching_and_selection(void) {
    uint32_t r[256];
    empty_row(r, "Ca", "da", 1);
    run_one(r, 1, 0, 1, "");
    run_one(r, 2, 0, 2, "Canada");
    assert(r[15] == 1 && r[20] == 1);
    assert(r[17] == 6 && r[18] == 6);
    run_one(r, 12, 0, 3, "");
    assert(r[15] == 2 && r[21] == 1);
    assert(r[17] == 6 && r[18] == 6);
    run_one(r, 15, 0, 4, "");
    assert(r[15] == 0 && r[16] == 6);
    run_one(r, 10, 0, 5, "");
    assert(r[3] == 1 && r[4] == 1);
    assert(fabsf(f32_at(r, 9) - 1.0f) < 1e-6f);

    /* The task accepts a non-country value when only its prefix is required. */
    empty_row(r, "Ca", "zz", 0);
    run_one(r, 1, 0, 1, "");
    run_one(r, 2, 0, 2, "Cactus");
    assert(r[15] == 0 && r[16] == 6);
    run_one(r, 10, 0, 3, "");
    assert(r[3] == 1 && r[4] == 1);

    /* Matching is case-sensitive even though the popup filter is not. */
    empty_row(r, "Ca", "DA", 1);
    run_one(r, 1, 0, 1, "");
    run_one(r, 2, 0, 2, "Canada");
    run_one(r, 10, 0, 3, "");
    assert(r[3] == 1 && r[4] == 0);
    assert(r[15] == 0);
    assert(fabsf(f32_at(r, 9) + 1.0f) < 1e-6f);
}

static void test_deadline_and_terminal_freeze(void) {
    uint32_t r[256], before[256];
    empty_row(r, "Ca", "da", 1);
    run_one(r, 1, 0, 9999, "");
    run_one(r, 10, 0, 10000, "");
    assert(r[3] == 2 && r[4] == 0 && r[5] == 10000 && r[6] == 10000);
    assert(fabsf(f32_at(r, 9) + 1.0f) < 1e-6f);
    assert(fabsf(f32_at(r, 10) + 1.0f) < 1e-6f);
    memcpy(before, r, sizeof before);
    run_one(r, 2, 0, UINT32_MAX, "Canada");
    assert(r[0] == before[0] && r[1] == before[1] && r[2] == before[2]);
    assert(r[3] == before[3] && r[4] == before[4] && r[6] == before[6]);
    assert(r[9] == before[9] && r[10] == before[10]);
    assert(r[12] == before[12] && r[13] == before[13] && r[14] == before[14]);
    assert(r[15] == before[15] && r[16] == before[16] && r[17] == before[17]);
    assert(r[18] == before[18] && r[19] == before[19] && r[20] == before[20]);
    assert(r[21] == before[21]);
    assert(!memcmp(r + 32, before + 32, 192u * sizeof *r));
}

static void test_independent_lanes(void) {
    uint32_t words[32u * 256u];
    memset(words, 0, sizeof words);
    for (unsigned lane = 0; lane < 32; ++lane) {
        uint32_t *r = words + lane * 256u;
        empty_row(r, "Ca", "da", 1);
        r[2] = 1;
        if ((lane & 1u) == 0) set_value(r, "Canada");
        else set_value(r, "Cactus");
        r[5] = lane;
        r[7] = 10;
    }
    for (unsigned lane = 0; lane < 32; ++lane)
        assert(webnav_autocomplete_valid(words + lane * 256u));
    autocomplete_batch(words);
    for (unsigned lane = 0; lane < 32; ++lane) {
        const uint32_t *r = words + lane * 256u;
        assert(r[3] == 1 && r[4] == ((lane & 1u) == 0));
        assert(r[5] == lane && r[6] == lane);
    }
}

int main(void) {
    test_validator();
    test_empty_and_closed_menu_search();
    test_menu_boundaries();
    test_matching_and_selection();
    test_deadline_and_terminal_freeze();
    test_independent_lanes();
    puts("PASS: tag-10 autocomplete validation, source filtering, menu boundaries, selection, exact goals, deadline/reset and 32 lanes");
    return 0;
}
