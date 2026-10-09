#include "webnav_unified.h"
#include <sys/prctl.h>

enum { TEST_LANES=104, TEST_RANDOM_STEPS=64 };

static uint32_t *test_row(Env *e,unsigned lane) {
    return e->family.words+(size_t)lane*e->family.api->row_words;
}

static void test_agent(Env *e,unsigned lane) {
    const Agent *a=e->agents+lane;
    const uint32_t *r=test_row(e,lane);
    assert(a->policy==0&&a->action_mask[0]);
    assert(r[WF_VERSION]==WF_ABI_VERSION&&r[WF_OP]==WF_OBSERVE);
    assert(r[WF_TASK]<e->family.api->task_count&&r[WF_STATUS]==WF_RUNNING);
    assert(isfinite(a->rewards[0]));
    assert(a->terminals[0]==0||a->terminals[0]==1);
    for(unsigned i=0;i<WU_OBS_SIZE;i++)assert(isfinite(a->observations[i]));
    for(unsigned i=0;i<WU_ACTIONS;i++)assert(a->action_mask[i]<=1);
    if(a->terminals[0]) {
        const WUState zero={0};
        assert(!memcmp(e->state+lane,&zero,sizeof zero));
    }
}

/* Reset one specified task without replacing any sibling's model state. */
static void test_stage_task(Env *e,unsigned lane,unsigned task,unsigned seed) {
    uint32_t *r=test_row(e,lane);
    memset(r,0,(size_t)e->family.api->row_words*sizeof *r);
    r[WF_VERSION]=WF_ABI_VERSION;r[WF_TASK]=task;
    r[WF_OP]=WF_RESET;r[WF_SEED]=seed;
    memset(e->state+lane,0,sizeof e->state[lane]);
    wnu_flush(e);wnu_observe(e);
    assert(r[WF_TASK]==task&&r[WF_STATUS]==WF_RUNNING);
}

/* The copied view/capabilities are complete encoder inputs. Unused public
 * buffer storage must not contribute features or legal actions. */
static void test_projection(Env *e,unsigned lane,float *copy_obs,
                            unsigned char *copy_mask) {
    WFView view,copy;WUCapabilities caps,copied_caps;
    wnu_public(e,lane,&view,&caps);
    copy=view;copied_caps=caps;
    memset(copy.nodes+copy.count,0xa5,
           (WF_MAX_NODES-copy.count)*sizeof *copy.nodes);
    memset(copy.text+copy.text_bytes,0x5a,WF_TEXT_BYTES-copy.text_bytes);
    memset(copied_caps.items+copied_caps.count,0xb6,
           (WU_MAX_CAPABILITIES-copied_caps.count)*sizeof *copied_caps.items);
    assert(!wu_project(&copy,&copied_caps,e->state+lane,copy_obs,copy_mask));
    assert(!memcmp(copy_obs,e->agents[lane].observations,
                   WU_OBS_SIZE*sizeof *copy_obs));
    assert(!memcmp(copy_mask,e->agents[lane].action_mask,WU_ACTIONS));
    WUState state=e->state[lane];WFAction action;
    assert(wu_decode(&copy,&copied_caps,&state,0,view.deadline_ms,&action)==1);
    assert(action.kind==WF_WAIT&&action.target==0);
    assert(action.elapsed_ms==view.deadline_ms);
}

static void test_private_click_goals(Env *e,float *copy_obs,unsigned char *copy_mask) {
    assert(!strcmp(e->family.api->family,"click"));
    test_stage_task(e,1,13,707);
    WFView view,other;WUCapabilities caps,other_caps;
    wnu_public(e,1,&view,&caps);
    size_t row_bytes=(size_t)e->family.api->row_words*sizeof(uint32_t);
    uint32_t *saved=malloc(row_bytes);assert(saved);

    /* The checked click-family wire stores each node's private goal at
     * 64+40*slot+2 (see families/click/test_click.c's independent oracle).
     * Changing every private goal must leave the public inputs identical. */
    uint32_t *r=test_row(e,1);memcpy(saved,r,row_bytes);
    for(unsigned i=0;i<view.count;i++)r[64+40*i+2]^=1;
    assert(!wf_observe(&e->family,1,&other));
    assert(!memcmp(&view,&other,sizeof view));
    assert(!wu_capabilities(e->family.api,13,&other,&other_caps));
    assert(!memcmp(&caps,&other_caps,sizeof caps));
    test_projection(e,1,copy_obs,copy_mask);
    memcpy(r,saved,row_bytes);
    assert(!e->agents[1].action_mask[WU_SET0]);
    assert(!e->agents[1].action_mask[WU_SET1]);
    free(saved);
}

