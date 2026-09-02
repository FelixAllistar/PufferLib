// Float32 forward and numerical-gradient checks for the shenaniguns3d encoder.
// Build through build.sh with --encoder-test so the test uses the same native
// encoder implementation and allocator layout as the trainer.

#include <cuda_runtime.h>
#include <cuda_bf16.h>
#include <cublas_v2.h>
#include <curand.h>
#include <curand_kernel.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define PRECISION_FLOAT
#define ENV_HEADER "ocean/shenaniguns3d/shenaniguns3d.h"
#ifndef PUFFER_SHENANIGUNS3D
#define PUFFER_SHENANIGUNS3D
#endif
#define PUFFER_ENV_NAME "shenaniguns3d"

#include "../src/pufferl_preamble.h"
#include "../src/algo.cu"

static void check_cuda(cudaError_t err, const char* what) {
    if (err != cudaSuccess) {
        fprintf(stderr, "%s: %s\n", what, cudaGetErrorString(err));
        exit(1);
    }
}

static float test_value(int index, float scale) {
    return scale * sinf((float)(index * 17 + 3) * 0.071f);
}

static void fill_host(float* dst, int n, float scale, float bias) {
    for (int i = 0; i < n; i++) dst[i] = bias + test_value(i, scale);
}

static void copy_to_device(PrecisionTensor tensor, const float* src) {
    check_cuda(cudaMemcpy(tensor.data, src,
        (size_t)numel(tensor.shape) * sizeof(float), cudaMemcpyHostToDevice),
        "copy parameter");
}

static void copy_to_host(float* dst, PrecisionTensor tensor) {
    check_cuda(cudaMemcpy(dst, tensor.data,
        (size_t)numel(tensor.shape) * sizeof(float), cudaMemcpyDeviceToHost),
        "copy tensor");
}

static void copy_float_to_host(float* dst, FloatTensor tensor) {
    check_cuda(cudaMemcpy(dst, tensor.data,
        (size_t)numel(tensor.shape) * sizeof(float), cudaMemcpyDeviceToHost),
        "copy float tensor");
}

static void cpu_encoder(const float* input, const float* scalar1_w,
        const float* scalar1_b, const float* scalar2_w, const float* scalar2_b,
        const float* depth_w, const float* depth_b, const float* occupancy_w,
        const float* occupancy_b, const float* proj_w, const float* proj_b,
        int batch, int hidden, float* output) {
    constexpr int scalar_hidden = 32;
    constexpr int depth_channels = 8;
    constexpr int depth_kernel = 3;
    constexpr int depth_height = DEPTH_MAP_HEIGHT - depth_kernel + 1;
    constexpr int depth_features = depth_channels * depth_height * DEPTH_MAP_WIDTH;
    constexpr int occupancy_channels = 4;
    constexpr int occupancy_k = 2;
    constexpr int occupancy_vertical = OCCUPANCY_VERTICAL_BINS - occupancy_k + 1;
    constexpr int occupancy_lateral = OCCUPANCY_LATERAL_BINS - occupancy_k + 1;
    constexpr int occupancy_forward = OCCUPANCY_FORWARD_BINS - occupancy_k + 1;
    constexpr int occupancy_features = occupancy_channels * occupancy_vertical *
        occupancy_lateral * occupancy_forward;
    constexpr int concat_features = scalar_hidden + depth_features + occupancy_features;

    float scalar1[scalar_hidden];
    float scalar[scalar_hidden];
    float depth[depth_features];
    float occupancy[occupancy_features];
    float concat[concat_features];

    for (int b = 0; b < batch; b++) {
        const float* in = input + b * OBS_SIZE;
        for (int o = 0; o < scalar_hidden; o++) {
            float sum = scalar1_b[o];
            for (int i = 0; i < OBS_SCALAR_SIZE; i++)
                sum += in[i] * scalar1_w[o * OBS_SCALAR_SIZE + i];
            scalar1[o] = fmaxf(0.0f, sum);
        }
        for (int o = 0; o < scalar_hidden; o++) {
            float sum = scalar2_b[o];
            for (int i = 0; i < scalar_hidden; i++)
                sum += scalar1[i] * scalar2_w[o * scalar_hidden + i];
            scalar[o] = fmaxf(0.0f, sum);
        }

        for (int oc = 0; oc < depth_channels; oc++) {
            for (int oh = 0; oh < depth_height; oh++) {
                for (int ow = 0; ow < DEPTH_MAP_WIDTH; ow++) {
                    float sum = depth_b[oc];
                    for (int kh = 0; kh < depth_kernel; kh++) {
                        for (int kw = 0; kw < depth_kernel; kw++) {
                            int iw = (ow + kw - depth_kernel / 2) % DEPTH_MAP_WIDTH;
                            if (iw < 0) iw += DEPTH_MAP_WIDTH;
                            sum += in[DEPTH_MAP_OFFSET + (oh + kh) * DEPTH_MAP_WIDTH + iw] *
                                depth_w[(oc * depth_kernel + kh) * depth_kernel + kw];
                        }
                    }
                    int index = (oc * depth_height + oh) * DEPTH_MAP_WIDTH + ow;
                    depth[index] = fmaxf(0.0f, sum);
                }
            }
        }

        for (int oc = 0; oc < occupancy_channels; oc++) {
            for (int ov = 0; ov < occupancy_vertical; ov++) {
                for (int ol = 0; ol < occupancy_lateral; ol++) {
                    for (int of = 0; of < occupancy_forward; of++) {
                        float sum = occupancy_b[oc];
                        for (int kv = 0; kv < occupancy_k; kv++) {
                            for (int kl = 0; kl < occupancy_k; kl++) {
                                for (int kf = 0; kf < occupancy_k; kf++) {
                                    int in_index = ((ov + kv) * OCCUPANCY_LATERAL_BINS +
                                        ol + kl) * OCCUPANCY_FORWARD_BINS + of + kf;
                                    int weight_index = (kv * occupancy_k + kl) * occupancy_k + kf;
                                    sum += in[OCCUPANCY_OFFSET + in_index] *
                                        occupancy_w[oc * 8 + weight_index];
                                }
                            }
                        }
                        int index = (((oc * occupancy_vertical + ov) * occupancy_lateral + ol) *
                            occupancy_forward + of);
                        occupancy[index] = fmaxf(0.0f, sum);
                    }
                }
            }
        }

        memcpy(concat, scalar, scalar_hidden * sizeof(float));
        memcpy(concat + scalar_hidden, depth, depth_features * sizeof(float));
        memcpy(concat + scalar_hidden + depth_features, occupancy,
            occupancy_features * sizeof(float));
        for (int o = 0; o < hidden; o++) {
            float sum = proj_b[o];
            for (int c = 0; c < concat_features; c++)
                sum += concat[c] * proj_w[o * concat_features + c];
            output[b * hidden + o] = fmaxf(0.0f, sum);
        }
    }
}

