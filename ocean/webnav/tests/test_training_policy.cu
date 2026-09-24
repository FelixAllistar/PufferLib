#define PRECISION_FLOAT
#define ENV_HEADER "../ocean/webnav_dom/webnav_dom.h"
#define PUFFER_ENV_NAME "webnav_dom"
#include "../../../src/pufferl.cu"
#include "../training_policy.h"

int main(int argc,char **argv) {
    if(argc!=2)return 2;
    Ini ini={0};puf_ini_load_env(&ini,"webnav_dom",0,NULL);
    puf_ini_put(&ini,"vec.total_agents","32");
    puf_ini_put(&ini,"vec.num_buffers","1");
    puf_ini_put(&ini,"vec.num_threads","1");
    puf_ini_put(&ini,"train.minibatch_size","32");
    puf_ini_put(&ini,"base.async","0");
    puf_ini_put(&ini,"base.cudagraphs","-1");
    int potion,hidden,layers;assert(!wt_contract_read(argv[1],&potion,&hidden,&layers));
    char number[32];snprintf(number,sizeof number,"%d",potion);puf_ini_put(&ini,"env.potion",number);
    snprintf(number,sizeof number,"%d",hidden);puf_ini_put(&ini,"policy.hidden_size",number);
    snprintf(number,sizeof number,"%d",layers);puf_ini_put(&ini,"policy.num_layers",number);
    wt_configure(&ini,"eval");
    TrainContext ctx={.world_size=1,.artifact_owner=1};
    PuffeRL *p=create_pufferl(&ini,&ctx);
    puf_load_weights_into(p->master_weights,p->param_puf,p->default_stream,argv[1],&p->checkpoint_contract);
    pufferl_sync_loaded_policy(p);
    WTPolicy *cpu=wt_policy_load(argv[1]);assert(cpu);
    float max_error=0;int terminals=0;
    for(int step=0;step<64;step++) {
        VecEnv *v=p->vec;
        int action=wt_policy_action(cpu,v->envs[0].views);
        pufferl_forward(p,0,step%16,p->streams[0],true);
        auto *dec=(DecoderActivations*)p->buffer_activations[0].decoder;
        float logits[WT_ACTIONS+1];
        cudaMemcpyAsync(logits,dec->out.data,sizeof logits,cudaMemcpyDeviceToHost,p->streams[0]);
        cudaMemcpyAsync(v->actions,v->gpu_actions,32*sizeof(float),cudaMemcpyDeviceToHost,p->streams[0]);
        cudaStreamSynchronize(p->streams[0]);
        const float *expected=wt_policy_logits(cpu);
        for(int i=0;i<WT_ACTIONS+1;i++) {
            float error=fabsf(logits[i]-expected[i]);max_error=fmaxf(error,max_error);
            if(!isfinite(error)||error>0.003f){fprintf(stderr,"logit mismatch step=%d i=%d cpu=%g gpu=%g\n",step,i,expected[i],logits[i]);return 1;}
        }
        if((int)v->actions[0]!=action){fprintf(stderr,"action mismatch step=%d cpu=%d gpu=%g\n",step,action,v->actions[0]);return 1;}
        puf_step(&v->envs[0]);
        if(v->terminals[0]){wt_policy_reset(cpu);terminals++;}
        cudaMemcpyAsync(p->env.obs.data,v->observations,32*OBS_SIZE*sizeof(float),cudaMemcpyHostToDevice,p->streams[0]);
        cudaMemcpyAsync(p->env.rewards.data,v->rewards,32*sizeof(float),cudaMemcpyHostToDevice,p->streams[0]);
        cudaMemcpyAsync(p->env.terminals.data,v->terminals,32*sizeof(float),cudaMemcpyHostToDevice,p->streams[0]);
        cudaMemcpyAsync(p->env.action_mask.data,v->action_mask,32*WT_ACTIONS,cudaMemcpyHostToDevice,p->streams[0]);
        cudaStreamSynchronize(p->streams[0]);
    }
    printf("{\"cpu_cuda_policy_parity\":\"PASS\",\"steps\":64,\"terminal_resets\":%d,\"max_logit_error\":%.9g}\n",terminals,max_error);
    wt_policy_free(cpu);close_pufferl(p);puf_ini_free(&ini);
}
