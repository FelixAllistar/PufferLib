#include "../../bridge.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

void webnav_tree_generate_batch(uint32_t words[WEBNAV_BATCH * WEBNAV_WORDS]);

#define CHECK(condition)                                                        \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "tree generator check failed at %s:%d: %s\n",   \
                    __FILE__, __LINE__, #condition);                          \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static unsigned packed_string(const uint32_t *row, unsigned offset,
                              unsigned capacity, char *out) {
    unsigned length = 0;
    int terminated = 0;
    for (unsigned i = 0; i < capacity; i++) {
        unsigned char ch = (unsigned char)((row[offset + i / 4] >>
                                            (8u * (i % 4u))) & 0xffu);
        if (!terminated) {
            if (ch == 0) {
                terminated = 1;
            } else {
                out[length++] = (char)ch;
            }
        } else {
            CHECK(ch == 0);
        }
    }
    CHECK(terminated);
    out[length] = 0;
    return length;
}

static unsigned parent_of(const uint32_t *row, unsigned index) {
    return row[16u + 6u * index + 3u];
}

static unsigned end_of(const uint32_t *row, unsigned index) {
    return row[16u + 6u * index + 4u];
}

static int descendant_of(const uint32_t *row, unsigned index, unsigned root) {
    unsigned parent = parent_of(row, index);
    for (unsigned hops = 0; hops < 8u && parent != 0; hops++) {
        if (parent - 1u == root) return 1;
        if (parent - 1u >= index) return 0;
        parent = parent_of(row, parent - 1u);
    }
    return 0;
}

static int validate_row(const uint32_t *row) {
    CHECK(row[0] == 9u);
    unsigned count = row[1];
    CHECK(count >= 1u && count <= 8u);
    CHECK(row[2] == 0u && row[3] == 0u && row[4] == 0u);
    CHECK(row[5] == 1u && row[6] == 0u);
    for (unsigned i = 0; i < count; i++) {
        const uint32_t *node = row + 16u + 6u * i;
        CHECK(node[0] <= 1u && node[1] <= 1u && node[2] == 0u);
        CHECK(node[3] <= i && node[4] > i && node[4] <= count);
        CHECK(node[5] == (node[3] == 0u));
        if (node[3] != 0u) {
            unsigned parent = node[3] - 1u;
            CHECK(row[16u + 6u * parent] == 1u);
            CHECK(node[4] <= end_of(row, parent));
        }
        unsigned expected_end = i + 1u;
        while (expected_end < count && descendant_of(row, expected_end, i))
            expected_end++;
        if (node[4] != expected_end) {
            fprintf(stderr, "end mismatch n=%u i=%u got=%u want=%u parent=%u\n",
                    count, i, node[4], expected_end, node[3]);
            return 1;
        }
    }
    unsigned targets = 0;
    unsigned target_index = 0;
    for (unsigned i = 0; i < count; i++) {
        targets += row[16u + 6u * i + 1u];
        if (row[16u + 6u * i + 1u]) target_index = i;
    }
    CHECK(targets == 1u);
    for (unsigned i = count; i < 8u; i++)
        for (unsigned j = 0; j < 6u; j++) CHECK(row[16u + 6u * i + j] == 0u);

    char labels[8][32];
    for (unsigned i = 0; i < 8u; i++) {
        unsigned length = packed_string(row, 64u + 8u * i, 32u, labels[i]);
        if (i < count) {
            CHECK(length > 0u && length <= 31u);
            for (unsigned j = 0; j < length; j++)
                CHECK((unsigned char)labels[i][j] >= 32u &&
                      (unsigned char)labels[i][j] <= 126u);
            for (unsigned j = 0; j < i; j++) CHECK(strcmp(labels[i], labels[j]) != 0);
        } else {
            CHECK(length == 0u);
        }
    }
    char instruction[128];
    unsigned instruction_length = packed_string(row, 224u, 128u, instruction);
    CHECK(instruction_length > 0u && instruction_length <= 127u);
    CHECK(strstr(instruction, labels[target_index]) != NULL);
    CHECK(strstr(instruction, "Navigate through the file tree.") == instruction);
    for (unsigned i = 128u; i < 224u; i++) CHECK(row[i] == 0u);
    return 0;
}

int main(void) {
    uint32_t words[WEBNAV_BATCH * WEBNAV_WORDS];
    uint32_t previous[WEBNAV_WORDS];
    unsigned shape_counts[9] = {0};
    memset(words, 0xa5, sizeof words);
    for (unsigned lane = 0; lane < WEBNAV_BATCH; lane++) {
        uint32_t *row = words + lane * WEBNAV_WORDS;
        row[0] = 1000u + lane;
    }
    webnav_tree_generate_batch(words);
    for (unsigned lane = 0; lane < WEBNAV_BATCH; lane++) {
        const uint32_t *row = words + lane * WEBNAV_WORDS;
        CHECK(validate_row(row) == 0);
        shape_counts[row[1]]++;
        if (lane == 0) memcpy(previous, row, sizeof previous);
        else CHECK(memcmp(previous, row, sizeof previous) != 0);
    }
    unsigned distinct_sizes = 0;
    for (unsigned i = 1; i <= 8; i++) distinct_sizes += shape_counts[i] != 0;
    CHECK(distinct_sizes >= 3u);

    /* A second reset over dirty rows proves that generator output does not
     * retain old topology, labels, instruction bytes or reward state. */
    memset(words, 0xff, sizeof words);
    for (unsigned lane = 0; lane < WEBNAV_BATCH; lane++)
        words[lane * WEBNAV_WORDS] = 9000u + lane;
    webnav_tree_generate_batch(words);
    for (unsigned lane = 0; lane < WEBNAV_BATCH; lane++)
        CHECK(validate_row(words + lane * WEBNAV_WORDS) == 0);

    printf("PASS: 32-lane tree generator rows, pre-order ends/parents, collapsed visibility, private target, packed public labels/instruction, seed variation and dirty reset\n");
    return 0;
}
