// Kaggriculture semantic-byte encoder. C emits normalized categorical flags,
// lifecycle summaries, economy fields, and egocentric worker routes as bytes.
// The generic byte->precision transfer normalizes once before this function,
// so every policy bank can use the same input without a scale kernel.

struct KaggricultureEncoderActivations {
    PrecisionTensor out, saved_input, wgrad_scratch;
};

static PrecisionTensor kaggriculture_encoder_forward(void* w, void* activations,
        PrecisionTensor input, cudaStream_t stream) {
    EncoderWeights* ew = (EncoderWeights*)w;
    KaggricultureEncoderActivations* a =
        (KaggricultureEncoderActivations*)activations;
    if (a->saved_input.data) {
        puf_copy(&a->saved_input, &input, stream);
    }
    puf_mm(&input, &ew->weight, &a->out, stream);
    return a->out;
}

static void kaggriculture_encoder_backward(void* w, void* activations,
        PrecisionTensor grad, cudaStream_t stream) {
    (void)w;
    KaggricultureEncoderActivations* a =
        (KaggricultureEncoderActivations*)activations;
    puf_mm_tn_async_after(&grad, &a->saved_input, &a->wgrad_scratch, stream);
}

static void kaggriculture_encoder_reg_train(void* w, void* activations,
        Allocator* acts, Allocator* grads, int B_TT) {
    EncoderWeights* ew = (EncoderWeights*)w;
    KaggricultureEncoderActivations* a =
        (KaggricultureEncoderActivations*)activations;
    *a = (KaggricultureEncoderActivations){
        .out = {.shape = {B_TT, ew->out_dim}},
        .saved_input = {.shape = {B_TT, ew->in_dim}},
        .wgrad_scratch = {.shape = {ew->out_dim, ew->in_dim}},
    };
    alloc_register(acts, &a->out);
    alloc_register(acts, &a->saved_input);
    alloc_register(grads, &a->wgrad_scratch);
}

static void kaggriculture_encoder_reg_rollout(void* w, void* activations,
        Allocator* alloc, int B) {
    EncoderWeights* ew = (EncoderWeights*)w;
    KaggricultureEncoderActivations* a =
        (KaggricultureEncoderActivations*)activations;
    *a = (KaggricultureEncoderActivations){
        .out = {.shape = {B, ew->out_dim}},
    };
    alloc_register(alloc, &a->out);
}

static void create_kaggriculture_encoder(Encoder* enc) {
    enc->forward = kaggriculture_encoder_forward;
    enc->backward = kaggriculture_encoder_backward;
    enc->reg_train = kaggriculture_encoder_reg_train;
    enc->reg_rollout = kaggriculture_encoder_reg_rollout;
    enc->activation_size = sizeof(KaggricultureEncoderActivations);
}
