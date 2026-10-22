// CPU-only launcher for the existing native Protein sweep. No CUDA or Python.
// Reuse the trainer's INI parser so defaults, CLI parsing and child argv agree.
#include "../../src/ini.h"
#include "training_config.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
namespace gs_sweep {
struct Config {
    Ini ini{};
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    ~Config() { puf_ini_free(&ini); }
};

static void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
static DictItem* item(Ini& ini, const std::string& key) {
    auto dot = key.rfind('.');
    if (dot == std::string::npos) return nullptr;
    for (int s = 0; s < ini.num_sections; ++s)
        if (key.substr(0, dot) == ini.sections[s].name)
            return dict_find(&ini.sections[s], key.substr(dot + 1).c_str());
    return nullptr;
}
static std::string raw(const DictItem& value) {
    if (value.str) return value.str;
    std::ostringstream out;
    out << std::setprecision(17);
    if (value.len) for (int i = 0; i < value.len; ++i)
        out << (i ? "," : "") << value.values[i];
    else out << value.value;
    return out.str();
}
static double number(Ini& ini, const std::string& key) {
    auto* v = item(ini, key);
    double parsed = v ? v->value : 0;
    require(v && !v->len && (!v->str || puf_ini_parse_val(v->str, &parsed))
        && std::isfinite(parsed), "expected finite number: " + key);
    return parsed;
}
static long long integer(Ini& ini, const std::string& key, long long minimum = 1) {
    double n = number(ini, key);
    require(n >= minimum && n <= 1e12 && std::floor(n) == n, "invalid integer: " + key);
    return (long long)n;
}
static std::string value(Ini& ini, const std::string& key) {
    auto* v = item(ini, key);
    require(v, "unknown setting: " + key);
    return raw(*v);
}
static void put(Ini& ini, const std::string& key, const std::string& text) {
    require(item(ini, key), "unknown setting: " + key);
    puf_ini_put(&ini, key.c_str(), text.c_str());
}
static void put(Ini& ini, const std::string& key, long long n) {
    put(ini, key, std::to_string(n));
}

struct Options {
    bool checkpoint_override = false;
    bool dry_run = false;
    int checkpoints = 0;
};
static int validate_ranges(Ini& ini) {
    int varying = 0;
    for (int s = 0; s < ini.num_sections; ++s) {
        std::string section = ini.sections[s].name;
        if (section.rfind("sweep.", 0)) continue;
        double lo = number(ini, section + ".min"), hi = number(ini, section + ".max");
        double initial = number(ini, section.substr(6));
        require(lo <= hi && initial >= lo && initial <= hi,
            "starting value must be inside range: " + section);
        std::string dist = value(ini, section + ".distribution");
        require(dist == "uniform" || dist == "int_uniform" || dist == "uniform_pow2"
            || dist == "log_normal" || dist == "logit_normal", "unknown distribution: " + section);
        if (item(ini, section + ".step")) {
            double requested = number(ini, section + ".step");
            float step = requested, low = lo, high = hi;
            require(std::isfinite(step) && step >= 0 && (!requested || step > 0)
                && (!step || dist == "uniform"), "invalid uniform step: " + section);
            if (step) {
                float n = (high - low) / step;
                require((low == high || (low + step > low && high - step < high))
                    && std::isfinite(n) && std::fabs(n - std::round(n))
                        <= 4 * std::numeric_limits<float>::epsilon() * std::fmax(1, n),
                    "range must contain a whole number of representable steps: " + section);
            }
        }
        if (lo == hi) continue; // Matches the native runner's fixed-dimension path.
        ++varying;
        if (dist == "log_normal") require(lo > 0, "log range must be positive: " + section);
        if (dist == "logit_normal") require(lo > 0 && hi < 1, "logit range must be inside (0,1): " + section);
        if (dist == "int_uniform" || dist == "uniform_pow2")
            require(std::floor(lo) == lo && std::floor(hi) == hi, "integer range required: " + section);
        if (dist == "uniform_pow2") {
            auto power_of_two = [](double x) {
                return x >= 1 && std::isfinite(std::log2(x)) && std::floor(std::log2(x)) == std::log2(x);
            };
            require(power_of_two(lo) && power_of_two(hi), "power-of-two bounds required: " + section);
        }
    }
    require(varying > 0, "all sweep dimensions are fixed");
    return varying;
}

struct Layout {
    long long epoch_steps, learner_rows, minibatches, epochs;
};
static Layout validate(Ini& ini, const std::string& architecture) {
    require(value(ini, "policy.architecture") == architecture, "architecture must match the selected model");
    require(value(ini, "base.env_name") == "goofspiel", "this launcher is only for Goofspiel");
    require(number(ini, "env.num_players") == 2 && number(ini, "env.num_cards") == 4
        && number(ini, "env.num_turns") == 4 && number(ini, "env.information") == 0
        && number(ini, "env.open_spiel_obs") == 0 && number(ini, "env.egocentric") == 1,
        "comparison requires four-card, two-player, compact perfect-information egocentric Goofspiel");
    require(value(ini, "base.load_model_path") == "None"
        && value(ini, "selfplay.initial_opponents") == "None", "comparison starts fresh; external checkpoints are not supported");
    require(value(ini, "sweep.goal") == "minimize" && number(ini, "sweep.downsample") == 1
        && (value(ini, "sweep.metric") == "best_exploitability" || value(ini, "sweep.metric") == "exploitability"),
        "use exact exploitability, goal=minimize and downsample=1");
    require(number(ini, "base.eval_episodes") == 0 && number(ini, "selfplay.eval_games") == 0
        && number(ini, "selfplay.eval_bot_games") == 0, "sampled post-training evaluation would replace the exact sweep objective");
    require(number(ini, "train.gpus") == 1 && number(ini, "sweep.gpus") == 1
        && number(ini, "base.gpu_offset") == 0, "this comparison runs one GPU/trial at a time; select it with CUDA_VISIBLE_DEVICES");
    integer(ini, "sweep.max_runs"); integer(ini, "sweep.max_failures");
    require(number(ini, "sweep.memory_limit_mb") >= 0, "memory_limit_mb must be nonnegative");
    long long width = integer(ini, "policy.hidden_size"), layers = integer(ini, "policy.num_layers");
    require(width >= 8 && width <= 256 && width % 8 == 0 && layers <= 16,
        "exact-evaluated comparison requires width 8-256 (multiple of 8), layers 1-16");
    if (architecture == "transformer") {
        auto heads = integer(ini, "policy.transformer_heads");
        require(width % heads == 0, "transformer_heads must divide hidden_size");
    }
    long long agents = integer(ini, "vec.total_agents"), buffers = integer(ini, "vec.num_buffers");
    integer(ini, "vec.num_threads");
    long long horizon = integer(ini, "train.horizon"), minibatch = integer(ini, "train.minibatch_size");
    require(agents % (2 * buffers) == 0, "total_agents must be divisible by 2*num_buffers");
    require(minibatch % horizon == 0, "minibatch_size must be divisible by horizon");
    long long learner_rows = agents;
    if (number(ini, "selfplay.enabled") != 0) {
        long long policies = integer(ini, "vec.num_policies", 2);
        require(policies <= 9, "native selfplay supports at most eight frozen banks");
        double fraction = number(ini, "vec.hist_policy_percent");
        require(fraction > 0 && fraction <= 1, "hist_policy_percent must be in (0,1]");
        long long frozen_games = (long long)((agents / buffers / 2) * fraction);
        require(frozen_games >= policies - 1, "not enough frozen games to give every policy a row in each buffer");
        learner_rows -= frozen_games * buffers;
        put(ini, "vec.hist_policy_hidden_size", width);
        put(ini, "vec.hist_policy_num_layers", layers);
    }
    require(learner_rows % (minibatch / horizon) == 0,
        "minibatch sequences must divide learner rows (Goofspiel frozen opponents use only one row/game)");
    double replay = number(ini, "train.replay_ratio");
    require(replay > 0 && replay <= 1e6, "invalid replay_ratio");
    require(agents <= 1000000000 / horizon, "rollout shape is too large");
    long long epoch_steps = agents * horizon;
    long long epochs = integer(ini, "train.total_timesteps") / epoch_steps;
    require(epochs >= 1, "training budget is smaller than one rollout");
    long long minibatches = (long long)(replay * learner_rows * horizon / minibatch);
    require(minibatches >= 1, "replay_ratio/minibatch_size would perform zero optimizer updates");
    const char* error = gs_training_config_error(&ini, 4);
    require(!error, error ? error : "");
    return {epoch_steps, learner_rows, minibatches, epochs};
}

static std::vector<std::string> arguments(Ini& ini, const std::string& binary) {
    std::vector<std::string> out{binary, "sweep"};
    for (int s = 0; s < ini.num_sections; ++s)
        for (int i = 0; i < ini.sections[s].size; ++i)
            out.push_back("--" + std::string(ini.sections[s].name) + "."
                + ini.sections[s].items[i].key + "=" + raw(ini.sections[s].items[i]));
    return out;
}
static std::string quoted(const std::string& s) {
    std::string q = "'";
    for (char c : s) q += c == '\'' ? "'\\''" : std::string(1, c);
    return q + "'";
}
static FILE* new_file(const fs::path& path) {
    FILE* f = fopen(path.c_str(), "wx");
    require(f, "cannot create " + path.string() + ": " + strerror(errno));
    return f;
}
static void save_command(FILE* f, const std::vector<std::string>& args) {
    fprintf(f, "#!/usr/bin/env bash\n# Audit receipt: direct execution bypasses the launcher's fresh-directory guard.\n"
        "# Use sweep.sh with a new report directory for a new campaign.\n");
    fprintf(f, "cd %s\nexec", quoted(fs::current_path().string()).c_str());
    for (const auto& arg : args) fprintf(f, " %s", quoted(arg).c_str());
    fputc('\n', f);
}

static volatile sig_atomic_t child_group = 0;
static void forward_signal(int signal) { if (child_group > 0) kill(-child_group, signal); }
static int launch(const std::vector<std::string>& args, const fs::path& report) {
    int pipefd[2];
    require(pipe(pipefd) == 0, "cannot create output pipe");
    FILE* log = new_file(report / "sweep.log");
    fflush(nullptr);
    sigset_t blocked, previous_mask;
    sigemptyset(&blocked); sigaddset(&blocked, SIGINT); sigaddset(&blocked, SIGTERM);
    require(sigprocmask(SIG_BLOCK, &blocked, &previous_mask) == 0, "cannot block launch signals");
    pid_t child = fork();
    if (child < 0) {
        sigprocmask(SIG_SETMASK, &previous_mask, nullptr);
        close(pipefd[0]); close(pipefd[1]); fclose(log);
        throw std::runtime_error("cannot fork trainer");
    }
    if (!child) {
        setpgid(0, 0);
        sigprocmask(SIG_SETMASK, &previous_mask, nullptr);
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO); dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]); fclose(log);
        std::vector<char*> argv;
        for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        execv(argv[0], argv.data());
        perror("exec trainer"); _exit(127);
    }
    setpgid(child, child);
    child_group = child;
    struct sigaction action{};
    action.sa_handler = forward_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr); sigaction(SIGTERM, &action, nullptr);
    sigprocmask(SIG_SETMASK, &previous_mask, nullptr);
    signal(SIGPIPE, SIG_IGN);
    close(pipefd[1]);
    char buffer[16384];
    bool console = true;
    for (;;) {
        ssize_t count = read(pipefd[0], buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        if (fwrite(buffer, 1, count, log) != (size_t)count) {
            kill(-child, SIGTERM); break;
        }
        fflush(log);
        if (console && fwrite(buffer, 1, count, stdout) != (size_t)count) console = false;
        if (console) fflush(stdout);
    }
    close(pipefd[0]); fclose(log);
    int status = 0;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) throw std::runtime_error("waitpid failed");
    child_group = 0;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

