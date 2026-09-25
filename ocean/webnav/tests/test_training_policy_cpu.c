#include "training_policy.h"
#include "training_contract.h"
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *path = "build/webnav/training/policy-cpu-test.bin";
    unsigned hidden = 16, layers = 2;
    unsigned count = WT_FEATURES * hidden + (WT_ACTIONS + 1) * hidden
        + 3 * layers * hidden * hidden;
    FILE *file = fopen(path, "wb");
    assert(file);
    uint32_t rng = 731;
    for (unsigned i = 0; i < count; i++) {
        rng = rng * 1664525u + 1013904223u;
        float value = ((int)(rng >> 16) - 32768) * (0.02f / 32768);
        assert(fwrite(&value, sizeof value, 1, file) == 1);
    }
    assert(!fclose(file));
    assert(!wt_contract_write(path, 0, hidden, layers, 731, 4095));
    WTPolicy *policy = wt_policy_load(path);
    assert(policy);
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned step = 0; step < 128; step++) {
        if (step % 13 == 0) {
            wt_policy_reset(policy);
        }
        WTView view = {0};
        view.count = 3;
        view.elapsed = (step % 13) * 250;
        snprintf(view.query, sizeof view.query, "Select item %u.", step % 3);
        for (unsigned i = 0; i < view.count; i++) {
            WTNode *node = view.nodes + i;
            node->ref = i + 1;
            node->role = WT_BUTTON;
            node->flags = WT_VISIBLE | WT_ENABLED | WT_CLICKABLE;
            snprintf(node->name, sizeof node->name, "item %u", i);
        }
        view.nodes[step % 3].flags &= ~WT_ENABLED;
        int action = wt_policy_action(policy, &view);
        unsigned char mask[WT_ACTIONS];
        wt_mask(&view, mask);
        assert(action >= 0 && action < WT_ACTIONS && mask[action]);
        const float *logits = wt_policy_logits(policy);
        for (unsigned i = 0; i < WT_ACTIONS + 1; i++) {
            assert(isfinite(logits[i]));
            uint32_t bits;
            memcpy(&bits, logits + i, sizeof bits);
            hash = (hash ^ bits) * UINT64_C(1099511628211);
        }
        hash = (hash ^ (unsigned)action) * UINT64_C(1099511628211);
    }
    wt_policy_free(policy);
    printf("PASS: 128 recurrent CPU steps, legal actions, 10 resets; "
        "trace=%016" PRIx64 "\n", hash);
    return 0;
}
