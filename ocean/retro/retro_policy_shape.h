#pragma once
#include <cstddef>
#include "retro_resolution.h"

// The only Retro policy architecture. Image tensors use NHWC; convolution
// weights use [output_channel, kernel_y, kernel_x, input_channel]. All spatial
// positions, including the bottom/right edges, feed at least one output.
struct RetroConvShape { int ih, iw, ic, oc, k, stride, pad, oh, ow; };
static constexpr RetroConvShape RETRO_CONVS[3] = {
    {RETRO_WINDOW_H,RETRO_WINDOW_W,1,8,8,4,0,(RETRO_WINDOW_H-8)/4+1,(RETRO_WINDOW_W-8)/4+1},
    {RETRO_CONVS[0].oh,RETRO_CONVS[0].ow,8,16,4,2,1,RETRO_CONVS[0].oh/2,RETRO_CONVS[0].ow/2},
    {RETRO_CONVS[1].oh,RETRO_CONVS[1].ow,16,32,3,2,1,(RETRO_CONVS[1].oh+1)/2,(RETRO_CONVS[1].ow+1)/2},
};
static constexpr int RETRO_RAM_INPUTS=112, RETRO_IMAGE_INPUTS=RETRO_WINDOW_H*RETRO_WINDOW_W;
static constexpr int RETRO_POLICY_INPUTS=RETRO_RAM_INPUTS+RETRO_IMAGE_INPUTS;
static constexpr int RETRO_RAM_HIDDEN=32, RETRO_CNN_FEATURES=RETRO_CONVS[2].oh*RETRO_CONVS[2].ow*32;
static constexpr int RETRO_FUSED_FEATURES=RETRO_CNN_FEATURES+RETRO_RAM_HIDDEN;
static constexpr int RETRO_COL_ELEMENTS=RETRO_CONVS[0].oh*RETRO_CONVS[0].ow*8*8;

static inline size_t retro_encoder_weights(int hidden) {
    size_t count=0;
    for(auto c:RETRO_CONVS) count+=c.oc*c.k*c.k*c.ic+c.oc;
    return count+RETRO_RAM_HIDDEN*RETRO_RAM_INPUTS+RETRO_RAM_HIDDEN
        +hidden*RETRO_FUSED_FEATURES+hidden;
}
static inline size_t retro_policy_weights(int hidden,int layers) {
    return retro_encoder_weights(hidden)+65*hidden+layers*3*hidden*hidden;
}