static int summary(int argc, char** argv) {
    struct Trial { double best, final, seconds, steps; std::string model, run, path; };
    std::vector<Trial> trials;
    for (int i = 2; i < argc; ++i) {
        fs::path logs = fs::path(argv[i]) / "logs/goofspiel";
        if (!fs::is_directory(logs)) continue;
        for (const auto& entry : fs::directory_iterator(logs)) {
            if (entry.path().extension() != ".ini") continue;
            Config c;
            puf_ini_load_file(&c.ini, entry.path().c_str());
            auto* best = item(c.ini, "metrics.env/best_exploitability");
            auto* final = item(c.ini, "metrics.env/exploitability");
            auto* seconds = item(c.ini, "metrics.uptime");
            auto* steps = item(c.ini, "metrics.agent_steps");
            if (!best || !final || !seconds || !steps || !std::isfinite(best->value)
                    || !std::isfinite(final->value) || !std::isfinite(seconds->value)) {
                std::cerr << "Skipping incomplete/non-finite trial: " << entry.path() << '\n';
                continue;
            }
            trials.push_back({best->value, final->value, seconds->value, steps->value,
                value(c.ini, "policy.architecture"), value(c.ini, "base.run_id"), entry.path().string()});
        }
    }
    std::sort(trials.begin(), trials.end(), [](const Trial& a, const Trial& b) {
        return a.best != b.best ? a.best < b.best : a.seconds < b.seconds;
    });
    std::cout << "architecture\tbest_exploitability\tfinal_exploitability\tseconds_with_exact_eval\tagent_steps\trun_id\tsettings\n";
    for (const auto& t : trials)
        std::cout << t.model << '\t' << std::setprecision(9) << t.best << '\t' << t.final
            << '\t' << t.seconds << '\t' << std::fixed << std::setprecision(0) << t.steps
            << std::defaultfloat << '\t' << t.run << '\t' << t.path << '\n';
    if (trials.empty()) std::cerr << "No completed exact-evaluated trials yet.\n";
    return 0;
}