struct TestState {
    Encoder encoder;
    S3DEncoderWeights* weights;
    S3DEncoderActivations* train_acts;
    Allocator params;
    Allocator acts;
    Allocator grads;
    PrecisionTensor params_flat;
    PrecisionTensor input;
    PrecisionTensor output_grad;
    float* host_input;
    float* host_output_grad;
    int batch;
    int hidden;
};

static void init_test(TestState* test) {
    memset(test, 0, sizeof(*test));
    test->batch = 3;
    test->hidden = 16;
    test->encoder.in_dim = OBS_SIZE;
    test->encoder.out_dim = test->hidden;
    create_shenaniguns3d_encoder(&test->encoder);
    test->weights = (S3DEncoderWeights*)test->encoder.create_weights(&test->encoder);
    test->encoder.reg_params(test->weights, &test->params);
    check_cuda(alloc_create(&test->params), "allocate parameters");
    test->params_flat = {
        (precision_t*)test->params.mem, {test->params.total_elems},
    };

    test->train_acts = (S3DEncoderActivations*)calloc(1, sizeof(*test->train_acts));
    test->encoder.reg_train(test->weights, test->train_acts, &test->acts,
        &test->grads, test->batch);
    check_cuda(alloc_create(&test->acts), "allocate activations");
    check_cuda(alloc_create(&test->grads), "allocate gradients");

    test->input.shape[0] = test->batch;
    test->input.shape[1] = OBS_SIZE;
    test->input.data = (precision_t*)nullptr;
    check_cuda(cudaMalloc((void**)&test->input.data,
        (size_t)test->batch * OBS_SIZE * sizeof(float)), "allocate input");
    test->output_grad.shape[0] = test->batch;
    test->output_grad.shape[1] = test->hidden;
    check_cuda(cudaMalloc((void**)&test->output_grad.data,
        (size_t)test->batch * test->hidden * sizeof(float)), "allocate output gradient");
    test->host_input = (float*)malloc((size_t)test->batch * OBS_SIZE * sizeof(float));
    test->host_output_grad = (float*)malloc((size_t)test->batch * test->hidden * sizeof(float));
    fill_host(test->host_input, test->batch * OBS_SIZE, 0.7f, 0.0f);
    fill_host(test->host_output_grad, test->batch * test->hidden, 0.3f, 0.2f);
    check_cuda(cudaMemcpy(test->input.data, test->host_input,
        (size_t)test->batch * OBS_SIZE * sizeof(float), cudaMemcpyHostToDevice),
        "copy input");
    check_cuda(cudaMemcpy(test->output_grad.data, test->host_output_grad,
        (size_t)test->batch * test->hidden * sizeof(float), cudaMemcpyHostToDevice),
        "copy output gradient");

    int n = test->params.total_elems;
    float* flat = (float*)calloc((size_t)n, sizeof(float));
    int offset = 0;
    for (int i = 0; i < test->params.num_regs; i++) {
        int count = (int)numel(test->params.regs[i].shape);
        for (int j = 0; j < count; j++) flat[offset + j] = test_value(offset + j, 0.025f);
        offset += count;
    }
    // Positive biases keep most ReLU branches active while still exercising
    // their derivatives at the selected test point.
    for (int j = 0; j < 32; j++) flat[32 * OBS_SCALAR_SIZE + j] = 0.4f;
    for (int j = 0; j < 32; j++) flat[32 * OBS_SCALAR_SIZE + 32 + 32 * 32 + j] = 0.4f;
    copy_to_device(test->weights->scalar1_w, flat);
    copy_to_device(test->weights->scalar1_b, flat + 32 * OBS_SCALAR_SIZE);
    copy_to_device(test->weights->scalar2_w,
        flat + 32 * OBS_SCALAR_SIZE + 32);
    copy_to_device(test->weights->scalar2_b,
        flat + 32 * OBS_SCALAR_SIZE + 32 + 32 * 32);
    int off = 32 * OBS_SCALAR_SIZE + 32 + 32 * 32 + 32;
    copy_to_device(test->weights->depth_w, flat + off); off += 8 * 9;
    for (int j = 0; j < 8; j++) flat[off + j] = 0.4f;
    copy_to_device(test->weights->depth_b, flat + off); off += 8;
    copy_to_device(test->weights->occupancy_w, flat + off); off += 4 * 8;
    for (int j = 0; j < 8; j++) flat[off + j] = 0.4f;
    copy_to_device(test->weights->occupancy_b, flat + off); off += 8;
    constexpr int concat = 32 + 8 * (DEPTH_MAP_HEIGHT - 3 + 1) * DEPTH_MAP_WIDTH +
        4 * (OCCUPANCY_VERTICAL_BINS - 2 + 1) *
        (OCCUPANCY_LATERAL_BINS - 2 + 1) * (OCCUPANCY_FORWARD_BINS - 2 + 1);
    copy_to_device(test->weights->proj_w, flat + off); off += test->hidden * concat;
    for (int j = 0; j < test->hidden; j++) flat[off + j] = 2.0f;
    copy_to_device(test->weights->proj_b, flat + off);
    free(flat);
    check_cuda(cudaDeviceSynchronize(), "initialize test");
}

