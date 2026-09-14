// Fresh entity policy: shared per-entity MLPs -> nonlinear fusion -> MinGRU.
// Separate nonlinear task, market, and value branches share the recurrent state.
#include "entity_contract.h"

struct KagMLPWeights {
    int in, mid, out;
    bool relu_out;
    float output_gain;
    PrecisionTensor w1, w2;
};
struct KagMLPActs {
    PrecisionTensor input_aug, mid, mid_aug, out;
    PrecisionTensor grad_out, grad_mid_aug, grad_mid, grad_input_aug, grad_input;
    PrecisionTensor dw1, dw2;
};

__global__ static void kag_zero_padding(precision_t* w, int rows, int features) {
    int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row < rows) for (int c = features; c < KAG_AUG_WIDTH(features); c++)
        w[row * KAG_AUG_WIDTH(features) + c] = from_float(0.0f);
}

static void kag_mlp_params(KagMLPWeights* w, Allocator* alloc) {
    w->w1 = {.shape = {w->mid, KAG_AUG_WIDTH(w->in)}};
    w->w2 = {.shape = {w->out, KAG_AUG_WIDTH(w->mid)}};
    alloc_register(alloc, &w->w1);
    alloc_register(alloc, &w->w2);
}

static void kag_mlp_init(KagMLPWeights* w, ulong* seed, cudaStream_t stream) {
    puf_kaiming_init(&w->w1, sqrtf(2.0f * KAG_AUG_WIDTH(w->in) / w->in), (*seed)++, stream);
    puf_kaiming_init(&w->w2, w->output_gain * sqrtf((float)KAG_AUG_WIDTH(w->mid) / w->mid), (*seed)++, stream);
    kag_zero_padding<<<grid_size(w->mid), BLOCK_SIZE, 0, stream>>>(w->w1.data, w->mid, w->in);
    kag_zero_padding<<<grid_size(w->out), BLOCK_SIZE, 0, stream>>>(w->w2.data, w->out, w->mid);
}

static void kag_mlp_acts(KagMLPWeights* w, KagMLPActs* a, Allocator* acts,
        Allocator* grads, int rows, bool input_grad) {
    *a = {};
    a->input_aug = {.shape = {rows, KAG_AUG_WIDTH(w->in)}};
    a->mid = {.shape = {rows, w->mid}};
    a->mid_aug = {.shape = {rows, KAG_AUG_WIDTH(w->mid)}};
    a->out = {.shape = {rows, w->out}};
    alloc_register(acts, &a->input_aug);
    alloc_register(acts, &a->mid);
    alloc_register(acts, &a->mid_aug);
    alloc_register(acts, &a->out);
    if (!grads) return;
    a->grad_out = {.shape = {rows, w->out}};
    a->grad_mid_aug = {.shape = {rows, KAG_AUG_WIDTH(w->mid)}};
    a->grad_mid = {.shape = {rows, w->mid}};
    alloc_register(acts, &a->grad_out);
    alloc_register(acts, &a->grad_mid_aug);
    alloc_register(acts, &a->grad_mid);
    if (input_grad) {
        a->grad_input_aug = {.shape = {rows, KAG_AUG_WIDTH(w->in)}};
        a->grad_input = {.shape = {rows, w->in}};
        alloc_register(acts, &a->grad_input_aug);
        alloc_register(acts, &a->grad_input);
    }
    // Parameter and gradient registration order MUST match.
    a->dw1 = {.shape = {w->mid, KAG_AUG_WIDTH(w->in)}};
    a->dw2 = {.shape = {w->out, KAG_AUG_WIDTH(w->mid)}};
    alloc_register(grads, &a->dw1);
    alloc_register(grads, &a->dw2);
}

__global__ static void kag_pack_aug(precision_t* dst, const precision_t* src,
        int rows, int cols, bool relu) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= rows * KAG_AUG_WIDTH(cols)) return;
    int r = idx / KAG_AUG_WIDTH(cols), c = idx % KAG_AUG_WIDTH(cols);
    float x = c == cols ? 1.0f : c > cols ? 0.0f : to_float(src[r * cols + c]);
    dst[idx] = from_float(relu && x < 0.0f ? 0.0f : x);
}

