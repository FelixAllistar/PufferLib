#define _POSIX_C_SOURCE 200809L
#include "../pokemon.h"
#include "../policy_view.h"

#ifdef PUFFERLIB_OBS_U8_NORMALIZED
#error Pokemon checkpoints use raw byte observations; review viewer conversion
#endif

int main(void) {
    Ini evaluation = {0};
    puf_ini_load_file(&evaluation, "config/default.ini");
    puf_ini_load_file(&evaluation, "config/pokemon.ini");
    puf_ini_put(&evaluation, "env.reset_state_prob", "1");
    puf_ini_put(&evaluation, "env.reset_state_bank", "/missing/training-only-bank.pks");
    pk_personality_configure(&evaluation, "eval");
    assert(puf_ini_get(&evaluation, "env", "reset_state_prob") == 0);
    assert(puf_ini_get(&evaluation, "env", "force_core_combos") == 0);
    assert(!pk_state_bank.states && !pk_core_deck.cards);
    puf_ini_free(&evaluation);
    // Exercise the production viewer forward path, not a second conversion
    // helper. All byte values are exactly representable in float and BF16.
    const int h = 32;
    size_t count=pk_parameter_count(h,1);
    // Match load_weights' seven-float alignment tail.
    float* data=calloc(count+7,sizeof(float));
    Weights weights = {.data = data, .size = count+7};
    PKPolicy policy = {.net = pk_cpu_make(&weights, 1, h, 1)};
    uint8_t obs[PK_OBS], mask[PK_ACTIONS] = {1};
    for (int offset = 0; offset < 2; offset++) {
        for (int i = 0; i < PK_OBS; i++) obs[i] = (uint8_t)(i + offset);
        assert(pk_policy_action(&policy, obs, mask, 1) == 0);
        for (int i = 0; i < PK_OBS; i++) assert(policy.observations[i] == (float)obs[i]);
    }
    pk_cpu_free(policy.net);
    char directory[] = "/tmp/pokemon-policy-load.XXXXXX";
    assert(mkdtemp(directory));
    char checkpoint[256], config[256];
    snprintf(checkpoint, sizeof(checkpoint), "%s/model.bin", directory);
    snprintf(config, sizeof(config), "%s/config.ini", directory);
    FILE* file = fopen(checkpoint, "wb");
    assert(file && fwrite(data, sizeof(float), count, file) == count);
    fclose(file);
    file = fopen(config, "w");
    assert(file);
    fprintf(file, "[env]\nabi_version=3\npolicy_version=3\nrules_sha=%s\n"
        "[policy]\nhidden_size=32\nnum_layers=1\n", PK_RULES_SHA);
    fclose(file);
    PKPolicy loaded = {0};
    pk_load_policy(&loaded, checkpoint, 0, 0, NULL);
    pk_reset_policy(&loaded, 42);
    assert(pk_policy_action(&loaded, obs, mask, 0) == 0);
    pk_reset_policy(&loaded, 42);
    assert(pk_policy_action(&loaded, obs, mask, 1) == 0);
    pk_free_policy(&loaded);
    unlink(checkpoint);
    unlink(config);
    rmdir(directory);
    free(data);
    puts("Pokemon viewer raw-byte input contract passed (all 256 values)");
    return 0;
}