static double forward_loss(TestState* test) {
    PrecisionTensor output = test->encoder.forward(test->weights, test->train_acts,
        test->input, 0);
    float* host_output = (float*)malloc((size_t)test->batch * test->hidden * sizeof(float));
    copy_to_host(host_output, output);
    check_cuda(cudaDeviceSynchronize(), "forward");
    double loss = 0.0;
    for (int i = 0; i < test->batch * test->hidden; i++)
        loss += host_output[i] * test->host_output_grad[i];
    free(host_output);
    return loss;
}

static void check_forward(TestState* test) {
    PrecisionTensor output = test->encoder.forward(test->weights, test->train_acts,
        test->input, 0);
    float* native = (float*)malloc((size_t)test->batch * test->hidden * sizeof(float));
    float* reference = (float*)malloc((size_t)test->batch * test->hidden * sizeof(float));
    copy_to_host(native, output);
    check_cuda(cudaDeviceSynchronize(), "forward parity");

    float* scalar1_w = (float*)malloc(32 * OBS_SCALAR_SIZE * sizeof(float));
    float* scalar1_b = (float*)malloc(32 * sizeof(float));
    float* scalar2_w = (float*)malloc(32 * 32 * sizeof(float));
    float* scalar2_b = (float*)malloc(32 * sizeof(float));
    float* depth_w = (float*)malloc(8 * 9 * sizeof(float));
    float* depth_b = (float*)malloc(8 * sizeof(float));
    float* occupancy_w = (float*)malloc(4 * 8 * sizeof(float));
    float* occupancy_b = (float*)malloc(8 * sizeof(float));
    constexpr int concat = 32 + 8 * (DEPTH_MAP_HEIGHT - 3 + 1) * DEPTH_MAP_WIDTH +
        4 * (OCCUPANCY_VERTICAL_BINS - 2 + 1) *
        (OCCUPANCY_LATERAL_BINS - 2 + 1) * (OCCUPANCY_FORWARD_BINS - 2 + 1);
    float* proj_w = (float*)malloc((size_t)test->hidden * concat * sizeof(float));
    float* proj_b = (float*)malloc((size_t)test->hidden * sizeof(float));
    copy_to_host(scalar1_w, test->weights->scalar1_w);
    copy_to_host(scalar1_b, test->weights->scalar1_b);
    copy_to_host(scalar2_w, test->weights->scalar2_w);
    copy_to_host(scalar2_b, test->weights->scalar2_b);
    copy_to_host(depth_w, test->weights->depth_w);
    copy_to_host(depth_b, test->weights->depth_b);
    copy_to_host(occupancy_w, test->weights->occupancy_w);
    copy_to_host(occupancy_b, test->weights->occupancy_b);
    copy_to_host(proj_w, test->weights->proj_w);
    copy_to_host(proj_b, test->weights->proj_b);
    check_cuda(cudaDeviceSynchronize(), "copy parity parameters");
    cpu_encoder(test->host_input, scalar1_w, scalar1_b, scalar2_w, scalar2_b,
        depth_w, depth_b, occupancy_w, occupancy_b, proj_w, proj_b,
        test->batch, test->hidden, reference);

    float max_error = 0.0f;
    for (int i = 0; i < test->batch * test->hidden; i++)
        max_error = fmaxf(max_error, fabsf(native[i] - reference[i]));
    printf("shenaniguns3d encoder forward max_error=%.9g\n", (double)max_error);
    if (max_error > 2.0e-5f) exit(1);
    free(native); free(reference);
    free(scalar1_w); free(scalar1_b); free(scalar2_w); free(scalar2_b);
    free(depth_w); free(depth_b); free(occupancy_w); free(occupancy_b);
    free(proj_w); free(proj_b);
}

