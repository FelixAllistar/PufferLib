#include "spatial_audio.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
static void impulse(SwatSpatialAudio* audio,b3Vec3 direction,float* l,float* r) {
    swat_spatial_reset(audio);
    for(int i=0;i<4;i++) {
        float input[SWAT_SPATIAL_FRAMES]={0}; if(!i) input[0]=1;
        swat_spatial_process(audio,0,1,direction,input,SWAT_SPATIAL_FRAMES,l+i*SWAT_SPATIAL_FRAMES,r+i*SWAT_SPATIAL_FRAMES);
    }
}
int main(int argc,char** argv) {
    SwatSpatialAudio* audio=swat_spatial_open(48000,2);
    if(!audio) { puts("Steam Audio runtime unavailable; stereo fallback remains available"); return argc>1 && !strcmp(argv[1],"--require") ? 1 : 0; }
    float l[1024],r[1024],front[1024],other[1024];
    impulse(audio,swat_v(1,0,0),l,r);
    float left=0,right=0;
    for(int i=0;i<1024;i++) { assert(isfinite(l[i]) && isfinite(r[i])); left+=l[i]*l[i]; right+=r[i]*r[i]; }
    assert(right>left*1.1f && left>0);
    impulse(audio,swat_v(0,0,-1),front,other);
    impulse(audio,swat_v(0,0,1),l,r);
    float difference=0; for(int i=0;i<1024;i++) difference+=fabsf(front[i]-l[i]);
    assert(difference>.01f);
    impulse(audio,swat_v(0,1,-1),l,r);
    difference=0; for(int i=0;i<1024;i++) difference+=fabsf(front[i]-l[i]);
    assert(difference>.01f);
    impulse(audio,swat_v(0,0,-1),l,r); assert(!memcmp(front,l,sizeof(front)));
    swat_spatial_close(audio);
    printf("PASS Steam Audio native HRTF: right/left energy %.3f/%.3f, distinct front/back/elevation and deterministic reset\n",right,left);
    return 0;
}
