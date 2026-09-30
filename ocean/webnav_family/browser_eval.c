#define _GNU_SOURCE
#include "../../src/puffercpu.c"
#include "webnav_family.h"
#include "../webnav/cdp.h"
#include <limits.h>
#include <sys/prctl.h>

static char *read_file(const char *path) {
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}
    long n=ftell(f);if(n<0||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
    char *s=(char*)calloc((size_t)n+1,1);
    if(!s||fread(s,1,(size_t)n,f)!=(size_t)n){free(s);fclose(f);return NULL;}
    fclose(f);return s;
}
static const cJSON *field(const cJSON *j,const char *key) {
    return cJSON_GetObjectItemCaseSensitive(j,key);
}
static int browser_view(const cJSON *j,WFView *v) {
    const cJSON *query=field(j,"query"),*nodes=field(j,"nodes"),*deadline=field(j,"deadline");
    if(!cJSON_IsString(query)||!cJSON_IsArray(nodes)||!cJSON_IsNumber(deadline)||
       cJSON_GetArraySize(nodes)>WFC_NODES)return -1;
    wf_view_init(v,0,(uint32_t)deadline->valuedouble);
    if(wf_text_add(v,query->valuestring,strlen(query->valuestring),&v->instruction))return -1;
    for(unsigned i=0;i<(unsigned)cJSON_GetArraySize(nodes);i++) {
        const cJSON *item=cJSON_GetArrayItem(nodes,i),*role=field(item,"role"),
                    *name=field(item,"name"),*checked=field(item,"checked"),
                    *x=field(item,"x"),*y=field(item,"y"),
                    *width=field(item,"width"),*height=field(item,"height");
        if(!cJSON_IsNumber(role)||!cJSON_IsString(name)||!cJSON_IsBool(checked)||
           !cJSON_IsNumber(x)||!cJSON_IsNumber(y)||!cJSON_IsNumber(width)||
           !cJSON_IsNumber(height))return -1;
        WFNode *n=&v->nodes[v->count++];
        n->ref=i+1;n->role=(uint32_t)role->valuedouble;
        n->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE|(cJSON_IsTrue(checked)?WF_CHECKED:0);
        n->x=(float)x->valuedouble;n->y=(float)y->valuedouble;
        n->width=(float)width->valuedouble;n->height=(float)height->valuedouble;
        if(wf_text_add(v,name->valuestring,strlen(name->valuestring),&n->name)||
           wf_text_add(v,"",0,&n->value))return -1;
    }
    return 0;
}
static int update_view(const cJSON *snapshot,WFView *v,int *done,float *raw) {
    const cJSON *finished=field(snapshot,"done"),*reward=field(snapshot,"raw"),
                *checked=field(snapshot,"checked");
    if(!cJSON_IsBool(finished)||!cJSON_IsNumber(reward)||!cJSON_IsArray(checked)||
       cJSON_GetArraySize(checked)!=(int)v->count)return -1;
    *done=cJSON_IsTrue(finished);*raw=(float)reward->valuedouble;
    for(unsigned i=0;i<v->count;i++) {
        const cJSON *c=cJSON_GetArrayItem(checked,i);
        if(!cJSON_IsBool(c))return -1;
        v->nodes[i].flags=(v->nodes[i].flags&~WF_CHECKED)|(cJSON_IsTrue(c)?WF_CHECKED:0);
    }
    return 0;
}
static int choose(PufferNet *net,const WFView *v,const float *semantic) {
    float features[OBS_SIZE];unsigned char mask[WFC_ACTIONS];
    wfc_project(v,semantic,features,mask);
    float terminals=0;
    mingru_zero_term(net->mingru,&terminals);
    linear(net->encoder,features);
    mingru(net->mingru,net->encoder->output);
    linear(net->decoder,net->mingru->output);
    if(getenv("WEBNAV_SAMPLE")) {
        float sampled=0;
        multidiscrete(net->multidiscrete,net->decoder->output,&sampled,0,mask);
        return (int)sampled;
    }
    const float *logits=net->decoder->output;
    int best=0;float score=-INFINITY;
    for(unsigned i=0;i<WFC_ACTIONS;i++)if(mask[i]&&logits[i]>score){best=i;score=logits[i];}
    return best;
}
int main(int argc,char **argv) {
    if(argc<3||argc>4){fprintf(stderr,"usage: %s CHECKPOINT EPISODES_PER_TASK [TASK_NAME]\n",argv[0]);return 2;}
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    int semantic_enabled=getenv("WEBNAV_SEMANTIC")!=NULL;
    if(semantic_enabled)wfc_text_init();
    unsigned episodes=(unsigned)strtoul(argv[2],NULL,10);
    unsigned trace_episode=getenv("WEBNAV_TRACE_EP")?(unsigned)strtoul(getenv("WEBNAV_TRACE_EP"),NULL,10):0;
    if(!episodes||episodes>1000)return 2;
    Weights *weights=load_weights(argv[1]);
    srand(12345);
    if(!weights||weights->size-7!=108160){fprintf(stderr,"expected H64/L1 WebNav family checkpoint\n");return 2;}
    int sizes[]={WFC_ACTIONS};PufferNet *net=make_puffernet(weights,1,OBS_SIZE,64,1,sizes,1);
    char *script=read_file("ocean/webnav/families/click/browser.js");
    if(!script)return 2;
    const char *names[]={"click-test","click-test-2","click-test-transfer","click-dialog",
        "click-dialog-2","click-widget","focus-text-2","click-checkboxes-transfer",
        "click-checkboxes-large","click-checkboxes-soft"};
    const char *chrome=getenv("WEBNAV_CHROME");
    if(!chrome)chrome="build/webnav/chrome-headless-shell-linux64/chrome-headless-shell";
    unsigned total=0;
    for(unsigned task=0;task<10;task++) {
        if(argc==4&&strcmp(argv[3],names[task]))continue;
        char file[PATH_MAX],resolved[PATH_MAX],url[PATH_MAX+8];
        snprintf(file,sizeof file,"build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/%s.html",names[task]);
        if(!realpath(file,resolved))return 2;
        snprintf(url,sizeof url,"file://%s",resolved);
        WebCdp c={.input=-1,.output=-1,.pid=-1};
        if(web_cdp_start_ready(&c,chrome,url,"Boolean(window.core&&core.cover_div)"))return 2;
        cJSON *j=web_cdp_eval(&c,script);if(!j)return 2;cJSON_Delete(j);
        unsigned wins=0,unhittable=0,actions=0,class_games[17]={0},class_wins[17]={0},diagnostics=0;
        for(unsigned ep=0;ep<episodes;ep++) {
            char js[256];
            /* Drop the oracle's private goal before CDP serializes the view. */
            snprintf(js,sizeof js,"(()=>{let a=__cl.reset(%u,%u,1);a.nodes.forEach(n=>delete n.goal);return a})()",200000+ep,task);
            j=web_cdp_eval(&c,js);WFView v;
            if(!j||browser_view(j,&v)){fprintf(stderr,"public view failed: %s seed=%u\n",names[task],200000+ep);return 1;}
            cJSON_Delete(j);
            unsigned intent=task==5?wfc_requested_role(wf_text_get(&v,v.instruction)):
                task==6?(unsigned)(wfc_requested_ordinal(wf_text_get(&v,v.instruction))+1):0;
            if(intent<17)class_games[intent]++;
            float semantic[WFC_NODES]={0};
            if(semantic_enabled)wfc_semantic_scores(&v,semantic);
            if(getenv("WEBNAV_TRACE")&&ep==trace_episode) {
                fprintf(stderr,"task=%s query=%s nodes=%u\n",names[task],wf_text_get(&v,v.instruction),v.count);
                float features[OBS_SIZE];unsigned char mask[WFC_ACTIONS];
                wfc_project(&v,semantic_enabled?semantic:NULL,features,mask);
                for(unsigned i=0;i<v.count;i++)fprintf(stderr,"  %u role=%u name=%s role_match=%.0f ordinal_match=%.0f\n",
                    i+1,v.nodes[i].role,wf_text_get(&v,v.nodes[i].name),
                    features[8+128+i*WFC_NODE_FEATURES+81],features[8+128+i*WFC_NODE_FEATURES+82]);
            }
            for(unsigned layer=0;layer<(unsigned)net->mingru->num_layers;layer++)
                memset(net->mingru->state+layer*64,0,64*sizeof(float));
            int done=0,first_action=-1;unsigned episode_actions=0;float raw=0;
            for(unsigned step=1;step<=100&&!done;step++) {
                int action=choose(net,&v,semantic_enabled?semantic:NULL);
                if(step==1)first_action=action;
                if(getenv("WEBNAV_TRACE")&&ep==trace_episode&&step<=5)fprintf(stderr,"  step=%u action=%d\n",step,action);
                unsigned now=step*250;
                snprintf(js,sizeof js,"__cl.tick(%u)",now);
                j=web_cdp_eval(&c,js);if(!j)return 1;cJSON_Delete(j);
                if(action&&now<v.deadline_ms) {
                    snprintf(js,sizeof js,"__cl.point(%d)",action);
                    j=web_cdp_eval(&c,js);if(!j)return 1;
                    if(cJSON_IsNull(j))unhittable++;
                    else {
                        const cJSON *x=field(j,"x"),*y=field(j,"y");
                        if(!cJSON_IsNumber(x)||!cJSON_IsNumber(y)||web_cdp_click(&c,x->valuedouble,y->valuedouble))return 1;
                    }
                    cJSON_Delete(j);
                }
                j=web_cdp_eval(&c,"__cl.snapshot()");
                if(!j||update_view(j,&v,&done,&raw))return 1;
                cJSON_Delete(j);v.elapsed_ms=now;actions++;episode_actions++;
            }
            if(!done){fprintf(stderr,"episode never ended: %s seed=%u\n",names[task],200000+ep);return 1;}
            wins+=raw>=0.999f;
            if(intent<17)class_wins[intent]+=raw>=0.999f;
            if(getenv("WEBNAV_CLASS_TRACE")&&raw<0.999f&&diagnostics++<12)
                fprintf(stderr,"task=%s seed=%u intent=%u first_action=%d actions=%u query=%s\n",
                    names[task],200000+ep,intent,first_action,episode_actions,wf_text_get(&v,v.instruction));
        }
        if(getenv("WEBNAV_CLASS_TRACE")&&(task==5||task==6))
            for(unsigned c=0;c<17;c++)if(class_games[c])
                fprintf(stderr,"task=%s intent=%u games=%u wins=%u\n",names[task],c,class_games[c],class_wins[c]);
        printf("{\"task\":\"%s\",\"episodes\":%u,\"successes\":%u,\"full_credit_rate\":%.6f,\"actions\":%u,\"unhittable\":%u,\"semantic\":%s,\"policy\":\"%s CPU checkpoint; original Chromium pages\"}\n",
            names[task],episodes,wins,(double)wins/episodes,actions,unhittable,
            semantic_enabled?"true":"false",getenv("WEBNAV_SAMPLE")?"sampled":"greedy");fflush(stdout);
        total+=episodes;web_cdp_close(&c);
    }
    free(script);free_puffernet(net);free(weights);
    return total?0:2;
}