struct GradientProbe {
    PrecisionTensor parameter;
    PrecisionTensor gradient;
    int index;
    const char* name;
};

static void check_gradients(TestState* test) {
    test->encoder.forward(test->weights, test->train_acts, test->input, 0);
    test->encoder.backward(test->weights, test->train_acts, test->output_grad, 0);
    check_cuda(cudaDeviceSynchronize(), "encoder backward");

    GradientProbe probes[] = {
        {test->weights->scalar1_w, test->train_acts->scalar1_wgrad, 0, "scalar1_w"},
        {test->weights->scalar1_w, test->train_acts->scalar1_wgrad, 127, "scalar1_w"},
        {test->weights->scalar1_b, test->train_acts->scalar1_bgrad, 11, "scalar1_b"},
        {test->weights->scalar2_w, test->train_acts->scalar2_wgrad, 321, "scalar2_w"},
        {test->weights->scalar2_b, test->train_acts->scalar2_bgrad, 23, "scalar2_b"},
        {test->weights->depth_w, test->train_acts->depth_wgrad, 17, "depth_w"},
        {test->weights->depth_b, test->train_acts->depth_bgrad, 5, "depth_b"},
        {test->weights->occupancy_w, test->train_acts->occupancy_wgrad, 19, "occupancy_w"},
        {test->weights->occupancy_b, test->train_acts->occupancy_bgrad, 2, "occupancy_b"},
        {test->weights->proj_w, test->train_acts->proj_wgrad, 1041, "proj_w"},
        {test->weights->proj_b, test->train_acts->proj_bgrad, 7, "proj_b"},
    };

    for (const GradientProbe& probe : probes) {
        float original = 0.0f;
        check_cuda(cudaMemcpy(&original, probe.parameter.data + probe.index,
            sizeof(float), cudaMemcpyDeviceToHost), "read probe parameter");
        constexpr float epsilon = 1.0e-3f;
        float plus = original + epsilon;
        float minus = original - epsilon;
        check_cuda(cudaMemcpy(probe.parameter.data + probe.index, &plus,
            sizeof(float), cudaMemcpyHostToDevice), "set plus probe");
        float loss_plus = forward_loss(test);
        check_cuda(cudaMemcpy(probe.parameter.data + probe.index, &minus,
            sizeof(float), cudaMemcpyHostToDevice), "set minus probe");
        float loss_minus = forward_loss(test);
        check_cuda(cudaMemcpy(probe.parameter.data + probe.index, &original,
            sizeof(float), cudaMemcpyHostToDevice), "restore probe");

        float analytic = 0.0f;
        check_cuda(cudaMemcpy(&analytic, probe.gradient.data + probe.index,
            sizeof(float), cudaMemcpyDeviceToHost), "read probe gradient");
        float numeric = (loss_plus - loss_minus) / (2.0f * epsilon);
        float error = fabsf(analytic - numeric);
        float tolerance = 3.0e-3f + 2.0e-2f * fmaxf(fabsf(analytic), fabsf(numeric));
        printf("shenaniguns3d gradient %-11s[%d] analytic=% .7g numeric=% .7g error=%g\n",
            probe.name, probe.index, (double)analytic, (double)numeric, (double)error);
        if (error > tolerance) exit(1);
    }

    float padding[4] = {1, 1, 1, 1};
    check_cuda(cudaMemcpy(padding, test->train_acts->occupancy_bgrad.data + 4,
        sizeof(padding), cudaMemcpyDeviceToHost), "read occupancy bias padding");
    for (float value : padding) {
        if (value != 0.0f) {
            fprintf(stderr, "occupancy bias padding gradient is nonzero\n");
            exit(1);
        }
    }
}

static float host_sigmoid(float x) {
    float z = expf(-fabsf(x));
    return x >= 0.0f ? 1.0f / (1.0f + z) : z / (1.0f + z);
}

static float host_lerp(float a, float b, float weight) {
    float diff = b - a;
    return fabsf(weight) < 0.5f
        ? a + weight * diff
        : b - diff * (1.0f - weight);
}

