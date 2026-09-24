#ifndef WEBNAV_AUTOCOMPLETE_VALIDATION_H
#define WEBNAV_AUTOCOMPLETE_VALIDATION_H

#include <stdint.h>

/*
 * Validate one private autocomplete simulator row before it reaches Bend.
 * The row is exactly 256 uint32_t words. This validator deliberately checks
 * the bounded ASCII/action preset; callers that need Unicode, longer input,
 * or arbitrary key events must negotiate a new wire version.
 */
static inline int webnav_autocomplete_valid(const uint32_t *r) {
    if (r == 0 || r[0] != 10u || r[1] != 1u) return 0;
    if (r[2] > 2u || r[3] > 2u || r[4] > 1u) return 0;
    if (r[5] < r[6]) return 0;
    if (r[7] > 15u || r[11] > 32u || r[12] > 1u) return 0;
    if (r[13] < 2u || r[13] > 5u || r[14] < 2u || r[14] > 5u) return 0;
    if (r[15] > 2u || r[16] > 64u || r[17] > r[16] || r[18] > r[16] || r[17] > r[18]) return 0;
    if (r[19] > 64u || r[20] > 249u || r[21] > 249u) return 0;

    /* A closed menu has no retained search term or active item. Open and
     * active menus have at least one result; active is one-based. */
    if (r[15] == 0u && (r[19] != 0u || r[20] != 0u || r[21] != 0u)) return 0;
    if (r[15] == 1u && (r[19] == 0u || r[21] != 0u || r[20] == 0u)) return 0;
    if (r[15] == 2u && (r[19] == 0u || r[20] == 0u || r[21] == 0u || r[21] > r[20])) return 0;

    /* Command arguments have one interpretation per command. */
    if ((r[7] == 0u || r[7] == 1u || r[7] == 2u || r[7] == 3u || r[7] == 4u || r[7] == 5u ||
         r[7] == 6u || r[7] == 7u || r[7] == 8u || r[7] == 9u ||
         r[7] == 10u || r[7] == 11u || r[7] == 12u || r[7] == 13u ||
         r[7] == 15u) && r[8] != 0u) return 0;
    if (r[7] != 2u && r[11] != 0u) return 0;
    if (r[7] == 14u && r[8] > 249u) return 0;

    for (uint32_t i = 0; i < r[16]; ++i)
        if (r[32u + i] < 32u || r[32u + i] > 126u) return 0;
    for (uint32_t i = 0; i < r[19]; ++i)
        if (r[96u + i] < 32u || r[96u + i] > 126u) return 0;
    for (uint32_t i = 0; i < r[13]; ++i)
        if (r[160u + i] < 32u || r[160u + i] > 126u) return 0;
    for (uint32_t i = 0; i < r[14]; ++i)
        if (r[192u + i] < 32u || r[192u + i] > 126u) return 0;
    for (uint32_t i = 0; i < r[11]; ++i)
        if (r[224u + i] < 32u || r[224u + i] > 126u) return 0;

    /* Input insertions must fit the one-field bounded text slot. */
    if (r[3] == 0u && r[5] < 10000u && r[7] == 2u && r[2] == 1u &&
        r[16] + r[11] - (r[18] - r[17]) > 64u) return 0;
    return 1;
}

#endif
