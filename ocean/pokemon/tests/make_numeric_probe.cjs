// Generate an isolated native build with stage-specific finite-value checks.
// Production sources and binaries are never edited by this generator.
const fs = require('node:fs');
const root = 'build/pokemon';
fs.mkdirSync(root, {recursive:true});
let source = fs.readFileSync('src/pufferl.cu', 'utf8');
let algo = fs.readFileSync('src/algo.cu', 'utf8');
if (process.argv.includes('--force-replay-endpoint')) {
    const draw = '    float u = curand_uniform(&rng_state);';
    if (algo.split(draw).length !== 2) throw Error('Replay sampler draw changed');
    algo = algo.replace(draw, '    float u = 1.0f; // diagnostic: valid RNG endpoint');
}
const gradientStore = '                a.grad_logits[grad_logits_base + logits_offset + j] = d_logit;';
if (algo.split(gradientStore).length !== 2) throw Error('PPO gradient store changed');
algo = '__device__ int pokemon_ppo_failed = 0;\n' + algo.replace(gradientStore, `
                if (!isfinite(d_logit) && atomicCAS(&pokemon_ppo_failed, 0, 1) == 0) {
                    printf("POKEMON_PPO_FAILURE row=%d head=%d action=%d chosen=%d legal=%d "
                        "old_lp=%g adv=%g mean=%g var=%g norm_adv=%g weight=%g "
                        "new_lp=%g ratio=%g d_new_lp=%g logit=%g lse=%g logp=%g p=%g "
                        "entropy=%g ent_coef=%g reach=%g magnet_lse=%g magnet_logp=%g q=%g "
                        "value=%g return=%g pred=%g\\n",
                        nt,h,j,act,(int)puf_mask_bit(a.action_mask,mask_base,logits_offset+j),
                        old_logp,adv,a.adv_mean[0],a.adv_var[0],adv_normalized,w,
                        total_log_prob,ratio,d_new_logp,l,logsumexp,logp,p,
                        ent,ent_coef,reach,magnet_logsumexp,magnet_logp,q,val,ret,val_pred);
                    __threadfence_system();
                    asm("trap;");
                }
` + gradientStore);
function replaceOnce(needle, replacement) {
    if (source.split(needle).length !== 2) throw Error('Probe insertion changed: '+needle);
    source = source.replace(needle, replacement);
}
replaceOnce('#include "protein.cu"', `#include "protein.cu"

// Stage IDs: 1 magnet forward, 2 learner forward, 3 loss/logit gradient,
// 4 value gradient, 5 backpropagated gradients, 6 optimizer weights,
// 7 optimizer momentum. A trap deliberately stops this diagnostic process.
__device__ int pokemon_numeric_failed = 0;
template<typename T>
__global__ void pokemon_numeric_check(const T* data, long n, int stage) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n && !isfinite((float)data[i]) &&
            atomicCAS(&pokemon_numeric_failed, 0, 1) == 0) {
        printf("POKEMON_NUMERIC_FAILURE stage=%d index=%ld value=%g\\n",
            stage, i, (float)data[i]);
        __threadfence_system();
        asm("trap;");
    }
}
template<typename Tensor>
static void pokemon_numeric_audit(Tensor tensor, int stage, cudaStream_t stream) {
    long n = numel(tensor.shape);
    pokemon_numeric_check<<<grid_size(n), BLOCK_SIZE, 0, stream>>>(tensor.data, n, stage);
}
`);
replaceOnce('            ppo_loss_fwd_bwd(dec_puf, p_logstd, magnet_out, magnet_logstd, graph,',
`            if (hypers.emag_kl_coef > 0.0f) pokemon_numeric_audit(magnet_out, 1, stream);
            pokemon_numeric_audit(dec_puf, 2, stream);
            ppo_loss_fwd_bwd(dec_puf, p_logstd, magnet_out, magnet_logstd, graph,`);
replaceOnce('            policy_backward(&pufferl.policy, pufferl.weights, pufferl.train_activations,',
`            pokemon_numeric_audit(grad_logits_puf, 3, stream);
            pokemon_numeric_audit(grad_values_puf, 4, stream);
            policy_backward(&pufferl.policy, pufferl.weights, pufferl.train_activations,`);
replaceOnce('            muon_step(&pufferl.muon, pufferl.master_weights,',
`            pokemon_numeric_audit(pufferl.grad_puf, 5, stream);
            muon_step(&pufferl.muon, pufferl.master_weights,`);
replaceOnce('                pufferl.grad_puf, hypers.max_grad_norm, stream);',
`                pufferl.grad_puf, hypers.max_grad_norm, stream);
            pokemon_numeric_audit(pufferl.master_weights, 6, stream);
            pokemon_numeric_audit(pufferl.muon.mb_puf, 7, stream);`);
replaceOnce('        // This version is consistent with PufferLib 3.0.',
`        // Diagnostic-only synchronization: never continue or save a checkpoint
        // after a device trap. The production loop does not add this overhead.
        cudaError_t numeric_status = cudaStreamSynchronize(train_stream);
        if (numeric_status != cudaSuccess) {
            fprintf(stderr, "Pokemon numeric audit stopped before checkpoint save: %s\\n",
                cudaGetErrorString(numeric_status));
            exit(86);
        }

        // This version is consistent with PufferLib 3.0.`);
fs.writeFileSync(root+'/algo_numeric_probe.cu', algo);
source = source.replace('#include "algo.cu"', '#include "algo_numeric_probe.cu"');
fs.writeFileSync(root+'/pufferl_numeric_probe.cu', source);
fs.writeFileSync(root+'/build_numeric_probe.sh',
    fs.readFileSync('build.sh','utf8').replaceAll('src/pufferl.cu',root+'/pufferl_numeric_probe.cu'));
console.log('Generated isolated numeric probe; shared sources unchanged');
