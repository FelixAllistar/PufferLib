// Native token-wise Transformer adapter. The existing decoder, masked sampler,
// PPO loss, Muon optimizer, snapshots and checkpoint writer are shared unchanged.
#pragma once
#include "native_transformer.cuh"

struct TransformerWeights {
    puf_transformer::Config config;
    Prec* parameters;
    int count;
};

struct TransformerActivations {
    puf_transformer::Model<precision_t>* model;
    Prec pooled;
    Prec* gradients;
    Float* fp32_gradients;
};

static Prec transformer_shape(puf_transformer::ParameterSpec s) {
    return s.rows == 1 ? Prec{.shape = {s.cols}}
                       : Prec{.shape = {s.rows, s.cols}};
}

static void* transformer_create_weights(void* self) {
    Network* n = (Network*)self;
    auto* w = new TransformerWeights{};
    w->config = {1, n->tokens, n->hidden, n->num_layers, n->heads, n->ff};
    w->config.validate();
    w->count = 12 * n->num_layers + 2;
    w->parameters = new Prec[w->count]{};
    return w;
}

static void transformer_reg_params(void* weights, Allocator* alloc) {
    auto* w = (TransformerWeights*)weights;
    auto specs = puf_transformer::parameter_specs(w->config);
    for (int i = 0; i < w->count; ++i) {
        w->parameters[i] = transformer_shape(specs[i]);
        alloc_register(alloc, &w->parameters[i]);
    }
}

static void transformer_init_weights(void* weights, ulong* seed, cudaStream_t stream) {
    auto* w = (TransformerWeights*)weights;
    std::mt19937 rng((*seed)++);
    auto specs = puf_transformer::parameter_specs(w->config);
    for (int i = 0; i < w->count; ++i) {
        auto s = specs[i];
        std::uniform_real_distribution<float> distribution(
            -sqrtf(6.f / (s.rows + s.cols)), sqrtf(6.f / (s.rows + s.cols)));
        std::vector<precision_t> values(size_t(s.rows) * s.cols);
        for (auto& value : values)
            value = from_float(s.initialization == 1 ? 1.f : s.initialization == 2 ? 0.f : distribution(rng));
        puf_transformer::check(cudaMemcpyAsync(w->parameters[i].data, values.data(),
            values.size() * sizeof(precision_t), cudaMemcpyHostToDevice, stream), "initialize policy");
        puf_transformer::check(cudaStreamSynchronize(stream), "finish policy initialization");
    }
}

static void transformer_reg_rollout(void* weights, void* activations, Allocator* acts, int batch) {
    auto* w = (TransformerWeights*)weights;
    auto* a = (TransformerActivations*)activations;
    auto config = w->config;
    config.batch = batch;
    a->model = new puf_transformer::Model<precision_t>(config, 17, false);
    a->pooled = {.shape = {batch, config.width}};
    alloc_register(acts, &a->pooled);
}

static void transformer_reg_train(void* weights, void* activations,
        Allocator* acts, Allocator* grads, int batch) {
    auto* w = (TransformerWeights*)weights;
    auto* a = (TransformerActivations*)activations;
    transformer_reg_rollout(weights, activations, acts, batch);
    a->gradients = new Prec[w->count]{};
    a->fp32_gradients = new Float[w->count]{};
    auto specs = puf_transformer::parameter_specs(w->config);
    for (int i = 0; i < w->count; ++i) {
        a->gradients[i] = transformer_shape(specs[i]);
        alloc_register(grads, &a->gradients[i]);
        if (USE_BF16) {
            a->fp32_gradients[i] = {.shape = {specs[i].rows * specs[i].cols}};
            alloc_register(acts, &a->fp32_gradients[i]);
        }
    }
}

// Pool the global token, not a timestep. Each observation gets its own attention
// matrix. PPO's B,T dimensions are folded into batch; there is no future leakage.
__global__ static void transformer_pool(const precision_t* tokens, precision_t* pooled,
        int batch, int count, int width) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < batch * width) pooled[i] = tokens[(i / width * count + count - 1) * width + i % width];
}
__global__ static void transformer_unpool(const precision_t* pooled, precision_t* tokens,
        int batch, int count, int width) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < batch * count * width)
        tokens[i] = (i / width % count == count - 1)
            ? pooled[i / (count * width) * width + i % width] : from_float(0.f);
}

