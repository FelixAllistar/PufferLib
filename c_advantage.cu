#include <torch/extension.h>

__global__ void advantage_kernel(
    float* reward_block,    // [num_steps, horizon]
    float* reward_mask,     // [num_steps, horizon]
    float* values_mean,     // [num_steps, horizon]
    float* values_std,      // [num_steps, horizon]
    float* returns,         // [num_steps, horizon]
    float* rewards,         // [num_steps]
    float* dones,           // [num_steps]
    int num_steps,
    int horizon,
    float r_std
) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= num_steps) return;

    int k = 0;

    for (int j = 0; j < horizon; j++) {
        int t = i + j;
        if (t >= num_steps - 1 || dones[t]) {
            break;
        }
        k = j + 1;
    }

    if (k == 0) {
        int idx = i * horizon;
        returns[idx] = 0.0f;
        return;
    }

    float gamma_sum = 0.0f;
    for (int j = k-2; j > 0; j--) {
        int t = i + j;
        int idx = i * horizon + j;
        float reward = rewards[t + 1];
        reward_block[idx] = reward;
        reward_mask[idx] = 1.0f;

        float vstd = values_std[idx];
        if (vstd == 0.0f || r_std == 0.0f) {
            returns[idx] = 0;
            continue;
        }

        float gamma = 1.0f/(vstd*vstd) - (1.0f/(r_std*r_std));

        if (gamma < 0.0f) {
            gamma = 0.0f;
        }

        returns[idx] = gamma;
        gamma_sum += gamma;
    }

    if (gamma_sum == 0) {
        return;
    }

    float R = 0.0f;
    for (int j = k-2; j > 0; j--) {
        int idx = i * horizon + j;
        float gamma = returns[idx];
        R += gamma*reward_block[idx]/gamma_sum;
        returns[idx] = R;
    }
}

// Pybind11 module definition
PYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {
    m.def("advantage_kernel", [](torch::Tensor reward_block,
                                torch::Tensor reward_mask,
                                torch::Tensor values_mean,
                                torch::Tensor values_std,
                                torch::Tensor returns,
                                torch::Tensor rewards,
                                torch::Tensor dones,
                                int num_steps,
                                int horizon,
                                float vstd_max) {
        // Launch the kernel
        int threads_per_block = 256;
        int blocks = (num_steps + threads_per_block - 1) / threads_per_block;

        advantage_kernel<<<blocks, threads_per_block>>>(
            reward_block.data_ptr<float>(),
            reward_mask.data_ptr<float>(),
            values_mean.data_ptr<float>(),
            values_std.data_ptr<float>(),
            returns.data_ptr<float>(),
            rewards.data_ptr<float>(),
            dones.data_ptr<float>(),
            num_steps,
            horizon,
            vstd_max
        );

        // Check for CUDA errors
        cudaError_t err = cudaGetLastError();
        if (err != cudaSuccess) {
            throw std::runtime_error(cudaGetErrorString(err));
        }
    }, "Compute advantages with CUDA");
}