static const WFFamily *test_batch_api;
static unsigned test_batch_calls;
static void test_no_replay_batch(uint32_t *words) {
    assert(test_batch_api&&words[WF_OP]==WF_STEP);
    for(unsigned l=1;l<test_batch_api->batch_lanes;l++)
        assert(words[(size_t)l*test_batch_api->row_words+WF_OP]==WF_OBSERVE);
    test_batch_calls++;
    test_batch_api->batch(words);
}

static void test_edit_event(Env *e,unsigned kind,unsigned ref,const char *text) {
    uint32_t *r=test_row(e,0);
    WFAction action={.kind=kind,.target=ref,.elapsed_ms=r[WF_ELAPSED]+50,
        .text=text,.text_length=text?strlen(text):0};
    assert(!wf_apply(&e->family,0,&action));wnu_flush(e);wnu_observe(e);
}
static void test_public_edits(Env *envs,int size) {
    for(int i=0;i<size;i++){
        Env *e=envs+i;WFView v;WUCapabilities caps;
        if(!strcmp(e->family.api->family,"editing")){
            test_stage_task(e,0,4,909);test_edit_event(e,WF_CLICK,1,NULL);
            wnu_public(e,0,&v,&caps);assert(wu_node(&v,1)->capacity==128);
            unsigned found=0;for(unsigned c=0;c<caps.count;c++)
                found+=caps.items[c].kind==WF_INSERT&&caps.items[c].ref==1&&caps.items[c].text_capacity==64;
            assert(found==1);test_edit_event(e,WF_INSERT,0,"x");
            wnu_public(e,0,&v,&caps);assert(!strcmp(wf_text_get(&v,wu_node(&v,2)->value),"x"));
        }
        if(!strcmp(e->family.api->family,"email")){
            test_stage_task(e,0,7,910);test_edit_event(e,WF_CLICK,10,NULL);
            test_edit_event(e,WF_CLICK,113,NULL);test_edit_event(e,WF_CLICK,117,NULL);
            test_edit_event(e,WF_INSERT,0,"x");test_edit_event(e,WF_SELECT_ALL,0,NULL);
            wnu_public(e,0,&v,&caps);const WFNode *field=wu_node(&v,117);assert(field);
            assert(field->selection_start==0&&field->selection_end==1);
            unsigned found=0;for(unsigned c=0;c<caps.count;c++)
                found+=caps.items[c].kind==WF_INSERT&&caps.items[c].ref==117&&caps.items[c].text_capacity==159;
            assert(found==1);
        }
    }
}
static void test_local_controls(Env *e) {
    assert(!strcmp(e->family.api->family,"geometry"));
    /* DrawModel.active's Click records a point and preserves Running;
     * only Submit ends task 0 (bisect-angle). Canvas bounds expose both
     * parameter registers, unlike the click family's scalar targets. */
    for(int l=0;l<e->num_agents;l++)test_stage_task(e,(unsigned)l,0,707u+(unsigned)l);
    size_t row_bytes=(size_t)e->family.api->row_words*sizeof(uint32_t);
    size_t batch_bytes=row_bytes*(size_t)e->num_agents;
    uint32_t *saved=malloc(batch_bytes);assert(saved);

    memcpy(saved,e->family.words,batch_bytes);
    WUState before[8];memcpy(before,e->state,sizeof before);
    for(int l=0;l<e->num_agents;l++) {
        assert(e->agents[l].action_mask[WU_SET0+219]);
        assert(e->agents[l].action_mask[WU_SET1+31]);
        e->agents[l].actions[0]=WU_SET0+219;
    }
    puf_step(e);
    assert(!memcmp(saved,e->family.words,batch_bytes));
    for(int l=0;l<e->num_agents;l++) {
        before[l].parameter0=219;before[l].steps++;
        assert(!memcmp(before+l,e->state+l,sizeof before[l]));
        assert(e->agents[l].rewards[0]==0&&e->agents[l].terminals[0]==0);
        test_agent(e,(unsigned)l);
    }

    WFView view,other;WUCapabilities caps,other_caps;
    wnu_public(e,1,&view,&caps);
    unsigned canvas=WF_MAX_NODES;
    for(unsigned i=0;i<view.count;i++)if(view.nodes[i].role==WF_CANVAS) {
        canvas=i;break;
    }
    assert(canvas<view.count);
    unsigned click=WF_CLICK*(WF_MAX_NODES+1u)+canvas+1;
    assert(e->agents[1].action_mask[click]);
    for(int l=0;l<e->num_agents;l++)e->agents[l].actions[0]=WU_SET1+31;
    e->agents[1].actions[0]=(float)click;puf_step(e);
    assert(!e->agents[1].terminals[0]);
    wnu_public(e,1,&other,&other_caps);
    assert(other.count==view.count+1);
    const WFNode *point=wu_node(&other,20);
    assert(point&&point->role==WF_OTHER);

    /* Lane 1 retains a previous CLICK in its wire header. Lane 0 executes
     * WAIT, forcing a batch, while all siblings select local parameters.
     * No sibling model row may replay that previous command. */
    memcpy(saved,e->family.words,batch_bytes);
    uint32_t elapsed=test_row(e,0)[WF_ELAPSED];
    for(int l=0;l<e->num_agents;l++)e->agents[l].actions[0]=WU_SET0+17;
    /* Inspect batch operations too: a repeated point click is idempotent,
     * so model-row equality alone would miss accidental replay. */
    test_batch_api=e->family.api;test_batch_calls=0;
    WFFamily observed_api=*test_batch_api;observed_api.batch=test_no_replay_batch;
    e->family.api=&observed_api;
    e->agents[0].actions[0]=0;puf_step(e);
    e->family.api=test_batch_api;test_batch_api=NULL;
    assert(test_batch_calls==1);
    assert(test_row(e,0)[WF_ELAPSED]>elapsed);
    for(int l=1;l<e->num_agents;l++) {
        assert(!memcmp(saved+(size_t)l*e->family.api->row_words,
                       test_row(e,(unsigned)l),row_bytes));
        assert(e->state[l].parameter0==17);
        test_agent(e,(unsigned)l);
    }
    free(saved);
}

