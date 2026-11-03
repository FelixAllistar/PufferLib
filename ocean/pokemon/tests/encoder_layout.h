// Parameter layout and fill pattern shared by the CUDA encoder tests.
// PK_EMBED/PK_MID select the encoder widths under test (default 32/64).
#pragma once
#include <vector>
extern "C" void pk_reference(float*, int, int, int, int, const float*, const float*, float*, float*);
static void pk_test_sizes() {
    if (getenv("PK_EMBED")) pk_embed = atoi(getenv("PK_EMBED"));
    if (getenv("PK_MID")) pk_mid = atoi(getenv("PK_MID"));
    printf("encoder embed=%d mid=%d\n", pk_embed, pk_mid);
}
// Per registered tensor: the MLP input width (bias column follows).
static std::vector<int> pk_test_features(int H) {
    std::vector<int> f = {PK_MOVE_IN, pk_mid, PK_MON_IN(pk_embed), pk_mid};
    f.push_back(PK_FUSION(pk_embed)); f.push_back(H);
    for (int x : {PK_SPECIES_IN, pk_mid, PK_MOVE_IN, pk_mid, H, H}) f.push_back(x);
    return f;
}
static int pk_test_cols(const Allocator& params, int p) {
    return params.regs[p].shape[1] ? params.regs[p].shape[1] : params.regs[p].shape[0];
}
static float pk_test_value(int feature, int c, int index, float scale) {
    float wave = sinf(index*0.731f);
    return c < feature ? scale*wave : c == feature ? 0.2f : 0;
}
