#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#define ROW 2048u
static const WFFamily *api;
static uint32_t *words;
static unsigned checks;
static const struct {const char *name;unsigned rgb;} named_colors[]={
    {"black",0x000000},{"white",0xffffff},{"aqua",0x00ffff},{"blue",0x0000ff},
    {"gray",0x808080},{"green",0x008000},{"lime",0x00ff00},{"maroon",0x800000},
    {"navy",0x000080},{"olive",0x808000},{"purple",0x800080},{"red",0xff0000},
    {"silver",0xc0c0c0},{"teal",0x008080},{"yellow",0xffff00},{"pink",0xffc0cb},
    {"magenta",0xff00ff},{"gold",0xffd700},{"orange",0xffa500}
};
static float bits(uint32_t x){float f;memcpy(&f,&x,sizeof f);return f;}
static uint32_t *row(unsigned lane){return words+lane*ROW;}
static void batch(void){api->batch(words);assert(!api->validate(row(0)));}
static void reset(unsigned task,unsigned seed){
    memset(words,0,ROW*4*sizeof *words);
    row(0)[WF_VERSION]=WF_ABI_VERSION;row(0)[WF_TASK]=task;
    row(0)[WF_OP]=WF_RESET;row(0)[WF_SEED]=seed;batch();
    assert(row(0)[WF_STATUS]==WF_RUNNING);
}
static WFView view(void){WFView v;assert(!api->observe(row(0),&v));return v;}
static const WFNode *ref(const WFView *v,unsigned r){
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==r)return &v->nodes[i];
    return NULL;
}
static void send(unsigned kind,unsigned target,unsigned arg0,unsigned arg1,
                 const char *text,unsigned ms){
    WFAction a={.kind=kind,.target=target,.arg0=arg0,.arg1=arg1,
                .text=text,.text_length=text?strlen(text):0,.elapsed_ms=ms};
    assert(!api->action(row(0),&a));batch();checks++;
}
static const char *node_text(const WFView *v,unsigned r,int value){
    const WFNode *n=ref(v,r);assert(n);
    const char *s=wf_text_get(v,value?n->value:n->name);assert(s);return s;
}
static unsigned color_from_hex(const char *s){
    unsigned x=0;assert(s);
    if(*s=='#')s++;
    assert(strlen(s)==6);
    for(unsigned i=0;i<6;i++){
        char c=s[i];unsigned d=c>='0'&&c<='9'?(unsigned)(c-'0'):
            c>='a'&&c<='f'?(unsigned)(c-'a'+10):
            c>='A'&&c<='F'?(unsigned)(c-'A'+10):16;
        assert(d<16);x=x*16+d;
    }
    return x;
}
static double color_reward(unsigned got,unsigned want){
    unsigned d=0;
    for(unsigned shift=0;shift<=16;shift+=8){
        int a=(got>>shift)&255,b=(want>>shift)&255;d+=(unsigned)abs(a-b);
    }
    return 1.0-(double)d/765.0;
}
static unsigned desired_color(const WFView *v,unsigned task){
    if(task==5)return color_from_hex(node_text(v,3,1));
    const char *q=wf_text_get(v,v->instruction);assert(q);
    char name[64];assert(sscanf(q,"Select %63s with the color picker",name)==1);
    for(unsigned i=0;i<sizeof named_colors/sizeof named_colors[0];i++)
        if(!strcmp(name,named_colors[i].name))return named_colors[i].rgb;
    abort();
}
static void check_projection_and_distribution(void){
    unsigned different[6]={0},same[6]={0};
    for(unsigned task=0;task<6;task++)for(unsigned seed=0;seed<80;seed++){
        reset(task,1000+seed);
        WFView v=view();const char *q=wf_text_get(&v,v.instruction);
        assert(q&&*q&&v.deadline_ms==(task==2?20000u:task>=4?7000u:10000u));
        if(task==0){
            assert(row(0)[32]>=3&&row(0)[32]<=9);
            assert(ref(&v,1)&&ref(&v,2));
            if(row(0)[42]==row(0)[43])same[task]++;else different[task]++;
        }else if(task==1||task==2){
            int lo,hi;char ori[32];
            assert(sscanf(node_text(&v,1,0),"Slider 1 (%d to %d, %31[^)])",&lo,&hi,ori)==3);
            assert(hi>=lo&&(!strcmp(ori,"vertical")||!strcmp(ori,"horizontal")));
            if(task==1)assert((lo==-100||lo==0||lo==10||lo==100)&&
                              (hi-lo==5||hi-lo==10||hi-lo==50||hi-lo==100));
            else assert(lo==0&&hi==20&&ref(&v,3));
            if(row(0)[36]==row(0)[37])same[task]++;else different[task]++;
        }else if(task==3){
            assert(!strcmp(node_text(&v,4,1),"0"));
            if(row(0)[36]==row(0)[37])same[task]++;else different[task]++;
        }else{
            assert(!strcmp(node_text(&v,1,1),"AB2567"));
            assert(desired_color(&v,task)<=0xffffff);
            if(desired_color(&v,task)==0xab2567)same[task]++;else different[task]++;
        }
        /* Private targets are absent from public projection. The swatch in
           task 5 is intentionally public, so its color is not changed here. */
        if(task!=5){
            uint32_t copy[ROW];memcpy(copy,row(0),sizeof copy);
            if(task==0)copy[43]=(copy[43]+1)%copy[32];
            else if(task<=2)copy[37]=copy[37]==copy[36]?
                (copy[36]==copy[33]?copy[34]:copy[33]):copy[36];
            else if(task==3)copy[37]=copy[37]==0?1:0;
            else copy[46]^=0x010101;
            WFView hidden;assert(!api->observe(copy,&hidden));
            assert(!memcmp(&v,&hidden,sizeof v));
        }
        checks++;
    }
    assert(different[0]&&different[1]&&different[2]&&different[3]&&
           different[4]&&different[5]);
    assert(same[0]||same[1]||same[2]||same[3]);
}
static void choose_list(void){
    reset(0,41);WFView v=view();const char *q=wf_text_get(&v,v.instruction);
    char wanted[64];assert(sscanf(q,"Select %63s from the list",wanted)==1);
    unsigned selected=99;
    for(unsigned i=0;i<row(0)[32];i++)
        if(!strcmp(node_text(&v,10+i,0),wanted))selected=i;
    assert(selected<row(0)[32]);
    send(WF_SELECT_OPTION,1,selected,0,NULL,100);
    send(WF_CLICK,2,0,0,NULL,200);
    assert(row(0)[WF_STATUS]==WF_TERMINAL&&fabsf(bits(row(0)[WF_RAW_REWARD])-1)<1e-6f);
    assert(fabsf(bits(row(0)[WF_TIMED_REWARD])-0.98f)<1e-5f);
    reset(0,42);send(WF_SELECT_OPTION,1,(row(0)[43]+1)%row(0)[32],0,NULL,100);
    send(WF_CLICK,2,0,0,NULL,200);assert(bits(row(0)[WF_RAW_REWARD])==-1.0f);
}
static void sliders(void){
    for(unsigned task=1;task<=2;task++){
        reset(task,52+task);WFView v=view();const char *q=wf_text_get(&v,v.instruction);
        int goal[3]={0},count=task==1?1:3;
        if(task==1)assert(sscanf(q,"Select %d with the slider",goal)==1);
        else assert(sscanf(q,"Set the sliders to the combination [%d,%d,%d]",
                           &goal[0],&goal[1],&goal[2])==3);
        for(int i=0;i<count;i++){
            int lo,hi;char ori[32];
            assert(sscanf(node_text(&v,(unsigned)i+1,0),"Slider %*u (%d to %d, %31[^)])",
                          &lo,&hi,ori)==3);
            unsigned fraction=(unsigned)((goal[i]-lo)*1000/(hi-lo));
            if(!strcmp(ori,"vertical"))fraction=1000-fraction;
            send(WF_POINTER_DOWN,(unsigned)i+1,fraction,0,NULL,100+i*30);
            send(WF_POINTER_UP,(unsigned)i+1,0,0,NULL,110+i*30);
            v=view();assert(atoi(node_text(&v,(unsigned)i+1,1))==goal[i]);
        }
        send(WF_CLICK,(unsigned)count+1,0,0,NULL,300);
        assert(row(0)[WF_STATUS]==WF_TERMINAL&&bits(row(0)[WF_RAW_REWARD])==1.0f);
        assert(fabsf(bits(row(0)[WF_TIMED_REWARD])-(1.0f-300.0f/row(0)[WF_DEADLINE]))<1e-5f);
    }
    reset(1,99);row(0)[33]=0;row(0)[34]=5;row(0)[36]=0;row(0)[37]=3;
    send(WF_LEFT,1,0,0,NULL,10);assert(row(0)[36]==0);
    send(WF_RIGHT,1,0,0,NULL,20);assert(row(0)[36]==1);
    send(WF_LEFT,1,0,0,NULL,30);assert(row(0)[36]==0);
}
static void spinner(void){
    reset(3,24);WFView v=view();int goal;
    assert(sscanf(wf_text_get(&v,v.instruction),"Select %d with the spinner",&goal)==1);
    WFAction blocked={.kind=WF_INSERT,.target=4,.text="9",.text_length=1,.elapsed_ms=10};
    assert(api->action(row(0),&blocked)<0);
    send(WF_KEY_DOWN,4,38,0,NULL,10);assert(row(0)[36]==0);
    for(int i=0;i<abs(goal);i++)send(WF_CLICK,goal>0?1:2,0,0,NULL,20+i);
    v=view();assert(atoi(node_text(&v,4,1))==goal);
    send(WF_CLICK,3,0,0,NULL,100);
    assert(row(0)[WF_STATUS]==WF_TERMINAL&&bits(row(0)[WF_RAW_REWARD])==1.0f);
}
static void color(void){
    for(unsigned task=4;task<=5;task++){
        reset(task,301+task);WFView v=view();unsigned desired=desired_color(&v,task);
        double expected=color_reward(color_from_hex(node_text(&v,1,1)),desired);
        send(WF_CLICK,2,0,0,NULL,100);
        assert(row(0)[WF_STATUS]==WF_TERMINAL);
        assert(fabs(bits(row(0)[WF_RAW_REWARD])-expected)<2e-5);
        reset(task,301+task);
        char hex[7];snprintf(hex,sizeof hex,"%06X",desired);
        send(WF_SELECT_ALL,1,0,0,NULL,100);
        send(WF_INSERT,1,0,0,hex,120);
        send(WF_CLICK,2,0,0,NULL,150);
        assert(row(0)[WF_STATUS]==WF_TERMINAL);
        assert(fabs(bits(row(0)[WF_RAW_REWARD])-1.0)<1e-6);
    }
    /* Integer law endpoints and interior distances must agree numerically
       with an independent double-precision implementation at the ABI. */
    const unsigned samples[]={0,0xffffff,0x123456,0xabcdef,0x808080};
    for(unsigned i=0;i<5;i++)for(unsigned j=0;j<5;j++){
        reset(5,900+i*5+j);row(0)[46]=samples[j];
        char hex[7];snprintf(hex,sizeof hex,"%06X",samples[i]);
        send(WF_SELECT_ALL,1,0,0,NULL,100);
        send(WF_INSERT,1,0,0,hex,120);
        send(WF_CLICK,2,0,0,NULL,150);
        double expected=color_reward(samples[i],samples[j]);
        assert(fabs(bits(row(0)[WF_RAW_REWARD])-expected)<2e-6);
        assert(fabs(bits(row(0)[WF_TIMED_REWARD])-expected*(1.0-150.0/7000.0))<2e-6);
    }
}
static void deadline_lanes_and_terminal(void){
    reset(1,404);uint32_t *lane=row(1);
    lane[WF_VERSION]=WF_ABI_VERSION;lane[WF_TASK]=3;lane[WF_SEED]=405;lane[WF_OP]=WF_RESET;
    batch();uint32_t saved[ROW];memcpy(saved,lane,sizeof saved);
    send(WF_WAIT,0,0,0,NULL,10);assert(!memcmp(saved,lane,sizeof saved));
    send(WF_CLICK,2,0,0,NULL,10000);
    assert(row(0)[WF_STATUS]==WF_TIMEOUT&&bits(row(0)[WF_RAW_REWARD])==-1.0f);
    uint32_t old=row(0)[36];
    row(0)[WF_ACTION]=WF_RIGHT;row(0)[WF_TARGET]=1;row(0)[WF_OP]=WF_STEP;
    batch();assert(row(0)[36]==old&&row(0)[WF_STATUS]==WF_TIMEOUT);
}
int main(void){
    prctl(PR_SET_DUMPABLE,0);
    api=webnav_family_v2();assert(api&&api->task_count==6&&api->row_words==ROW&&api->batch_lanes==4);
    words=calloc(ROW*4,sizeof *words);assert(words);
    check_projection_and_distribution();choose_list();sliders();spinner();color();deadline_lanes_and_terminal();
    free(words);printf("PASS: %u independent controls source checks\n",checks);return 0;
}
