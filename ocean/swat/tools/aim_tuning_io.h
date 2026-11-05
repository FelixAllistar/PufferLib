// Flat parameter view of SwatAimTuning for offline fitting tools.
#ifndef SWAT_AIM_TUNING_IO_H
#define SWAT_AIM_TUNING_IO_H
#include "character_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#define AIM_PARAMS 19
static float* aim_param(SwatAimTuning* k,int i) {
    float* p[AIM_PARAMS]={&k->pocket[0],&k->pocket[1],&k->pocket[2],&k->eye_weight,&k->eye_relief,&k->blade_deg,
        &k->torso_share,&k->torso_max_deg,&k->head_tip_deg,&k->head_cant_deg,&k->neck_share,&k->clavicle_share,
        &k->elbow_hint[0][0],&k->elbow_hint[0][1],&k->elbow_hint[0][2],&k->elbow_hint[1][0],&k->elbow_hint[1][1],&k->elbow_hint[1][2],&k->slide_max};
    return p[i];
}
static const char* aim_param_names[AIM_PARAMS]={"pocket_fwd","pocket_up","pocket_right","eye_weight","eye_relief","blade_deg",
    "torso_share","torso_max_deg","head_tip_deg","head_cant_deg","neck_share","clavicle_share",
    "elbowL_fwd","elbowL_up","elbowL_right","elbowR_fwd","elbowR_up","elbowR_right","slide_max"};
static const float aim_param_lo[AIM_PARAMS]={.02f,.02f,.04f,0,.12f,0, .4f,20,-5,-10,0,0, -1,-1.5f,-1, -1,-1.5f,-.5f,.04f};
static const float aim_param_hi[AIM_PARAMS]={.20f,.20f,.20f,1,.32f,40, 1,50,30,20,1,1, 1,0,.5f, 1,0,1.5f,.20f};
// Reads whitespace-separated name=value pairs; unknown names are ignored.
static int aim_tuning_load(SwatAimTuning* k,const char* path) {
    FILE* f=fopen(path,"r"); if(!f) return 0; char name[64]; float value; int n=0;
    while(fscanf(f," %63[^=]=%f",name,&value)==2) for(int i=0;i<AIM_PARAMS;i++) if(!strcmp(name,aim_param_names[i])) { *aim_param(k,i)=value; n++; }
    fclose(f); return n;
}
static void aim_tuning_print(FILE* f,SwatAimTuning* k) {
    for(int i=0;i<AIM_PARAMS;i++) fprintf(f,"%s=%.4f\n",aim_param_names[i],*aim_param(k,i));
}
#endif
