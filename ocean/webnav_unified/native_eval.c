#include <sys/prctl.h>
#include <sys/types.h>
#include "../../src/puffercpu.c"
#include "webnav_unified.h"
#include "cpu_linear.h"

#define WU_EVAL_LANES 8u
#define WU_EVAL_HIDDEN 64u

static size_t align8(size_t n) { return (n+7u)&~7u; }
static size_t expected_weights(void) {
    size_t n=align8(WU_EVAL_HIDDEN*OBS_SIZE);
    n=align8(n+WU_EVAL_HIDDEN*(WU_ACTIONS+1u));
    return align8(n+3u*WU_EVAL_HIDDEN*WU_EVAL_HIDDEN);
}

static unsigned choose_action(const unsigned char *mask,const float *logits,
                              uint32_t *rng,int sample) {
    unsigned best=0,legal=0;
    float score=-INFINITY;
    for(unsigned a=0;a<WU_ACTIONS;a++)if(mask[a]) {
        if(!legal)best=a;
        legal++;
        if(logits&&!isfinite(logits[a]))wnu_error("nonfinite policy logits");
        if(logits&&logits[a]>score){best=a;score=logits[a];}
    }
    if(!legal)wnu_error("empty shared action mask");
    if(logits&&!sample)return best;
    *rng=*rng*1664525u+1013904223u;
    if(logits) {
        double mass=0;
        for(unsigned a=0;a<WU_ACTIONS;a++)if(mask[a])mass+=exp((double)logits[a]-score);
        double draw=((*rng>>8)+0.5)/16777216.0*mass;
        for(unsigned a=0;a<WU_ACTIONS;a++)if(mask[a]) {
            draw-=exp((double)logits[a]-score);
            if(draw<=0)return a;
        }
        return best;
    }
    unsigned selected=(*rng>>8)%legal;
    for(unsigned a=0;a<WU_ACTIONS;a++)if(mask[a]) {
        if(!selected)return a;
        selected--;
    }
    return best;
}

