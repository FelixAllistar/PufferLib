// Fixed teacher KL for chronological, single-pass PufferNet PPO.
// The teacher carries ITS OWN recurrent state; learner states are never reused.
struct KagDirectTeacher {
    float coefficient;
    Arch arch;
    Weights weights;
    Activations activations;
    Allocator params_alloc, acts_alloc, grads_alloc;
    Prec parameters, state;
    Float master, metrics;
};

// Each block owns one (transition, head). KL and gradients share exactly the
// stored rollout support, including singleton forced/inactive heads.
__global__ void kag_teacher_kl(const precision_t* learner, const precision_t* teacher,
        const precision_t* mask, float* gradient, float* metrics,
        float* losses, float coefficient, int rows) {
    int row = blockIdx.x / KAG_ACTION_HEADS, h = blockIdx.x % KAG_ACTION_HEADS;
    int off = kag_direct_offset(h), width = kag_direct_width(h), tid = threadIdx.x;
    int base = row*(KAG_ALL_LOGITS+1)+off, mb = row*KAG_ALL_LOGITS+off;
    __shared__ float pm[256], qm[256];
    float pmax = -INFINITY, qmax = -INFINITY;
    for (int j = tid; j < width; j += 256) if (to_float(mask[mb+j]) != 0) {
        pmax = fmaxf(pmax,to_float(learner[base+j]));
        qmax = fmaxf(qmax,to_float(teacher[base+j]));
    }
    pm[tid] = pmax; qm[tid] = qmax; __syncthreads();
    for (int stride = 128; stride; stride /= 2) {
        if (tid < stride) { pm[tid] = fmaxf(pm[tid],pm[tid+stride]); qm[tid] = fmaxf(qm[tid],qm[tid+stride]); }
        __syncthreads();
    }
    pmax = pm[0]; qmax = qm[0]; __syncthreads();
    float psum = 0, qsum = 0;
    for (int j = tid; j < width; j += 256) if (to_float(mask[mb+j]) != 0) {
        psum += expf(to_float(learner[base+j])-pmax);
        qsum += expf(to_float(teacher[base+j])-qmax);
    }
    pm[tid] = psum; qm[tid] = qsum; __syncthreads();
    for (int stride = 128; stride; stride /= 2) {
        if (tid < stride) { pm[tid] += pm[tid+stride]; qm[tid] += qm[tid+stride]; }
        __syncthreads();
    }
    float plse = pmax+logf(pm[0]), qlse = qmax+logf(qm[0]);
    float kl = 0;
    for (int j = tid; j < width; j += 256) if (to_float(mask[mb+j]) != 0) {
        float lp = to_float(learner[base+j])-plse, lq = to_float(teacher[base+j])-qlse;
        float q = expf(lq);
        gradient[mb+j] += coefficient*(expf(lp)-q)/rows;
        kl += q*(lq-lp)/rows;
    }
    __syncthreads(); pm[tid] = kl; __syncthreads();
    for (int stride = 128; stride; stride /= 2) {
        if (tid < stride) pm[tid] += pm[tid+stride];
        __syncthreads();
    }
    if (tid == 0) {
        atomicAdd(metrics,pm[0]);
        if (losses) atomicAdd(losses+LOSS_TOTAL,coefficient*pm[0]);
        if (blockIdx.x == 0) atomicAdd(metrics+1,1.0f);
    }
}

void kag_teacher_create(KagDirectTeacher* t, const Arch* arch, int rows,
        int agents, int layers, int hidden, float coefficient, cudaStream_t stream) {
    t->coefficient = coefficient;
    if (!coefficient) return;
    t->arch = *arch;
    t->weights = weights_create(&t->arch,&t->params_alloc);
    t->activations = arch_reg_train(&t->arch,t->weights,&t->acts_alloc,&t->grads_alloc,rows);
    t->state = {.shape={layers,agents,hidden}};
    t->metrics = {.shape={2}};
    alloc_register(&t->acts_alloc,&t->state);
    alloc_register(&t->acts_alloc,&t->metrics);
    alloc_create(&t->params_alloc); alloc_create(&t->acts_alloc); alloc_create(&t->grads_alloc);
    t->parameters = {.data=(precision_t*)t->params_alloc.mem,.shape={t->params_alloc.total_elems}};
    t->master = {.shape={t->params_alloc.total_elems}};
    if (USE_BF16) assert(cudaMalloc((void**)&t->master.data,numel(t->master.shape)*sizeof(float)) == cudaSuccess);
    else t->master.data = (float*)t->parameters.data;
    cudaMemsetAsync(t->state.data,0,numel(t->state.shape)*sizeof(precision_t),stream);
    cudaMemsetAsync(t->metrics.data,0,2*sizeof(float),stream);
}

void kag_teacher_regularize(KagDirectTeacher* t, Prec obs, Prec terminals, int agent_off,
        Prec learner, Prec mask, Float gradient, float* losses, cudaStream_t stream) {
    if (!t->coefficient) return;
    int batch = obs.shape[0], horizon = obs.shape[1];
    Prec x = *puf_squeeze(&obs,0);
    Prec encoded = t->arch.encoder.forward(t->weights.encoder,t->activations.encoder,x,stream);
    Prec hidden = t->arch.network.forward_train(t->weights.network,
        *puf_unsqueeze(&encoded,0,batch,horizon),t->state,terminals,
        t->activations.network,agent_off,stream);
    // forward_train saves the final states, but does not itself commit them.
    MinGRUActivations* a = (MinGRUActivations*)t->activations.network;
    for (int l = 0; l < t->arch.network.num_layers; l++) {
        Prec dst = mingru_state_layer(t->state,l,agent_off,batch);
        puf_copy(&dst,&a->scan_bufs[l].next_state,stream);
    }
    if (t->arch.decoder.bind_observation) t->arch.decoder.bind_observation(t->activations.decoder,obs);
    Prec reference = t->arch.decoder.forward(t->weights.decoder,t->activations.decoder,
        *puf_squeeze(&hidden,0),stream);
    kag_teacher_kl<<<batch*horizon*KAG_ACTION_HEADS,256,0,stream>>>(learner.data,reference.data,
        mask.data,gradient.data,t->metrics.data,losses,t->coefficient,batch*horizon);
}
