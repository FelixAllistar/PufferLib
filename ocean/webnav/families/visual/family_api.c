#include "../common/family_api.h"
#include <stdio.h>
#include <string.h>

#define ROW 2048u
void family_visual_batch(uint32_t *words);
static const char *const tasks[]={"click-color","click-pie","click-pie-nodelay",
    "click-shades","click-shape","count-shape","count-sides",
    "identify-shape","visual-addition"};
static const char *const colors[]={"","red","blue","olive","lime","black",
    "white","grey","purple","orange","yellow","cyan","pink","magenta"};
static const char *const shape_colors[]={"red","green","blue","aqua","black",
    "magenta","yellow"};

static int ascii_z(const uint32_t *s,unsigned cap){
    for(unsigned i=0;i<cap;i++){
        if(!s[i])return 1;
        if(s[i]<32u||s[i]>126u)return 0;
    }
    return 0;
}
static int add_words(WFView *v,const uint32_t *src,unsigned cap,WFText *out){
    char buf[256];unsigned i=0;
    while(i<cap&&src[i]){if(i+1u>=sizeof buf)return -1;buf[i]=(char)src[i];i++;}
    if(i==cap)return -1;
    return wf_text_add(v,buf,i,out);
}
static int valid(const uint32_t *r){
    if(!r||r[WF_VERSION]!=2u||r[WF_TASK]>8u||r[WF_OP]>WF_STEP)return -1;
    if(r[WF_OP]==WF_RESET)return 0;
    unsigned task=r[WF_TASK];
    if(r[WF_STATUS]>WF_TIMEOUT||
       r[WF_DEADLINE]!=((task==3u||task==8u)?15000u:10000u)||
       !ascii_z(r+512,256u))return -1;
    if(task==1u||task==2u){
        if(r[32]<4u||r[32]>8u||r[33]<33u||r[33]>122u||
           r[34]>1u||r[35]>11500u)return -1;
        int found=0;
        for(unsigned i=0;i<r[32];i++){
            if(r[64+i]<33u||r[64+i]>122u)return -1;
            for(unsigned j=0;j<i;j++)if(r[64+i]==r[64+j])return -1;
            found+=r[64+i]==r[33];
        }
        if(found!=1)return -1;
    }else if(task==3u){
        if(r[32]>2u||r[33]>4095u||r[34]!=12u)return -1;
        for(unsigned i=0;i<12u;i++)if(r[64+i]>2u||
            r[80+i]<30u||r[80+i]>89u||
            r[96+i]<30u||r[96+i]>89u)return -1;
    }else if(task==4u||task==5u){
        unsigned count=r[32];
        if(count<3u||count>(task==5u?9u:19u)||r[33]>2u||
           r[34]>7u||r[37]!=(task==5u)||r[36]>count)return -1;
        if(r[35]>3u&&r[35]<48u)return -1;
        for(unsigned i=0;i<count;i++){
            const uint32_t *s=r+128u+i*6u;
            if(s[0]>=7u||s[1]>=(task==5u?6u:7u)||s[2]>=7u||
               s[3]>1u||s[4]<1u||s[4]>3u||
               (s[4]==3u?(s[5]<201u||s[5]>203u):
                 (s[5]<48u||s[5]>122u)))return -1;
            for(unsigned j=0;j<i;j++)if(s[0]==r[128u+j*6u]&&
                s[1]==r[129u+j*6u])return -1;
        }
        if(task==5u){
            for(unsigned i=0;i<5u;i++){
                if(r[64u+i]>9u)return -1;
                for(unsigned j=0;j<i;j++)if(r[64u+i]==r[64u+j])return -1;
            }
        }
    }else if(task==6u){
        if(r[32]<3u||r[32]>7u||r[33]<3u||r[33]>7u||r[34]>180u)
            return -1;
        for(unsigned i=0;i<r[33];i++)if(r[64u+i*2u]>150u||
            r[65u+i*2u]>100u)return -1;
    }else if(task==7u){
        if(r[32]>4u||r[33]>3u||r[35]>6u)return -1;
    }else if(task==8u){
        if(r[32]<1u||r[32]>10u||r[33]<1u||r[33]>10u||
           r[34]>64u||r[35]>r[34]||r[36]>r[34]||r[37]>1u)return -1;
        for(unsigned i=0;i<r[34];i++)if(r[64u+i]<32u||r[64u+i]>126u)
            return -1;
    }else{
        if(r[37]>1u)return -1;
        int found=0;
        for(unsigned i=0;i<4u;i++){
            if(r[32+i]<1u||r[32+i]>13u)return -1;
            for(unsigned j=0;j<i;j++)if(r[32+i]==r[32+j])return -1;
            found+=r[32+i]==r[36];
        }
        if(found!=1)return -1;
    }
    if(r[WF_OP]==WF_STEP&&r[WF_ACTION]!=WF_WAIT&&r[WF_ACTION]!=WF_CLICK){
        if(task!=8u||r[WF_ACTION]<WF_INSERT||r[WF_ACTION]>WF_SELECT_ALL)
            return -1;
    }
    return 0;
}
static int named(WFView *v,WFNode *n,const char *name,const char *value){
    return wf_text_add(v,name,strlen(name),&n->name)||
           wf_text_add(v,value,strlen(value),&n->value)?-1:0;
}
static int color_view(const uint32_t *r,WFView *v){
    if(r[37]){
        WFNode *n=&v->nodes[v->count++];
        n->ref=1u;n->role=WF_BUTTON;n->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
        n->x=80.0f;n->y=0.0f;n->width=n->height=10.0f;
        if(named(v,n,"Query swatch",colors[r[36]]))return -1;
    }
    for(unsigned i=0;i<4u;i++){
        WFNode *n=&v->nodes[v->count++];
        n->ref=10u+i;n->role=WF_BUTTON;
        n->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
        n->x=(float)(i%2u*62u+19u);n->y=(float)(i/2u*62u+70u);
        n->width=n->height=52.0f;
        if(named(v,n,"Color box",colors[r[32+i]]))return -1;
    }
    return 0;
}
static int pie_view(const uint32_t *r,WFView *v){
    WFNode *n=&v->nodes[v->count++];
    n->ref=1u;n->role=WF_BUTTON;n->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
    n->x=64.0f;n->y=112.0f;n->width=n->height=24.0f;
    if(named(v,n,"Pie spreader",r[34]?"open":"closed"))return -1;
    unsigned ready=r[34]&&r[WF_ELAPSED]>=r[35];
    if(!ready)return 0;
    for(unsigned i=0;i<r[32];i++){
        n=&v->nodes[v->count++];
        n->ref=10u+i;n->role=WF_BUTTON;
        n->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
        n->x=(float)(10u+(i%4u)*30u);n->y=(float)(70u+(i/4u)*34u);
        n->width=n->height=24.0f;
        char label[2]={(char)r[64+i],0};
        if(named(v,n,label,label))return -1;
    }
    return 0;
}
static int shades_view(const uint32_t *r,WFView *v){
    for(unsigned i=0;i<12u;i++){
        WFNode *n=&v->nodes[v->count++];
        n->ref=10u+i;n->role=WF_BUTTON;
        n->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE|
            ((r[33]>>i)&1u?WF_SELECTED:0u);
        n->x=(float)(8u+(i%6u)*24u);n->y=(float)(65u+(i/6u)*38u);
        n->width=n->height=12.0f;
        char hsl[48];snprintf(hsl,sizeof hsl,"hsl(%u, %u%%, %u%%)",
            r[64+i]*120u,r[80+i],r[96+i]);
        if(named(v,n,"Shade",hsl))return -1;
    }
    WFNode *button=&v->nodes[v->count++];
    button->ref=2u;button->role=WF_BUTTON;
    button->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
    button->x=95.0f;button->y=130.0f;
    button->width=58.0f;button->height=22.0f;
    return named(v,button,"Submit","");
}
static const char *svg_tag(uint32_t kind,uint32_t glyph){
    if(kind!=3u)return "text";
    return glyph==201u?"circle":glyph==202u?"rect":"polygon";
}
static int shape_view(const uint32_t *r,WFView *v){
    WFNode *blank=&v->nodes[v->count++];
    blank->ref=1u;blank->role=WF_CANVAS;
    blank->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
    blank->x=0.0f;blank->y=60.0f;
    blank->width=(r[WF_TASK]==5u?154.0f:160.0f);
    blank->height=(r[WF_TASK]==5u?130.0f:160.0f);
    if(named(v,blank,"SVG background",""))return -1;
    for(unsigned i=0;i<r[32];i++){
        const uint32_t *s=r+128u+i*6u;
        WFNode *n=&v->nodes[v->count++];
        n->ref=10u+i;n->role=WF_OTHER;
        n->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
        unsigned size=s[3]?20u:10u;
        n->x=(float)(s[0]*20u+10u-size/2u);
        n->y=(float)(s[1]*20u+70u-size/2u);
        n->width=n->height=(float)size;
        char value[128];
        if(s[4]==3u)
            snprintf(value,sizeof value,"fill=%s; size=%upx",
                shape_colors[s[2]],s[3]?20u:10u);
        else
            snprintf(value,sizeof value,"text=%c; fill=%s; font-size=%upx",
                (char)s[5],shape_colors[s[2]],s[3]?20u:10u);
        if(named(v,n,svg_tag(s[4],s[5]),value))return -1;
    }
    if(r[WF_TASK]==5u)for(unsigned i=0;i<5u;i++){
        WFNode *n=&v->nodes[v->count++];
        n->ref=40u+i;n->role=WF_BUTTON;
        n->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
        n->x=(float)(8u+i*29u);n->y=190.0f;
        n->width=25.0f;n->height=22.0f;
        char label[16];snprintf(label,sizeof label,"%u",r[64u+i]);
        if(named(v,n,label,label))return -1;
    }
    return 0;
}
static int sides_view(const uint32_t *r,WFView *v){
    WFNode *n=&v->nodes[v->count++];
    n->ref=1u;n->role=WF_CANVAS;n->flags=WF_VISIBLE;
    n->x=0.0f;n->y=60.0f;n->width=150.0f;n->height=100.0f;
    char path[256];size_t used=0;
    used+=(size_t)snprintf(path+used,sizeof path-used,"M");
    for(unsigned i=0;i<r[33];i++)
        used+=(size_t)snprintf(path+used,sizeof path-used,
            "%s%u,%u",i?" L":"",r[64u+i*2u],r[65u+i*2u]);
    snprintf(path+used,sizeof path-used," Z; stroke=#000000; width=3");
    if(named(v,n,"Canvas polygon outline",path))return -1;
    for(unsigned i=0;i<5u;i++){
        n=&v->nodes[v->count++];n->ref=10u+i;n->role=WF_BUTTON;
        n->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
        n->x=(float)(10u+i*24u);n->y=177.0f;
        n->width=20.0f;n->height=25.0f;
        char label[2]={(char)('3'+i),0};
        if(named(v,n,label,label))return -1;
    }
    return 0;
}
static int identify_view(const uint32_t *r,WFView *v){
    static const char *const labels[]={"Rectangle","Circle","Triangle",
        "Letter","Number"};
    WFNode *n=&v->nodes[v->count++];
    n->ref=1u;n->role=WF_OTHER;n->flags=WF_VISIBLE;
    n->x=17.0f;n->y=77.0f;n->width=n->height=30.0f;
    char value[128];
    if(r[33])snprintf(value,sizeof value,"fill=%s; size=30px",
        shape_colors[r[35]]);
    else snprintf(value,sizeof value,"text=%c; fill=%s; font-size=30px",
        (char)r[34],shape_colors[r[35]]);
    if(named(v,n,svg_tag(r[33]?3u:1u,
        r[33]==1u?201u:r[33]==2u?202u:203u),value))return -1;
    for(unsigned i=0;i<5u;i++){
        n=&v->nodes[v->count++];n->ref=10u+i;n->role=WF_BUTTON;
        n->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
        n->x=(float)(3u+i*30u);n->y=120.0f;
        n->width=28.0f;n->height=25.0f;
        if(named(v,n,labels[i],""))return -1;
    }
    return 0;
}
static int addition_view(const uint32_t *r,WFView *v){
    for(unsigned group=0;group<2u;group++){
        unsigned count=r[32u+group];
        for(unsigned i=0;i<count;i++){
            WFNode *n=&v->nodes[v->count++];
            n->ref=10u+group*16u+i;n->role=WF_OTHER;n->flags=WF_VISIBLE;
            n->x=(float)(10u+group*76u+(i%5u)*12u);
            n->y=(float)(85u+(i/5u)*12u);
            n->width=n->height=10.0f;
            if(named(v,n,group?"Right blue block":"Left blue block",
                "background-color: #7fb0ff"))return -1;
        }
    }
    WFNode *plus=&v->nodes[v->count++];
    plus->ref=3u;plus->role=WF_TEXT;plus->flags=WF_VISIBLE;
    plus->x=72.0f;plus->y=85.0f;plus->width=12.0f;plus->height=30.0f;
    if(named(v,plus,"+",""))return -1;
    WFNode *input=&v->nodes[v->count++];
    input->ref=1u;input->role=WF_INPUT;
    input->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE|
        (r[37]?WF_FOCUSED:0u);
    input->x=10.0f;input->y=160.0f;input->width=35.0f;input->height=19.0f;
    input->capacity=64u;input->selection_start=r[35];
    input->selection_end=r[36];
    char value[65];for(unsigned i=0;i<r[34];i++)value[i]=(char)r[64u+i];
    value[r[34]]=0;
    if(named(v,input,"math-answer",value))return -1;
    WFNode *submit=&v->nodes[v->count++];
    submit->ref=2u;submit->role=WF_BUTTON;
    submit->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
    submit->x=52.0f;submit->y=160.0f;
    submit->width=60.0f;submit->height=22.0f;
    return named(v,submit,"Submit","");
}
static int observe(const uint32_t *r,WFView *v){
    if(!v||valid(r)||r[WF_OP]==WF_RESET)return -1;
    wf_view_init(v,r[WF_ELAPSED],r[WF_DEADLINE]);
    if(add_words(v,r+512,256u,&v->instruction))return -1;
    if(r[WF_TASK]==0u)return color_view(r,v);
    if(r[WF_TASK]==3u)return shades_view(r,v);
    if(r[WF_TASK]==1u||r[WF_TASK]==2u)return pie_view(r,v);
    if(r[WF_TASK]==4u||r[WF_TASK]==5u)return shape_view(r,v);
    if(r[WF_TASK]==6u)return sides_view(r,v);
    if(r[WF_TASK]==7u)return identify_view(r,v);
    return addition_view(r,v);
}
static int action(uint32_t *r,const WFAction *a){
    if(!a||valid(r)||r[WF_OP]==WF_RESET||
       a->elapsed_ms<r[WF_ELAPSED]||a->arg0||a->arg1)return -1;
    unsigned task=r[WF_TASK];
    if(a->kind==WF_WAIT){if(a->target||a->text_length)return -1;}
    else if(a->kind==WF_CLICK){
        if(a->text_length)return -1;
        if(task==0u){
            if((a->target<10u||a->target>13u)&&!(r[37]&&a->target==1u))return -1;
        }else if(task==1u||task==2u){
            if(a->target!=1u&&(a->target<10u||a->target>=10u+r[32]))return -1;
        }else if(task==3u){
            if(a->target!=2u&&(a->target<10u||a->target>21u))return -1;
        }else if(task==4u){
            if(a->target!=1u&&(a->target<10u||a->target>=10u+r[32]))return -1;
        }else if(task==5u){
            if(a->target!=1u&&(a->target<40u||a->target>44u))return -1;
        }else if(task==6u||task==7u){
            if(a->target<10u||a->target>14u)return -1;
        }else if(a->target!=1u&&a->target!=2u)return -1;
    }else if(task==8u&&a->kind>=WF_INSERT&&a->kind<=WF_SELECT_ALL){
        if(a->target!=1u)return -1;
        if(a->kind==WF_INSERT){
            if(!a->text||!a->text_length||a->text_length>64u)return -1;
            if(r[37]&&r[34]-(r[36]-r[35])+a->text_length>64u)
                return -1;
            for(size_t i=0;i<a->text_length;i++)
                if((unsigned char)a->text[i]<32u||
                   (unsigned char)a->text[i]>126u)return -1;
        }else if(a->text_length)return -1;
    }else return -1;
    r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;r[WF_TARGET]=a->target;
    r[WF_ELAPSED]=a->elapsed_ms;
    if(task==8u){
        r[WF_ARG0]=(uint32_t)a->text_length;
        memset(r+256u,0,64u*sizeof *r);
        for(size_t i=0;i<a->text_length;i++)r[256u+i]=(unsigned char)a->text[i];
    }
    return 0;
}
static const WFFamily api={WF_ABI_VERSION,ROW,4,9,"visual",tasks,
    valid,family_visual_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
