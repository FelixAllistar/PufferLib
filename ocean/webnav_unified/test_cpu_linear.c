#include <sys/prctl.h>
#include "../../src/puffercpu.c"
#include "webnav_unified.h"
#include "cpu_linear.h"
#include <time.h>

static uint32_t test_rng=7;
static float test_value(void){
    test_rng=test_rng*1664525u+1013904223u;
    return (float)(test_rng>>8)/16777216.0f-0.5f;
}
static void check(int width,int outputs,int batch){
    float *input=malloc((size_t)batch*width*sizeof(float));
    float *weights=malloc((size_t)width*outputs*sizeof(float));
    float *expected=malloc((size_t)batch*outputs*sizeof(float));
    float *actual=malloc((size_t)batch*outputs*sizeof(float));
    assert(input&&weights&&expected&&actual);
    for(int i=0;i<batch*width;i++)input[i]=test_value();
    for(int i=0;i<width*outputs;i++)weights[i]=test_value()/sqrtf((float)width);
    Linear reference={.output=expected,.weights=weights,.batch_size=batch,.input_dim=width,.output_dim=outputs};
    Linear fast=reference;fast.output=actual;
    linear(&reference,input);wu_linear(&fast,input);
    for(int i=0;i<batch*outputs;i++){
        assert(isfinite(actual[i]));
        assert(fabsf(actual[i]-expected[i])<=2e-4f+2e-5f*fabsf(expected[i]));
    }
    if(width==OBS_SIZE&&outputs==64&&batch==8){
        volatile float consume=0;
        clock_t start=clock();
        for(int repeat=0;repeat<3;repeat++){linear(&reference,input);consume+=expected[repeat];}
        double scalar=(double)(clock()-start)/CLOCKS_PER_SEC;
        start=clock();
        for(int repeat=0;repeat<3;repeat++){wu_linear(&fast,input);consume+=actual[repeat];}
        double simd=(double)(clock()-start)/CLOCKS_PER_SEC;
        printf("CPU encoder benchmark: scalar %.3f ms, SIMD %.3f ms per 8-lane forward, %.2fx; checksum %.6f\n",
            scalar*1000/3,simd*1000/3,simd>0?scalar/simd:0,(double)consume);
    }
    free(input);free(weights);free(expected);free(actual);
}
int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    check(OBS_SIZE,64,4);check(OBS_SIZE,64,8);
    check(64,WU_ACTIONS+1,4);check(64,WU_ACTIONS+1,8);
    check(3,7,1);check(35,11,3);
    puts("PASS: SIMD FP32 linear layers match scalar reference within tolerance; 4/8 lanes and tail dimensions");
    return 0;
}
