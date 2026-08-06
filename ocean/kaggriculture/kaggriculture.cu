// Kaggriculture semantic-byte encoder. C emits normalized categorical flags,
// lifecycle summaries, economy fields, and egocentric worker routes as bytes;
// scale once here before the tensor-core input projection.

struct KaggricultureEncoderActivations {
    PrecisionTensor out, scaled_input, wgrad_scratch;
};

__global__ void kaggriculture_scale_bytes(precision_t* dst,
        const precision_t* src, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) dst[idx] = from_float(to_float(src[idx]) * (1.0f / 255.0f));
}

static PrecisionTensor kaggriculture_encoder_forward(void* w, void* activations,
        PrecisionTensor input, cudaStream_t stream) {
    EncoderWeights* ew = (EncoderWeights*)w;
    KaggricultureEncoderActivations* a =
        (KaggricultureEncoderActivations*)activations;
    int n = (int)numel(input.shape);
    kaggriculture_scale_bytes<<<grid_size(n), BLOCK_SIZE, 0, stream>>>(
        a->scaled_input.data, input.data, n);
    puf_mm(&a->scaled_input, &ew->weight, &a->out, stream);
    return a->out;
}

static void kaggriculture_encoder_backward(void* w, void* activations,
        PrecisionTensor grad, cudaStream_t stream) {
    (void)w;
    KaggricultureEncoderActivations* a =
        (KaggricultureEncoderActivations*)activations;
    puf_mm_tn_async_after(&grad, &a->scaled_input, &a->wgrad_scratch, stream);
}

static void kaggriculture_encoder_reg_train(void* w, void* activations,
        Allocator* acts, Allocator* grads, int B_TT) {
    EncoderWeights* ew = (EncoderWeights*)w;
    KaggricultureEncoderActivations* a =
        (KaggricultureEncoderActivations*)activations;
    *a = (KaggricultureEncoderActivations){
        .out = {.shape = {B_TT, ew->out_dim}},
        .scaled_input = {.shape = {B_TT, ew->in_dim}},
        .wgrad_scratch = {.shape = {ew->out_dim, ew->in_dim}},
    };
    alloc_register(acts, &a->out);
    alloc_register(acts, &a->scaled_input);
    alloc_register(grads, &a->wgrad_scratch);
}

static void kaggriculture_encoder_reg_rollout(void* w, void* activations,
        Allocator* alloc, int B) {
    EncoderWeights* ew = (EncoderWeights*)w;
    KaggricultureEncoderActivations* a =
        (KaggricultureEncoderActivations*)activations;
    *a = (KaggricultureEncoderActivations){
        .out = {.shape = {B, ew->out_dim}},
        .scaled_input = {.shape = {B, ew->in_dim}},
    };
    alloc_register(alloc, &a->out);
    alloc_register(alloc, &a->scaled_input);
}

static void create_kaggriculture_encoder(Encoder* enc) {
    enc->forward = kaggriculture_encoder_forward;
    enc->backward = kaggriculture_encoder_backward;
    enc->reg_train = kaggriculture_encoder_reg_train;
    enc->reg_rollout = kaggriculture_encoder_reg_rollout;
    enc->activation_size = sizeof(KaggricultureEncoderActivations);
}
