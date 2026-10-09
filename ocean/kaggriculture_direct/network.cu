// PufferNet/MinGRU, with a single actor-input projection. No attention,
// entity MLPs, pooling, task fusion, or route features run in this encoder.
// Keep the already-qualified task/market/paired-value decoder implementation.
#define create_kaggriculture_encoder create_unused_legacy_encoder
#include "../kaggriculture/network.cu"
#undef create_kaggriculture_encoder

struct KagDirectEncoderActs {
    EncoderActivations base;
    Prec actor_input;
};

void* kag_direct_encoder_weights(void* self) {
    Encoder copy = *(Encoder*)self;
    assert(copy.in_dim == KAG_TRAIN_OBS_SIZE);
    copy.in_dim = KAG_ENTITY_OBS_SIZE;
    return encoder_create_weights(&copy);
}

void kag_direct_encoder_train(void* w, void* activations, Allocator* acts,
        Allocator* grads, int rows) {
    KagDirectEncoderActs* a = (KagDirectEncoderActs*)activations;
    encoder_reg_train(w, &a->base, acts, grads, rows);
    a->actor_input = {.shape = {rows, KAG_ENTITY_OBS_SIZE}};
    alloc_register(acts, &a->actor_input);
}

void kag_direct_encoder_rollout(void* w, void* activations, Allocator* acts, int rows) {
    KagDirectEncoderActs* a = (KagDirectEncoderActs*)activations;
    encoder_reg_rollout(w, &a->base, acts, rows);
    a->actor_input = {.shape = {rows, KAG_ENTITY_OBS_SIZE}};
    alloc_register(acts, &a->actor_input);
}

__global__ void kag_direct_actor_input(precision_t* dst, const precision_t* src, int n, int stride) {
    int i = blockIdx.x*blockDim.x + threadIdx.x;
    if (i < n) dst[i] = src[(i/KAG_ENTITY_OBS_SIZE)*stride + i%KAG_ENTITY_OBS_SIZE];
}

Prec kag_direct_encoder_forward(void* w, void* activations, Prec input, cudaStream_t stream) {
    KagDirectEncoderActs* a = (KagDirectEncoderActs*)activations;
    int n = numel(a->actor_input.shape);
    kag_direct_actor_input<<<grid_size(n), BLOCK_SIZE, 0, stream>>>(
        a->actor_input.data, input.data, n, input.shape[ndim(input.shape)-1]);
    return encoder_forward(w, &a->base, a->actor_input, stream);
}

void kag_direct_encoder_backward(void* w, void* activations, Prec grad, cudaStream_t stream) {
    encoder_backward(w, &((KagDirectEncoderActs*)activations)->base, grad, stream);
}

void create_kaggriculture_encoder(Encoder* enc) {
    enc->forward = kag_direct_encoder_forward;
    enc->backward = kag_direct_encoder_backward;
    enc->create_weights = kag_direct_encoder_weights;
    enc->reg_train = kag_direct_encoder_train;
    enc->reg_rollout = kag_direct_encoder_rollout;
    enc->activation_size = sizeof(KagDirectEncoderActs);
    // Stock encoder init and parameter registration are retained.
}
