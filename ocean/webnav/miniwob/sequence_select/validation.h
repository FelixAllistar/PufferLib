#ifndef WEBNAV_MINIWOB_SEQUENCE_SELECT_VALIDATION_H
#define WEBNAV_MINIWOB_SEQUENCE_SELECT_VALIDATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Validate one 256-word sequence/select row before it crosses the Bend ABI.
 * The caller owns the row and guarantees that it has 256 uint32_t words.
 * Unknown tags are rejected here; the shared webnav dispatcher should call
 * this function only for rows it intends to route to this model.
 */
static inline bool webnav_sequence_select_packed_ascii(const uint32_t *r,
                                                        unsigned offset,
                                                        unsigned length) {
    if (length > 64u) return false;
    for (unsigned word = 0; word < 16u; ++word) {
        uint32_t value = r[offset + word];
        for (unsigned lane = 0; lane < 4u; ++lane) {
            unsigned at = word * 4u + lane;
            unsigned char ch = (unsigned char)((value >> (8u * lane)) & 0xffu);
            if (at < length) {
                if (ch < 32u || ch > 126u) return false;
            } else if (ch != 0u) {
                return false;
            }
        }
    }
    return true;
}

static inline bool webnav_sequence_select_sequence_valid(const uint32_t *r) {
    /* The source generator independently draws left from randi(0, 118) and
     * top from randi(0, 118) + 50.  The upper endpoint is exclusive. */
    if (r[1] > 2u || r[2] > 1u || r[3] > 4u) return false;
    if (r[3] != 4u && r[6] < r[7]) return false;
    if (r[10] > 2u || r[11] > 2u || r[12] > 2u || r[17] != 1u) return false;
    if (r[13] > 117u || r[15] > 117u || r[14] < 50u || r[14] > 167u ||
        r[16] < 50u || r[16] > 167u) return false;
    if (r[1] == 0u && r[2] != 0u) return false;
    if (r[10] == 0u && r[11] != 0u) return false;
    if (r[10] != 0u && r[11] == 0u) return false;
    return true;
}

static inline bool webnav_sequence_select_choose_valid(const uint32_t *r) {
    if (r[1] > 2u || r[2] > 1u || r[3] > 4u) return false;
    if (r[3] != 4u && r[6] < r[7]) return false;
    if (r[12] != 1u || r[13] < 3u || r[13] > 9u) return false;
    if (r[10] >= r[13] || r[11] >= r[13]) return false;
    for (unsigned i = 0; i < 9u; ++i) {
        unsigned length = r[17u + i];
        if (i < r[13]) {
            if (length == 0u || !webnav_sequence_select_packed_ascii(
                    r, 32u + 16u * i, length)) return false;
        } else {
            if (length != 0u || !webnav_sequence_select_packed_ascii(
                    r, 32u + 16u * i, 0u)) return false;
        }
    }
    return true;
}

static inline bool webnav_sequence_select_valid(const uint32_t *r) {
    if (r == NULL) return false;
    if (r[0] == 7u) return webnav_sequence_select_sequence_valid(r);
    if (r[0] == 8u) return webnav_sequence_select_choose_valid(r);
    return false;
}

#endif