__global__ static void kag_relu(precision_t* values, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n && to_float(values[idx]) < 0.0f) values[idx] = from_float(0.0f);
}

static PrecisionTensor kag_mlp_forward(KagMLPWeights* w, KagMLPActs* a, cudaStream_t stream) {
    int rows = a->out.shape[0];
    puf_mm(&a->input_aug, &w->w1, &a->mid, stream);
    kag_pack_aug<<<grid_size(rows * KAG_AUG_WIDTH(w->mid)), BLOCK_SIZE, 0, stream>>>(
        a->mid_aug.data, a->mid.data, rows, w->mid, true);
    puf_mm(&a->mid_aug, &w->w2, &a->out, stream);
    if (w->relu_out) kag_relu<<<grid_size(rows * w->out), BLOCK_SIZE, 0, stream>>>(a->out.data, rows * w->out);
    return a->out;
}

__global__ static void kag_relu_backward(precision_t* dst, const precision_t* grad,
        const precision_t* output, int n, bool relu) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) dst[idx] = from_float(relu && to_float(output[idx]) <= 0.0f ? 0.0f : to_float(grad[idx]));
}

__global__ static void kag_strip_aug(precision_t* dst, const precision_t* grad,
        const precision_t* output_aug, int rows, int cols, bool relu) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= rows * cols) return;
    int source = (idx / cols) * KAG_AUG_WIDTH(cols) + idx % cols;
    dst[idx] = from_float(relu && to_float(output_aug[source]) <= 0.0f ? 0.0f : to_float(grad[source]));
}

static PrecisionTensor kag_mlp_backward(KagMLPWeights* w, KagMLPActs* a,
        PrecisionTensor grad, cudaStream_t stream) {
    int rows = a->out.shape[0];
    kag_relu_backward<<<grid_size(rows * w->out), BLOCK_SIZE, 0, stream>>>(
        a->grad_out.data, grad.data, a->out.data, rows * w->out, w->relu_out);
    // These GEMMs stay on the caller's stream: no cross-stream cache reuse.
    puf_mm_tn(&a->grad_out, &a->mid_aug, &a->dw2, stream);
    puf_mm_nn(&a->grad_out, &w->w2, &a->grad_mid_aug, stream);
    kag_strip_aug<<<grid_size(rows * w->mid), BLOCK_SIZE, 0, stream>>>(
        a->grad_mid.data, a->grad_mid_aug.data, a->mid_aug.data, rows, w->mid, true);
    puf_mm_tn(&a->grad_mid, &a->input_aug, &a->dw1, stream);
    if (a->grad_input.data) {
        puf_mm_nn(&a->grad_mid, &w->w1, &a->grad_input_aug, stream);
        kag_strip_aug<<<grid_size(rows * w->in), BLOCK_SIZE, 0, stream>>>(
            a->grad_input.data, a->grad_input_aug.data, nullptr, rows, w->in, false);
    }
    return a->grad_input;
}

struct KagEncoderWeights {
    KagMLPWeights entity[4], fusion;
};
struct KagEncoderActs {
    KagMLPActs entity[4], fusion;
};

