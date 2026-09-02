// Spatial encoder for the shenaniguns3d observation contract.
//
// The environment transports observations as one flat vector, but the depth
// and occupancy regions retain their spatial topology here. Depth uses a
// circular horizontal convolution because azimuth zero and azimuth one-past-
// the-end are neighbors. Occupancy uses a small valid 3D convolution over
// vertical, lateral, and forward cells. The resulting features are projected
// to the existing MinGRU width.

static constexpr int S3D_SCALAR_HIDDEN = 32;

static constexpr int S3D_DEPTH_CHANNELS = 8;
static constexpr int S3D_DEPTH_KERNEL = 3;
static constexpr int S3D_DEPTH_OUT_HEIGHT =
    DEPTH_MAP_HEIGHT - S3D_DEPTH_KERNEL + 1;
static constexpr int S3D_DEPTH_OUT_WIDTH = DEPTH_MAP_WIDTH;
static constexpr int S3D_DEPTH_FEATURES =
    S3D_DEPTH_CHANNELS * S3D_DEPTH_OUT_HEIGHT * S3D_DEPTH_OUT_WIDTH;

static constexpr int S3D_OCC_CHANNELS = 4;
// BF16 parameter and gradient flat buffers advance by element count while
// allocator pointers are 16-byte aligned, so keep the four live bias values
// in an eight-element serialized tensor.
static constexpr int S3D_OCC_BIAS_STORAGE = 8;
static constexpr int S3D_OCC_KERNEL_VERTICAL = 2;
static constexpr int S3D_OCC_KERNEL_LATERAL = 2;
static constexpr int S3D_OCC_KERNEL_FORWARD = 2;
static constexpr int S3D_OCC_OUT_VERTICAL =
    OCCUPANCY_VERTICAL_BINS - S3D_OCC_KERNEL_VERTICAL + 1;
static constexpr int S3D_OCC_OUT_LATERAL =
    OCCUPANCY_LATERAL_BINS - S3D_OCC_KERNEL_LATERAL + 1;
static constexpr int S3D_OCC_OUT_FORWARD =
    OCCUPANCY_FORWARD_BINS - S3D_OCC_KERNEL_FORWARD + 1;
static constexpr int S3D_OCC_FEATURES =
    S3D_OCC_CHANNELS * S3D_OCC_OUT_VERTICAL * S3D_OCC_OUT_LATERAL *
    S3D_OCC_OUT_FORWARD;

static constexpr int S3D_CONCAT =
    S3D_SCALAR_HIDDEN + S3D_DEPTH_FEATURES + S3D_OCC_FEATURES;

struct S3DEncoderWeights {
    PrecisionTensor scalar1_w, scalar1_b;
    PrecisionTensor scalar2_w, scalar2_b;
    PrecisionTensor depth_w, depth_b;
    PrecisionTensor occupancy_w, occupancy_b;
    PrecisionTensor proj_w, proj_b;
    int obs_size, hidden;
};

struct S3DEncoderActivations {
    PrecisionTensor saved_input;
    PrecisionTensor scalar1_out, scalar_out, scalar1_grad;
    PrecisionTensor depth_out, occupancy_out;
    PrecisionTensor concat, out;

    PrecisionTensor scalar1_wgrad, scalar1_bgrad;
    PrecisionTensor scalar2_wgrad, scalar2_bgrad;
    PrecisionTensor depth_wgrad, depth_bgrad;
    PrecisionTensor occupancy_wgrad, occupancy_bgrad;
    PrecisionTensor proj_wgrad, proj_bgrad;
};

__global__ void s3d_scalar1_relu_kernel(
        precision_t* __restrict__ out, const precision_t* __restrict__ input,
        const precision_t* __restrict__ weight, const precision_t* __restrict__ bias,
        int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * S3D_SCALAR_HIDDEN) return;

    int b = idx / S3D_SCALAR_HIDDEN;
    int o = idx % S3D_SCALAR_HIDDEN;
    float sum = to_float(bias[o]);
    const precision_t* in = input + b * OBS_SIZE;
    const precision_t* w = weight + o * OBS_SCALAR_SIZE;
    for (int i = 0; i < OBS_SCALAR_SIZE; i++)
        sum += to_float(in[i]) * to_float(w[i]);
    out[idx] = from_float(fmaxf(0.0f, sum));
}