static void cpu_policy_forward(const float* input, const float* scalar1_w,
        const float* scalar1_b, const float* scalar2_w, const float* scalar2_b,
        const float* depth_w, const float* depth_b, const float* occupancy_w,
        const float* occupancy_b, const float* proj_w, const float* proj_b,
        const float* decoder_w, const float* const* mingru_w, float* state,
        int batch, int hidden, int layers, int decoder_output, float* output) {
    std::vector<float> encoded((size_t)batch * hidden);
    cpu_encoder(input, scalar1_w, scalar1_b, scalar2_w, scalar2_b,
        depth_w, depth_b, occupancy_w, occupancy_b, proj_w, proj_b,
        batch, hidden, encoded.data());

    std::vector<float> x((size_t)batch * hidden);
    memcpy(x.data(), encoded.data(), x.size() * sizeof(float));
    std::vector<float> combined((size_t)batch * 3 * hidden);
    std::vector<float> next((size_t)batch * hidden);
    for (int layer = 0; layer < layers; layer++) {
        const float* weights = mingru_w[layer];
        for (int b = 0; b < batch; b++) {
            for (int o = 0; o < 3 * hidden; o++) {
                float sum = 0.0f;
                for (int i = 0; i < hidden; i++)
                    sum += x[b * hidden + i] * weights[o * hidden + i];
                combined[b * 3 * hidden + o] = sum;
            }
        }
        for (int b = 0; b < batch; b++) {
            for (int h = 0; h < hidden; h++) {
                int state_idx = (layer * batch + b) * hidden + h;
                float hidden_value = combined[b * 3 * hidden + h];
                float gate_value = combined[b * 3 * hidden + hidden + h];
                float highway_value = combined[b * 3 * hidden + 2 * hidden + h];
                float x_value = x[b * hidden + h];
                float z = host_sigmoid(gate_value);
                float h_tilde = hidden_value >= 0.0f
                    ? hidden_value + 0.5f : host_sigmoid(hidden_value);
                float h_out = host_lerp(state[state_idx], h_tilde, z);
                float highway = host_sigmoid(highway_value);
                x[b * hidden + h] = highway * h_out +
                    (1.0f - highway) * x_value;
                state[state_idx] = h_out;
            }
        }
    }

    for (int b = 0; b < batch; b++) {
        for (int o = 0; o < decoder_output; o++) {
            float sum = 0.0f;
            for (int i = 0; i < hidden; i++)
                sum += x[b * hidden + i] * decoder_w[o * hidden + i];
            output[b * decoder_output + o] = sum;
        }
    }
}

struct FullPolicyTest {
    Policy policy;
    PolicyWeights weights;
    PolicyActivations rollout_activations;
    Allocator params;
    Allocator acts;
    PrecisionTensor observations;
    PrecisionTensor state;
    int batch;
    int hidden;
    int layers;
    int decoder_output;
};

static void fill_device_tensor(PrecisionTensor tensor, int seed, float scale,
        float bias) {
    int n = (int)numel(tensor.shape);
    std::vector<float> values(n);
    for (int i = 0; i < n; i++)
        values[i] = bias + test_value(seed + i, scale);
    copy_to_device(tensor, values.data());
}

static void initialize_full_policy(FullPolicyTest* test) {
    memset(test, 0, sizeof(*test));
    test->batch = 3;
    test->hidden = 16;
    test->layers = 2;
    test->decoder_output = 16;
    test->policy = build_policy("shenaniguns3d", OBS_SIZE, test->hidden,
        test->layers, 15, false, 4);
    test->weights = policy_weights_create(&test->policy, &test->params);
    test->rollout_activations = policy_reg_rollout(&test->policy, test->weights,
        &test->acts, test->batch);
    test->state.shape[0] = test->layers;
    test->state.shape[1] = test->batch;
    test->state.shape[2] = test->hidden;
    alloc_register(&test->acts, &test->state);
    check_cuda(alloc_create(&test->params), "allocate full parameters");
    check_cuda(alloc_create(&test->acts), "allocate full activations");

    test->observations.shape[0] = test->batch;
    test->observations.shape[1] = OBS_SIZE;
    check_cuda(cudaMalloc((void**)&test->observations.data,
        (size_t)test->batch * OBS_SIZE * sizeof(float)), "allocate full input");

    S3DEncoderWeights* encoder = (S3DEncoderWeights*)test->weights.encoder;
    fill_device_tensor(encoder->scalar1_w, 11, 0.025f, 0.0f);
    fill_device_tensor(encoder->scalar1_b, 101, 0.0f, 0.4f);
    fill_device_tensor(encoder->scalar2_w, 211, 0.025f, 0.0f);
    fill_device_tensor(encoder->scalar2_b, 311, 0.0f, 0.4f);
    fill_device_tensor(encoder->depth_w, 401, 0.025f, 0.0f);
    fill_device_tensor(encoder->depth_b, 503, 0.0f, 0.4f);
    fill_device_tensor(encoder->occupancy_w, 601, 0.025f, 0.0f);
    fill_device_tensor(encoder->occupancy_b, 701, 0.0f, 0.4f);
    std::vector<float> occupancy_padding(8, 0.4f);
    for (int i = 4; i < 8; i++) occupancy_padding[i] = 7.0f;
    copy_to_device(encoder->occupancy_b, occupancy_padding.data());
    fill_device_tensor(encoder->proj_w, 811, 0.0025f, 0.0f);
    fill_device_tensor(encoder->proj_b, 911, 0.0f, 0.2f);

    DecoderWeights* decoder = (DecoderWeights*)test->weights.decoder;
    fill_device_tensor(decoder->weight, 1001, 0.03f, 0.0f);
    MinGRUWeights* mingru = (MinGRUWeights*)test->weights.network;
    for (int layer = 0; layer < test->layers; layer++)
        fill_device_tensor(mingru->weights[layer], 1101 + 100 * layer,
            0.02f, 0.0f);
    check_cuda(cudaDeviceSynchronize(), "initialize full policy");
}

