#include "webnav_forms.h"
#include "../webnav/families/forms/public_controller.h"
#include <assert.h>
#include <stdio.h>
#include <sys/prctl.h>

static unsigned adapter_action(const WFView *v, const WFFCatalog *cat,
                               const WFAction *wanted) {
    if (wanted->kind==WF_WAIT) return 0;
    if (wanted->kind==WF_SELECT_ALL) return 17;
    if (wanted->kind==WF_CLICK) {
        for (unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==wanted->target)return 1+i;
    }
    if (wanted->kind==WF_INSERT) {
        for (unsigned i=0;i<WFF_CANDIDATES;i++)if(cat->c[i].length==wanted->text_length&&
            !memcmp(cat->c[i].text,wanted->text,wanted->text_length))return 18+i;
    }
    return WFF_ACTIONS;
}

int main(void) {
    prctl(PR_SET_DUMPABLE,0);
    Dict cfg={0};dict_set(&cfg,"task_mask",1);dict_set(&cfg,"seed_offset",0);
    dict_set(&cfg,"data_mode",0);
    dict_set(&cfg,"progress_reward",0);
    Env env={0};env.rng=7;puf_init(&env,&cfg);
    float obs[WFF_LANES][OBS_SIZE]={0},actions[WFF_LANES]={0};
    float rewards[WFF_LANES]={0},terminals[WFF_LANES]={0};
    unsigned char masks[WFF_LANES][WFF_ACTIONS]={0};
    for(unsigned lane=0;lane<WFF_LANES;lane++){
        env.agents[lane].observations=obs[lane];
        env.agents[lane].action_mask=masks[lane];
        env.agents[lane].actions=&actions[lane];
        env.agents[lane].rewards=&rewards[lane];
        env.agents[lane].terminals=&terminals[lane];
    }
    unsigned episodes=0;
    for(unsigned task=0;task<8;task++){
        env.task_mask=1u<<task;
        for(unsigned batch=0;batch<8;batch++){
            puf_reset(&env);
            unsigned done[WFF_LANES]={0},finished=0;
            for(unsigned step=0;step<25;step++){
                for(unsigned lane=0;lane<WFF_LANES;lane++){
                    if(done[lane]){actions[lane]=0;continue;}
                    WFView v;WFFCatalog cat;WFAction wanted={0};
                    assert(!wf_observe(&env.family,lane,&v));
                    wff_catalog(&v,&cat);
                    assert(!forms_public_action(&v,&wanted));
                    unsigned choice=adapter_action(&v,&cat,&wanted);
                    assert(choice<WFF_ACTIONS&&masks[lane][choice]);
                    if(task==6&&wanted.kind==WF_INSERT){
                        unsigned slot=choice-18;
                        assert(choice>=18&&slot<WFF_CANDIDATES);
                        size_t at=16+192+WFF_NODES*WFF_NODE_FEATURES+
                                  slot*WFF_CANDIDATE_FEATURES;
                        assert(obs[lane][at+9]==1.0f);
                    }
                    actions[lane]=(float)choice;
                }
                puf_step(&env);
                for(unsigned lane=0;lane<WFF_LANES;lane++)if(!done[lane]&&terminals[lane]){
                    assert(rewards[lane]>=0.999f);
                    assert(masks[lane][0]);
                    done[lane]=1;finished++;episodes++;
                }
                if(finished==WFF_LANES)break;
                if(step==24)fprintf(stderr,"task=%u batch=%u finished=%u\n",task,batch,finished);
                assert(step<24);
            }
        }
    }
    assert(episodes==8*8*WFF_LANES);
    env.task_mask=1u<<6;env.data_mode=1;
    unsigned prefilled_side[2]={0};
    for(unsigned batch=0;batch<32;batch++){
        puf_reset(&env);
        for(unsigned lane=0;lane<WFF_LANES;lane++){
            WFView v;assert(!wf_observe(&env.family,lane,&v));
            unsigned filled=0;
            for(unsigned field=0;field<2;field++){
                const char *value=wf_text_get(&v,v.nodes[field].value),*goal;
                size_t length;
                assert(!forms_public_table_value(&v,wf_text_get(&v,v.nodes[field].name),&goal,&length));
                if(*value){assert(strlen(value)==length&&!memcmp(value,goal,length));filled++;prefilled_side[field]++;}
            }
            assert(filled==1);
        }
    }
    assert(prefilled_side[0]&&prefilled_side[1]);
    env.progress_reward=1;unsigned positive_shaping=0;
    for(unsigned batch=0;batch<8;batch++){
        puf_reset(&env);unsigned done[WFF_LANES]={0},finished=0;
        for(unsigned step=0;step<16&&finished<WFF_LANES;step++){
            for(unsigned lane=0;lane<WFF_LANES;lane++){
                if(done[lane]){actions[lane]=0;continue;}
                WFView v;WFFCatalog cat;WFAction wanted={0};
                assert(!wf_observe(&env.family,lane,&v));wff_catalog(&v,&cat);
                assert(!forms_public_action(&v,&wanted));
                unsigned choice=adapter_action(&v,&cat,&wanted);
                assert(choice<WFF_ACTIONS&&masks[lane][choice]);actions[lane]=(float)choice;
            }
            puf_step(&env);
            for(unsigned lane=0;lane<WFF_LANES;lane++)if(!done[lane]&&!terminals[lane]&&rewards[lane]>0)
                positive_shaping++;
            for(unsigned lane=0;lane<WFF_LANES;lane++)if(!done[lane]&&terminals[lane]){
                assert(rewards[lane]>=0.49f);done[lane]=1;finished++;
            }
        }
        assert(finished==WFF_LANES);
    }
    assert(positive_shaping);env.progress_reward=0;
    char colors[8][56]={{0}};unsigned unique_colors=0;
    for(unsigned seed=0;seed<128;seed++){
        assert(!wf_reset(&env.family,6,seed*19u));
        WFView v;assert(!wf_observe(&env.family,0,&v));
        for(unsigned i=0;i+1<v.count;i++)if(v.nodes[i].role==WF_CELL&&
            !strcmp(wf_text_get(&v,v.nodes[i].name),"Color")){
            const char *value=wf_text_get(&v,v.nodes[i+1].value);unsigned seen=0;
            for(unsigned k=0;k<unique_colors;k++)seen|=!strcmp(colors[k],value);
            if(!seen&&unique_colors<8)snprintf(colors[unique_colors++],56,"%s",value);
        }
    }
    assert(unique_colors>=3);
    env.task_mask=1u<<5;env.data_mode=0;unsigned ordinals[3]={0},third_length_rank[3]={0};
    for(unsigned batch=0;batch<64;batch++){
        puf_reset(&env);
        for(unsigned lane=0;lane<WFF_LANES;lane++){
            WFView v;assert(!wf_observe(&env.family,lane,&v));
            const char *q=wf_text_get(&v,v.instruction);
            ordinals[0]+=strstr(q,"the 1st text area")!=NULL;
            ordinals[1]+=strstr(q,"the 2nd text area")!=NULL;
            ordinals[2]+=strstr(q,"the 3rd text area")!=NULL;
            if(strstr(q,"the 3rd text area")){
                WFFCatalog cat;wff_catalog(&v,&cat);
                unsigned a=cat.c[4].length,b=cat.c[5].length,c=cat.c[6].length;
                if(a!=b&&a!=c&&b!=c)third_length_rank[(a<c)+(b<c)]++;
            }
        }
    }
    for(unsigned ordinal=0;ordinal<3;ordinal++)assert(ordinals[ordinal]>=20);
    for(unsigned rank=0;rank<3;rank++)assert(third_length_rank[rank]>=5);
    for(unsigned task=0;task<8;task++)if(task==0||task==2||task==7){
        unsigned upper=0,six=0;
        for(unsigned seed=0;seed<128;seed++){
            assert(!wf_reset(&env.family,task,seed*17u));
            WFView v;assert(!wf_observe(&env.family,0,&v));
            const char *quote;size_t length;
            assert(wff_quoted(wf_text_get(&v,v.instruction),task==7?1:0,&quote,&length));
            assert(length>=(task==0?1u:2u)&&length<=6u);
            six+=length==6u;
            for(size_t i=0;i<length;i++)upper+=quote[i]>='A'&&quote[i]<='Z';
        }
        assert(upper&&six);
    }
    puf_close(&env);
    printf("PASS: %u public-controller episodes through forms text catalog, mask, Bend actions, rewards and resets\n",episodes);
    return 0;
}