__global__ void s3d_scalar2_relu_kernel(
        precision_t* __restrict__ out, const precision_t* __restrict__ input,
        const precision_t* __restrict__ weight, const precision_t* __restrict__ bias,
        int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * S3D_SCALAR_HIDDEN) return;

    int b = idx / S3D_SCALAR_HIDDEN;
    int o = idx % S3D_SCALAR_HIDDEN;
    float sum = to_float(bias[o]);
    const precision_t* in = input + b * S3D_SCALAR_HIDDEN;
    const precision_t* w = weight + o * S3D_SCALAR_HIDDEN;
    for (int i = 0; i < S3D_SCALAR_HIDDEN; i++)
        sum += to_float(in[i]) * to_float(w[i]);
    out[idx] = from_float(fmaxf(0.0f, sum));
}

__global__ void s3d_depth_conv_kernel(
        precision_t* __restrict__ out, const precision_t* __restrict__ input,
        const precision_t* __restrict__ weight, const precision_t* __restrict__ bias,
        int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * S3D_DEPTH_FEATURES) return;

    int q = idx;
    int ow = q % S3D_DEPTH_OUT_WIDTH; q /= S3D_DEPTH_OUT_WIDTH;
    int oh = q % S3D_DEPTH_OUT_HEIGHT; q /= S3D_DEPTH_OUT_HEIGHT;
    int oc = q % S3D_DEPTH_CHANNELS; q /= S3D_DEPTH_CHANNELS;
    int b = q;

    float sum = to_float(bias[oc]);
    const precision_t* in = input + b * OBS_SIZE + DEPTH_MAP_OFFSET;
    const precision_t* w = weight + oc * S3D_DEPTH_KERNEL * S3D_DEPTH_KERNEL;
    for (int kh = 0; kh < S3D_DEPTH_KERNEL; kh++) {
        for (int kw = 0; kw < S3D_DEPTH_KERNEL; kw++) {
            int iw = (ow + kw - S3D_DEPTH_KERNEL / 2) % DEPTH_MAP_WIDTH;
            if (iw < 0) iw += DEPTH_MAP_WIDTH;
            sum += to_float(in[(oh + kh) * DEPTH_MAP_WIDTH + iw]) *
                   to_float(w[kh * S3D_DEPTH_KERNEL + kw]);
        }
    }
    int out_idx = ((b * S3D_DEPTH_CHANNELS + oc) * S3D_DEPTH_OUT_HEIGHT + oh) *
                  S3D_DEPTH_OUT_WIDTH + ow;
    out[out_idx] = from_float(fmaxf(0.0f, sum));
}

__global__ void s3d_occupancy_conv_kernel(
        precision_t* __restrict__ out, const precision_t* __restrict__ input,
        const precision_t* __restrict__ weight, const precision_t* __restrict__ bias,
        int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * S3D_OCC_FEATURES) return;

    int q = idx;
    int of = q % S3D_OCC_OUT_FORWARD; q /= S3D_OCC_OUT_FORWARD;
    int ol = q % S3D_OCC_OUT_LATERAL; q /= S3D_OCC_OUT_LATERAL;
    int ov = q % S3D_OCC_OUT_VERTICAL; q /= S3D_OCC_OUT_VERTICAL;
    int oc = q % S3D_OCC_CHANNELS; q /= S3D_OCC_CHANNELS;
    int b = q;

    float sum = to_float(bias[oc]);
    const precision_t* in = input + b * OBS_SIZE + OCCUPANCY_OFFSET;
    const precision_t* w = weight + oc *
        S3D_OCC_KERNEL_VERTICAL * S3D_OCC_KERNEL_LATERAL *
        S3D_OCC_KERNEL_FORWARD;
    for (int kv = 0; kv < S3D_OCC_KERNEL_VERTICAL; kv++) {
        for (int kl = 0; kl < S3D_OCC_KERNEL_LATERAL; kl++) {
            for (int kf = 0; kf < S3D_OCC_KERNEL_FORWARD; kf++) {
                int iv = ov + kv;
                int il = ol + kl;
                int in_idx = (iv * OCCUPANCY_LATERAL_BINS + il) *
                             OCCUPANCY_FORWARD_BINS + of + kf;
                int w_idx = (kv * S3D_OCC_KERNEL_LATERAL + kl) *
                            S3D_OCC_KERNEL_FORWARD + kf;
                sum += to_float(in[in_idx]) * to_float(w[w_idx]);
            }
        }
    }
    int out_idx = (((b * S3D_OCC_CHANNELS + oc) * S3D_OCC_OUT_VERTICAL + ov) *
                   S3D_OCC_OUT_LATERAL + ol) * S3D_OCC_OUT_FORWARD + of;
    out[out_idx] = from_float(fmaxf(0.0f, sum));
}