static void* kag_encoder_weights(void* self) {
    Encoder* enc = (Encoder*)self;
    if (enc->in_dim != KAG_ENTITY_OBS_SIZE || enc->out_dim < 8 || enc->out_dim % 8) {
        fprintf(stderr, "Entity policy v2 requires %d float observations and hidden_size divisible by 8\n", KAG_ENTITY_OBS_SIZE);
        exit(1);
    }
    KagEncoderWeights* w = (KagEncoderWeights*)calloc(1, sizeof(*w));
    w->entity[0] = {KAG_GLOBAL_FEATURES + KAG_TASK_FEATURES, 64, 64, true, sqrtf(2.0f), {}, {}};
    w->entity[1] = {KAG_PRODUCT_FEATURES, 32, 32, true, sqrtf(2.0f), {}, {}};
    w->entity[2] = {KAG_PLOT_FEATURES, 32, 32, true, sqrtf(2.0f), {}, {}};
    w->entity[3] = {KAG_WORKER_FEATURES, 16, 16, true, sqrtf(2.0f), {}, {}};
    w->fusion = {KAG_FUSION_WIDTH, enc->out_dim, enc->out_dim, true, sqrtf(2.0f), {}, {}};
    return w;
}
static void kag_encoder_params(void* weights, Allocator* alloc) {
    KagEncoderWeights* w = (KagEncoderWeights*)weights;
    for (int i = 0; i < 4; i++) kag_mlp_params(&w->entity[i], alloc);
    kag_mlp_params(&w->fusion, alloc);
}
static void kag_encoder_init(void* weights, ulong* seed, cudaStream_t stream) {
    KagEncoderWeights* w = (KagEncoderWeights*)weights;
    for (int i = 0; i < 4; i++) kag_mlp_init(&w->entity[i], seed, stream);
    kag_mlp_init(&w->fusion, seed, stream);
}
static void kag_encoder_acts(void* weights, void* activations, Allocator* acts, Allocator* grads, int rows) {
    KagEncoderWeights* w = (KagEncoderWeights*)weights;
    KagEncoderActs* a = (KagEncoderActs*)activations;
    const int entities[4] = {1, KAG_PRODUCT_COUNT, KAG_PLOT_COUNT, KAG_WORKER_COUNT};
    for (int i = 0; i < 4; i++) kag_mlp_acts(&w->entity[i], &a->entity[i], acts, grads, rows * entities[i], false);
    kag_mlp_acts(&w->fusion, &a->fusion, acts, grads, rows, true);
}
static void kag_encoder_rollout(void* w, void* a, Allocator* alloc, int rows) {
    kag_encoder_acts(w, a, alloc, nullptr, rows);
}

__global__ static void kag_entity_input(precision_t* dst, const precision_t* obs,
        int rows, int features, int entities, int offset, bool global) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= rows * entities * KAG_AUG_WIDTH(features)) return;
    int r = idx / KAG_AUG_WIDTH(features), f = idx % KAG_AUG_WIDTH(features);
    if (f >= features) { dst[idx] = from_float(f == features ? 1.0f : 0.0f); return; }
    int source = global ? (f < KAG_GLOBAL_FEATURES ? f : KAG_TASK_OFFSET + f - KAG_GLOBAL_FEATURES)
        : offset + (r % entities) * features + f;
    dst[idx] = obs[(r / entities) * KAG_ENTITY_OBS_SIZE + source];
}

__global__ static void kag_fuse_entities(precision_t* dst, const precision_t* global,
        const precision_t* product, const precision_t* plot, const precision_t* worker, int rows) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= rows * KAG_AUG_WIDTH(KAG_FUSION_WIDTH)) return;
    int r = idx / KAG_AUG_WIDTH(KAG_FUSION_WIDTH), f = idx % KAG_AUG_WIDTH(KAG_FUSION_WIDTH);
    if (f < 64) dst[idx] = global[r * 64 + f];
    else if (f < 64 + 288) dst[idx] = product[r * 288 + f - 64];
    else if (f < 64 + 288 + 256) dst[idx] = plot[r * 256 + f - 64 - 288];
    else if (f < KAG_FUSION_WIDTH) dst[idx] = worker[r * 272 + f - 64 - 288 - 256];
    else dst[idx] = from_float(f == KAG_FUSION_WIDTH ? 1.0f : 0.0f);
}

static PrecisionTensor kag_encoder_forward(void* weights, void* activations, PrecisionTensor obs, cudaStream_t stream) {
    KagEncoderWeights* w = (KagEncoderWeights*)weights;
    KagEncoderActs* a = (KagEncoderActs*)activations;
    int rows = a->fusion.out.shape[0];
    const int entities[4] = {1, 9, 8, 17};
    const int offsets[4] = {0, KAG_PRODUCT_OFFSET, KAG_PLOT_OFFSET, KAG_WORKER_OFFSET};
    for (int i = 0; i < 4; i++) {
        kag_entity_input<<<grid_size(rows * entities[i] * KAG_AUG_WIDTH(w->entity[i].in)), BLOCK_SIZE, 0, stream>>>(
            a->entity[i].input_aug.data, obs.data, rows, w->entity[i].in, entities[i], offsets[i], i == 0);
        kag_mlp_forward(&w->entity[i], &a->entity[i], stream);
    }
    kag_fuse_entities<<<grid_size(rows * KAG_AUG_WIDTH(KAG_FUSION_WIDTH)), BLOCK_SIZE, 0, stream>>>(
        a->fusion.input_aug.data, a->entity[0].out.data, a->entity[1].out.data,
        a->entity[2].out.data, a->entity[3].out.data, rows);
    return kag_mlp_forward(&w->fusion, &a->fusion, stream);
}

