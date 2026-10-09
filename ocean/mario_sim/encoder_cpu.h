#pragma once
#include "policy_shape.h"
#include <string.h>

// Standalone FP32 reference/evaluation path. Tensor order matches encoder.cu;
// this implementation deliberately uses ordinary scalar loops, not CUDA code.
typedef struct {
    const float *w[FPT_ENCODER_LAYERS],*b[FPT_ENCODER_LAYERS];
    int hidden;
    float terrain[FPT_GRID_CELLS*FPT_TERRAIN_CHANNELS],image[2][FPT_CNN_FLAT];
    float terrain_features[FPT_TERRAIN_FEATURES],player[FPT_PLAYER_HIDDEN];
    float entities[2][FPT_ENTITY_COUNT*FPT_ENTITY_HIDDEN];
    float joined[FPT_JOINED],out[1024];
} FptCpuEncoder;

static int fpt_cpu_encoder_bind(FptCpuEncoder* e,const float* weights,int hidden) {
    e->hidden=hidden;int offset=0;
    for(int l=0;l<FPT_ENCODER_LAYERS;l++) {
        e->w[l]=weights+offset;offset+=fpt_encoder_rows(l,hidden)*fpt_encoder_cols(l);
        e->b[l]=weights+offset;offset+=fpt_encoder_rows(l,hidden);
    }
    return offset;
}
static void fpt_cpu_dense(const float* input,const float* w,const float* bias,
        float* out,int rows,int cols) {
    for(int r=0;r<rows;r++) {
        float sum=0;for(int c=0;c<cols;c++)sum=fmaf(input[c],w[r*cols+c],sum);
        out[r]=fmaxf(0,sum+bias[r]);
    }
}
static const float* fpt_cpu_encode(FptCpuEncoder* e,const float* obs) {
    for(int cell=0;cell<FPT_GRID_CELLS;cell++)for(int ch=0;ch<FPT_TERRAIN_CHANNELS;ch++)
        e->terrain[cell*FPT_TERRAIN_CHANNELS+ch]=fpt_terrain_feature(
            (int)obs[FPT_TERRAIN_OFFSET+cell],ch,cell,obs[FPT_P_GRID_DX],
            obs[FPT_P_ANCHOR_Y],obs[FPT_P_SCROLL_FRACTION]);
    for(int l=0;l<2;l++) {
        FptConvShape c=FPT_CONVS[l];int channels=c.ic;
        const float* input=l?e->image[l-1]:e->terrain;
        for(int y=0;y<c.oh;y++)for(int x=0;x<c.ow;x++)for(int oc=0;oc<c.oc;oc++) {
            float sum=0;
            for(int ky=0;ky<3;ky++)for(int kx=0;kx<3;kx++) {
                int iy=y*c.stride+ky-c.pad,ix=x*c.stride+kx-c.pad;if(iy<0||iy>=c.ih||ix<0||ix>=c.iw)continue;
                for(int ic=0;ic<channels;ic++)sum=fmaf(input[(iy*c.iw+ix)*channels+ic],
                    e->w[l][oc*9*channels+(ky*3+kx)*channels+ic],sum);
            }
            e->image[l][(y*c.ow+x)*c.oc+oc]=fmaxf(0,sum+e->b[l][oc]);
        }
    }
    fpt_cpu_dense(e->image[1],e->w[2],e->b[2],e->terrain_features,FPT_TERRAIN_FEATURES,FPT_CNN_FLAT);
    fpt_cpu_dense(obs,e->w[3],e->b[3],e->player,FPT_PLAYER_HIDDEN,FPT_PLAYER_FEATURES);
    int count=0;
    for(int k=0;k<FPT_ENTITY_COUNT;k++) {
        const float* in=obs+FPT_ENTITY_OFFSET+k*FPT_ENTITY_FEATURES;
        float* first=e->entities[0]+k*FPT_ENTITY_HIDDEN,*second=e->entities[1]+k*FPT_ENTITY_HIDDEN;
        int type=(int)in[0];
        if(type<=0||type>=FPT_ENTITY_TYPES){memset(first,0,FPT_ENTITY_HIDDEN*sizeof(float));memset(second,0,FPT_ENTITY_HIDDEN*sizeof(float));continue;}
        count++;
        for(int ch=0;ch<FPT_ENTITY_HIDDEN;ch++) {
            float sum=e->w[4][ch*FPT_ENTITY_INPUTS+FPT_ENTITY_FEATURES-1+type];
            for(int f=0;f<FPT_ENTITY_FEATURES-1;f++)sum=fmaf(in[f+1],e->w[4][ch*FPT_ENTITY_INPUTS+f],sum);
            first[ch]=fmaxf(0,sum+e->b[4][ch]);
        }
        fpt_cpu_dense(first,e->w[5],e->b[5],second,FPT_ENTITY_HIDDEN,FPT_ENTITY_HIDDEN);
    }
    memcpy(e->joined,e->terrain_features,sizeof(e->terrain_features));
    memcpy(e->joined+FPT_TERRAIN_FEATURES,e->player,sizeof(e->player));
    for(int ch=0;ch<FPT_ENTITY_HIDDEN;ch++) {
        float maximum=0,sum=0;
        for(int k=0;k<FPT_ENTITY_COUNT;k++) {
            int type=(int)obs[FPT_ENTITY_OFFSET+k*FPT_ENTITY_FEATURES];
            if(type<=0||type>=FPT_ENTITY_TYPES)continue;
            float v=e->entities[1][k*FPT_ENTITY_HIDDEN+ch];sum+=v;if(v>maximum)maximum=v;
        }
        e->joined[FPT_TERRAIN_FEATURES+FPT_PLAYER_HIDDEN+ch]=maximum;
        e->joined[FPT_TERRAIN_FEATURES+FPT_PLAYER_HIDDEN+FPT_ENTITY_HIDDEN+ch]=count?sum/count:0;
    }
    e->joined[FPT_JOINED-1]=count*(1.0f/128);
    fpt_cpu_dense(e->joined,e->w[6],e->b[6],e->out,e->hidden,FPT_JOINED);return e->out;
}