__global__ void s3d_concat_kernel(
        precision_t* __restrict__ out, const precision_t* __restrict__ scalar,
        const precision_t* __restrict__ depth, const precision_t* __restrict__ occupancy,
        int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * S3D_CONCAT) return;
    int b = idx / S3D_CONCAT;
    int c = idx % S3D_CONCAT;
    if (c < S3D_SCALAR_HIDDEN) {
        out[idx] = scalar[b * S3D_SCALAR_HIDDEN + c];
    } else if (c < S3D_SCALAR_HIDDEN + S3D_DEPTH_FEATURES) {
        out[idx] = depth[b * S3D_DEPTH_FEATURES + c - S3D_SCALAR_HIDDEN];
    } else {
        out[idx] = occupancy[b * S3D_OCC_FEATURES + c - S3D_SCALAR_HIDDEN -
                             S3D_DEPTH_FEATURES];
    }
}

__global__ void s3d_bias_relu_kernel(
        precision_t* __restrict__ data, const precision_t* __restrict__ bias,
        int total, int dim) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= total) return;
    float value = to_float(data[idx]) + to_float(bias[idx % dim]);
    data[idx] = from_float(fmaxf(0.0f, value));
}

__global__ void s3d_relu_backward_kernel(
        precision_t* __restrict__ grad, const precision_t* __restrict__ out, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;
    if (to_float(out[idx]) <= 0.0f)
        grad[idx] = from_float(0.0f);
}

__global__ void s3d_bias_grad_kernel(
        const precision_t* __restrict__ grad, const precision_t* __restrict__ out,
        precision_t* __restrict__ bias_grad, int B, int features, int row_stride,
        int offset) {
    int feature = blockIdx.x * blockDim.x + threadIdx.x;
    if (feature >= features) return;
    float sum = 0.0f;
    for (int b = 0; b < B; b++) {
        if (to_float(out[b * features + feature]) > 0.0f)
            sum += to_float(grad[b * row_stride + offset + feature]);
    }
    bias_grad[feature] = from_float(sum);
}

__global__ void s3d_dense_wgrad_kernel(
        precision_t* __restrict__ wgrad, const precision_t* __restrict__ input,
        const precision_t* __restrict__ grad, const precision_t* __restrict__ out,
        int B, int input_stride, int input_offset, int input_features,
        int output_features, int row_stride, int grad_offset) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int n = output_features * input_features;
    if (idx >= n) return;
    int o = idx / input_features;
    int i = idx % input_features;
    float sum = 0.0f;
    for (int b = 0; b < B; b++) {
        if (to_float(out[b * output_features + o]) > 0.0f)
            sum += to_float(grad[b * row_stride + grad_offset + o]) *
                   to_float(input[b * input_stride + input_offset + i]);
    }
    wgrad[idx] = from_float(sum);
}

__global__ void s3d_scalar1_input_grad_kernel(
        precision_t* __restrict__ grad1, const precision_t* __restrict__ grad2,
        const precision_t* __restrict__ scalar1_out,
        const precision_t* __restrict__ scalar2_out,
        const precision_t* __restrict__ scalar2_w, int B, int row_stride,
        int grad2_offset) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * S3D_SCALAR_HIDDEN) return;
    int b = idx / S3D_SCALAR_HIDDEN;
    int i = idx % S3D_SCALAR_HIDDEN;
    float sum = 0.0f;
    for (int o = 0; o < S3D_SCALAR_HIDDEN; o++) {
        if (to_float(scalar2_out[b * S3D_SCALAR_HIDDEN + o]) > 0.0f)
            sum += to_float(grad2[b * row_stride + grad2_offset + o]) *
                   to_float(scalar2_w[o * S3D_SCALAR_HIDDEN + i]);
    }
    if (to_float(scalar1_out[idx]) <= 0.0f)
        sum = 0.0f;
    grad1[idx] = from_float(sum);
}

