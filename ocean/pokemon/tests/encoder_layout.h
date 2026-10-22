// Parameter layout and fill pattern shared by the CUDA encoder tests.
// PK_EMBED/PK_MID select the encoder widths under test (default 32/64);
// PK_ATTN_LAYERS/WIDTH/HEADS enable the entity Transformer.
#pragma once
#include <vector>
extern "C" void pk_reference(float*, int, int, int, int, PKAttn, const float*, const float*, float*, float*);
static void pk_test_sizes() {
    if (getenv("PK_EMBED")) pk_embed = atoi(getenv("PK_EMBED"));
    if (getenv("PK_MID")) pk_mid = atoi(getenv("PK_MID"));
    if (getenv("PK_ATTN_LAYERS")) pk_attn.layers = atoi(getenv("PK_ATTN_LAYERS"));
    if (getenv("PK_ATTN_WIDTH")) pk_attn.width = atoi(getenv("PK_ATTN_WIDTH"));
    if (getenv("PK_ATTN_HEADS")) pk_attn.heads = atoi(getenv("PK_ATTN_HEADS"));
    printf("encoder embed=%d mid=%d attention layers=%d width=%d heads=%d\n",
        pk_embed, pk_mid, pk_attn.layers, pk_attn.width, pk_attn.heads);
}
// Per registered tensor: the MLP input width (bias column follows), or -1 for a
// dense Transformer matrix/bias and -2 for an LN scale.
static std::vector<int> pk_test_features(int H) {
    std::vector<int> f = {PK_MOVE_IN, pk_mid, PK_MON_IN(pk_embed), pk_mid};
    if (pk_attn.layers) {
        int D = pk_attn.width;
        f.push_back(PK_TOKEN_IN(pk_embed)); f.push_back(D);
        puf_transformer::Config c = {1, PK_TOKENS, D, pk_attn.layers, pk_attn.heads, 4*D};
        for (auto s : puf_transformer::parameter_specs(c)) f.push_back(s.initialization == 1 ? -2 : -1);
    }
    f.push_back(pk_attn.layers ? pk_attn.width : PK_FUSION(pk_embed)); f.push_back(H);
    for (int x : {PK_SPECIES_IN, pk_mid, PK_MOVE_IN, pk_mid, H, H}) f.push_back(x);
    return f;
}
static int pk_test_cols(const Allocator& params, int p) {
    return params.regs[p].shape[1] ? params.regs[p].shape[1] : params.regs[p].shape[0];
}
static float pk_test_value(int feature, int c, int index, float scale) {
    float wave = sinf(index*0.731f);
    if (feature == -2) return 1 + 5*scale*wave;
    if (feature == -1) return 10*scale*wave;
    return c < feature ? scale*wave : c == feature ? 0.2f : 0;
}
