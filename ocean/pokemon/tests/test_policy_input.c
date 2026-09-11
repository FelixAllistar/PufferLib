#define _POSIX_C_SOURCE 200809L
#include "../pokemon.h"
#include "../policy_view.h"

#ifdef PUFFERLIB_OBS_U8_NORMALIZED
#error Pokemon checkpoints use raw byte observations; review viewer conversion
#endif

int main(void) {
    // Exercise the production viewer forward path, not a second conversion
    // helper. All byte values are exactly representable in float and BF16.
    const int h = 8;
    float data[PK_OBS * 8 + (PK_ACTIONS + 1) * 8 + 3 * 8 * 8 + 32] = {0};
    Weights weights = {.data = data, .size = sizeof(data) / sizeof(*data)};
    int sizes[] = {PK_ACTIONS};
    PKPolicy policy = {.net = make_puffernet(&weights, 1, PK_OBS, h, 1, sizes, 1)};
    uint8_t obs[PK_OBS], mask[PK_ACTIONS] = {1};
    for (int offset = 0; offset < 2; offset++) {
        for (int i = 0; i < PK_OBS; i++) obs[i] = (uint8_t)(i + offset);
        assert(pk_policy_action(&policy, obs, mask, 1) == 0);
        for (int i = 0; i < PK_OBS; i++) assert(policy.net->obs[i] == (float)obs[i]);
    }
    free_puffernet(policy.net);
    puts("Pokemon viewer raw-byte input contract passed (all 256 values)");
    return 0;
}
