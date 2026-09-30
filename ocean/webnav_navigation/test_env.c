#include "webnav_navigation.h"

int main(void) {
    assert(wfn_link_match("Find the link \"the\".","the"));
    assert(!wfn_link_match("Find the link \"the\".","link"));
    assert(!wfn_link_match("Find the link \"Word-10\".","Word-1"));
    assert(!wfn_link_match("Find the link \"The\".","the"));
    WFView v;wf_view_init(&v,0,10000);v.count=2;
    wf_text_add(&v,"Find target",11,&v.instruction);
    for(unsigned i=0;i<2;i++) {
        v.nodes[i].ref=i?155:1;v.nodes[i].role=WF_BUTTON;
        v.nodes[i].flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
        wf_text_add(&v,i?"target":"Menu",i?6:4,&v.nodes[i].name);
        wf_text_add(&v,"",0,&v.nodes[i].value);
    }
    float out[OBS_SIZE],before[OBS_SIZE];unsigned char mask[WFN_ACTIONS],visited[256]={0};
    visited[155]=1;wfn_project(&v,visited,out,mask);
    assert(mask[0]&&mask[1]&&mask[2]&&!mask[3]);
    assert(out[8+WFN_NODE_FEATURES+20]==1&&out[8+WFN_NODE_FEATURES+22]==1);
    assert(wfn_action_kind(1,&v,1)==WF_CLICK&&wfn_action_kind(1,&v,2)==WF_POINTER_MOVE);
    assert(wfn_action_kind(0,&v,2)==WF_CLICK);
    memcpy(before,out,sizeof out);v.nodes[1].x=999;v.nodes[1].width=999;
    wfn_project(&v,visited,out,mask);assert(!memcmp(before,out,sizeof out));
    v.nodes[1].flags&=~WF_ENABLED;wfn_project(&v,visited,out,mask);assert(!mask[2]);
    for(unsigned family=0;family<2;family++)for(unsigned task=0;task<(family?2:9);task++) {
        Dict config={0};dict_set(&config,"family",family);dict_set(&config,"task_mask",1u<<task);
        Env env={0};env.rng=123+task;puf_init(&env,&config);
        float obs[WFN_LANES][OBS_SIZE],actions[WFN_LANES]={0},rewards[WFN_LANES]={0},terminals[WFN_LANES]={0};
        unsigned char masks[WFN_LANES][WFN_ACTIONS];
        for(unsigned i=0;i<WFN_LANES;i++)env.agents[i]=(Agent){obs[i],actions+i,rewards+i,terminals+i,masks[i],0};
        puf_reset(&env);unsigned episodes=0;
        for(unsigned step=0;step<240;step++) {
            for(unsigned lane=0;lane<WFN_LANES;lane++) {
                unsigned legal[WFN_ACTIONS],count=0;
                for(unsigned a=0;a<WFN_ACTIONS;a++)if(masks[lane][a])legal[count++]=a;
                actions[lane]=(float)legal[wfn_random(&env)%count];
            }
            puf_step(&env);
            for(unsigned lane=0;lane<WFN_LANES;lane++) {
                assert(masks[lane][0]);
                for(unsigned k=0;k<OBS_SIZE;k++)assert(isfinite(obs[lane][k]));
                if(terminals[lane]) {
                    episodes++;assert(isfinite(rewards[lane]));
                    for(unsigned k=0;k<256;k++)assert(!env.visited[lane][k]);
                    const uint32_t *r=env.family.words+(size_t)lane*env.family.api->row_words;
                    assert(r[WF_TASK]==task&&r[WF_STATUS]==WF_RUNNING);
                }
            }
        }
        assert(episodes);puf_close(&env);free(config.items);
    }
    puts("PASS: panels/menus routing, masks, stable-ref history, public features, deadlines and autoresets across 11 tasks");
}
