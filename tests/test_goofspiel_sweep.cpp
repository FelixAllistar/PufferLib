// CPU-only contracts. The executable also acts as a fake trainer to test exec,
// output capture and signals. No CUDA initialization, learning or Python occurs.
#define GS_SWEEP_NO_MAIN
#include "../ocean/goofspiel/sweep.cpp"
#include <fstream>
#include <iterator>

static std::string read_text(const fs::path& path) {
    std::ifstream in(path);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
static int tests = 0;
static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

struct Sandbox {
    fs::path path;
    Sandbox() {
        char pattern[] = "/tmp/goofspiel-sweep-contract-XXXXXX";
        char* result = mkdtemp(pattern);
        check(result != nullptr, "mkdtemp");
        path = result;
    }
    ~Sandbox() { fs::remove_all(path); }
};

static pid_t start(const std::vector<std::string>& args, const fs::path& output) {
    fflush(nullptr);
    pid_t child = fork();
    check(child >= 0, "fork contract");
    if (!child) {
        FILE* stream = freopen(output.c_str(), "w", stdout);
        if (!stream || dup2(STDOUT_FILENO, STDERR_FILENO) < 0) _exit(99);
        std::vector<char*> argv;
        for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        int code = 0;
        try { code = gs_sweep::main((int)argv.size(), argv.data()); }
        catch (const std::exception& e) { std::cerr << e.what() << '\n'; code = 1; }
        fflush(nullptr);
        _exit(code);
    }
    return child;
}
static int finish(pid_t child) {
    int status = 0;
    check(waitpid(child, &status, 0) == child, "wait contract");
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
static int run(const std::vector<std::string>& args, const fs::path& output) {
    return finish(start(args, output));
}
static void fixed(Ini& ini, const std::string& key, double expected) {
    check(gs_sweep::number(ini, key) == expected, "wrong fixed setting");
    check(gs_sweep::number(ini, "sweep." + key + ".min") == expected, "fixed minimum not repinned");
    check(gs_sweep::number(ini, "sweep." + key + ".max") == expected, "fixed maximum not repinned");
}

int main(int argc, char** argv) {
    // Tests execute this binary through the real launcher's fork/exec path.
    if (argc >= 2 && std::string(argv[1]) == "sweep") {
        gs_sweep::Config c;
        puf_ini_load_env(&c.ini, "goofspiel", argc - 2, argv + 2);
        check(gs_sweep::value(c.ini, "sweep.metric") == "best_exploitability", "wrong forwarded metric");
        check(gs_sweep::validate_ranges(c.ini) == 36, "wrong forwarded search space");
        std::cout << "CONTRACT_ONLY: no training; model=" << gs_sweep::value(c.ini, "policy.architecture") << '\n';
        std::cerr << "CONTRACT_STDERR\n";
        std::cout.flush(); std::cerr.flush();
        if (getenv("GOOFSPIEL_CONTRACT_SLEEP")) for (;;) pause();
        const char* exit_code = getenv("GOOFSPIEL_CONTRACT_EXIT");
        return exit_code ? atoi(exit_code) : 0;
    }
    try {
        Sandbox sandbox;
        fs::path out = sandbox.path / "output.ini";
        fs::path report = sandbox.path / "uncreated";
        for (const auto* model : {"puffernet", "transformer"}) {
            check(run({"sweep", model, report.string(), "--dry-run"}, out) == 0, "dry-run failed");
            check(!fs::exists(report), "dry-run created report directory");
            gs_sweep::Config c;
            puf_ini_load_file(&c.ini, out.c_str());
            check(gs_sweep::validate_ranges(c.ini) == 36, "expected the full shared search space");
            check(gs_sweep::value(c.ini, "policy.architecture") == model, "wrong model");
            check(gs_sweep::number(c.ini, "vec.total_agents") == 16384, "wrong starting agents");
            check(gs_sweep::number(c.ini, "sweep.vec.total_agents.min") == 256, "agents are not swept");
            check(gs_sweep::number(c.ini, "sweep.vec.total_agents.max") == 262144, "high agent range lost");
            check(gs_sweep::number(c.ini, "sweep.vec.num_buffers.max") == 4, "buffers are not swept");
            check(gs_sweep::number(c.ini, "sweep.vec.num_threads.min") == 1, "threads are not swept");
            check(gs_sweep::number(c.ini, "sweep.train.replay_ratio.max") == 8, "replay is not swept");
            check(gs_sweep::value(c.ini, "sweep.policy.num_layers.distribution") == "int_uniform", "fractional layers");
            fixed(c.ini, "train.total_timesteps", 50000000);
            check(gs_sweep::number(c.ini, "sweep.max_runs") == 500, "wrong campaign size");
            check(gs_sweep::number(c.ini, "base.checkpoint_interval") == -1, "checkpoint cadence is not per-trial");
            check(gs_sweep::number(c.ini, "env.exploitability_interval_steps") == 1048576, "helper changed native cadence");
            fixed(c.ini, !strcmp(model, "puffernet") ? "policy.transformer_heads"
                : "base.reset_every_horizon", !strcmp(model, "puffernet") ? 2 : 0);
            check(gs_sweep::number(c.ini, "sweep.vec.hist_policy_percent.step") == .125,
                "missing fractional selfplay grid");
            check(!gs_sweep::item(c.ini, "sweep.train.replay_ratio.step")
                && !gs_sweep::item(c.ini, "sweep.train.reward_clip.step"), "unnecessary quantization");
            check(gs_sweep::number(c.ini, "sweep.train.gae_lambda.min") <= .427651882,
                "NMMO lambda excluded");
            check(gs_sweep::number(c.ini, "sweep.train.gae_lambda.max") >= .998220325,
                "old Goofspiel lambda excluded");
            // No profile or hidden helper default may diverge from ordinary native sweep.
            gs_sweep::Config native;
            puf_ini_load_env(&native.ini, "goofspiel", 0, nullptr);
            gs_sweep::put(native.ini, "policy.architecture", model);
            gs_prepare_sweep(&native.ini);
            for (int s = 0; s < native.ini.num_sections; ++s) {
                Dict* section = native.ini.sections + s;
                for (int k = 0; k < section->size; ++k) {
                    std::string key = std::string(section->name) + "." + section->items[k].key;
                    if (key == "base.checkpoint_dir" || key == "base.log_dir") continue;
                    check(gs_sweep::value(native.ini, key) == gs_sweep::value(c.ini, key),
                        ("helper/native config divergence: " + key).c_str());
                }
            }
            ++tests;
        }
        std::vector<std::string> tuned{"sweep", "transformer", report.string(), "--dry-run",
            "--vec.total_agents=512", "--vec.num_buffers=2", "--train.minibatch_size=512",
            "--train.replay_ratio=2", "--policy.hidden_size=64", "--policy.num_layers=3"};
        check(run(tuned, out) == 0, "manual controls rejected");
        {
            gs_sweep::Config c; puf_ini_load_file(&c.ini, out.c_str());
            check(gs_sweep::number(c.ini, "vec.total_agents") == 512, "starting agents override lost");
            check(gs_sweep::number(c.ini, "train.replay_ratio") == 2, "starting replay override lost");
            check(gs_sweep::number(c.ini, "sweep.vec.total_agents.max") == 262144, "starting value silently pins search");
            check(gs_sweep::number(c.ini, "base.checkpoint_interval") == -1, "lost per-trial cadence");
            check(gs_sweep::number(c.ini, "vec.hist_policy_hidden_size") == 64, "frozen width not synchronized");
            check(gs_sweep::number(c.ini, "vec.hist_policy_num_layers") == 3, "frozen depth not synchronized");
            ++tests;
        }
        tuned.push_back("--sweep.train.replay_ratio.min=2");
        tuned.push_back("--sweep.train.replay_ratio.max=2");
        check(run(tuned, out) == 0, "manual fixed range rejected");
        {
            gs_sweep::Config c; puf_ini_load_file(&c.ini, out.c_str());
            check(gs_sweep::validate_ranges(c.ini) == 35, "manual fixed range was ignored");
            fixed(c.ini, "train.replay_ratio", 2);
            ++tests;
        }
        for (const auto& pair : std::vector<std::pair<std::string, int>>{
                {"--checkpoints=2", -1}, {"--base.checkpoint_interval=3", 3}}) {
            check(run({"sweep", "puffernet", report.string(), "--dry-run", pair.first}, out) == 0, "checkpoint override failed");
            gs_sweep::Config c; puf_ini_load_file(&c.ini, out.c_str());
            check(gs_sweep::number(c.ini, "base.checkpoint_interval") == pair.second, "checkpoint override ignored");
            if (pair.second == -1)
                check(gs_sweep::number(c.ini, "env.exploitability_interval_steps") == 25000000, "checkpoint count ignored");
            ++tests;
        }
        check(run({"sweep", "puffernet", report.string(), "--dry-run", "--selfplay.enabled=0"}, out) == 0,
            "mirror-play override failed");
        {
            gs_sweep::Config c; puf_ini_load_file(&c.ini, out.c_str());
            check(gs_sweep::number(c.ini, "vec.num_policies") == 5
                && gs_sweep::number(c.ini, "vec.hist_policy_percent") == .5,
                "helper changed conditional settings before later selfplay suggestions");
            ++tests;
        }
        check(run({"sweep", "puffernet", report.string(), "--dry-run",
            "--train.horizon=4", "--train.replay_ratio=.37"}, out) == 0,
            "FP32 horizon or continuous replay override rejected");
        ++tests;
        const std::vector<std::pair<std::string, std::string>> bad{
            {"--train.emag_tau=.1", "unknown setting"},
            {"--train.prio_alpha=.1", "unknown setting"},
            {"--sweep.goal=maximize", "exact exploitability"},
            {"--sweep.metric=score", "exact exploitability"},
            {"--base.eval_episodes=8", "replace the exact"},
            {"--sweep.max_runs=0", "invalid integer"},
            {"--sweep.max_failures=0", "invalid integer"},
            {"--sweep.memory_limit_mb=-1", "nonnegative"},
            {"--sweep.memory_limit_mb=1", "estimated training/evaluation memory"},
            {"--policy.hidden_size=12", "width 8-256"},
            {"--policy.transformer_heads=3", "must divide"},
            {"--vec.total_agents=8191", "2*num_buffers"},
            {"--vec.num_buffers=8", "num_threads must"},
            {"--train.minibatch_size=513", "divisible by horizon"},
            {"--train.minibatch_size=65536", "divide learner rows"},
            {"--train.replay_ratio=0.00001", "zero optimizer"},
            {"--env.information=1", "perfect-information"},
            {"--train.gpus=2", "one GPU"},
            {"--sweep.gpus=2", "one GPU"},
            {"--train.learning_rate=nan", "finite number"},
            {"--sweep.train.learning_rate.max=.001", "inside range"},
            {"--sweep.train.learning_rate.min=0", "log range"},
            {"--sweep.train.gamma.max=1", "logit range"},
            {"--sweep.vec.hist_policy_percent.step=-.125", "invalid uniform step"},
            {"--sweep.vec.hist_policy_percent.step=.13", "whole number"},
            {"--base.log_dir=/tmp/wrong", "report directory owns"},
            {"--checkpoints=0", "checkpoints must"},
        };
        for (const auto& test : bad) {
            check(run({"sweep", "transformer", report.string(), "--dry-run", test.first}, out) != 0,
                "invalid override accepted");
            check(read_text(out).find(test.second) != std::string::npos, "wrong rejection reason");
            check(!fs::exists(report), "invalid dry-run wrote report");
            ++tests;
        }
        fs::path stub = sandbox.path / "trainer with spaces";
        fs::copy_file(fs::absolute(argv[0]), stub);
        setenv("GOOFSPIEL_TRAIN_BINARY", stub.c_str(), 1);
        fs::path launched = sandbox.path / "report with spaces";
        check(run({"sweep", "transformer", launched.string()}, out) == 0, "stub launch failed");
        check(fs::is_regular_file(launched / "sweep.ini") && fs::is_regular_file(launched / "command.sh"), "missing launch receipts");
        check(read_text(launched / "sweep.log").find("CONTRACT_ONLY") != std::string::npos, "stdout not logged");
        check(read_text(launched / "sweep.log").find("CONTRACT_STDERR") != std::string::npos, "stderr not logged");
        check(run({"sweep", "transformer", launched.string()}, out) != 0, "existing report overwritten");
        check(read_text(out).find("report already exists") != std::string::npos, "wrong existing-report rejection");
        check(gs_sweep::quoted("a'b") == "'a'\\''b'", "unsafe shell quoting");
        tests += 2;
        setenv("GOOFSPIEL_CONTRACT_EXIT", "7", 1);
        check(run({"sweep", "puffernet", (sandbox.path / "failed-stub").string()}, out) == 7, "child exit status lost");
        unsetenv("GOOFSPIEL_CONTRACT_EXIT"); ++tests;

        setenv("GOOFSPIEL_CONTRACT_SLEEP", "1", 1);
        fs::path signaled = sandbox.path / "signaled-stub";
        pid_t child = start({"sweep", "puffernet", signaled.string()}, out);
        bool ready = false;
        for (int i = 0; i < 400; ++i) {
            if (read_text(signaled / "sweep.log").find("CONTRACT_ONLY") != std::string::npos) { ready = true; break; }
            usleep(5000);
        }
        kill(child, SIGTERM);
        check(finish(child) == 143, "SIGTERM not forwarded to trainer process group");
        check(ready, "signal stub never became ready");
        unsetenv("GOOFSPIEL_CONTRACT_SLEEP"); ++tests;

        fs::path logs = launched / "logs/goofspiel";
        fs::create_directories(logs);
        for (int i = 0; i < 2; ++i) {
            FILE* f = gs_sweep::new_file(logs / (std::to_string(i) + ".ini"));
            fprintf(f, "# SYNTHETIC CONTRACT FIXTURE, NOT A TRAINING RESULT\n[base]\nrun_id = fake_%d\n"
                "[policy]\narchitecture = transformer\n[metrics]\nenv/best_exploitability = %.9f\n"
                "env/exploitability = .6\nuptime = 12.5\nagent_steps = 2097152\n", i, i ? .123456789 : .5);
            fclose(f);
        }
        check(run({"sweep", "summary", launched.string()}, out) == 0, "summary failed");
        std::string summary = read_text(out);
        check(summary.find("0.123456789") != std::string::npos, "summary lost score precision");
        check(summary.find("fake_1") < summary.find("fake_0"), "summary not ranked lower-is-better");
        ++tests;
        std::cout << "Goofspiel sweep contracts: " << tests << " passed (CPU only; no training launched).\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Sweep contract failure: " << e.what() << '\n';
        return 1;
    }
}
