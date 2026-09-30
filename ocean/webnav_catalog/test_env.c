#include <sys/prctl.h>
#include "webnav_catalog.h"

static WFNode *add(WFView *v,unsigned ref,unsigned parent,unsigned role,unsigned flags,
                   const char *name,const char *value){
    WFNode *n=v->nodes+v->count++;*n=(WFNode){0};
    n->ref=ref;n->parent=parent;n->role=role;n->flags=WF_VISIBLE|flags;
    assert(!wf_text_add(v,name,strlen(name),&n->name));
    assert(!wf_text_add(v,value,strlen(value),&n->value));
    return n;
}
static void start(WFView *v,const char *q){
    wf_view_init(v,0,20000);
    assert(!wf_text_add(v,q,strlen(q),&v->instruction));
}
int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    WFView v;float out[OBS_SIZE],before[OBS_SIZE];
    unsigned char mask[WFC_ACTIONS],visits[512]={0};
    start(&v,"Find Aarika in the contact book and click on their email.");
    add(&v,1,0,WF_BUTTON,WF_SELECTED,"Page 1","");
    add(&v,2,0,WF_BUTTON,WF_ENABLED|WF_CLICKABLE,"Page 2","");
    add(&v,99,0,WF_TEXT,0,"Aarika","");
    add(&v,100,0,WF_LINK,WF_ENABLED|WF_CLICKABLE,"Phone","111-222-3333");
    add(&v,101,0,WF_LINK,WF_ENABLED|WF_CLICKABLE,"Email","a@example.org");
    wfc_project(&v,visits,out,mask);
    assert(!mask[1]&&mask[2]&&mask[4]&&mask[5]);
    assert(out[2]==0.2f&&out[10]==1);
    assert(out[16+192+2*WFC_NODE_FEATURES+75]==1);
    assert(wfc_action(&v,2).target==2);
    memcpy(before,out,sizeof out);v.nodes[2].x=1000;v.nodes[2].width=999;
    wfc_project(&v,visits,out,mask);assert(!memcmp(before,out,sizeof out));

    start(&v,"Order one of each item: Caesar Salad, Garlic bread");
    add(&v,18,0,WF_TEXT,0,"Caesar Salad","1");
    add(&v,17,0,WF_BUTTON,WF_ENABLED|WF_CLICKABLE,"Add","");
    add(&v,16,0,WF_BUTTON,WF_ENABLED|WF_CLICKABLE,"Remove","");
    add(&v,34,0,WF_TEXT,0,"Garlic bread","0");
    add(&v,33,0,WF_BUTTON,WF_ENABLED|WF_CLICKABLE,"Add","");
    add(&v,1,0,WF_BUTTON,WF_ENABLED|WF_CLICKABLE,"Order!","");
    wfc_project(&v,visits,out,mask);
    assert(out[16+192+0*WFC_NODE_FEATURES+76]==1);
    assert(out[16+192+3*WFC_NODE_FEATURES+76]==1);
    assert(out[16+192+3*WFC_NODE_FEATURES+79]==0);
    assert(mask[5]&&mask[6]);
    start(&v,"Order 3 items that are vegan");
    add(&v,18,0,WF_TEXT,0,"Caesar Salad","1");
    add(&v,200,18,WF_TEXT,0,"vegan","");
    wfc_project(&v,visits,out,mask);
    assert(out[8]==1&&out[16+192+WFC_NODE_FEATURES+77]==1);
    assert(out[16+192+WFC_NODE_FEATURES+93]==0.125f);

    start(&v,"Use the textbox to enter \"Kenda\" and press \"Search\", then find and click the 7th search result.");
    WFNode *input=add(&v,1,0,WF_INPUT,WF_ENABLED|WF_CLICKABLE|WF_FOCUSED,"Search text","wrong");
    input->selection_start=input->selection_end=5;
    add(&v,2,0,WF_BUTTON,WF_ENABLED|WF_CLICKABLE,"Search","");
    add(&v,10,0,WF_BUTTON,WF_SELECTED,"Page 1","");
    add(&v,11,0,WF_BUTTON,WF_ENABLED|WF_CLICKABLE,"Page 2","");
    add(&v,12,0,WF_BUTTON,WF_ENABLED|WF_CLICKABLE,"Page 3","");
    add(&v,100,0,WF_LINK,WF_ENABLED|WF_CLICKABLE,"Kenda","https://example.org");
    wfc_project(&v,visits,out,mask);
    assert(out[3]==7.0f/9&&mask[1+WFC_NODES]&&
           mask[2+WFC_NODES]);
    input->selection_start=0;input->selection_end=5;
    wfc_project(&v,visits,out,mask);
    assert(!mask[1+WFC_NODES]&&mask[2+WFC_NODES]);
    WFAction a=wfc_action(&v,2+WFC_NODES);
    assert(a.kind==WF_INSERT&&a.text_length==5&&!strncmp(a.text,"Kenda",5));
    /* Editing the input alone does not prove loaded results used that query. */
    assert(out[16+192+5*WFC_NODE_FEATURES+82]==0);
    v.nodes[5].ref=106;
    wfc_project(&v,visits,out,mask);
    assert(out[16+192+5*WFC_NODE_FEATURES+82]==1);
    v.nodes[5].ref=200;
    wfc_project(&v,visits,out,mask);
    assert(out[16+192+5*WFC_NODE_FEATURES+83]==1);
    assert(out[16+192+5*WFC_NODE_FEATURES+82]==0);

    for(unsigned task=0;task<3;task++){
        Dict config={0};dict_set(&config,"task_mask",1u<<task);
        Env env={0};env.rng=123u+task;puf_init(&env,&config);
        float obs[WFC_LANES][OBS_SIZE],actions[WFC_LANES]={0};
        float rewards[WFC_LANES]={0},terminals[WFC_LANES]={0};
        unsigned char masks[WFC_LANES][WFC_ACTIONS];
        for(unsigned lane=0;lane<WFC_LANES;lane++)
            env.agents[lane]=(Agent){obs[lane],actions+lane,rewards+lane,terminals+lane,masks[lane],0};
        puf_reset(&env);unsigned episodes=0;
        for(unsigned step=0;step<220;step++){
            for(unsigned lane=0;lane<WFC_LANES;lane++){
                unsigned legal[WFC_ACTIONS],count=0;
                for(unsigned i=0;i<WFC_ACTIONS;i++)if(masks[lane][i])legal[count++]=i;
                actions[lane]=(float)legal[wfc_random(&env)%count];
            }
            puf_step(&env);
            for(unsigned lane=0;lane<WFC_LANES;lane++){
                assert(masks[lane][0]);
                for(unsigned i=0;i<OBS_SIZE;i++)assert(isfinite(obs[lane][i]));
                if(terminals[lane]){
                    episodes++;assert(isfinite(rewards[lane]));
                    for(unsigned i=0;i<512;i++)assert(env.visits[lane][i]==0);
                    const uint32_t *r=env.family.words+(size_t)lane*env.family.api->row_words;
                    assert(r[WF_TASK]==task&&r[WF_STATUS]==WF_RUNNING);
                }
            }
        }
        assert(episodes);puf_close(&env);free(config.items);
    }
    puts("PASS: catalog public features, masks, text, partial-score transport and resets");
}
