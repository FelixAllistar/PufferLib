#include "webnav_family.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    WFView feature_view;wf_view_init(&feature_view,0,10000);
    feature_view.count=3;
    for(unsigned i=0;i<3;i++) {
        feature_view.nodes[i].role=i==1?WF_RADIO:WF_INPUT;
        feature_view.nodes[i].flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
        assert(!wf_text_add(&feature_view,"",0,&feature_view.nodes[i].name));
        assert(!wf_text_add(&feature_view,"",0,&feature_view.nodes[i].value));
    }
    float projected[OBS_SIZE];unsigned char legal[WFC_ACTIONS];
    const char *role_query="Click on a \"radio\" widget.";
    assert(!wf_text_add(&feature_view,role_query,strlen(role_query),&feature_view.instruction));
    wfc_project(&feature_view,NULL,projected,legal);
    assert(projected[5]==1.0f&&projected[4]==0.0f);
    assert(projected[8+128+WFC_NODE_FEATURES+81]==1.0f);
    assert(projected[8+128+81]==0.0f);
    const char *ordinal_query="Focus into the 3rd input textbox.";
    assert(!wf_text_add(&feature_view,ordinal_query,strlen(ordinal_query),&feature_view.instruction));
    wfc_project(&feature_view,NULL,projected,legal);
    assert(projected[4]==1.0f&&projected[5]==0.0f);
    assert(projected[8+128+2*WFC_NODE_FEATURES+82]==1.0f);
    assert(projected[8+128+WFC_NODE_FEATURES+82]==0.0f);
    Dict config={0};
    dict_set(&config,"task_mask",1);
    dict_set(&config,"data_mode",0);
    dict_set(&config,"semantic",0);
    Env env={0};
    env.rng=17;
    puf_init(&env,&config);
    float observations[WFC_LANES][OBS_SIZE]={0};
    unsigned char masks[WFC_LANES][WFC_ACTIONS]={0};
    float actions[WFC_LANES]={0},rewards[WFC_LANES]={0},terminals[WFC_LANES]={0};
    for(unsigned i=0;i<WFC_LANES;i++) {
        env.agents[i].observations=observations[i];
        env.agents[i].action_mask=masks[i];
        env.agents[i].actions=&actions[i];
        env.agents[i].rewards=&rewards[i];
        env.agents[i].terminals=&terminals[i];
    }
    puf_reset(&env);
    for(unsigned i=0;i<WFC_LANES;i++) {
        uint32_t *r=env.family.words+(size_t)i*env.family.api->row_words;
        assert(r[WF_TASK]==0&&masks[i][0]);
        assert(observations[i][0]>0);
    }
    unsigned episodes=0;
    for(unsigned step=0;step<240;step++) {
        for(unsigned i=0;i<WFC_LANES;i++) {
            unsigned a=1+(step+i)%WFC_NODES;
            actions[i]=masks[i][a]?(float)a:0;
        }
        puf_step(&env);
        for(unsigned i=0;i<WFC_LANES;i++) {
            assert(masks[i][0]);
            if(terminals[i]) {
                episodes++;
                assert(isfinite(rewards[i]));
                uint32_t *r=env.family.words+(size_t)i*env.family.api->row_words;
                assert(r[WF_TASK]==0&&r[WF_STATUS]==WF_RUNNING);
            }
        }
    }
    assert(episodes>=8&&env.log.n==episodes);
    puf_close(&env);
    dict_set(&config,"task_mask",1<<2);
    dict_set(&config,"data_mode",1);
    Env transfer={0};transfer.rng=17;
    puf_init(&transfer,&config);
    for(unsigned i=0;i<WFC_LANES;i++) {
        transfer.agents[i].observations=observations[i];
        transfer.agents[i].action_mask=masks[i];
        transfer.agents[i].actions=&actions[i];
        transfer.agents[i].rewards=&rewards[i];
        transfer.agents[i].terminals=&terminals[i];
    }
    puf_reset(&transfer);
    for(unsigned i=0;i<WFC_LANES;i++) {
        uint32_t *r=transfer.family.words+(size_t)i*transfer.family.api->row_words;
        WFView v;assert(!wf_observe(&transfer.family,i,&v));
        assert(r[WF_TASK]==2&&r[6]==1);
        assert(strstr(wf_text_get(&v,v.instruction),"TWO"));
        assert(v.count==2);
        assert(observations[i][8+128+70]==0.0f);
        assert(observations[i][8+128+WFC_NODE_FEATURES+70]==1.0f);
        actions[i]=2;
    }
    puf_step(&transfer);
    for(unsigned i=0;i<WFC_LANES;i++)assert(terminals[i]&&rewards[i]>=0.999f);
    puf_close(&transfer);
    dict_set(&config,"task_mask",1023);
    dict_set(&config,"data_mode",2);
    Env mixed={0};mixed.rng=17;
    puf_init(&mixed,&config);
    for(unsigned i=0;i<WFC_LANES;i++) {
        mixed.agents[i].observations=observations[i];
        mixed.agents[i].action_mask=masks[i];
    }
    unsigned mode_count[10][2]={0};
    for(unsigned round=0;round<100;round++) {
        puf_reset(&mixed);
        for(unsigned i=0;i<WFC_LANES;i++) {
            uint32_t *r=mixed.family.words+(size_t)i*mixed.family.api->row_words;
            assert(r[WF_TASK]<10&&r[6]<=1);
            mode_count[r[WF_TASK]][r[6]]++;
        }
    }
    for(unsigned task=0;task<10;task++)assert(mode_count[task][0]&&mode_count[task][1]);
    puf_close(&mixed);
    dict_set(&config,"task_mask",1<<9);
    dict_set(&config,"data_mode",1);
    dict_set(&config,"semantic",1);
    Env semantic={0};semantic.rng=23;
    puf_init(&semantic,&config);
    for(unsigned i=0;i<WFC_LANES;i++) {
        semantic.agents[i].observations=observations[i];
        semantic.agents[i].action_mask=masks[i];
    }
    puf_reset(&semantic);
    unsigned populated=0;
    for(unsigned lane=0;lane<WFC_LANES;lane++) {
        WFView v;assert(!wf_observe(&semantic.family,lane,&v));
        assert(strstr(wf_text_get(&v,v.instruction),"Select words similar to "));
        assert(semantic.semantic_valid[lane]);
        for(unsigned node=0;node<v.count-1;node++) {
            float score=observations[lane][8+128+node*WFC_NODE_FEATURES+80];
            assert(isfinite(score)&&score>=0&&score<=1.001f);
            populated+=score>0;
        }
    }
    assert(populated>=8);
    puf_close(&semantic);
    printf("PASS: %u native click-family episodes through PufferLib action, reward, observation and reset path\n",episodes);
    return 0;
}