int main(int argc,char **argv) {
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    if(argc<3||argc>6) {
        fprintf(stderr,"usage: %s CHECKPOINT|random EPISODES [TASK_NAME] [--semantic=0|1] [--sample]\n",argv[0]);
        return 2;
    }
    char *end=NULL;
    unsigned long count=strtoul(argv[2],&end,10);
    if(end==argv[2]||*end||count<1||count>100000) {
        fputs("EPISODES must be in 1..100000\n",stderr);return 2;
    }
    const char *task_name=NULL;
    int semantic=1,sample=0;
    for(int i=3;i<argc;i++) {
        if(!strcmp(argv[i],"--semantic=0"))semantic=0;
        else if(!strcmp(argv[i],"--semantic=1"))semantic=1;
        else if(!strcmp(argv[i],"--sample"))sample=1;
        else if(!task_name&&strncmp(argv[i],"--",2))task_name=argv[i];
        else {fprintf(stderr,"invalid argument: %s\n",argv[i]);return 2;}
    }
    if(task_name) {
        unsigned t=0;
        while(t<WU_TASK_COUNT&&strcmp(task_name,wu_task_names[t]))t++;
        if(t==WU_TASK_COUNT) {
            fprintf(stderr,"unknown task: %s\n",task_name);return 2;
        }
    }

    int random_policy=!strcmp(argv[1],"random");
    Weights *weights=NULL;
    PufferNet *net=NULL;
    if(!random_policy) {
        struct stat checkpoint;
        if(stat(argv[1],&checkpoint)||
           checkpoint.st_size!=(off_t)(expected_weights()*sizeof(float))) {
            fprintf(stderr,"expected H64/L1 unified checkpoint with %zu floats\n",expected_weights());
            return 2;
        }
        weights=load_weights(argv[1]);
        if(!weights||weights->size<7||(size_t)(weights->size-7)!=expected_weights()) {
            fputs("checkpoint load failed\n",stderr);free(weights);return 2;
        }
        for(size_t i=0;i<expected_weights();i++)if(!isfinite(weights->data[i])) {
            fputs("checkpoint contains nonfinite weights\n",stderr);free(weights);return 2;
        }
        int sizes[]={WU_ACTIONS};
        net=make_puffernet(weights,WU_EVAL_LANES,OBS_SIZE,WU_EVAL_HIDDEN,1,sizes,1);
        if(!net||(size_t)weights->idx!=expected_weights()) {
            fputs("checkpoint layout mismatch\n",stderr);
            if(net)free_puffernet(net);
            free(weights);return 2;
        }
    }
    if(wu_semantic_init(semantic)) {
        fputs("pinned frozen text encoder failed to load\n",stderr);
        if(net)free_puffernet(net);
        free(weights);return 2;
    }
    float *observations=calloc((size_t)WU_EVAL_LANES*OBS_SIZE,sizeof(float));
    unsigned char *masks=calloc((size_t)WU_EVAL_LANES*WU_ACTIONS,1);
    float *actions=calloc(WU_EVAL_LANES,sizeof(float));
    float *rewards=calloc(WU_EVAL_LANES,sizeof(float));
    float *terminals=calloc(WU_EVAL_LANES,sizeof(float));
    if(!observations||!masks||!actions||!rewards||!terminals)
        wnu_error("evaluation buffer allocation failed");

    unsigned evaluated_tasks=0;
    double total_episodes=0,total_wins=0,total_reward=0,total_steps=0;
    double total_rejected=0,total_incomplete=0;
    for(unsigned task=0;task<WU_TASK_COUNT;task++) {
        const char *name=wu_task_names[task];
        if(task_name&&strcmp(task_name,name))continue;
        unsigned family=WU_FAMILY_COUNT,local=0;
        for(unsigned f=0;f<WU_FAMILY_COUNT;f++)
            for(unsigned t=0;t<wu_families[f].tasks;t++)
                if(wu_families[f].global_tasks[t]==task){family=f;local=t;}
        if(family==WU_FAMILY_COUNT)wnu_error("task missing from family registry");

        Dict config={0};
        dict_set(&config,"step_ms",50);
        dict_set(&config,"max_episode_steps",512);
        dict_set(&config,"task",local);
        Env env={0};
        env.family_index=family;
        env.rng=4000000u+task*10000u;
        puf_init(&env,&config);
        if(strcmp(name,env.family.api->task_names[local]))
            wnu_error("task name differs from family registry");

        /* Four-lane families use the first four lanes of the same network.
         * Clear every input lane and recurrent carry before each new task. */
        memset(observations,0,(size_t)WU_EVAL_LANES*OBS_SIZE*sizeof(float));
        memset(masks,0,(size_t)WU_EVAL_LANES*WU_ACTIONS);
        memset(actions,0,WU_EVAL_LANES*sizeof(float));
        memset(rewards,0,WU_EVAL_LANES*sizeof(float));
        memset(terminals,0,WU_EVAL_LANES*sizeof(float));
        if(net)memset(net->mingru->state,0,WU_EVAL_LANES*WU_EVAL_HIDDEN*sizeof(float));
        if(net){net->encoder->batch_size=env.num_agents;net->decoder->batch_size=env.num_agents;net->mingru->batch_size=env.num_agents;}
        for(int lane=0;lane<env.num_agents;lane++)
            env.agents[lane]=(Agent){observations+(size_t)lane*OBS_SIZE,
                actions+lane,rewards+lane,terminals+lane,
                masks+(size_t)lane*WU_ACTIONS,0};
        puf_reset(&env);
        uint32_t rng=91037u+task*10000u;
        unsigned long long vector_steps=0;
        unsigned long quota=(count+(unsigned)env.num_agents-1)/(unsigned)env.num_agents;
        unsigned long completed[WU_EVAL_LANES]={0};
        unsigned episode_steps[WU_EVAL_LANES]={0},episode_incomplete[WU_EVAL_LANES]={0};
        unsigned previous[WU_EVAL_LANES];
        for(unsigned lane=0;lane<WU_EVAL_LANES;lane++)previous[lane]=UINT32_MAX;
        double local_steps=0,wait_steps=0,repeated_choices=0;
        double games=0,wins=0,reward=0,steps=0,incomplete=0;
        /* Finish a fixed quota in every lane. A first-completion threshold
         * censors slower initial instances and biases small comparisons. */
        while(games<(double)(quota*(unsigned)env.num_agents)) {
            if(net) {
                mingru_zero_term(net->mingru,terminals);
                wu_linear(net->encoder,observations);
                mingru(net->mingru,net->encoder->output);
                wu_linear(net->decoder,net->mingru->output);
            }
            for(int lane=0;lane<env.num_agents;lane++) {
                if(completed[lane]>=quota){actions[lane]=0;continue;}
                const float *logits=net?net->decoder->output+(size_t)lane*(WU_ACTIONS+1u):NULL;
                unsigned choice=choose_action(masks+(size_t)lane*WU_ACTIONS,logits,&rng,sample);
                actions[lane]=(float)choice;
                local_steps+=choice>=WU_EXEC_COUNT;wait_steps+=choice==0;
                repeated_choices+=choice==previous[lane];previous[lane]=choice;
                episode_steps[lane]++;
                WFView view;WUCapabilities capabilities;wnu_public(&env,(unsigned)lane,&view,&capabilities);
                episode_incomplete[lane]|=capabilities.incomplete!=0;
            }
            puf_step(&env);
            vector_steps+=(unsigned)env.num_agents;
            for(int lane=0;lane<env.num_agents;lane++)if(completed[lane]<quota&&terminals[lane]){
                completed[lane]++;games++;wins+=rewards[lane]>=0.999f;reward+=rewards[lane];
                steps+=episode_steps[lane];incomplete+=episode_incomplete[lane]!=0;
                episode_steps[lane]=episode_incomplete[lane]=0;
                previous[lane]=UINT32_MAX;
            }
        }
        printf("{\"task\":\"%s\",\"family\":\"%s\",\"requested_episodes\":%lu,"
               "\"episodes\":%.0f,\"wins\":%.0f,\"full_credit_rate\":%.6f,"
               "\"reward\":%.6f,\"mean_reward\":%.6f,\"steps\":%.0f,"
               "\"vector_steps\":%llu,\"rejected\":%.0f,\"incomplete\":%.0f,"
               "\"local_steps\":%.0f,\"wait_steps\":%.0f,\"repeated_choices\":%.0f,"
               "\"policy\":\"%s\",\"semantic\":%d}\n",
               name,wu_families[family].name,count,games,wins,wins/games,
               reward,reward/games,steps,vector_steps,
               (double)env.log.invalid_actions,incomplete,local_steps,wait_steps,repeated_choices,
               random_policy?"random":sample?"sampled":"greedy",semantic);
        fflush(stdout);
        evaluated_tasks++;
        total_episodes+=games;total_wins+=wins;
        total_reward+=reward;total_steps+=steps;
        total_rejected+=env.log.invalid_actions;total_incomplete+=incomplete;
        puf_close(&env);
        free(config.items);
    }
    fprintf(stderr,"Unified WebNav: tasks=%u episodes=%.0f wins=%.0f reward=%.6f "
            "steps=%.0f rejected=%.0f incomplete=%.0f policy=%s semantic=%d\n",
            evaluated_tasks,total_episodes,total_wins,total_reward,total_steps,
            total_rejected,total_incomplete,random_policy?"random":sample?"sampled":"greedy",semantic);
    free(observations);free(masks);free(actions);free(rewards);free(terminals);
    wu_semantic_close();
    if(net)free_puffernet(net);
    free(weights);
    return evaluated_tasks?0:2;
}
