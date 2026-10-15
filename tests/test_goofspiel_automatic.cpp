// CPU-only contracts for latest/metadata resolution and coupled sweep layouts.
#include "../ocean/goofspiel/checkpoint.h"
#include "../ocean/goofspiel/training_config.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <string>

namespace fs = std::filesystem;
static void put(Ini* ini, const char* key, long long value) {
    char text[64]; snprintf(text, sizeof(text), "%lld", value);
    puf_ini_put(ini, key, text);
}
static void file(const fs::path& path, long nanos) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << "contract fixture, not model weights\n";
    struct timespec times[2] = {{1000, nanos}, {1000, nanos}};
    assert(utimensat(AT_FDCWD, path.c_str(), times, 0) == 0);
}

int main() {
    char pattern[] = "/tmp/goofspiel-automatic-contract-XXXXXX";
    char* dir = mkdtemp(pattern); assert(dir);
    fs::path root(dir), checkpoints = root / "checkpoints";
    Ini ini = {0};
    puf_ini_load_env(&ini, "goofspiel", 0, nullptr);
    assert(!strcmp(puf_ini_get_str(&ini, "sweep", "metric"), "best_exploitability"));
    assert(puf_ini_get(&ini, "base", "eval_episodes") == 0);
    assert(puf_ini_get(&ini, "base", "checkpoint_interval") == -1);
    assert(!gs_training_config_error(&ini, 8));
    puf_ini_put(&ini, "base.checkpoint_dir", checkpoints.c_str());
    puf_ini_put(&ini, "base.log_dir", (root / "logs").c_str());
    char resolved[4096];
    assert(!gs_resolve_checkpoint(&ini, "latest", resolved, sizeof(resolved)));
    fs::path older = checkpoints / "goofspiel/old/0000000000004096.bin";
    fs::path newest = checkpoints / "goofspiel/new/0000000000008192.bin";
    file(older, 100); file(newest, 200);
    file(checkpoints / "goofspiel/new/not_a_checkpoint.bin.exact", 500);
    file(checkpoints / "different_environment/newest.bin", 900);
    fs::create_directory_symlink(checkpoints, checkpoints / "goofspiel/cycle");
    std::ofstream(checkpoints / "goofspiel/empty.bin");
    assert(gs_resolve_checkpoint(&ini, "latest", resolved, sizeof(resolved)));
    assert(resolved == newest.string()); // Nanosecond ordering within one second.
    assert(gs_resolve_checkpoint(&ini, nullptr, resolved, sizeof(resolved)));
    assert(resolved == newest.string());
    assert(!strcmp(gs_resolve_checkpoint(&ini, "uniform", resolved, sizeof(resolved)), "uniform"));

    puf_ini_put(&ini, "policy.architecture", "transformer");
    put(&ini, "policy.hidden_size", 64);
    put(&ini, "policy.num_layers", 3);
    gs_save_checkpoint_settings(newest.c_str(), &ini);
    puf_ini_put(&ini, "policy.architecture", "puffernet");
    put(&ini, "policy.hidden_size", 32);
    put(&ini, "policy.num_layers", 2);
    put(&ini, "train.total_timesteps", 123456);
    assert(gs_load_checkpoint_settings(&ini, newest.c_str(), 0, nullptr));
    assert(!strcmp(puf_ini_get_str(&ini, "policy", "architecture"), "transformer"));
    assert(puf_ini_get(&ini, "policy", "hidden_size") == 64);
    assert(puf_ini_get(&ini, "policy", "num_layers") == 3);
    assert(puf_ini_get(&ini, "train", "total_timesteps") == 123456);
    char override[] = "--policy.hidden_size=16";
    char* args[] = {override};
    assert(gs_load_checkpoint_settings(&ini, newest.c_str(), 1, args));
    assert(puf_ini_get(&ini, "policy", "hidden_size") == 16);
    // Legacy log fallback works without requiring checkpoint filenames from users.
    fs::create_directories(root / "logs/goofspiel");
    fs::copy_file(newest.string() + ".ini", root / "logs/goofspiel/old.ini");
    assert(gs_load_checkpoint_settings(&ini, older.c_str(), 0, nullptr));
    assert(puf_ini_get(&ini, "policy", "hidden_size") == 64);
    puf_ini_free(&ini);

    puf_ini_load_env(&ini, "goofspiel", 0, nullptr);
    put(&ini, "train.total_timesteps", 1048576);
    gs_prepare_sweep(&ini);
    assert(puf_ini_get(&ini, "sweep.train.total_timesteps", "min") == 1048576);
    assert(puf_ini_get(&ini, "sweep.train.total_timesteps", "max") == 1048576);
    put(&ini, "train.total_timesteps", 50000000);
    int accepted = 0, rejected = 0;
    for (int agents = 256; agents <= 262144; agents *= 2)
    for (int buffers : {1, 2, 4})
    for (int threads : {1, 2, 4, 8})
    for (int horizon : {4, 8, 16, 64, 128})
    for (int minibatch = 256; minibatch <= 32768; minibatch *= 2)
    for (double fraction : {.125, .375, .5, .875, 1.0})
    for (int policies : {2, 5, 9}) {
        put(&ini, "vec.total_agents", agents); put(&ini, "vec.num_buffers", buffers);
        put(&ini, "vec.num_threads", threads); put(&ini, "train.horizon", horizon);
        put(&ini, "train.minibatch_size", minibatch);
        put(&ini, "vec.num_policies", policies);
        char value[64]; snprintf(value, sizeof(value), "%.3f", fraction);
        puf_ini_put(&ini, "vec.hist_policy_percent", value);
        int frozen = (int)((agents / buffers / 2) * fraction);
        int learner = agents - frozen * buffers;
        bool valid = threads >= buffers && threads % buffers == 0
            && frozen >= policies - 1 && agents % (minibatch / horizon) == 0
            && learner % (minibatch / horizon) == 0;
        const char* error = gs_training_config_error(&ini, 4);
        assert(valid == (error == nullptr));
        assert((valid && horizon % 8 == 0) == (gs_training_config_error(&ini, 8) == nullptr));
        valid ? ++accepted : ++rejected;
    }
    assert(accepted && rejected);
    puf_ini_free(&ini);

    puf_ini_load_env(&ini, "goofspiel", 0, nullptr);
    put(&ini, "env.exact_exploiter", 1);
    put(&ini, "selfplay.enabled", 0);
    assert(strstr(gs_training_config_error(&ini, 4), "exact_exploiter requires"));
    put(&ini, "selfplay.enabled", 1);
    put(&ini, "env.exact_exploiter_banks", 5);
    assert(strstr(gs_training_config_error(&ini, 4), "enough frozen"));
    put(&ini, "env.exact_exploiter_banks", 4);
    assert(!gs_training_config_error(&ini, 4));
    put(&ini, "train.vtrace", 1);
    puf_ini_put(&ini, "train.vtrace_rho_clip", ".5");
    assert(strstr(gs_training_config_error(&ini, 4), "vtrace_c_clip"));
    puf_ini_put(&ini, "train.vtrace_c_clip", ".5");
    assert(!gs_training_config_error(&ini, 4));
    puf_ini_free(&ini);

    for (const char* model : {"puffernet", "transformer"}) {
        puf_ini_load_env(&ini, "goofspiel", 0, nullptr);
        puf_ini_put(&ini, "policy.architecture", model);
        put(&ini, "sweep.memory_limit_mb", 3072);
        assert(!gs_training_config_error(&ini, 4));
        put(&ini, "sweep.memory_limit_mb", 1);
        assert(strstr(gs_training_config_error(&ini, 4), "estimated training/evaluation memory"));
        put(&ini, "sweep.memory_limit_mb", 3072);
        put(&ini, "vec.total_agents", 262144);
        put(&ini, "train.horizon", 128);
        put(&ini, "train.minibatch_size", 32768);
        put(&ini, "policy.hidden_size", 256);
        put(&ini, "policy.num_layers", 8);
        assert(strstr(gs_training_config_error(&ini, 4), "estimated training/evaluation memory"));
        puf_ini_free(&ini);
    }
    fs::remove_all(root);
    printf("Automatic evaluation contracts passed; %d valid and %d rejected sweep layouts.\n", accepted, rejected);
}