__global__ void s3d_depth_wgrad_kernel(
        precision_t* __restrict__ wgrad, const precision_t* __restrict__ input,
        const precision_t* __restrict__ grad, const precision_t* __restrict__ out,
        int B, int row_stride, int offset) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int n = S3D_DEPTH_CHANNELS * S3D_DEPTH_KERNEL * S3D_DEPTH_KERNEL;
    if (idx >= n) return;
    int q = idx;
    int kw = q % S3D_DEPTH_KERNEL; q /= S3D_DEPTH_KERNEL;
    int kh = q % S3D_DEPTH_KERNEL; q /= S3D_DEPTH_KERNEL;
    int oc = q;
    float sum = 0.0f;
    for (int b = 0; b < B; b++) {
        for (int oh = 0; oh < S3D_DEPTH_OUT_HEIGHT; oh++) {
            for (int ow = 0; ow < S3D_DEPTH_OUT_WIDTH; ow++) {
                int out_idx = ((b * S3D_DEPTH_CHANNELS + oc) *
                               S3D_DEPTH_OUT_HEIGHT + oh) *
                              S3D_DEPTH_OUT_WIDTH + ow;
                if (to_float(out[out_idx]) <= 0.0f) continue;
                int iw = (ow + kw - S3D_DEPTH_KERNEL / 2) % DEPTH_MAP_WIDTH;
                if (iw < 0) iw += DEPTH_MAP_WIDTH;
                sum += to_float(grad[b * row_stride + offset +
                                     oc * S3D_DEPTH_OUT_HEIGHT * S3D_DEPTH_OUT_WIDTH +
                                     oh * S3D_DEPTH_OUT_WIDTH + ow]) *
                       to_float(input[b * OBS_SIZE + DEPTH_MAP_OFFSET +
                                      (oh + kh) * DEPTH_MAP_WIDTH + iw]);
            }
        }
    }
    wgrad[idx] = from_float(sum);
}

__global__ void s3d_occupancy_wgrad_kernel(
        precision_t* __restrict__ wgrad, const precision_t* __restrict__ input,
        const precision_t* __restrict__ grad, const precision_t* __restrict__ out,
        int B, int row_stride, int offset) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int n = S3D_OCC_CHANNELS * S3D_OCC_KERNEL_VERTICAL *
            S3D_OCC_KERNEL_LATERAL * S3D_OCC_KERNEL_FORWARD;
    if (idx >= n) return;
    int q = idx;
    int kf = q % S3D_OCC_KERNEL_FORWARD; q /= S3D_OCC_KERNEL_FORWARD;
    int kl = q % S3D_OCC_KERNEL_LATERAL; q /= S3D_OCC_KERNEL_LATERAL;
    int kv = q % S3D_OCC_KERNEL_VERTICAL; q /= S3D_OCC_KERNEL_VERTICAL;
    int oc = q;
    float sum = 0.0f;
    for (int b = 0; b < B; b++) {
        for (int ov = 0; ov < S3D_OCC_OUT_VERTICAL; ov++) {
            for (int ol = 0; ol < S3D_OCC_OUT_LATERAL; ol++) {
                for (int of = 0; of < S3D_OCC_OUT_FORWARD; of++) {
                    int out_idx = (((b * S3D_OCC_CHANNELS + oc) *
                                    S3D_OCC_OUT_VERTICAL + ov) *
                                   S3D_OCC_OUT_LATERAL + ol) *
                                  S3D_OCC_OUT_FORWARD + of;
                    if (to_float(out[out_idx]) <= 0.0f) continue;
                    int in_idx = ((ov + kv) * OCCUPANCY_LATERAL_BINS +
                                  ol + kl) * OCCUPANCY_FORWARD_BINS + of + kf;
                    int grad_idx = offset + oc * S3D_OCC_OUT_VERTICAL *
                                   S3D_OCC_OUT_LATERAL * S3D_OCC_OUT_FORWARD +
                                   ov * S3D_OCC_OUT_LATERAL * S3D_OCC_OUT_FORWARD +
                                   ol * S3D_OCC_OUT_FORWARD + of;
                    sum += to_float(grad[b * row_stride + grad_idx]) *
                           to_float(input[b * OBS_SIZE + OCCUPANCY_OFFSET + in_idx]);
                }
            }
        }
    }
    wgrad[idx] = from_float(sum);
}

__global__ void s3d_depth_bias_grad_kernel(
        precision_t* __restrict__ bgrad, const precision_t* __restrict__ grad,
        const precision_t* __restrict__ out, int B, int row_stride, int offset) {
    int oc = blockIdx.x * blockDim.x + threadIdx.x;
    if (oc >= S3D_DEPTH_CHANNELS) return;
    float sum = 0.0f;
    for (int b = 0; b < B; b++) {
        for (int oh = 0; oh < S3D_DEPTH_OUT_HEIGHT; oh++) {
            for (int ow = 0; ow < S3D_DEPTH_OUT_WIDTH; ow++) {
                int out_idx = ((b * S3D_DEPTH_CHANNELS + oc) *
                               S3D_DEPTH_OUT_HEIGHT + oh) *
                              S3D_DEPTH_OUT_WIDTH + ow;
                if (to_float(out[out_idx]) > 0.0f)
                    sum += to_float(grad[b * row_stride + offset +
                                         oc * S3D_DEPTH_OUT_HEIGHT * S3D_DEPTH_OUT_WIDTH +
                                         oh * S3D_DEPTH_OUT_WIDTH + ow]);
            }
        }
    }
    bgrad[oc] = from_float(sum);
}