static int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "summary") return summary(argc, argv);
    if (argc < 3 || std::string(argv[1]) == "--help") {
        std::cout << "Usage: sweep.sh {puffernet|transformer} NEW_REPORT_DIRECTORY [--dry-run] [--checkpoints=N] [--section.key=value ...]\n"
            "       sweep.sh summary REPORT_DIRECTORY [REPORT_DIRECTORY ...]\n"
            "Runtime/model sizes and PPO are searched; incompatible layouts are rejected before training.\n"
            "Settings and ranges come from config/goofspiel.ini, exactly as for native sweep.\n"
            "Default: 500 trials/model, 50M steps/trial, one GPU, CPU environments.\n"
            "--dry-run starts no job; --checkpoints=N optionally overrides the normal evaluation cadence.\n";
        return argc < 2 || std::string(argv[1]) == "--help" ? 0 : 2;
    }
    std::string architecture = argv[1];
    require(architecture == "puffernet" || architecture == "transformer", "choose puffernet or transformer");
    fs::path report = fs::absolute(argv[2]).lexically_normal();
    require(report.string().find_first_of("\n\r\t#;") == std::string::npos,
        "report path cannot contain tabs/newlines or INI comment characters (#;)");
    Config config;
    Ini& ini = config.ini;
    puf_ini_load_env(&ini, "goofspiel", 0, nullptr);
    Options options;
    put(ini, "policy.architecture", architecture);
    for (int a = 3; a < argc; ++a) {
        std::string arg = argv[a];
        if (arg == "--dry-run") { options.dry_run = true; continue; }
        if (arg.rfind("--checkpoints=", 0) == 0) {
            std::string count = arg.substr(14);
            require(!count.empty() && count.find_first_not_of("0123456789") == std::string::npos, "checkpoints must be an integer");
            long long n = std::stoll(count);
            require(n >= 1 && n <= 1000, "checkpoints must be in 1..1000");
            options.checkpoints = (int)n; continue;
        }
        require(arg.rfind("--", 0) == 0 && arg.find('=') != std::string::npos, "use --section.key=value overrides");
        std::string key = arg.substr(2, arg.find('=') - 2);
        require(key != "base.checkpoint_dir" && key != "base.log_dir" && key != "base.run_id"
            && key != "base.result_fd", "report directory owns " + key);
        require(item(ini, key), "unknown setting: " + key);
        puf_ini_apply_arg(&ini, "base", argv[a], a);
        if (key == "base.checkpoint_interval") options.checkpoint_override = true;
    }
    gs_prepare_sweep(&ini);
    Layout layout = validate(ini, architecture);
    int dimensions = validate_ranges(ini);
    if (options.checkpoints && !options.checkpoint_override) {
        put(ini, "base.checkpoint_interval", -1LL);
        put(ini, "env.exploitability_interval_steps",
            (integer(ini, "train.total_timesteps") + options.checkpoints - 1) / options.checkpoints);
    }
    integer(ini, "base.checkpoint_interval", -1);
    put(ini, "base.checkpoint_dir", (report / "checkpoints").string());
    put(ini, "base.log_dir", (report / "logs").string());
    put(ini, "base.run_id", "None"); put(ini, "base.result_fd", 0LL);
    const char* override_binary = getenv("GOOFSPIEL_TRAIN_BINARY");
    std::string binary = fs::absolute(override_binary ? override_binary : "puffer").lexically_normal().string();
    std::vector<std::string> args = arguments(ini, binary);
    std::cout << "# Goofspiel " << architecture << ": " << dimensions << " search dimensions, "
        << integer(ini, "sweep.max_runs") << " trials, " << layout.epochs * layout.epoch_steps << " effective agent steps/trial\n"
        << "# Starting shape: " << value(ini, "policy.hidden_size") << 'x' << value(ini, "policy.num_layers")
        << "; learner rows=" << layout.learner_rows << "; optimizer updates/rollout=" << layout.minibatches << '\n'
        << "# Exact checkpoint interval=" << value(ini, "base.checkpoint_interval")
        << " epochs (-1 = every " << value(ini, "env.exploitability_interval_steps")
        << " agent steps, rounded per trial). Report=" << report.string() << '\n';
    if (layout.epochs * layout.epoch_steps != integer(ini, "train.total_timesteps"))
        std::cerr << "# Warning: the trainer rounds this step budget down to a whole rollout.\n";
    if (options.dry_run) {
        std::cout << "# DRY RUN: no trainer started and no report directory created.\n";
        std::cout.flush(); puf_ini_write(stdout, &ini); return 0;
    }
    require(access(binary.c_str(), X_OK) == 0, "trainer is missing/not executable: " + binary);
    require(!fs::exists(report) && !fs::is_symlink(report), "report already exists; choose a new directory");
    fs::create_directories(report.parent_path());
    require(fs::create_directory(report), "could not create new report directory");
    FILE* settings = new_file(report / "sweep.ini");
    puf_ini_write(settings, &ini); fclose(settings);
    FILE* command = new_file(report / "command.sh");
    save_command(command, args); fclose(command);
    std::cout << "# Starting native sweep; Ctrl-C stops this sweep and its trial processes.\n";
    return launch(args, report);
}
} // namespace gs_sweep

#ifndef GS_SWEEP_NO_MAIN
int main(int argc, char** argv) {
    try { return gs_sweep::main(argc, argv); }
    catch (const std::exception& error) { std::cerr << "Goofspiel sweep: " << error.what() << '\n'; return 1; }
}
#endif