static Prec transformer_forward(void* weights, Prec x, Prec state,
        void* activations, cudaStream_t stream) {
    auto* w = (TransformerWeights*)weights;
    auto* a = (TransformerActivations*)activations;
    auto& m = *a->model;
    auto params = m.parameters();
    m.set_stream(stream);
    for (int i = 0; i < w->count; ++i) {
        size_t count = numel(w->parameters[i].shape);
        params[i]->value.borrow(w->parameters[i].data, count);
        if (a->gradients) {
            float* grad = USE_BF16 ? a->fp32_gradients[i].data : (float*)a->gradients[i].data;
            params[i]->gradient.borrow(grad, count);
        }
    }
    assert(numel(x.shape) == (int64_t)m.input.count);
    puf_transformer::check(cudaMemcpyAsync(m.input.data, x.data,
        m.input.count * sizeof(precision_t), cudaMemcpyDeviceToDevice, stream), "copy tokens");
    m.forward();
    auto c = m.config;
    transformer_pool<<<grid_size(c.batch * c.width), BLOCK_SIZE, 0, stream>>>(
        m.output.data, a->pooled.data, c.batch, c.tokens, c.width);
    return a->pooled;
}

static Prec transformer_forward_train(void* weights, Prec x, Prec state, Prec terminals,
        void* activations, int agent_off, cudaStream_t stream) {
    Prec pooled = transformer_forward(weights, x, state, activations, stream);
    return *puf_unsqueeze(&pooled, 0, x.shape[0], x.shape[1]);
}

static Prec transformer_backward(void* weights, Prec grad, void* activations, cudaStream_t stream) {
    auto* w = (TransformerWeights*)weights;
    auto* a = (TransformerActivations*)activations;
    auto& m = *a->model;
    auto c = m.config;
    m.set_stream(stream);
    transformer_unpool<<<grid_size(c.batch * c.tokens * c.width), BLOCK_SIZE, 0, stream>>>(
        grad.data, m.upstream.data, c.batch, c.tokens, c.width);
    m.backward();
    if (USE_BF16) for (int i = 0; i < w->count; ++i) {
        int count = numel(w->parameters[i].shape);
        cast<<<grid_size(count), BLOCK_SIZE, 0, stream>>>(
            a->gradients[i].data, a->fp32_gradients[i].data, count);
    }
    return {.data = m.state_gradient.data, .shape = {c.batch, c.tokens, c.width}};
}

#ifdef GS_O_HANDS
#include "../ocean/goofspiel/transformer_features.cuh"

static void gs_tokens_reg_rollout(void* weights, void* activations, Allocator* acts, int batch) {
    auto* w = (EncoderWeights*)weights;
    auto* a = (EncoderActivations*)activations;
    encoder_reg_rollout(weights, activations, acts, batch * GS_TRANSFORMER_TOKENS);
    a->saved_input = {.shape = {batch * GS_TRANSFORMER_TOKENS, w->in_dim}};
    alloc_register(acts, &a->saved_input);
}
static void gs_tokens_reg_train(void* weights, void* activations,
        Allocator* acts, Allocator* grads, int batch) {
    encoder_reg_train(weights, activations, acts, grads, batch * GS_TRANSFORMER_TOKENS);
}
static Prec gs_tokens_forward(void* weights, void* activations, Prec input, cudaStream_t stream) {
    auto* w = (EncoderWeights*)weights;
    auto* a = (EncoderActivations*)activations;
    int batch = input.shape[0];
    int count = batch * GS_TRANSFORMER_TOKENS * GS_TRANSFORMER_FEATURES;
    gs_transformer_tokenize<<<grid_size(count), BLOCK_SIZE, 0, stream>>>(input.data, a->saved_input.data, batch);
    puf_mm(&a->saved_input, &w->weight, &a->out, stream);
    return {.data = a->out.data, .shape = {batch, GS_TRANSFORMER_TOKENS, w->out_dim}};
}
#endif