__global__ static void kag_unfuse_gradient(precision_t* dst, const precision_t* fused,
        int rows, int width, int offset) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < rows * width) dst[idx] = fused[(idx / width) * KAG_FUSION_WIDTH + offset + idx % width];
}
static void kag_encoder_backward(void* weights, void* activations, PrecisionTensor grad, cudaStream_t stream) {
    KagEncoderWeights* w = (KagEncoderWeights*)weights;
    KagEncoderActs* a = (KagEncoderActs*)activations;
    PrecisionTensor fused = kag_mlp_backward(&w->fusion, &a->fusion, grad, stream);
    int rows = a->fusion.out.shape[0], offset = 0;
    const int widths[4] = {64, 288, 256, 272};
    for (int i = 0; i < 4; i++) {
        kag_unfuse_gradient<<<grid_size(rows * widths[i]), BLOCK_SIZE, 0, stream>>>(
            a->entity[i].grad_out.data, fused.data, rows, widths[i], offset);
        kag_mlp_backward(&w->entity[i], &a->entity[i], a->entity[i].grad_out, stream);
        offset += widths[i];
    }
}

struct KagDecoderWeights { KagMLPWeights branch[3]; int hidden; };
struct KagDecoderActs { KagMLPActs branch[3]; PrecisionTensor out, grad_input; };

static void* kag_decoder_weights(void* self) {
    Decoder* dec = (Decoder*)self;
    if (dec->continuous || dec->output_dim != KAG_ALL_LOGITS || dec->hidden_dim < 8 || dec->hidden_dim % 8) {
        fprintf(stderr, "Invalid decoder dimensions for Kaggriculture entity policy v2\n"); exit(1);
    }
    KagDecoderWeights* w = (KagDecoderWeights*)calloc(1, sizeof(*w));
    w->hidden = dec->hidden_dim;
    const int outputs[3] = {KAG_TASK_LOGITS, KAG_MARKET_LOGITS, 1};
    for (int i = 0; i < 3; i++) {
        w->branch[i] = {w->hidden, w->hidden / 2, outputs[i], false, i == 2 ? 1.0f : 0.01f, {}, {}};
    }
    return w;
}
static void kag_decoder_params(void* weights, Allocator* alloc) {
    KagDecoderWeights* w = (KagDecoderWeights*)weights;
    for (int i = 0; i < 3; i++) kag_mlp_params(&w->branch[i], alloc);
}
static void kag_decoder_init(void* weights, ulong* seed, cudaStream_t stream) {
    KagDecoderWeights* w = (KagDecoderWeights*)weights;
    for (int i = 0; i < 3; i++) kag_mlp_init(&w->branch[i], seed, stream);
}
static void kag_decoder_acts(void* weights, void* activations, Allocator* acts, Allocator* grads, int rows) {
    KagDecoderWeights* w = (KagDecoderWeights*)weights;
    KagDecoderActs* a = (KagDecoderActs*)activations;
    for (int i = 0; i < 3; i++) kag_mlp_acts(&w->branch[i], &a->branch[i], acts, grads, rows, true);
    a->out = {.shape = {rows, KAG_ALL_LOGITS + 1}};
    alloc_register(acts, &a->out);
    if (grads) {
        a->grad_input = {.shape = {rows, w->hidden}};
        alloc_register(acts, &a->grad_input);
    }
}
static void kag_decoder_rollout(void* w, void* a, Allocator* alloc, int rows) {
    kag_decoder_acts(w, a, alloc, nullptr, rows);
}

