#define _POSIX_C_SOURCE 200809L
#include "../pokemon.h"
#include "../policy_view.h"

#ifdef PUFFERLIB_OBS_U8_NORMALIZED
#error Pokemon checkpoints use raw byte observations; review viewer conversion
#endif

int main(void) {
    // Exercise the production viewer forward path, not a second conversion
    // helper. All byte values are exactly representable in float and BF16.
    const int h = 32;
    size_t count=pk_parameter_count(h,1);
    // Match load_weights' seven-float alignment tail.
    float* data=calloc(count+7,sizeof(float));
    Weights weights = {.data = data, .size = count+7};
    PKPolicy policy = {.net = make_pokemon_puffernet(&weights, 1, h, 1)};
    uint8_t obs[PK_OBS], mask[PK_ACTIONS] = {1};
    for (int offset = 0; offset < 2; offset++) {
        for (int i = 0; i < PK_OBS; i++) obs[i] = (uint8_t)(i + offset);
        assert(pk_policy_action(&policy, obs, mask, 1) == 0);
        for (int i = 0; i < PK_OBS; i++) assert(policy.net->obs[i] == (float)obs[i]);
    }
    free_puffernet(policy.net);
    free(data);
    puts("Pokemon viewer raw-byte input contract passed (all 256 values)");
    return 0;
}
