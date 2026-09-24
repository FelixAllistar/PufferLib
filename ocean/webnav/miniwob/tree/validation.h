#ifndef WEBNAV_MINIWOB_TREE_VALIDATION_H
#define WEBNAV_MINIWOB_TREE_VALIDATION_H

#include <stdint.h>

/* Structural validation for the private tag-9 matched-instance row.  This is
 * deliberately independent of Bend and can run before a row enters the shared
 * stock CPU runtime.  It accepts multiple roots, as the HTML generator appends
 * siblings directly under #tree, and checks the pre-order parent/subtree
 * contract used by Tree.visible_at. */
static inline unsigned webnav_tree_depth(const uint32_t *r, unsigned i) {
    unsigned n = r[1], depth = 0, guard = 0;
    unsigned parent = r[16u + i * 6u + 3u];
    while (parent != 0u) {
        if (parent - 1u >= i || parent - 1u >= n || ++guard > n) return n + 1u;
        i = parent - 1u;
        parent = r[16u + i * 6u + 3u];
        depth++;
    }
    return depth;
}

static inline int webnav_tree_descendant(const uint32_t *r, unsigned i, unsigned ancestor) {
    unsigned n = r[1], guard = 0;
    unsigned parent = r[16u + i * 6u + 3u];
    while (parent != 0u) {
        if (parent - 1u >= i || parent - 1u >= n || ++guard > n) return 0;
        if (parent - 1u == ancestor) return 1;
        i = parent - 1u;
        parent = r[16u + i * 6u + 3u];
    }
    return 0;
}

static inline int webnav_tree_valid(const uint32_t *r) {
    if (!r || r[0] != 9u || r[1] > 8u || r[2] > 2u || r[3] > r[1]) return 0;
    if (r[4] > 1u || r[5] > 1u || r[6] > 1u || r[7] > 3u) return 0;
    /* Reset starts a new logical episode and may set elapsed back to zero. */
    if (r[7] != 3u && r[9] < r[10]) return 0;
    if (r[2] == 0u && (r[4] != 0u || r[6] != 0u)) return 0;
    if (r[2] == 2u && (r[4] != 0u || r[6] != 0u)) return 0;
    if (r[2] == 1u && r[6] != r[4]) return 0;
    for (unsigned i = 0; i < r[1]; i++) {
        const uint32_t *n = r + 16u + i * 6u;
        if (n[0] > 1u || n[1] > 1u || n[2] > 1u || n[3] > i || n[4] <= i || n[4] > r[1]) return 0;
        if (n[0] == 0u && (n[2] != 0u || n[4] != i + 1u)) return 0;
        if (n[3] != 0u) {
            unsigned p = n[3] - 1u;
            const uint32_t *parent = r + 16u + p * 6u;
            if (parent[0] != 1u || parent[4] <= i) return 0;
        }
        /* `subtree_end` is the exact first non-descendant in pre-order. */
        unsigned expected_end = i + 1u;
        while (expected_end < r[1] && webnav_tree_descendant(r, expected_end, i)) expected_end++;
        if (n[4] != expected_end) return 0;
        /* The stock generator's recursive call is guarded by level < 2. */
        if (webnav_tree_depth(r, i) > 2u) return 0;
    }
    return 1;
}

#endif