__global__ void s3d_occupancy_bias_grad_kernel(
        precision_t* __restrict__ bgrad, const precision_t* __restrict__ grad,
        const precision_t* __restrict__ out, int B, int row_stride, int offset) {
    int oc = blockIdx.x * blockDim.x + threadIdx.x;
    if (oc >= S3D_OCC_BIAS_STORAGE) return;
    if (oc >= S3D_OCC_CHANNELS) {
        bgrad[oc] = from_float(0.0f);
        return;
    }
    float sum = 0.0f;
    for (int b = 0; b < B; b++) {
        for (int ov = 0; ov < S3D_OCC_OUT_VERTICAL; ov++) {
            for (int ol = 0; ol < S3D_OCC_OUT_LATERAL; ol++) {
                for (int of = 0; of < S3D_OCC_OUT_FORWARD; of++) {
                    int out_idx = (((b * S3D_OCC_CHANNELS + oc) *
                                    S3D_OCC_OUT_VERTICAL + ov) *
                                   S3D_OCC_OUT_LATERAL + ol) *
                                  S3D_OCC_OUT_FORWARD + of;
                    if (to_float(out[out_idx]) > 0.0f) {
                        int grad_idx = offset + oc * S3D_OCC_OUT_VERTICAL *
                                       S3D_OCC_OUT_LATERAL * S3D_OCC_OUT_FORWARD +
                                       ov * S3D_OCC_OUT_LATERAL * S3D_OCC_OUT_FORWARD +
                                       ol * S3D_OCC_OUT_FORWARD + of;
                        sum += to_float(grad[b * row_stride + grad_idx]);
                    }
                }
            }
        }
    }
    bgrad[oc] = from_float(sum);
}

__global__ void s3d_projection_bias_grad_kernel(
        precision_t* __restrict__ bgrad, const precision_t* __restrict__ grad,
        int B, int features) {
    int feature = blockIdx.x * blockDim.x + threadIdx.x;
    if (feature >= features) return;
    float sum = 0.0f;
    for (int b = 0; b < B; b++)
        sum += to_float(grad[b * features + feature]);
    bgrad[feature] = from_float(sum);
}

static PrecisionTensor s3d_encoder_forward(void* w, void* activations,
        PrecisionTensor input, cudaStream_t stream) {
    S3DEncoderWeights* ew = (S3DEncoderWeights*)w;
    S3DEncoderActivations* a = (S3DEncoderActivations*)activations;
    int B = input.shape[0];
    if (a->saved_input.data)
        puf_copy(&a->saved_input, &input, stream);

    s3d_scalar1_relu_kernel<<<grid_size(B * S3D_SCALAR_HIDDEN), BLOCK_SIZE, 0, stream>>>(
        a->scalar1_out.data, input.data, ew->scalar1_w.data, ew->scalar1_b.data, B);
    s3d_scalar2_relu_kernel<<<grid_size(B * S3D_SCALAR_HIDDEN), BLOCK_SIZE, 0, stream>>>(
        a->scalar_out.data, a->scalar1_out.data,
        ew->scalar2_w.data, ew->scalar2_b.data, B);
    s3d_depth_conv_kernel<<<grid_size(B * S3D_DEPTH_FEATURES), BLOCK_SIZE, 0, stream>>>(
        a->depth_out.data, input.data, ew->depth_w.data, ew->depth_b.data, B);
    s3d_occupancy_conv_kernel<<<grid_size(B * S3D_OCC_FEATURES), BLOCK_SIZE, 0, stream>>>(
        a->occupancy_out.data, input.data, ew->occupancy_w.data,
        ew->occupancy_b.data, B);
    s3d_concat_kernel<<<grid_size(B * S3D_CONCAT), BLOCK_SIZE, 0, stream>>>(
        a->concat.data, a->scalar_out.data, a->depth_out.data,
        a->occupancy_out.data, B);
    puf_mm(&a->concat, &ew->proj_w, &a->out, stream);
    s3d_bias_relu_kernel<<<grid_size(B * ew->hidden), BLOCK_SIZE, 0, stream>>>(
        a->out.data, ew->proj_b.data, B * ew->hidden, ew->hidden);
    return a->out;
}

