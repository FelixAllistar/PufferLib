// Tiny, opt-in qualification of the ACTUAL sampler, PPO and teacher KL kernels.
// Does not instantiate a trainer or change any checkpoint.
#include "../../../src/pufferl.cu"

template<class T> T* managed(int64_t n) {
    T* p;
    assert(cudaMallocManaged(&p,n*sizeof(T)) == cudaSuccess);
    memset(p,0,n*sizeof(T));
    return p;
}
void sync_test() { assert(cudaDeviceSynchronize() == cudaSuccess); }
void close_test(double a, double b, double tolerance=3e-5) {
    if (!isfinite(a) || !isfinite(b) || fabs(a-b)>tolerance*(1+fabs(b))) {
        fprintf(stderr,"mismatch %.12g != %.12g\n",a,b); abort();
    }
}

int main(int argc, char** argv) {
    int graphs = argc > 1 ? atoi(argv[1]) : 0;
    int greedy = argc > 2 ? atoi(argv[2]) : 0;
    static_assert(sizeof(logprob_t)==4,"direct PPO old logprobs must retain FP32");
    // Exercise full warps and varied unit counts, not just four active lanes.
    const int rows=64, A=KAG_ALL_LOGITS, C=A+1;
    Env* env=managed<Env>(rows/2);
    for (int e=0;e<rows/2;e++) {
        KGConfig config; kg_config_default(&config); config.seed=17+e;
        kg_init(&env[e].game,&config);
        env[e].policy.market_slots=10; env[e].policy.max_hands=19;
        for (int p=0;p<2;p++) {
            KGPlayer* f=&env[e].game.players[p];
            f->unit_count=1+e%20; f->hand_count=f->unit_count-1;
            for (int u=1;u<f->unit_count;u++) {
                f->units[u]=f->units[0]; f->units[u].x=u%10; f->units[u].y=(u+e)%10;
            }
        }
        kag_policy_reset(&env[e].policy,&env[e].game,0);
        if (e%2) {
            env[e].game.step=718; env[e].game.day=29; env[e].game.hour=22;
            for (int p=0;p<2;p++) {
                KGPlayer* f=&env[e].game.players[p];
                f->units[0].x=f->units[0].y=4;
                kg_inventory_add(&f->units[0],4,5); f->shed[5]=8;
            }
        }
    }
    int* map=managed<int>(rows); env->sampling_rows=map;
    for(int r=0;r<rows;r++) map[r]=rows-r-1; // shuffled learner row map
    auto logits=managed<precision_t>(rows*C), teacher=managed<precision_t>(rows*C);
    auto mask=managed<precision_t>(rows*A), values=managed<precision_t>(rows);
    auto oldlp=managed<logprob_t>(rows);
    auto actions=managed<float>(rows*KAG_ACTION_HEADS), dispatch=managed<float>(rows*KAG_ACTION_HEADS);
    auto rng=managed<curandStatePhilox4_32_10_t>(rows);
    auto sizes=managed<int>(KAG_ACTION_HEADS); const int widths[]=ACT_SIZES; memcpy(sizes,widths,sizeof(widths));
    for(int i=0;i<rows*C;i++) {
        logits[i]=from_float(((i*13)%43-21)/8.0f);
        teacher[i]=from_float(((i*19)%53-26)/8.0f);
    }
    rng_init<<<1,256>>>(rng,71,rows); sync_test();
    cudaStream_t stream; assert(cudaStreamCreate(&stream)==cudaSuccess);
    if(graphs) assert(cudaStreamBeginCapture(stream,cudaStreamCaptureModeGlobal)==cudaSuccess);
    sample_logits<<<1,256,0,stream>>>({.data=logits,.shape={rows,C}}, {}, sizes,
        actions,dispatch,oldlp,values,rng,mask,A,env,0,greedy);
    if(graphs) {
        cudaGraph_t graph; cudaGraphExec_t exec;
        assert(cudaStreamEndCapture(stream,&graph)==cudaSuccess);
        assert(cudaGraphInstantiate(&exec,graph,NULL,NULL,0)==cudaSuccess);
        assert(cudaGraphLaunch(exec,stream)==cudaSuccess);
    }
    sync_test();
    assert(!memcmp(actions,dispatch,rows*KAG_ACTION_HEADS*sizeof(float)));
    unsigned char cpu[A];
    int forced=0;
    for(int r=0;r<rows;r++) {
        KagActionMaskState prefix;
        kag_action_mask_begin(&prefix,&env[map[r]/2].game,&env[map[r]/2].policy,map[r]%2);
        double lp=0;
        for(int h=0;h<KAG_ACTION_HEADS;h++) {
            int start=kag_direct_offset(h), n=widths[h], action=actions[r*KAG_ACTION_HEADS+h], count=0;
            kag_action_mask_before(&prefix,h,cpu);
            double sum=0;
            for(int j=0;j<n;j++) {
                assert(cpu[start+j]==to_float(mask[r*A+start+j]));
                if(cpu[start+j]) { sum+=exp(to_float(logits[r*C+start+j])); count++; }
            }
            assert(cpu[start+action]); forced+=count==1;
            if (greedy) {
                int best = -1;
                for (int j=0;j<n;j++) if (cpu[start+j] && (best<0 ||
                        to_float(logits[r*C+start+j])>to_float(logits[r*C+start+best]))) best=j;
                assert(action==best);
            }
            lp+=to_float(logits[r*C+start+action])-log(sum);
            kag_action_mask_commit(&prefix,h,action);
        }
        close_test(oldlp[r],lp);
    }
    assert(forced>0);
    auto imp=managed<precision_t>(rows), v=managed<precision_t>(rows);
    auto adv=managed<precision_t>(rows), ret=managed<precision_t>(rows);
    auto gradient=managed<float>(rows*A), newlp=managed<float>(rows), losses=managed<float>(LOSS_N);
    auto entropy=managed<float>(1); *entropy=.0015f;
    for(int r=0;r<rows;r++) { adv[r]=from_float(r%2?.75f:-.75f); ret[r]=from_float(1); }
    cache_imp_and_v<<<1,256>>>({.data=logits,.shape={rows,1,C}},actions,oldlp,mask,{},sizes,
        imp,v,gradient,newlp);
    sync_test();
    for(int r=0;r<rows;r++) { assert(newlp[r]==oldlp[r]); assert(to_float(imp[r])==1); }
    PPOKernelArgs args={.grad_logits=gradient,.grad_values_pred=newlp,.logits=logits,
        .values_pred=logits+C-1,.act_sizes=sizes,.action_mask=mask,.num_atns=KAG_ACTION_HEADS,
        .clip_coef=.2,.vf_clip_coef=.2,.vf_coef=2,.ent_coef=entropy,.T_seq=1,.A_total=A,.N=rows};
    PPOGraphArgs data={.imp=imp,.actions=actions,.old_logprobs=oldlp,.advantages=adv,.values=v,.returns=ret};
    ppo_loss_compute<<<1,256>>>(losses,args,data); sync_test();
    close_test(losses[LOSS_IMP],1); assert(losses[LOSS_CLIPFRAC]==0);
    for(int r=0;r<rows;r++) for(int h=0;h<KAG_ACTION_HEADS;h++) {
        int off=kag_direct_offset(h), count=0;
        for(int j=0;j<widths[h];j++) count+=to_float(mask[r*A+off+j])!=0;
        for(int j=0;j<widths[h];j++) if(count==1 || !to_float(mask[r*A+off+j]))
            assert(gradient[r*A+off+j]==0);
    }
    auto kl_grad=managed<float>(rows*A), metrics=managed<float>(2);
    kag_teacher_kl<<<rows*KAG_ACTION_HEADS,256>>>(logits,teacher,mask,kl_grad,metrics,NULL,.2f,rows); sync_test();
    double expected_kl=0;
    for(int r=0;r<rows;r++) for(int h=0;h<KAG_ACTION_HEADS;h++) {
        int off=kag_direct_offset(h), n=widths[h]; double ps=0,qs=0;
        for(int j=0;j<n;j++) if(to_float(mask[r*A+off+j])) {
            ps+=exp(to_float(logits[r*C+off+j])); qs+=exp(to_float(teacher[r*C+off+j]));
        }
        for(int j=0;j<n;j++) {
            double expected=0;
            if(to_float(mask[r*A+off+j])) {
                double p=exp(to_float(logits[r*C+off+j]))/ps, q=exp(to_float(teacher[r*C+off+j]))/qs;
                expected=.2*(p-q)/rows; expected_kl+=q*log(q/p)/rows;
            }
            close_test(kl_grad[r*A+off+j],expected,2e-6);
        }
    }
    close_test(metrics[0],expected_kl); assert(metrics[1]==1);
    cudaMemset(kl_grad,0,rows*A*sizeof(float)); cudaMemset(metrics,0,2*sizeof(float));
    kag_teacher_kl<<<rows*KAG_ACTION_HEADS,256>>>(logits,logits,mask,kl_grad,metrics,NULL,.2f,rows); sync_test();
    for(int i=0;i<rows*A;i++) assert(kl_grad[i]==0);
    assert(metrics[0]==0);
    printf("direct CUDA graphs=%d greedy=%d: CPU/GPU prefix parity, unchanged PPO ratio=1, zero forced gradients, teacher KL oracle PASS\n",graphs,greedy);
}
