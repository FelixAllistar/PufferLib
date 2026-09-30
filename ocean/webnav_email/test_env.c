#include <sys/prctl.h>
#include "webnav_email.h"

static void public_projection(void){
    WFView v;wf_view_init(&v,0,30000);
    const char *q="Could you forward Ada's email to Abbey, please?";
    assert(!wf_text_add(&v,q,strlen(q),&v.instruction));
    v.count=2;
    v.nodes[0].ref=10;v.nodes[0].role=WF_BUTTON;
    v.nodes[0].flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
    assert(!wf_text_add(&v,"Ada",3,&v.nodes[0].name));
    assert(!wf_text_add(&v,"Subject",7,&v.nodes[0].value));
    v.nodes[1].ref=11;v.nodes[1].parent=10;v.nodes[1].role=WF_BUTTON;
    v.nodes[1].flags=WF_VISIBLE|WF_CLICKABLE;
    assert(!wf_text_add(&v,"Star",4,&v.nodes[1].name));
    assert(!wf_text_add(&v,"",0,&v.nodes[1].value));
    WFEHistory h={0};WFECatalog cat;
    wfe_record(&h,&v);wfe_catalog(&v,&h,&cat);
    int source=0,recipient=0;
    for(unsigned i=0;i<WFE_CANDIDATES;i++){
        source|=!strcmp(cat.items[i].text,"Ada");
        recipient|=!strcmp(cat.items[i].text,"Abbey");
    }
    assert(source&&recipient&&h.senders==1);
    float obs[OBS_SIZE],again[OBS_SIZE];
    unsigned char mask[WFE_ACTIONS];
    wfe_project(&v,&h,&cat,obs,mask);
    assert(mask[0]&&mask[WFE_CLICK_BASE]&&!mask[WFE_CLICK_BASE+1]);
    assert(!mask[WFE_SELECT]&&!mask[WFE_BACKSPACE]);
    v.nodes[0].x=500;v.nodes[0].width=900;
    wfe_project(&v,&h,&cat,again,mask);
    assert(!memcmp(obs,again,sizeof obs));
    assert(WFE_ACTIONS==170);
    wf_view_init(&v,0,30000);
    const char *r="Reply with \"hello there.\"";
    assert(!wf_text_add(&v,r,strlen(r),&v.instruction));
    wfe_catalog(&v,&h,&cat);
    assert(!strcmp(cat.items[0].text,"hello there."));
    wf_view_init(&v,0,30000);v.count=1;
    v.nodes[0].ref=121;v.nodes[0].role=WF_TEXTAREA;
    v.nodes[0].flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE|WF_FOCUSED;
    v.nodes[0].capacity=160;
    assert(!wf_text_add(&v,"Forward body",12,&v.nodes[0].name));
    char full[159];memset(full,'x',158);full[158]=0;
    assert(!wf_text_add(&v,full,158,&v.nodes[0].value));
    assert(!wf_text_add(&v,"Forward the email",17,&v.instruction));
    wfe_record(&h,&v);wfe_catalog(&v,&h,&cat);
    wfe_project(&v,&h,&cat,obs,mask);
    assert(mask[WFE_SELECT]&&!mask[WFE_COPY_BASE]);
    h.selected_ref=121;
    wfe_project(&v,&h,&cat,obs,mask);
    assert(!mask[WFE_SELECT]&&mask[WFE_COPY_BASE]);
}

int main(void){
    public_projection();
    for(unsigned task=0;task<10;task++){
        Dict config={0};dict_set(&config,"task_mask",1u<<task);
        Env env={0};env.rng=123+task;puf_init(&env,&config);
        float obs[WFE_LANES][OBS_SIZE],actions[WFE_LANES]={0};
        float rewards[WFE_LANES]={0},terminals[WFE_LANES]={0};
        unsigned char masks[WFE_LANES][WFE_ACTIONS];
        for(unsigned i=0;i<WFE_LANES;i++)
            env.agents[i]=(Agent){obs[i],actions+i,rewards+i,terminals+i,masks[i],0};
        puf_reset(&env);unsigned episodes=0;
        for(unsigned step=0;step<250;step++){
            for(unsigned lane=0;lane<WFE_LANES;lane++){
                unsigned legal[WFE_ACTIONS],count=0;
                for(unsigned a=0;a<WFE_ACTIONS;a++)if(masks[lane][a])legal[count++]=a;
                assert(count);
                actions[lane]=(float)legal[wfe_random(&env)%count];
            }
            puf_step(&env);
            for(unsigned lane=0;lane<WFE_LANES;lane++){
                assert(masks[lane][0]);
                for(unsigned k=0;k<OBS_SIZE;k++)assert(isfinite(obs[lane][k]));
                if(terminals[lane]){
                    episodes++;assert(isfinite(rewards[lane]));
                    assert(env.history[lane].clicks==0&&env.history[lane].edits==0);
                    const uint32_t *r=env.family.words+(size_t)lane*env.family.api->row_words;
                    assert(r[WF_TASK]==task&&r[WF_STATUS]==WF_RUNNING);
                }
            }
        }
        assert(episodes);puf_close(&env);free(config.items);
    }
    puts("PASS: public candidate projection, masks, history reset, 10-task routing, finite random episodes");
}