static void s3d_encoder_backward(void* w, void* activations,
        PrecisionTensor grad, cudaStream_t stream) {
    S3DEncoderWeights* ew = (S3DEncoderWeights*)w;
    S3DEncoderActivations* a = (S3DEncoderActivations*)activations;
    int B = grad.shape[0];

    s3d_relu_backward_kernel<<<grid_size(B * ew->hidden), BLOCK_SIZE, 0, stream>>>(
        grad.data, a->out.data, B * ew->hidden);
    puf_mm_tn(&grad, &a->concat, &a->proj_wgrad, stream);
    s3d_projection_bias_grad_kernel<<<grid_size(ew->hidden), BLOCK_SIZE, 0, stream>>>(
        a->proj_bgrad.data, grad.data, B, ew->hidden);

    // The concat buffer is no longer needed after dW. Reuse it for the branch
    // gradients so the convolutional maps do not need duplicate grad storage.
    puf_mm_nn(&grad, &ew->proj_w, &a->concat, stream);

    s3d_dense_wgrad_kernel<<<grid_size(S3D_SCALAR_HIDDEN * S3D_SCALAR_HIDDEN),
                             BLOCK_SIZE, 0, stream>>>(
        a->scalar2_wgrad.data, a->scalar1_out.data, a->concat.data,
        a->scalar_out.data, B, S3D_SCALAR_HIDDEN, 0, S3D_SCALAR_HIDDEN,
        S3D_SCALAR_HIDDEN, S3D_CONCAT, 0);
    s3d_bias_grad_kernel<<<grid_size(S3D_SCALAR_HIDDEN), BLOCK_SIZE, 0, stream>>>(
        a->concat.data, a->scalar_out.data, a->scalar2_bgrad.data,
        B, S3D_SCALAR_HIDDEN, S3D_CONCAT, 0);

    s3d_scalar1_input_grad_kernel<<<grid_size(B * S3D_SCALAR_HIDDEN),
                                    BLOCK_SIZE, 0, stream>>>(
        a->scalar1_grad.data, a->concat.data, a->scalar1_out.data,
        a->scalar_out.data, ew->scalar2_w.data, B, S3D_CONCAT, 0);
    s3d_dense_wgrad_kernel<<<grid_size(S3D_SCALAR_HIDDEN * OBS_SCALAR_SIZE),
                             BLOCK_SIZE, 0, stream>>>(
        a->scalar1_wgrad.data, a->saved_input.data, a->scalar1_grad.data,
        a->scalar1_out.data, B, OBS_SIZE, 0, OBS_SCALAR_SIZE,
        S3D_SCALAR_HIDDEN, S3D_SCALAR_HIDDEN, 0);
    s3d_bias_grad_kernel<<<grid_size(S3D_SCALAR_HIDDEN), BLOCK_SIZE, 0, stream>>>(
        a->scalar1_grad.data, a->scalar1_out.data, a->scalar1_bgrad.data,
        B, S3D_SCALAR_HIDDEN, S3D_SCALAR_HIDDEN, 0);

    int depth_offset = S3D_SCALAR_HIDDEN;
    s3d_depth_wgrad_kernel<<<grid_size(S3D_DEPTH_CHANNELS * S3D_DEPTH_KERNEL *
                                        S3D_DEPTH_KERNEL), BLOCK_SIZE, 0, stream>>>(
        a->depth_wgrad.data, a->saved_input.data, a->concat.data,
        a->depth_out.data, B, S3D_CONCAT, depth_offset);
    s3d_depth_bias_grad_kernel<<<grid_size(S3D_DEPTH_CHANNELS), BLOCK_SIZE, 0, stream>>>(
        a->depth_bgrad.data, a->concat.data, a->depth_out.data,
        B, S3D_CONCAT, depth_offset);

    int occupancy_offset = S3D_SCALAR_HIDDEN + S3D_DEPTH_FEATURES;
    s3d_occupancy_wgrad_kernel<<<grid_size(S3D_OCC_CHANNELS *
                                            S3D_OCC_KERNEL_VERTICAL *
                                            S3D_OCC_KERNEL_LATERAL *
                                            S3D_OCC_KERNEL_FORWARD),
                                 BLOCK_SIZE, 0, stream>>>(
        a->occupancy_wgrad.data, a->saved_input.data, a->concat.data,
        a->occupancy_out.data, B, S3D_CONCAT, occupancy_offset);
    s3d_occupancy_bias_grad_kernel<<<grid_size(S3D_OCC_BIAS_STORAGE), BLOCK_SIZE, 0, stream>>>(
        a->occupancy_bgrad.data, a->concat.data, a->occupancy_out.data,
        B, S3D_CONCAT, occupancy_offset);
}