__global__ static void kag_merge_branches(precision_t* dst, const precision_t* tasks,
        const precision_t* market, const precision_t* value, int rows) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= rows * (KAG_ALL_LOGITS + 1)) return;
    int r = idx / (KAG_ALL_LOGITS + 1), c = idx % (KAG_ALL_LOGITS + 1);
    dst[idx] = c < KAG_TASK_LOGITS ? tasks[r * KAG_TASK_LOGITS + c]
        : c < KAG_ALL_LOGITS ? market[r * KAG_MARKET_LOGITS + c - KAG_TASK_LOGITS] : value[r];
}
static PrecisionTensor kag_decoder_forward(void* weights, void* activations, PrecisionTensor input, cudaStream_t stream) {
    KagDecoderWeights* w = (KagDecoderWeights*)weights;
    KagDecoderActs* a = (KagDecoderActs*)activations;
    int rows = a->out.shape[0];
    for (int i = 0; i < 3; i++) {
        kag_pack_aug<<<grid_size(rows * KAG_AUG_WIDTH(w->hidden)), BLOCK_SIZE, 0, stream>>>(
            a->branch[i].input_aug.data, input.data, rows, w->hidden, false);
        kag_mlp_forward(&w->branch[i], &a->branch[i], stream);
    }
    kag_merge_branches<<<grid_size(rows * (KAG_ALL_LOGITS + 1)), BLOCK_SIZE, 0, stream>>>(
        a->out.data, a->branch[0].out.data, a->branch[1].out.data, a->branch[2].out.data, rows);
    return a->out;
}

__global__ static void kag_branch_gradient(precision_t* dst, const float* logits,
        const float* value, int rows, int width, int offset, bool critic) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < rows * width) dst[idx] = from_float(critic ? value[idx] : logits[(idx / width) * KAG_ALL_LOGITS + offset + idx % width]);
}
__global__ static void kag_sum_branch_gradients(precision_t* dst, const precision_t* a,
        const precision_t* b, const precision_t* c, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) dst[idx] = from_float(to_float(a[idx]) + to_float(b[idx]) + to_float(c[idx]));
}
static PrecisionTensor kag_decoder_backward(void* weights, void* activations,
        FloatTensor logits, FloatTensor logstd, FloatTensor value, cudaStream_t stream) {
    (void)logstd;
    KagDecoderWeights* w = (KagDecoderWeights*)weights;
    KagDecoderActs* a = (KagDecoderActs*)activations;
    int rows = a->out.shape[0], offset = 0;
    for (int i = 0; i < 3; i++) {
        kag_branch_gradient<<<grid_size(rows * w->branch[i].out), BLOCK_SIZE, 0, stream>>>(
            a->branch[i].grad_out.data, logits.data, value.data, rows, w->branch[i].out, offset, i == 2);
        kag_mlp_backward(&w->branch[i], &a->branch[i], a->branch[i].grad_out, stream);
        offset += w->branch[i].out;
    }
    kag_sum_branch_gradients<<<grid_size(rows * w->hidden), BLOCK_SIZE, 0, stream>>>(
        a->grad_input.data, a->branch[0].grad_input.data, a->branch[1].grad_input.data,
        a->branch[2].grad_input.data, rows * w->hidden);
    return a->grad_input;
}

static void create_kaggriculture_encoder(Encoder* enc) {
    enc->forward = kag_encoder_forward;
    enc->backward = kag_encoder_backward;
    enc->reg_train = kag_encoder_acts;
    enc->reg_rollout = kag_encoder_rollout;
    enc->reg_params = kag_encoder_params;
    enc->init_weights = kag_encoder_init;
    enc->create_weights = kag_encoder_weights;
    enc->activation_size = sizeof(KagEncoderActs);
}
static void create_kaggriculture_decoder(Decoder* dec) {
    dec->forward = kag_decoder_forward;
    dec->backward = kag_decoder_backward;
    dec->reg_train = kag_decoder_acts;
    dec->reg_rollout = kag_decoder_rollout;
    dec->reg_params = kag_decoder_params;
    dec->init_weights = kag_decoder_init;
    dec->create_weights = kag_decoder_weights;
    dec->activation_size = sizeof(KagDecoderActs);
}
