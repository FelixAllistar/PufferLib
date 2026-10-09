#pragma once
#include <math.h>

#ifdef __CUDACC__
#define FPT_HD __host__ __device__ static inline
#else
#define FPT_HD static inline
#endif

// Semantic observation v1. Transport categorical IDs exactly (all fit in bf16),
// then expand them before learned layers. No RAM, pixels, HUD or route labels
// are passed through to the policy.
enum {
    FPT_OBSERVATION_VERSION=1,
    FPT_PLAYER_FEATURES=64,
    FPT_GRID_W=17, FPT_GRID_H=13, FPT_GRID_CELLS=FPT_GRID_W*FPT_GRID_H,
    FPT_MAP_W=9, FPT_MAP_H=7, FPT_MAP_CELLS=FPT_MAP_W*FPT_MAP_H,
    FPT_TERRAIN_CHANNELS=12, FPT_CONV_CHANNELS=8,
    FPT_TERRAIN_FEATURES=64, FPT_PLAYER_HIDDEN=32, FPT_ENTITY_HIDDEN=32,
    // A short bar uses six segments. A long bar reserves a second enemy slot,
    // whose six entries carry its outer half. This covers the actual maximum
    // without allocating twelve entries for every ordinary enemy slot.
    FPT_ENTITY_MISC_OFFSET=36, FPT_ENTITY_FIREBALL_OFFSET=45, FPT_ENTITY_BLOCK_OFFSET=47,
    FPT_ENTITY_COUNT=49, FPT_ENTITY_FEATURES=24,
    FPT_ENTITY_TYPES=72,
    FPT_TERRAIN_OFFSET=FPT_PLAYER_FEATURES,
    FPT_ENTITY_OFFSET=FPT_TERRAIN_OFFSET+FPT_GRID_CELLS,
    FPT_OBS=FPT_ENTITY_OFFSET+FPT_ENTITY_COUNT*FPT_ENTITY_FEATURES,
    FPT_CNN_FLAT=FPT_MAP_CELLS*FPT_CONV_CHANNELS,
    FPT_JOINED=FPT_TERRAIN_FEATURES+FPT_PLAYER_HIDDEN+2*FPT_ENTITY_HIDDEN+1,
    FPT_ENTITY_INPUTS=FPT_ENTITY_FEATURES-1+FPT_ENTITY_TYPES,
    FPT_ENCODER_LAYERS=7
};
typedef struct {int ih,iw,ic,oh,ow,oc,k,stride,pad;} FptConvShape;
#ifdef __cplusplus
static constexpr FptConvShape FPT_CONVS[2]={
#else
static const FptConvShape FPT_CONVS[2]={
#endif
    {13,17,FPT_TERRAIN_CHANNELS,7,9,8,3,2,1},
    {7,9,8,7,9,8,3,1,1}
};
enum {
    FPT_P_GRID_DX=9, FPT_P_ANCHOR_Y=8, FPT_P_SCROLL_FRACTION=52,
    FPT_E_TYPE=0, FPT_E_DX=1, FPT_E_DY=2, FPT_E_WIDTH=3, FPT_E_HEIGHT=4,
    FPT_E_VX=5, FPT_E_VY=6, FPT_E_MOTION_KNOWN=7,
    FPT_E_STATE_BITS=8, FPT_E_DIRECTION=16, FPT_E_TIMER=17,
    FPT_E_INTERVAL=18, FPT_E_PHASE_X=19, FPT_E_PHASE_Y=20,
    FPT_E_SPIN=21, FPT_E_COLLISION=22, FPT_E_ACTIVE=23,
    FPT_ENTITY_HAMMER=65, FPT_ENTITY_FIREBALL=66, FPT_ENTITY_BLOCK=67,
    FPT_ENTITY_MUSHROOM=68, FPT_ENTITY_FLOWER=69, FPT_ENTITY_STAR=70,
    FPT_ENTITY_1UP=71
};

// -2 is outside the viewport, -1 is a visible but not resident column, 0 is
// known empty. Categories are never treated as ordered numeric magnitudes.
FPT_HD float fpt_terrain_feature(int tile,int channel,int cell,
        float grid_dx,float player_y,float scroll_fraction) {
    int col=cell%FPT_GRID_W,row=cell/FPT_GRID_W;
    float coverage=col==0?1-scroll_fraction:col==16?scroll_fraction:1;
    if(tile==-2)coverage=0;
    int known=tile>=0;
    int climb=tile==0x24||tile==0x25||tile==0x26||tile==0x6d;
    int coin=tile==0xc2||tile==0xc3;
    int invisible=tile==0x5f||tile==0x60;
    // 0x23 looks blank while a block bounces, but still collides in the ROM.
    int blank=tile<=0;
    int threshold=tile<0x40?0x10:tile<0x80?0x61:tile<0xc0?0x88:0xc4;
    switch(channel) {
    case 0:return (float)known;
    case 1:return known&&!blank&&!climb&&!coin&&!invisible&&tile!=0xc5;
    case 2:return known&&!blank&&!climb&&!coin&&tile>=threshold&&tile!=0xc5;
    case 3:return known&&((tile>=0x51&&tile<=0x60)||tile==0xc0||tile==0xc1);
    case 4:return known&&((tile>=0x10&&tile<=0x15)||(tile>=0x1c&&tile<=0x21)||tile==0x6b||tile==0x6c);
    case 5:return known&&climb;
    case 6:return known&&(tile==0x24||tile==0x25||tile==0xc5);
    case 7:return known&&coin;
    case 8:return known&&(tile==0x67||tile==0x68);
    case 9:return coverage;
    case 10:return coverage>0?grid_dx+(col*16+8)*(1.0f/256.0f):0;
    case 11:return coverage>0?(32+row*16+8)*(1.0f/256.0f)-player_y:0;
    default:return 0;
    }
}

// Parameter order shared by the CUDA encoder and standalone CPU evaluator.
FPT_HD int fpt_encoder_rows(int layer,int hidden) {
    return layer<2?FPT_CONV_CHANNELS:layer==2?FPT_TERRAIN_FEATURES:
        layer==3?FPT_PLAYER_HIDDEN:layer<6?FPT_ENTITY_HIDDEN:hidden;
}
FPT_HD int fpt_encoder_cols(int layer) {
    return layer==0?9*FPT_TERRAIN_CHANNELS:layer==1?9*FPT_CONV_CHANNELS:
        layer==2?FPT_CNN_FLAT:layer==3?FPT_PLAYER_FEATURES:
        layer==4?FPT_ENTITY_INPUTS:layer==5?FPT_ENTITY_HIDDEN:FPT_JOINED;
}
FPT_HD int fpt_encoder_weight_count(int hidden) {
    int n=0;for(int l=0;l<FPT_ENCODER_LAYERS;l++)n+=fpt_encoder_rows(l,hidden)*(fpt_encoder_cols(l)+1);return n;
}
FPT_HD int fpt_policy_weight_count(int hidden,int layers) {
    return fpt_encoder_weight_count(hidden)+65*hidden+layers*3*hidden*hidden;
}