static void copy_full_policy_weights(FullPolicyTest* test,
        std::vector<float>* scalar1_w, std::vector<float>* scalar1_b,
        std::vector<float>* scalar2_w, std::vector<float>* scalar2_b,
        std::vector<float>* depth_w, std::vector<float>* depth_b,
        std::vector<float>* occupancy_w, std::vector<float>* occupancy_b,
        std::vector<float>* proj_w, std::vector<float>* proj_b,
        std::vector<float>* decoder_w, std::vector<std::vector<float>>* mingru_w) {
    S3DEncoderWeights* encoder = (S3DEncoderWeights*)test->weights.encoder;
    scalar1_w->resize(numel(encoder->scalar1_w.shape));
    scalar1_b->resize(numel(encoder->scalar1_b.shape));
    scalar2_w->resize(numel(encoder->scalar2_w.shape));
    scalar2_b->resize(numel(encoder->scalar2_b.shape));
    depth_w->resize(numel(encoder->depth_w.shape));
    depth_b->resize(numel(encoder->depth_b.shape));
    occupancy_w->resize(numel(encoder->occupancy_w.shape));
    occupancy_b->resize(numel(encoder->occupancy_b.shape));
    proj_w->resize(numel(encoder->proj_w.shape));
    proj_b->resize(numel(encoder->proj_b.shape));
    copy_to_host(scalar1_w->data(), encoder->scalar1_w);
    copy_to_host(scalar1_b->data(), encoder->scalar1_b);
    copy_to_host(scalar2_w->data(), encoder->scalar2_w);
    copy_to_host(scalar2_b->data(), encoder->scalar2_b);
    copy_to_host(depth_w->data(), encoder->depth_w);
    copy_to_host(depth_b->data(), encoder->depth_b);
    copy_to_host(occupancy_w->data(), encoder->occupancy_w);
    copy_to_host(occupancy_b->data(), encoder->occupancy_b);
    copy_to_host(proj_w->data(), encoder->proj_w);
    copy_to_host(proj_b->data(), encoder->proj_b);

    DecoderWeights* decoder = (DecoderWeights*)test->weights.decoder;
    decoder_w->resize(numel(decoder->weight.shape));
    copy_to_host(decoder_w->data(), decoder->weight);
    MinGRUWeights* mingru = (MinGRUWeights*)test->weights.network;
    mingru_w->resize(test->layers);
    for (int layer = 0; layer < test->layers; layer++) {
        (*mingru_w)[layer].resize(numel(mingru->weights[layer].shape));
        copy_to_host((*mingru_w)[layer].data(), mingru->weights[layer]);
    }
    check_cuda(cudaDeviceSynchronize(), "copy full policy weights");
}

static void check_full_forward() {
    FullPolicyTest test;
    initialize_full_policy(&test);

    std::vector<float> scalar1_w, scalar1_b, scalar2_w, scalar2_b;
    std::vector<float> depth_w, depth_b, occupancy_w, occupancy_b;
    std::vector<float> proj_w, proj_b, decoder_w;
    std::vector<std::vector<float>> mingru_w;
    copy_full_policy_weights(&test, &scalar1_w, &scalar1_b, &scalar2_w,
        &scalar2_b, &depth_w, &depth_b, &occupancy_w, &occupancy_b,
        &proj_w, &proj_b, &decoder_w, &mingru_w);
    std::vector<const float*> mingru_ptr(test.layers);
    for (int layer = 0; layer < test.layers; layer++)
        mingru_ptr[layer] = mingru_w[layer].data();

    std::vector<float> input((size_t)test.batch * OBS_SIZE);
    std::vector<float> expected_state((size_t)test.layers * test.batch * test.hidden);
    std::vector<float> native_state(expected_state.size());
    std::vector<float> expected_output((size_t)test.batch * test.decoder_output);
    std::vector<float> native_output(expected_output.size());
    for (int i = 0; i < (int)expected_state.size(); i++)
        expected_state[i] = 0.1f * test_value(2001 + i, 1.0f);
    check_cuda(cudaMemcpy(test.state.data, expected_state.data(),
        expected_state.size() * sizeof(float), cudaMemcpyHostToDevice),
        "copy full initial state");

    float max_output_error = 0.0f;
    float max_state_error = 0.0f;
    for (int step = 0; step < 5; step++) {
        for (int i = 0; i < (int)input.size(); i++)
            input[i] = test_value(3001 + step * (int)input.size() + i, 0.35f);
        check_cuda(cudaMemcpy(test.observations.data, input.data(),
            input.size() * sizeof(float), cudaMemcpyHostToDevice),
            "copy full observation");
        PrecisionTensor native = policy_forward(&test.policy, test.weights,
            test.rollout_activations, test.observations, test.state, 0);
        copy_to_host(native_output.data(), native);
        check_cuda(cudaMemcpy(native_state.data(), test.state.data,
            native_state.size() * sizeof(float), cudaMemcpyDeviceToHost),
            "copy full state");
        cpu_policy_forward(input.data(), scalar1_w.data(), scalar1_b.data(),
            scalar2_w.data(), scalar2_b.data(), depth_w.data(), depth_b.data(),
            occupancy_w.data(), occupancy_b.data(), proj_w.data(), proj_b.data(),
            decoder_w.data(), mingru_ptr.data(), expected_state.data(),
            test.batch, test.hidden, test.layers, test.decoder_output,
            expected_output.data());
        for (int i = 0; i < (int)expected_output.size(); i++)
            max_output_error = fmaxf(max_output_error,
                fabsf(native_output[i] - expected_output[i]));
        for (int i = 0; i < (int)expected_state.size(); i++)
            max_state_error = fmaxf(max_state_error,
                fabsf(native_state[i] - expected_state[i]));
    }
    printf("shenaniguns3d policy forward output_error=%.9g state_error=%.9g\n",
        (double)max_output_error, (double)max_state_error);
    if (max_output_error > 2.0e-4f || max_state_error > 2.0e-4f)
        exit(1);
}