static void s3d_encoder_init_weights(void* w, ulong* seed, cudaStream_t stream) {
    S3DEncoderWeights* ew = (S3DEncoderWeights*)w;
    puf_kaiming_init(&ew->scalar1_w, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&ew->scalar2_w, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&ew->depth_w, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&ew->occupancy_w, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&ew->proj_w, 1.0f, (*seed)++, stream);
    cudaMemsetAsync(ew->scalar1_b.data, 0,
        numel(ew->scalar1_b.shape) * sizeof(precision_t), stream);
    cudaMemsetAsync(ew->scalar2_b.data, 0,
        numel(ew->scalar2_b.shape) * sizeof(precision_t), stream);
    cudaMemsetAsync(ew->depth_b.data, 0,
        numel(ew->depth_b.shape) * sizeof(precision_t), stream);
    cudaMemsetAsync(ew->occupancy_b.data, 0,
        numel(ew->occupancy_b.shape) * sizeof(precision_t), stream);
    cudaMemsetAsync(ew->proj_b.data, 0,
        numel(ew->proj_b.shape) * sizeof(precision_t), stream);
}

static void s3d_encoder_reg_params(void* w, Allocator* alloc) {
    S3DEncoderWeights* ew = (S3DEncoderWeights*)w;
    ew->scalar1_w = {.shape = {S3D_SCALAR_HIDDEN, OBS_SCALAR_SIZE}};
    ew->scalar1_b = {.shape = {S3D_SCALAR_HIDDEN}};
    ew->scalar2_w = {.shape = {S3D_SCALAR_HIDDEN, S3D_SCALAR_HIDDEN}};
    ew->scalar2_b = {.shape = {S3D_SCALAR_HIDDEN}};
    ew->depth_w = {.shape = {S3D_DEPTH_CHANNELS,
                             S3D_DEPTH_KERNEL * S3D_DEPTH_KERNEL}};
    ew->depth_b = {.shape = {S3D_DEPTH_CHANNELS}};
    ew->occupancy_w = {.shape = {S3D_OCC_CHANNELS,
                                 S3D_OCC_KERNEL_VERTICAL *
                                 S3D_OCC_KERNEL_LATERAL *
                                 S3D_OCC_KERNEL_FORWARD}};
    ew->occupancy_b = {.shape = {S3D_OCC_BIAS_STORAGE}};
    ew->proj_w = {.shape = {ew->hidden, S3D_CONCAT}};
    ew->proj_b = {.shape = {ew->hidden}};
    alloc_register(alloc, &ew->scalar1_w);
    alloc_register(alloc, &ew->scalar1_b);
    alloc_register(alloc, &ew->scalar2_w);
    alloc_register(alloc, &ew->scalar2_b);
    alloc_register(alloc, &ew->depth_w);
    alloc_register(alloc, &ew->depth_b);
    alloc_register(alloc, &ew->occupancy_w);
    alloc_register(alloc, &ew->occupancy_b);
    alloc_register(alloc, &ew->proj_w);
    alloc_register(alloc, &ew->proj_b);
}

