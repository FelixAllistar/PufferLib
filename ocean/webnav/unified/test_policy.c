#include "policy.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

/* Standalone public-contract checks; the host decides when/how to compile. */
int main(void) {
    prctl(PR_SET_DUMPABLE,0);
    WFView *v=calloc(1,sizeof *v);
    WUCapabilities *caps=calloc(1,sizeof *caps);
    float *obs=malloc(WU_OBS_SIZE*sizeof *obs);
    float *before=malloc(WU_OBS_SIZE*sizeof *before);
    unsigned char *mask=malloc(WU_ACTIONS);
    assert(v&&caps&&obs&&before&&mask);
    wf_view_init(v,100,1000);
    caps->version=WU_CAP_VERSION;
    WUState s={0};WFAction action;
    assert(wf_text_add(v,"Select blue then red",20,&v->instruction)==0);
    v->count=WF_MAX_NODES;
    for(unsigned i=0;i<v->count;i++) {
        v->nodes[i].ref=i+10;
        v->nodes[i].role=WF_INPUT;
        v->nodes[i].flags=WF_VISIBLE|WF_ENABLED;
        assert(wf_text_add(v,"red blue",8,&v->nodes[i].name)==0);
    }
    v->nodes[127].x=NAN;v->nodes[127].scroll_y=INFINITY;
    caps->items[caps->count++]=(WUCapability){.kind=WF_CLICK,.ref=137,.wire_target=137};
    caps->items[caps->count++]=(WUCapability){.kind=WF_INSERT,.ref=10,.wire_target=0,
        .flags=WU_CAP_TEXT|WU_CAP_ASCII,.text_capacity=4};
    caps->items[caps->count++]=(WUCapability){.kind=WF_SELECT_RANGE,.ref=10,.wire_target=10,
        .flags=WU_CAP_RANGE,.min0=10,.max0=20,.step0=3,
        .min1=10,.max1=20,.step1=3,.unit0=WU_UNIT_TEXT_OFFSET,.unit1=WU_UNIT_TEXT_OFFSET};
    caps->items[caps->count++]=(WUCapability){.kind=WF_KEY_DOWN,.ref=0,
        .flags=WU_CAP_RANGE,.min0=32,.max0=126,.unit0=WU_UNIT_KEY};
    caps->items[caps->count++]=(WUCapability){.kind=WF_POINTER_DOWN,.ref=11,.wire_target=11,
        .flags=WU_CAP_RANGE,.min0=10,.max0=100,.unit0=WU_UNIT_PIXEL};
    assert(wu_project(v,caps,&s,obs,mask)==0);
    for(unsigned i=0;i<WU_OBS_SIZE;i++)assert(isfinite(obs[i])&&obs[i]>=-1&&obs[i]<=1);
    assert(obs[WU_GLOBAL_FEATURES+127*WU_NODE_FEATURES]==1);
    assert(mask[WF_CLICK*129+128]);
    assert(mask[WF_WAIT*129]);
    assert(!mask[WF_INSERT*129+1]);
    assert(wu_decode(v,caps,&s,WF_CLICK*129+128,7,&action)==1);
    assert(action.kind==WF_CLICK&&action.target==137&&action.elapsed_ms==7);
    assert(wu_decode(v,caps,&s,WU_TOKEN+1,7,&action)==0);
    assert(action.kind==0&&action.text==NULL&&action.text_length==0);
    assert(s.text_length==4&&!memcmp(s.text,"blue",4));
    assert(wu_project(v,caps,&s,obs,mask)==0&&mask[WF_INSERT*129+1]);
    assert(wu_decode(v,caps,&s,WF_INSERT*129+1,7,&action)==1);
    assert(action.target==0&&action.text==s.text&&action.text_length==4);
    /* Legal but task-incorrect text is kept: the mask never parses an answer. */
    assert(wu_decode(v,caps,&s,WU_LITERAL+'x'-32,7,&action)==0);
    assert(wu_project(v,caps,&s,obs,mask)==0&&mask[WF_INSERT*129+1]);
    s.text[0]=(char)0xff;
    assert(wu_project(v,caps,&s,obs,mask)==0&&!mask[WF_INSERT*129+1]);
    assert(wu_decode(v,caps,&s,WF_INSERT*129+1,7,&action)<0);
    s.text[0]='\n';
    assert(wu_project(v,caps,&s,obs,mask)==0&&!mask[WF_INSERT*129+1]);
    s.text[0]=127;
    assert(wu_project(v,caps,&s,obs,mask)==0&&!mask[WF_INSERT*129+1]);
    assert(wu_decode(v,caps,&s,WU_INSTRUCTION,7,&action)==0);
    assert(wu_project(v,caps,&s,obs,mask)==0&&!mask[WF_INSERT*129+1]);
    assert(wu_decode(v,caps,&s,WU_VALUE,7,&action)==0);
    assert(s.text_length==8&&!memcmp(s.text,"red blue",8));
    assert(wu_decode(v,caps,&s,WU_SET0+128,7,&action)==0);
    assert(wu_decode(v,caps,&s,WU_SET1+256,7,&action)==0);
    assert(wu_decode(v,caps,&s,WF_SELECT_RANGE*129+1,7,&action)==1);
    assert(action.arg0==16&&action.arg1==19);
    s.parameter0=256;s.parameter1=0;
    assert(wu_project(v,caps,&s,obs,mask)==0&&!mask[WF_SELECT_RANGE*129+1]);
    assert(wu_decode(v,caps,&s,WF_SELECT_RANGE*129+1,7,&action)<0);
    assert(mask[WF_KEY_DOWN*129]&&mask[WF_POINTER_DOWN*129+2]);
    assert(wu_decode(v,caps,&s,WF_KEY_DOWN*129,7,&action)==1);
    assert(action.arg0==126&&action.arg1==0);
    assert(wu_decode(v,caps,&s,WF_POINTER_DOWN*129+2,7,&action)==1);
    assert(action.arg0==100&&action.arg1==0);
    /* Equal mapped ranges stay legal even if normalized bins are reversed. */
    s.parameter0=1;s.parameter1=0;
    assert(wu_project(v,caps,&s,obs,mask)==0&&mask[WF_SELECT_RANGE*129+1]);
    memcpy(before,obs,WU_OBS_SIZE*sizeof *obs);
    /* Reordering identical words changes ordered public text features. */
    memcpy(v->text+v->nodes[0].name.offset,"blue red",8);
    assert(wu_project(v,caps,&s,obs,mask)==0);
    assert(!memcmp(before+WU_GLOBAL_FEATURES+64,obs+WU_GLOBAL_FEATURES+64,8*sizeof *obs));
    assert(memcmp(before+WU_GLOBAL_FEATURES+80,obs+WU_GLOBAL_FEATURES+80,8*sizeof *obs));
    assert(wu_decode(v,caps,&s,WF_WAIT*129,7,&action)==1&&action.target==0);
    /* Pure click views expose no unused text/parameter controls. */
    caps->count=1;
    assert(wu_project(v,caps,&s,obs,mask)==0);
    for(unsigned i=WU_SET0;i<WU_ACTIONS;i++)assert(!mask[i]);
    assert(wu_decode(v,caps,&s,WU_SET0,7,&action)<0);
    assert(wu_decode(v,caps,&s,WU_LITERAL,7,&action)<0);
    free(mask);free(before);free(obs);free(caps);free(v);
    return 0;
}