static double train_policy_loss(FullPolicyTest* test, PolicyActivations& activations,
        PrecisionTensor input, PrecisionTensor state, PrecisionTensor terminals,
        FloatTensor grad_logits, FloatTensor grad_value, int batch, int horizon,
        int decoder_output) {
    PrecisionTensor output = policy_forward_train(&test->policy, test->weights,
        activations, input, state, terminals, 0);
    std::vector<float> values((size_t)batch * horizon * decoder_output);
    copy_to_host(values.data(), output);
    std::vector<float> logits((size_t)batch * horizon * (decoder_output - 1));
    std::vector<float> value((size_t)batch * horizon);
    copy_float_to_host(logits.data(), grad_logits);
    copy_float_to_host(value.data(), grad_value);
    check_cuda(cudaDeviceSynchronize(), "train loss");
    double loss = 0.0;
    for (int row = 0; row < batch * horizon; row++) {
        for (int o = 0; o < decoder_output - 1; o++)
            loss += (double)values[row * decoder_output + o] *
                (double)logits[row * (decoder_output - 1) + o];
        loss += (double)values[row * decoder_output + decoder_output - 1] *
            (double)value[row];
    }
    return loss;
}

static void check_policy_gradients() {
    FullPolicyTest test;
    initialize_full_policy(&test);
    const int batch = 2;
    const int horizon = 4;
    const int decoder_output = test.decoder_output;
    Allocator train_acts_alloc = {}, train_grads_alloc = {};
    PolicyActivations train_activations = policy_reg_train(&test.policy,
        test.weights, &train_acts_alloc, &train_grads_alloc, batch * horizon);
    check_cuda(alloc_create(&train_acts_alloc), "allocate train activations");
    check_cuda(alloc_create(&train_grads_alloc), "allocate train gradients");

    PrecisionTensor input = {};
    input.shape[0] = batch;
    input.shape[1] = horizon;
    input.shape[2] = OBS_SIZE;
    check_cuda(cudaMalloc((void**)&input.data,
        (size_t)batch * horizon * OBS_SIZE * sizeof(float)),
        "allocate train observations");
    PrecisionTensor state = {};
    state.shape[0] = test.layers;
    state.shape[1] = batch;
    state.shape[2] = test.hidden;
    check_cuda(cudaMalloc((void**)&state.data,
        (size_t)test.layers * batch * test.hidden * sizeof(float)),
        "allocate train state");
    PrecisionTensor terminals = {};
    terminals.shape[0] = batch;
    terminals.shape[1] = horizon;
    check_cuda(cudaMalloc((void**)&terminals.data,
        (size_t)batch * horizon * sizeof(float)),
        "allocate train terminals");
    FloatTensor grad_logits = {};
    grad_logits.shape[0] = batch;
    grad_logits.shape[1] = horizon;
    grad_logits.shape[2] = decoder_output - 1;
    check_cuda(cudaMalloc((void**)&grad_logits.data,
        (size_t)batch * horizon * (decoder_output - 1) * sizeof(float)),
        "allocate train logit gradient");
    FloatTensor grad_value = {};
    grad_value.shape[0] = batch;
    grad_value.shape[1] = horizon;
    check_cuda(cudaMalloc((void**)&grad_value.data,
        (size_t)batch * horizon * sizeof(float)),
        "allocate train value gradient");

    std::vector<float> host_input((size_t)batch * horizon * OBS_SIZE);
    std::vector<float> host_state((size_t)test.layers * batch * test.hidden);
    std::vector<float> host_terminals((size_t)batch * horizon, 0.0f);
    std::vector<float> host_grad_logits((size_t)batch * horizon * (decoder_output - 1));
    std::vector<float> host_grad_value((size_t)batch * horizon);
    for (int i = 0; i < (int)host_input.size(); i++)
        host_input[i] = test_value(4001 + i, 0.3f);
    for (int i = 0; i < (int)host_state.size(); i++)
        host_state[i] = 0.1f * test_value(5001 + i, 1.0f);
    host_terminals[2] = 1.0f;
    host_terminals[horizon + 1] = 1.0f;
    for (int i = 0; i < (int)host_grad_logits.size(); i++)
        host_grad_logits[i] = 0.2f + test_value(6001 + i, 0.25f);
    for (int i = 0; i < (int)host_grad_value.size(); i++)
        host_grad_value[i] = 0.2f + test_value(7001 + i, 0.25f);
    check_cuda(cudaMemcpy(input.data, host_input.data(),
        host_input.size() * sizeof(float), cudaMemcpyHostToDevice),
        "copy train observations");
    check_cuda(cudaMemcpy(state.data, host_state.data(),
        host_state.size() * sizeof(float), cudaMemcpyHostToDevice),
        "copy train state");
    check_cuda(cudaMemcpy(terminals.data, host_terminals.data(),
        host_terminals.size() * sizeof(float), cudaMemcpyHostToDevice),
        "copy train terminals");
    check_cuda(cudaMemcpy(grad_logits.data, host_grad_logits.data(),
        host_grad_logits.size() * sizeof(float), cudaMemcpyHostToDevice),
        "copy train logit gradient");
    check_cuda(cudaMemcpy(grad_value.data, host_grad_value.data(),
        host_grad_value.size() * sizeof(float), cudaMemcpyHostToDevice),
        "copy train value gradient");

    policy_forward_train(&test.policy, test.weights, train_activations,
        input, state, terminals, 0);
    FloatTensor empty = {};
    policy_backward(&test.policy, test.weights, train_activations,
        grad_logits, empty, grad_value, 0);
    check_cuda(cudaDeviceSynchronize(), "policy backward");

    DecoderWeights* decoder = (DecoderWeights*)test.weights.decoder;
    DecoderActivations* decoder_acts = (DecoderActivations*)train_activations.decoder;
    MinGRUWeights* mingru = (MinGRUWeights*)test.weights.network;
    MinGRUActivations* mingru_acts = (MinGRUActivations*)train_activations.network;
    GradientProbe probes[] = {
        {decoder->weight, decoder_acts->wgrad_scratch, 17, "decoder_w"},
        {decoder->weight, decoder_acts->wgrad_scratch, 127, "decoder_w"},
        {mingru->weights[0], mingru_acts->wgrad_scratch[0], 31, "mingru0_w"},
        {mingru->weights[1], mingru_acts->wgrad_scratch[1], 211, "mingru1_w"},
    };
    for (const GradientProbe& probe : probes) {
        float original = 0.0f;
        check_cuda(cudaMemcpy(&original, probe.parameter.data + probe.index,
            sizeof(float), cudaMemcpyDeviceToHost), "read policy probe");
        constexpr float epsilon = 1.0e-3f;
        float plus = original + epsilon;
        float minus = original - epsilon;
        check_cuda(cudaMemcpy(probe.parameter.data + probe.index, &plus,
            sizeof(float), cudaMemcpyHostToDevice), "set policy plus probe");
        double loss_plus = train_policy_loss(&test, train_activations, input,
            state, terminals, grad_logits, grad_value, batch, horizon,
            decoder_output);
        check_cuda(cudaMemcpy(probe.parameter.data + probe.index, &minus,
            sizeof(float), cudaMemcpyHostToDevice), "set policy minus probe");
        double loss_minus = train_policy_loss(&test, train_activations, input,
            state, terminals, grad_logits, grad_value, batch, horizon,
            decoder_output);
        check_cuda(cudaMemcpy(probe.parameter.data + probe.index, &original,
            sizeof(float), cudaMemcpyHostToDevice), "restore policy probe");
        float analytic = 0.0f;
        check_cuda(cudaMemcpy(&analytic, probe.gradient.data + probe.index,
            sizeof(float), cudaMemcpyDeviceToHost), "read policy gradient");
        float numeric = (float)((loss_plus - loss_minus) / (2.0 * epsilon));
        float error = fabsf(analytic - numeric);
        float tolerance = 3.0e-3f + 2.0e-2f * fmaxf(fabsf(analytic), fabsf(numeric));
        printf("shenaniguns3d policy gradient %-8s[%d] analytic=% .7g numeric=% .7g error=%g\n",
            probe.name, probe.index, (double)analytic, (double)numeric, (double)error);
        if (error > tolerance) exit(1);
    }
}

int main() {
    cublas_init_handle();
    TestState test;
    init_test(&test);
    check_forward(&test);
    check_gradients(&test);
    check_full_forward();
    check_policy_gradients();
    printf("shenaniguns3d encoder and policy checks passed\n");
    return 0;
}
