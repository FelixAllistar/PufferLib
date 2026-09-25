#include "bridge.h"
#include "miniwob/sequence_select/validation.h"
#include "miniwob/tree/validation.h"
#include "miniwob/autocomplete/validation.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    unsigned tags[] = {0, 0, 2, 7, 8, 1, 3, 4, 5, 6, 9, 10};
    uint32_t first[WEBNAV_BATCH * WEBNAV_WORDS];
    uint32_t second[WEBNAV_BATCH * WEBNAV_WORDS];
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned batch = 0; batch < 96; batch++) {
        memset(first, 0xa5, sizeof first);
        memset(second, 0x5a, sizeof second);
        for (unsigned lane = 0; lane < WEBNAV_BATCH; lane++) {
            unsigned at = lane * WEBNAV_WORDS;
            first[at] = second[at] = 11;
            first[at + 1] = second[at + 1] = (batch + lane) % 12;
            unsigned seed = (batch % 48) * 32 + lane;
            if (batch >= 48) {
                seed |= UINT32_C(0x80000000);
            }
            first[at + 2] = second[at + 2] = seed;
        }
        webnav_batch(first);
        webnav_batch(second);
        assert(!memcmp(first, second, sizeof first));
        for (unsigned lane = 0; lane < WEBNAV_BATCH; lane++) {
            uint32_t *row = first + lane * WEBNAV_WORDS;
            unsigned tag = tags[(batch + lane) % 12];
            assert(row[0] == tag);
            if (tag == 7 || tag == 8) {
                assert(webnav_sequence_select_valid(row));
                assert(row[1] == 0);
            } else if (tag == 9) {
                assert(webnav_tree_valid(row));
                assert(row[2] == 0);
            } else if (tag == 10) {
                assert(webnav_autocomplete_valid(row));
                assert(row[3] == 0);
            } else if (tag >= 4) {
                assert(row[1] == (tag == 5 ? 2u : 1u));
                assert(row[3] == 0);
            } else {
                assert(row[1] > 0 && row[1] <= 16);
                assert(row[2] == 0);
            }
        }
        const unsigned char *bytes = (const unsigned char *)first;
        for (unsigned i = 0; i < sizeof first; i++) {
            hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
        }
    }
    printf("PASS: 3072 mixed train/eval resets, all 12 presets, dirty-row independence; "
        "trace=%016" PRIx64 "\n", hash);
    return 0;
}