int main(void) {
    /* Disable dumpability before the first assertion: WSL's core hook can
     * otherwise start a Windows dump process after a native test failure. */
    int dump_result=prctl(PR_SET_DUMPABLE,0,0,0,0);
    assert(!dump_result);
    Dict env_config={0},vec_config={0};
    dict_set(&env_config,"family_mask",8388607);
    dict_set(&env_config,"step_ms",50);
    dict_set(&env_config,"max_episode_steps",128);
    dict_set(&env_config,"seed_offset",7);
    dict_set(&env_config,"semantic",0);
    dict_set(&vec_config,"total_agents",TEST_LANES);
    dict_set(&vec_config,"num_buffers",1);
    dict_set(&vec_config,"num_policies",1);
    int size=0,starts[1]={-1},counts[1]={-1};
    Env *envs=my_vec_init(&size,starts,counts,&vec_config,&env_config);
    assert(envs&&size==23&&starts[0]==0&&counts[0]==23);
    Log quiet={0};Dict quiet_logs={0};puf_log(&quiet,&quiet_logs);
    assert(quiet_logs.size==5+2*WU_TASK_COUNT);
    float *obs=calloc((size_t)TEST_LANES*WU_OBS_SIZE,sizeof *obs);
    unsigned char *masks=calloc((size_t)TEST_LANES*WU_ACTIONS,1);
    float *actions=calloc(TEST_LANES,sizeof *actions);
    float *rewards=calloc(TEST_LANES,sizeof *rewards);
    float *terminals=calloc(TEST_LANES,sizeof *terminals);
    float *copy_obs=malloc(WU_OBS_SIZE*sizeof *copy_obs);
    unsigned char *copy_mask=malloc(WU_ACTIONS);
    assert(obs&&masks&&actions&&rewards&&terminals&&copy_obs&&copy_mask);
    unsigned lane_index=0,lanes4=0,lanes8=0;
    unsigned task_seen[WU_TASK_COUNT]={0};
    for(int i=0;i<size;i++) {
        Env *e=envs+i;const WUFamilySpec *spec=wu_families+i;
        assert(e->family_index==(unsigned)i&&e->num_agents==(int)spec->lanes);
        assert(e->step_ms==50&&e->max_steps==128);
        assert(e->rng==7u+(unsigned)i*2654435761u);
        assert(e->num_agents==4||e->num_agents==8);
        lanes4+=e->num_agents==4;lanes8+=e->num_agents==8;
        for(unsigned t=0;t<spec->tasks;t++) {
            unsigned global=spec->global_tasks[t];assert(global<WU_TASK_COUNT);
            assert(!strcmp(e->family.api->task_names[t],wu_task_names[global]));
            assert(!task_seen[global]++);
        }
        for(int l=0;l<e->num_agents;l++,lane_index++) {
            Agent *a=e->agents+l;assert(lane_index<TEST_LANES&&a->policy==0);
            a->observations=obs+(size_t)lane_index*WU_OBS_SIZE;
            a->action_mask=masks+(size_t)lane_index*WU_ACTIONS;
            a->actions=actions+lane_index;a->rewards=rewards+lane_index;
            a->terminals=terminals+lane_index;
        }
        puf_reset(e);
        for(int l=0;l<e->num_agents;l++)test_agent(e,(unsigned)l);
    }
    assert(lane_index==TEST_LANES&&lanes4==20&&lanes8==3);
    test_public_edits(envs,size);
    for(unsigned t=0;t<WU_TASK_COUNT;t++)assert(task_seen[t]==1);
    test_private_click_goals(envs,copy_obs,copy_mask);
    Env *geometry=NULL;
    for(int i=0;i<size;i++)if(!strcmp(envs[i].family.api->family,"geometry"))geometry=envs+i;
    assert(geometry);test_local_controls(geometry);

    unsigned random_episodes=0;
    for(unsigned step=0;step<TEST_RANDOM_STEPS;step++)for(int i=0;i<size;i++) {
        Env *e=envs+i;
        for(int l=0;l<e->num_agents;l++) {
            unsigned legal_count=0,choice=0;
            /* Reservoir selection avoids allocating another action catalog. */
            for(unsigned a=0;a<WU_ACTIONS;a++)if(e->agents[l].action_mask[a]) {
                legal_count++;if(wnu_random(e)%legal_count==0)choice=a;
            }
            assert(legal_count);e->agents[l].actions[0]=(float)choice;
        }
        puf_step(e);
        for(int l=0;l<e->num_agents;l++) {
            test_agent(e,(unsigned)l);
            random_episodes+=e->agents[l].terminals[0]!=0;
        }
    }

    unsigned tasks=0,wait_episodes=0;
    for(int i=0;i<size;i++) {
        Env *e=envs+i;e->step_ms=1000;e->max_steps=16;
        for(unsigned task=0;task<e->family.api->task_count;task++) {
            test_stage_task(e,0,task,7000u+tasks);
            test_agent(e,0);test_projection(e,0,copy_obs,copy_mask);
            unsigned global=wu_families[i].global_tasks[task];
            float previous=e->log.task_episodes[global];
            unsigned steps=0;
            do {
                for(int l=0;l<e->num_agents;l++)e->agents[l].actions[0]=0;
                puf_step(e);steps++;
                for(int l=0;l<e->num_agents;l++) {
                    test_agent(e,(unsigned)l);
                    wait_episodes+=e->agents[l].terminals[0]!=0;
                }
            } while(!e->agents[0].terminals[0]&&steps<16);
            assert(e->agents[0].terminals[0]&&steps<=16);
            assert(e->log.task_episodes[global]>=previous+1);
            tasks++;
        }
        float episode_sum=0,success_sum=0;
        for(unsigned t=0;t<WU_TASK_COUNT;t++) {
            assert(e->log.task_success[t]<=e->log.task_episodes[t]);
            episode_sum+=e->log.task_episodes[t];success_sum+=e->log.task_success[t];
        }
        assert(episode_sum==e->log.n&&success_sum==e->log.perf);
        Dict logs={0};puf_log(&e->log,&logs);
        assert(logs.size==quiet_logs.size);
        for(int key=0;key<quiet_logs.size;key++)assert(dict_find(&logs,quiet_logs.items[key].key));
        assert(dict_get(&logs,"score")==e->log.score);
        assert(dict_get(&logs,"episode_length")==e->log.episode_length);
        for(unsigned task=0;task<e->family.api->task_count;task++) {
            unsigned global=wu_families[i].global_tasks[task];char key[128];
            snprintf(key,sizeof key,"task/%s/success",wu_task_names[global]);
            double success=dict_get(&logs,key);assert(success>=0&&success<=1);
        }
        dict_clear(&logs);puf_close(e);
        assert(!e->family.handle&&!e->family.api&&!e->family.words);
    }
    assert(tasks==WU_TASK_COUNT&&wait_episodes>=WU_TASK_COUNT);
    free(envs);free(obs);free(masks);free(actions);free(rewards);free(terminals);
    free(copy_obs);free(copy_mask);dict_clear(&env_config);dict_clear(&vec_config);dict_clear(&quiet_logs);
    printf("PASS: 23 families, 104 lanes, %u task projections/deadlines, "
           "%u random episodes, %u WAIT episodes; one policy, public-input "
           "invariance, local controls, sibling isolation and autoresets\n",
           tasks,random_episodes,wait_episodes);
    return 0;
}
