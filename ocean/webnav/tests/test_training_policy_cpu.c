#include "training_policy.h"
#include "training_contract.h"
#include "cJSON.h"
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void reject_field(const char *path, const char *key, cJSON *value) {
    assert(!wt_contract_write(path, 0, 16, 2, 731, 4095));
    char sidecar[256], text[8192];
    snprintf(sidecar, sizeof sidecar, "%s.webnav.json", path);
    FILE *file = fopen(sidecar, "rb");
    assert(file);
    size_t size = fread(text, 1, sizeof text - 1, file);
    text[size] = 0;
    assert(!fclose(file));
    cJSON *json = cJSON_Parse(text);
    assert(json);
    assert(cJSON_ReplaceItemInObjectCaseSensitive(json, key, value));
    char *changed = cJSON_PrintUnformatted(json);
    assert(changed);
    file = fopen(sidecar, "wb");
    assert(file && fputs(changed, file) >= 0);
    assert(!fclose(file));
    free(changed);
    cJSON_Delete(json);
    assert(!wt_policy_load(path));
}

static void write_weights(const char *path, unsigned hidden, unsigned layers) {
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
}

int main(void) {
    const char *path = "build/webnav/training/policy-cpu-test.bin";
    unsigned hidden = 16, layers = 2;
    write_weights(path, hidden, layers);
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
    reject_field(path, "source_sha256", cJSON_CreateString("different-source"));
    reject_field(path, "version", cJSON_CreateNumber(WT_VERSION + 1));
    reject_field(path, "features", cJSON_CreateNumber(WT_FEATURES + 1));
    reject_field(path, "actions", cJSON_CreateNumber(WT_ACTIONS + 1));
    reject_field(path, "potion", cJSON_CreateNumber(2));
    reject_field(path, "hidden", cJSON_CreateNumber(15));
    reject_field(path, "hidden", cJSON_CreateNumber(4104));
    reject_field(path, "layers", cJSON_CreateNumber(0));
    reject_field(path, "layers", cJSON_CreateNumber(17));
    assert(!wt_contract_write(path, 0, 32, 2, 731, 4095));
    assert(!wt_policy_load(path));
    assert(!wt_contract_write(path, 0, hidden, layers, 731, 4095));
    FILE *file = fopen(path, "ab");
    assert(file && fputc(0, file) != EOF);
    assert(!fclose(file));
    assert(!wt_policy_load(path));
    file = fopen(path, "wb");
    assert(file && !fclose(file));
    assert(!wt_policy_load(path));
    puts("PASS: 12 incompatible contract/weight-size cases rejected");
    write_weights(path, hidden, layers);
    return 0;
}
