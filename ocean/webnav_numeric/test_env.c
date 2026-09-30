#include <sys/prctl.h>
#include "webnav_numeric.h"

static void agents(Env *env,float obs[WNN_LANES][OBS_SIZE],
                   float actions[WNN_LANES],float rewards[WNN_LANES],
                   float terminals[WNN_LANES],
                   unsigned char masks[WNN_LANES][WNN_ACTIONS]) {
    for(unsigned i=0;i<WNN_LANES;i++)env->agents[i]=(Agent){
        obs[i],actions+i,rewards+i,terminals+i,masks[i],0};
}
static void choose_all(float actions[WNN_LANES],unsigned choice) {
    for(unsigned i=0;i<WNN_LANES;i++)actions[i]=(float)choice;
}
int main(void) {
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    float extremes[4]={0};
    assert(wnn_numbers("99999999999999999999999 -99999999999999999999999",extremes,4)==2);
    assert(extremes[0]==10&&extremes[1]==-10);
    assert(wnn_single_number("99999",extremes)&&extremes[0]==10);
    /* Projection and masks depend on the supplied public view and own history. */
    WFView v;wf_view_init(&v,0,15000);
    const char *query="Solve the math problem.";
    wf_text_add(&v,query,strlen(query),&v.instruction);
    v.count=3;
    for(unsigned i=0;i<3;i++){
        v.nodes[i].ref=i+1;v.nodes[i].flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
        wf_text_add(&v,i==0?"Answer":i==1?"Submit":"Problem",
                    i==0?6:i==1?6:7,&v.nodes[i].name);
        const char *value=i==2?"4 + 5 =":"";
        wf_text_add(&v,value,strlen(value),&v.nodes[i].value);
    }
    v.nodes[0].role=WF_INPUT;v.nodes[0].capacity=23;
    v.nodes[1].role=WF_BUTTON;v.nodes[2].role=WF_TEXT;
    Env local={0};local.x[0]=77;local.y[0]=62;
    float out[OBS_SIZE],same[OBS_SIZE];unsigned char mask[WNN_ACTIONS],same_mask[WNN_ACTIONS];
    wnn_project(&v,&local,0,out,mask);
    wnn_project(&v,&local,0,same,same_mask);
    assert(!memcmp(out,same,sizeof out)&&!memcmp(mask,same_mask,sizeof mask));
    assert(mask[WNN_CLICK]&&mask[WNN_CLICK+1]&&mask[WNN_NUMBER+99]);
    assert(!mask[WNN_SET_X]&&!mask[WNN_MOVE]&&!mask[WNN_ENTER]);
    assert(out[104]==0.04f&&out[105]==0.05f&&out[108]==1);
    wf_text_add(&v,"12345678901234567890123",23,&v.nodes[0].value);
    v.nodes[0].selection_start=0;v.nodes[0].selection_end=23;
    wnn_project(&v,&local,0,out,mask);
    assert(mask[WNN_BACKSPACE]&&!mask[WNN_NUMBER+99]);
    wf_text_add(&v,"",0,&v.nodes[0].value);v.nodes[0].selection_end=0;
    v.nodes[0].flags&=~WF_ENABLED;wnn_project(&v,&local,0,out,mask);
    assert(!mask[WNN_CLICK]&&!mask[WNN_SELECT_ALL]&&!mask[WNN_NUMBER]);
    v.nodes[0].flags|=WF_ENABLED;
    v.nodes[0].role=WF_CANVAS;v.nodes[0].capacity=0;
    wnn_project(&v,&local,0,out,mask);
    assert(mask[WNN_SET_X]&&mask[WNN_SET_X+154]&&
           mask[WNN_SET_Y+125]&&mask[WNN_MOVE]&&mask[WNN_HOT_CLICK]);
    char payload[16];WFAction a=wnn_action(&local,0,&v,WNN_SET_X+154,payload);
    assert(a.kind==WF_WAIT&&local.x[0]==154);
    a=wnn_action(&local,0,&v,WNN_SET_Y+125,payload);
    assert(a.kind==WF_WAIT&&local.y[0]==125);
    a=wnn_action(&local,0,&v,WNN_MOVE,payload);
    assert(a.kind==WF_POINTER_MOVE&&a.target==1&&a.arg0==154&&a.arg1==125);
    a=wnn_action(&local,0,&v,WNN_HOT_CLICK,payload);
    assert(a.kind==WF_CLICK&&a.target==1&&a.arg0==154&&a.arg1==125);

    for(unsigned task=0;task<9;task++){
        Dict config={0};dict_set(&config,"task_mask",1u<<task);
        Env env={0};env.rng=12345u+task;puf_init(&env,&config);
        float obs[WNN_LANES][OBS_SIZE],actions[WNN_LANES]={0};
        float rewards[WNN_LANES]={0},terminals[WNN_LANES]={0};
        unsigned char masks[WNN_LANES][WNN_ACTIONS];
        agents(&env,obs,actions,rewards,terminals,masks);puf_reset(&env);
        for(unsigned lane=0;lane<WNN_LANES;lane++){
            WFView public;if(wf_observe(&env.family,lane,&public))abort();
            assert(masks[lane][0]&&public.count<=WNN_NODES);
            for(unsigned k=0;k<OBS_SIZE;k++)assert(isfinite(obs[lane][k]));
        }
        if(task==3){
            choose_all(actions,WNN_NUMBER+(5-WNN_NUMBER_MIN));
            for(unsigned lane=0;lane<WNN_LANES;lane++)assert(masks[lane][(unsigned)actions[lane]]);
            puf_step(&env);
            for(unsigned lane=0;lane<WNN_LANES;lane++){
                WFView public;assert(!wf_observe(&env.family,lane,&public));
                assert(!strcmp(wf_text_get(&public,public.nodes[0].value),"5"));
            }
            choose_all(actions,WNN_SELECT_ALL);puf_step(&env);
            choose_all(actions,WNN_BACKSPACE);puf_step(&env);
            for(unsigned lane=0;lane<WNN_LANES;lane++){
                WFView public;assert(!wf_observe(&env.family,lane,&public));
                assert(!strcmp(wf_text_get(&public,public.nodes[0].value),""));
            }
        }
        if(task==4){
            choose_all(actions,WNN_SET_X+80);puf_step(&env);
            choose_all(actions,WNN_SET_Y+60);puf_step(&env);
            choose_all(actions,WNN_MOVE);puf_step(&env);
            for(unsigned lane=0;lane<WNN_LANES;lane++){
                WFView public;assert(!wf_observe(&env.family,lane,&public));
                assert(env.x[lane]==80&&env.y[lane]==60&&env.probes[lane]==1);
                const char *heat=wf_text_get(&public,public.nodes[1].value);
                assert(heat&&*heat);
            }
        }
        /* The numeric task family accepts a wait until the deadline. This
         * exercises the real DSO batch, terminal reward, and auto-reset. */
        choose_all(actions,0);unsigned ended=0;
        for(unsigned step=0;step<130&&!ended;step++){
            puf_step(&env);
            for(unsigned lane=0;lane<WNN_LANES;lane++){
                assert(masks[lane][0]);
                for(unsigned k=0;k<OBS_SIZE;k++)assert(isfinite(obs[lane][k]));
                if(terminals[lane]){
                    ended++;
                    assert(isfinite(rewards[lane]));
                    assert(env.steps[lane]==0&&env.probes[lane]==0&&env.clicks[lane]==0);
                    assert(env.x[lane]==77&&env.y[lane]==62);
                    for(unsigned ref=0;ref<=WNN_NODES;ref++)assert(!env.visited[lane][ref]);
                    /* Reset immediately observes the next episode: public card
                     * values may already be remembered. They must equal a fresh
                     * observer, never the previous episode's memory. */
                    WFView reset_view;assert(!wf_observe(&env.family,lane,&reset_view));
                    Env fresh={0};float projected[OBS_SIZE];unsigned char legal[WNN_ACTIONS];
                    wnn_project(&reset_view,&fresh,0,projected,legal);
                    assert(!memcmp(env.known[lane],fresh.known[0],sizeof env.known[lane]));
                    assert(!memcmp(env.remembered[lane],fresh.remembered[0],sizeof env.remembered[lane]));
                    const uint32_t *r=env.family.words+(size_t)lane*env.family.api->row_words;
                    assert(r[WF_STATUS]==WF_RUNNING&&r[WF_TASK]==task);
                }
            }
        }
        assert(ended==WNN_LANES);
        puf_close(&env);free(config.items);
    }
    puts("PASS: public projection, numeric/coordinate masks, pointer transport, nine-task DSO routing and autoreset");
}
