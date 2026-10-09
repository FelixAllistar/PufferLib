// Diagnostic only: constructs and closes the real trainer without collecting
// rollouts, performing PPO updates, creating run artifacts or loading opponents.
#include "../../src/pufferl.cu"

int main(int argc, char** argv) {
    Ini ini = {0};
    puf_ini_load_env(&ini, "kaggriculture", argc - 1, argv + 1);
    kag_configure_potential(&ini, "train");
    puf_ini_put(&ini, "selfplay.enabled", "0");
    puf_ini_put(&ini, "vec.num_policies", "1");
    puf_ini_put(&ini, "vec.hist_policy_percent", "0");
    TrainContext context = {.rank = 0, .world_size = 1, .gpu_id = 0, .artifact_owner = 0};
    PuffeRL* trainer = create_pufferl(&ini, &context);
    assert(cudaDeviceSynchronize() == cudaSuccess);
    size_t free_bytes, total_bytes;
    assert(cudaMemGetInfo(&free_bytes, &total_bytes) == cudaSuccess);
    assert(trainer->num_policies == 1);
    assert(trainer->train_rollouts.observations.shape[0] == trainer->hypers.total_agents);
    printf("ALLOCATION_ONLY_PASS agents=%d policies=%d train_agents=%ld used_gib=%.3f free_gib=%.3f no_rollouts_no_updates\n",
        trainer->hypers.total_agents, trainer->num_policies,
        (long)trainer->train_rollouts.observations.shape[0],
        (total_bytes - free_bytes) / (1024.0 * 1024.0 * 1024.0),
        free_bytes / (1024.0 * 1024.0 * 1024.0));
    close_pufferl(trainer);
    puf_ini_free(&ini);
}
