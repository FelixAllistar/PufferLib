#include "validation.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void webnav_tree_batch(uint32_t words[32u * 256u]);

static float f32_at(const uint32_t *r, unsigned at) {
    float value;
    memcpy(&value, r + at, sizeof value);
    return value;
}

static void node(uint32_t *r, unsigned i, unsigned folder, unsigned target,
                 unsigned expanded, unsigned parent, unsigned subtree_end) {
    uint32_t *n = r + 16u + i * 6u;
    n[0] = folder;
    n[1] = target;
    n[2] = expanded;
    n[3] = parent;
    n[4] = subtree_end;
    n[5] = 0;
}

static void empty_row(uint32_t *r, unsigned count) {
    memset(r, 0, 256u * sizeof *r);
    r[0] = 9;
    r[1] = count;
    r[5] = 1;
}

static void run_one(uint32_t *r, unsigned command, unsigned index, unsigned elapsed) {
    uint32_t words[32u * 256u];
    for (unsigned lane = 0; lane < 32; lane++) {
        memcpy(words + lane * 256u, r, 256u * sizeof *r);
        words[lane * 256u + 7] = command;
        words[lane * 256u + 8] = index;
        words[lane * 256u + 9] = elapsed;
    }
    webnav_tree_batch(words);
    memcpy(r, words, 256u * sizeof *r);
}

static void test_validator(void) {
    uint32_t r[256];
    empty_row(r, 2);
    node(r, 0, 1, 0, 0, 0, 2);
    node(r, 1, 0, 1, 0, 1, 2);
    assert(webnav_tree_valid(r));
    r[16 + 3] = 2;
    assert(!webnav_tree_valid(r));
    r[16 + 3] = 0;
    r[22 + 4] = 1;
    assert(!webnav_tree_valid(r));
    r[22 + 4] = 2;
    r[22 + 2] = 1;
    assert(!webnav_tree_valid(r));
    r[22 + 2] = 0;
    r[2] = 1;
    r[4] = 1;
    r[6] = 0;
    assert(!webnav_tree_valid(r));

    empty_row(r, 3);
    node(r, 0, 1, 0, 0, 0, 3);
    node(r, 1, 0, 0, 0, 1, 2);
    node(r, 2, 0, 0, 0, 0, 3);
    assert(!webnav_tree_valid(r));
}

static void test_leaf_and_folder(void) {
    uint32_t r[256];

    empty_row(r, 1);
    node(r, 0, 0, 0, 0, 0, 1);
    assert(webnav_tree_valid(r));
    run_one(r, 1, 0, 137);
    assert(r[2] == 1 && r[3] == 1 && r[4] == 0 && r[6] == 0);
    assert(fabsf(f32_at(r, 11) + 1.0f) < 1e-6f);
    assert(fabsf(f32_at(r, 12) + 1.0f) < 1e-6f);
    assert(r[10] == 137);

    empty_row(r, 1);
    node(r, 0, 1, 0, 0, 0, 1);
    run_one(r, 1, 0, 500);
    assert(r[2] == 0 && r[3] == 1 && r[18] == 1);
    assert(r[21] == 1);

    run_one(r, 1, 99, 600);
    assert(r[2] == 0 && r[3] == 1 && r[18] == 1);
}

static void test_visibility_and_bubbling(void) {
    uint32_t r[256];

    /* A hidden target cannot be clicked until the parent is expanded. */
    empty_row(r, 2);
    node(r, 0, 1, 0, 0, 0, 2);
    node(r, 1, 0, 1, 0, 1, 2);
    run_one(r, 1, 1, 100);
    assert(r[2] == 0 && r[3] == 0 && r[27] == 0);
    run_one(r, 1, 0, 200);
    assert(r[2] == 0 && r[3] == 1 && r[18] == 1 && r[21] == 1 && r[27] == 1);
    run_one(r, 1, 1, 300);
    assert(r[2] == 1 && r[3] == 2 && r[4] == 1 && r[6] == 1);

    /* Wrong descendant files stop propagation; a target ancestor does not
     * rescue a wrong leaf. */
    empty_row(r, 2);
    node(r, 0, 1, 1, 1, 0, 2);
    node(r, 1, 0, 0, 0, 1, 2);
    run_one(r, 1, 1, 300);
    assert(r[2] == 1 && r[4] == 0 && r[3] == 2 && r[22] == 0);

    /* Wrong folder handlers return undefined, so a target ancestor handler
     * sees the same bubbling click.  Only the clicked folder toggles. */
    empty_row(r, 2);
    node(r, 0, 1, 1, 1, 0, 2);
    node(r, 1, 1, 0, 0, 1, 2);
    run_one(r, 1, 1, 300);
    assert(r[2] == 1 && r[4] == 1 && r[3] == 2);
    assert(r[18] == 1 && r[24] == 1);
}

static void test_deadline_terminal_reset(void) {
    uint32_t r[256], before[256];
    empty_row(r, 1);
    node(r, 0, 1, 1, 0, 0, 1);
    run_one(r, 1, 0, 10000);
    assert(r[2] == 2 && r[4] == 0 && r[10] == 10000);
    assert(fabsf(f32_at(r, 11) + 1.0f) < 1e-6f);
    assert(fabsf(f32_at(r, 12) + 1.0f) < 1e-6f);
    memcpy(before, r, sizeof before);
    run_one(r, 1, 0, UINT32_MAX);
    assert(r[2] == before[2] && r[3] == before[3] && r[10] == before[10]);
    assert(r[11] == before[11] && r[12] == before[12]);
    run_one(r, 3, 0, 0);
    assert(r[2] == 0 && r[3] == 0 && r[18] == 0 && r[10] == 0);
    assert(f32_at(r, 11) == 0.0f && f32_at(r, 12) == 0.0f);
}

static void test_independent_lanes(void) {
    uint32_t words[32u * 256u];
    memset(words, 0, sizeof words);
    for (unsigned lane = 0; lane < 32; lane++) {
        uint32_t *r = words + lane * 256u;
        empty_row(r, 1);
        node(r, 0, 0, lane == 0, 0, 0, 1);
        r[7] = 1;
        r[8] = 0;
        r[9] = lane;
    }
    webnav_tree_batch(words);
    for (unsigned lane = 0; lane < 32; lane++) {
        const uint32_t *r = words + lane * 256u;
        assert(r[2] == 1 && r[4] == (lane == 0) && r[5] == 1);
        assert(r[10] == lane);
    }
}

int main(void) {
    test_validator();
    test_leaf_and_folder();
    test_visibility_and_bubbling();
    test_deadline_terminal_reset();
    test_independent_lanes();
    puts("PASS: tag-9 tree validation, visibility, expand/collapse, leaf stop, ancestor bubbling, deadline/reset, rewards and 32 lanes");
    return 0;
}