static void s3d_encoder_reg_train(void* w, void* activations,
        Allocator* acts, Allocator* grads, int B_TT) {
    S3DEncoderWeights* ew = (S3DEncoderWeights*)w;
    S3DEncoderActivations* a = (S3DEncoderActivations*)activations;
    *a = {};
    a->saved_input = {.shape = {B_TT, ew->obs_size}};
    a->scalar1_out = {.shape = {B_TT, S3D_SCALAR_HIDDEN}};
    a->scalar_out = {.shape = {B_TT, S3D_SCALAR_HIDDEN}};
    a->scalar1_grad = {.shape = {B_TT, S3D_SCALAR_HIDDEN}};
    a->depth_out = {.shape = {B_TT, S3D_DEPTH_FEATURES}};
    a->occupancy_out = {.shape = {B_TT, S3D_OCC_FEATURES}};
    a->concat = {.shape = {B_TT, S3D_CONCAT}};
    a->out = {.shape = {B_TT, ew->hidden}};
    alloc_register(acts, &a->saved_input);
    alloc_register(acts, &a->scalar1_out);
    alloc_register(acts, &a->scalar_out);
    alloc_register(acts, &a->scalar1_grad);
    alloc_register(acts, &a->depth_out);
    alloc_register(acts, &a->occupancy_out);
    alloc_register(acts, &a->concat);
    alloc_register(acts, &a->out);

    a->scalar1_wgrad = {.shape = {S3D_SCALAR_HIDDEN, OBS_SCALAR_SIZE}};
    a->scalar1_bgrad = {.shape = {S3D_SCALAR_HIDDEN}};
    a->scalar2_wgrad = {.shape = {S3D_SCALAR_HIDDEN, S3D_SCALAR_HIDDEN}};
    a->scalar2_bgrad = {.shape = {S3D_SCALAR_HIDDEN}};
    a->depth_wgrad = {.shape = {S3D_DEPTH_CHANNELS,
                                S3D_DEPTH_KERNEL * S3D_DEPTH_KERNEL}};
    a->depth_bgrad = {.shape = {S3D_DEPTH_CHANNELS}};
    a->occupancy_wgrad = {.shape = {S3D_OCC_CHANNELS,
                                    S3D_OCC_KERNEL_VERTICAL *
                                    S3D_OCC_KERNEL_LATERAL *
                                    S3D_OCC_KERNEL_FORWARD}};
    a->occupancy_bgrad = {.shape = {S3D_OCC_BIAS_STORAGE}};
    a->proj_wgrad = {.shape = {ew->hidden, S3D_CONCAT}};
    a->proj_bgrad = {.shape = {ew->hidden}};
    alloc_register(grads, &a->scalar1_wgrad);
    alloc_register(grads, &a->scalar1_bgrad);
    alloc_register(grads, &a->scalar2_wgrad);
    alloc_register(grads, &a->scalar2_bgrad);
    alloc_register(grads, &a->depth_wgrad);
    alloc_register(grads, &a->depth_bgrad);
    alloc_register(grads, &a->occupancy_wgrad);
    alloc_register(grads, &a->occupancy_bgrad);
    alloc_register(grads, &a->proj_wgrad);
    alloc_register(grads, &a->proj_bgrad);
}

static void s3d_encoder_reg_rollout(void* w, void* activations,
        Allocator* alloc, int B) {
    S3DEncoderWeights* ew = (S3DEncoderWeights*)w;
    S3DEncoderActivations* a = (S3DEncoderActivations*)activations;
    *a = {};
    a->scalar1_out = {.shape = {B, S3D_SCALAR_HIDDEN}};
    a->scalar_out = {.shape = {B, S3D_SCALAR_HIDDEN}};
    a->depth_out = {.shape = {B, S3D_DEPTH_FEATURES}};
    a->occupancy_out = {.shape = {B, S3D_OCC_FEATURES}};
    a->concat = {.shape = {B, S3D_CONCAT}};
    a->out = {.shape = {B, ew->hidden}};
    alloc_register(alloc, &a->scalar1_out);
    alloc_register(alloc, &a->scalar_out);
    alloc_register(alloc, &a->depth_out);
    alloc_register(alloc, &a->occupancy_out);
    alloc_register(alloc, &a->concat);
    alloc_register(alloc, &a->out);
}

static void* s3d_encoder_create_weights(void* self) {
    Encoder* e = (Encoder*)self;
    if (e->in_dim != OBS_SIZE) {
        fprintf(stderr, "shenaniguns3d encoder: obs size %d != expected %d\n",
                e->in_dim, OBS_SIZE);
        exit(1);
    }
    if (e->out_dim <= 0 || e->out_dim % 8 != 0) {
        fprintf(stderr, "shenaniguns3d encoder: hidden size %d must be a multiple of 8\n",
                e->out_dim);
        exit(1);
    }
    S3DEncoderWeights* ew =
        (S3DEncoderWeights*)calloc(1, sizeof(S3DEncoderWeights));
    ew->obs_size = e->in_dim;
    ew->hidden = e->out_dim;
    return ew;
}

static void create_shenaniguns3d_encoder(Encoder* enc) {
    *enc = Encoder{
        .forward = s3d_encoder_forward,
        .backward = s3d_encoder_backward,
        .init_weights = s3d_encoder_init_weights,
        .reg_params = s3d_encoder_reg_params,
        .reg_train = s3d_encoder_reg_train,
        .reg_rollout = s3d_encoder_reg_rollout,
        .create_weights = s3d_encoder_create_weights,
        .in_dim = enc->in_dim,
        .out_dim = enc->out_dim,
        .activation_size = sizeof(S3DEncoderActivations),
    };
}
