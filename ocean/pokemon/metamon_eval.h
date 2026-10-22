#pragma once
// Native, in-training Metamon checks. No sweep worker or replacement dashboard.
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>
#include "ini.h"
extern char** environ;

typedef struct {
    int enabled, games, interim_games, final_games, seed, state, completed, failures, has_score, best_games;
    int wins, draws, losses, last_games;
    long interval_epochs, next_epoch, target_steps, eval_steps, score_steps, best_steps;
    double fraction, timeout, started, score, best;
    pid_t pid;
    char python[4096], device[16], output[4096], error[160];
} PKMetamonEval;
static PKMetamonEval pk_metamon;

static double pk_metamon_clock(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static void pk_metamon_init(Ini* ini, int world_size) {
    memset(&pk_metamon, 0, sizeof(pk_metamon));
    Dict* cfg = NULL;
    for (int i=0; i<ini->num_sections; i++)
        if (!strcmp(ini->sections[i].name, "metamon")) cfg = &ini->sections[i];
    if (!cfg || !dict_find(cfg, "enabled") || !dict_get(cfg, "enabled")) return;
    pk_metamon.enabled = 1;
    pk_metamon.fraction = dict_get(cfg, "interval_fraction");
    double games = dict_get(cfg, "games"), seed = dict_get(cfg, "seed");
    DictItem* final_option = dict_find(cfg, "final_games");
    double final_games = final_option ? final_option->value : games;
    pk_metamon.timeout = dict_get(cfg, "timeout");
    if (world_size != 1 || !isfinite(pk_metamon.fraction) || pk_metamon.fraction <= 0
        || pk_metamon.fraction > 1 || !isfinite(games) || games < 2 || games > 10000
        || games != floor(games) || (int)games % 2
        || !isfinite(final_games) || final_games < 2 || final_games > 10000
        || final_games != floor(final_games) || (int)final_games % 2 || !isfinite(seed) || seed < 0
        || seed > 2147483647 || seed != floor(seed) || !isfinite(pk_metamon.timeout)
        || pk_metamon.timeout <= 0) {
        fprintf(stderr, "Metamon requires one GPU, interval_fraction in (0,1], even games>=2, and timeout>0\n");
        exit(1);
    }
    pk_metamon.games = (int)games; pk_metamon.seed = (int)seed;
    pk_metamon.interim_games = (int)games; pk_metamon.final_games = (int)final_games;
    DictItem* reserve = dict_find(cfg, "min_free_gb");
    if (reserve && reserve->value > 0) {
        struct statvfs disk;
        const char* directory = puf_ini_get_str(ini,"base","checkpoint_dir");
        if (statvfs(directory,&disk) || (double)disk.f_bavail*disk.f_frsize < reserve->value*1073741824.) {
            fprintf(stderr,"Metamon: insufficient checkpoint disk reserve (%.1f GiB required); refusing new training, no files deleted.\n",reserve->value);
            exit(1);
        }
    }
    const char* python = dict_get_str(cfg, "python");
    const char* device = dict_get_str(cfg, "device");
    if (strlen(python) >= sizeof(pk_metamon.python) || access(python, X_OK)
        || (strcmp(device, "cuda") && strcmp(device, "cpu"))
        || access("ocean/pokemon/benchmarks/evaluate_private.py", R_OK)
        || access("build/pokemon/private_adapter.so", R_OK)) {
        fprintf(stderr, "Metamon evaluator unavailable. Check metamon.python/device and run ocean/pokemon/benchmarks/setup_private.sh cu128; use --metamon.enabled=0 to disable.\n");
        exit(1);
    }
    snprintf(pk_metamon.python, sizeof(pk_metamon.python), "%s", python);
    snprintf(pk_metamon.device, sizeof(pk_metamon.device), "%s", device);
    printf("Metamon: native dashboard every %.1f%%, %d interim / %d final games; training briefly waits during evaluation.\n",
           100*pk_metamon.fraction, 3*pk_metamon.interim_games, 3*pk_metamon.final_games);
    fflush(stdout);
}

static int pk_metamon_due(long epoch, long epochs, long rollout) {
    if (!pk_metamon.enabled || epochs < 1) return 0;
    if (!pk_metamon.interval_epochs) {
        pk_metamon.interval_epochs = (long)ceil(epochs * pk_metamon.fraction);
        if (pk_metamon.interval_epochs < 1) pk_metamon.interval_epochs = 1;
        pk_metamon.next_epoch = pk_metamon.interval_epochs;
        pk_metamon.target_steps = epochs * rollout;
    }
    return epoch >= pk_metamon.next_epoch || epoch == epochs;
}

static void pk_metamon_failed(const char* why) {
    pk_metamon.state = 3; pk_metamon.failures++;
    snprintf(pk_metamon.error, sizeof(pk_metamon.error), "%s", why);
    fprintf(stderr, "Metamon evaluation failed: %s; checkpoint retained; log=%s.log\n", why, pk_metamon.output);
}

static void pk_metamon_begin(const char* checkpoint, long steps, long epoch) {
    // Legacy/test callers without separate counts retain their configured games.
    if (pk_metamon.interim_games) pk_metamon.games =
        steps >= pk_metamon.target_steps ? pk_metamon.final_games : pk_metamon.interim_games;
    pk_metamon.eval_steps = steps;
    pk_metamon.next_epoch = epoch + pk_metamon.interval_epochs;
    pk_metamon.completed = 0;
    pk_metamon.state = 1;
    pk_metamon.error[0] = 0;
    pk_metamon.started = pk_metamon_clock();
    if (snprintf(pk_metamon.output, sizeof(pk_metamon.output), "%s.metamon", checkpoint)
        >= (int)sizeof(pk_metamon.output)) {
        pk_metamon_failed("checkpoint path too long"); return;
    }
    char log_path[4140], games[32], seed[32], parent[32];
    snprintf(log_path, sizeof(log_path), "%s.log", pk_metamon.output);
    snprintf(games, sizeof(games), "%d", pk_metamon.games);
    snprintf(seed, sizeof(seed), "%d", pk_metamon.seed);
    snprintf(parent, sizeof(parent), "%ld", (long)getpid());
    char* argv[] = {pk_metamon.python, (char*)"ocean/pokemon/benchmarks/evaluate_private.py",
        (char*)checkpoint, (char*)"--opponent", (char*)"TaurosV0", (char*)"--mode", (char*)"argmax",
        (char*)"--device", pk_metamon.device, (char*)"--threads", (char*)"1",
        (char*)"--games", games, (char*)"--seed", seed, (char*)"--output", pk_metamon.output,
        (char*)"--parent-pid", parent, NULL};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log_path, O_WRONLY|O_CREAT|O_EXCL, 0664);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    fflush(NULL);
    int err = posix_spawnp(&pk_metamon.pid, pk_metamon.python, &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (err) { pk_metamon.pid = 0; pk_metamon_failed(strerror(err)); }
}

static int pk_metamon_read(const char* name, int* completed, int* wins, int* draws, int* losses, double* score) {
    char path[4140]; snprintf(path, sizeof(path), "%s/%s", pk_metamon.output, name);
    FILE* fp = fopen(path, "r");
    if (!fp) return 0;
    int total=0; char extra;
    int fields = fscanf(fp, "METAMON_V1 %d %d %d %d %lf %d %c", completed, wins, draws, losses, score, &total, &extra);
    fclose(fp);
    return fields == 6 && total == 3*pk_metamon.games && *completed >= 0 && *completed <= total
        && *wins >= 0 && *draws >= 0 && *losses >= 0 && *wins+*draws+*losses == *completed
        && isfinite(*score) && *score >= 0 && *score <= 1
        && fabs(*score - (*completed ? (*wins + .5 * *draws) / *completed : 0)) < 1e-8;
}

static int pk_metamon_poll(void) {
    if (pk_metamon.state != 1 || !pk_metamon.pid) return 0;
    int n,w,d,l; double score;
    if (pk_metamon_read("progress.txt", &n,&w,&d,&l,&score)) pk_metamon.completed = n;
    int status=0;
    pid_t done = waitpid(pk_metamon.pid, &status, WNOHANG);
    if (done < 0 && errno == EINTR) return 1;
    if (done < 0) { pk_metamon.pid=0; pk_metamon_failed("waitpid failed"); return 0; }
    if (!done) {
        if (pk_metamon_clock()-pk_metamon.started <= pk_metamon.timeout) return 1;
        kill(pk_metamon.pid, SIGKILL);
        while (waitpid(pk_metamon.pid, &status, 0) < 0 && errno == EINTR) {}
        pk_metamon.pid=0; pk_metamon_failed("timeout"); return 0;
    }
    pk_metamon.pid=0;
    if (!WIFEXITED(status) || WEXITSTATUS(status) ||
        !pk_metamon_read("result.txt", &n,&w,&d,&l,&score) || n != 3*pk_metamon.games) {
        pk_metamon_failed("evaluator failed or result incomplete (see .metamon.log)"); return 0;
    }
    pk_metamon.state=2; pk_metamon.completed=n;
    pk_metamon.score=score; pk_metamon.score_steps=pk_metamon.eval_steps;
    pk_metamon.last_games=n; pk_metamon.wins=w; pk_metamon.draws=d; pk_metamon.losses=l;
    if (!pk_metamon.has_score || score > pk_metamon.best) {
        pk_metamon.best=score; pk_metamon.best_steps=pk_metamon.eval_steps;
        pk_metamon.best_games=n;
    }
    pk_metamon.has_score=1;
    return 0;
}

static void pk_metamon_log(Dict* log) {
    if (!pk_metamon.enabled) return;
    dict_set(log, "env/metamon_state", pk_metamon.state);
    dict_set(log, "env/metamon_failures", pk_metamon.failures);
    if (!pk_metamon.has_score) return;
    dict_set(log, "env/metamon_score", pk_metamon.score);
    dict_set(log, "env/metamon_best_score", pk_metamon.best);
    dict_set(log, "env/metamon_games", pk_metamon.last_games);
    dict_set(log, "env/metamon_eval_steps", pk_metamon.score_steps);
    dict_set(log, "env/metamon_best_steps", pk_metamon.best_steps);
    dict_set(log, "env/metamon_best_games", pk_metamon.best_games);
}

static int pk_metamon_dashboard(int tty, int max_rows) {
    if (!pk_metamon.enabled || max_rows < 2) return 0;
    char first[200], second[200], third[200];
    if (pk_metamon.has_score)
        snprintf(first,sizeof(first),"Metamon  latest %.2f%%   best %.2f%%   %dW/%dD/%dL (%d games)",
            100*pk_metamon.score,100*pk_metamon.best,pk_metamon.wins,pk_metamon.draws,pk_metamon.losses,pk_metamon.last_games);
    else snprintf(first,sizeof(first),"Metamon  latest pending   best pending   %d games per check",3*pk_metamon.games);
    if (pk_metamon.state == 1)
        snprintf(second,sizeof(second),"Evaluating %d/%d games at %.1f%% | training waits, then resumes",
            pk_metamon.completed,3*pk_metamon.games,
            pk_metamon.target_steps?100.*pk_metamon.eval_steps/pk_metamon.target_steps:0);
    else if (pk_metamon.state == 3)
        snprintf(second,sizeof(second),"Metamon ERROR: %.60s",pk_metamon.error);
    else snprintf(second,sizeof(second),"Metamon every %.1f%% | last checked at %.1fM steps | sample is noisy",
            100*pk_metamon.fraction,pk_metamon.score_steps/1e6);
    snprintf(third,sizeof(third),"Best checkpoint: %.1fM steps (%d games) | finals alone rank sweep",pk_metamon.best_steps/1e6,pk_metamon.best_games);
    const char* rows[]={first,second,third};
    int count = max_rows >= 3 ? 3 : 2;
    for (int i=0;i<count;i++) printf("│ %-76.76s │%s\n",rows[i],tty?"\033[K":"");
    return count;
}

static void pk_metamon_compact(char* out, size_t n, const char* steps, const char* sps) {
    char score[24]="pending", best[24]="pending";
    if (pk_metamon.has_score) {
        snprintf(score,sizeof(score),"%.2f%%",100*pk_metamon.score);
        snprintf(best,sizeof(best),"%.2f%%",100*pk_metamon.best);
    }
    snprintf(out,n,"Metamon=%s best=%s %s steps=%s SPS=%s",score,best,
        pk_metamon.state==1?"EVALUATING":pk_metamon.state==3?"ERROR":"",steps,sps);
}

#define PUF_LIVE_EVAL_INIT(ini,world) pk_metamon_init(ini,world)
#define PUF_LIVE_EVAL_DUE(epoch,epochs,rollout) pk_metamon_due(epoch,epochs,rollout)
#define PUF_LIVE_EVAL_BEGIN(checkpoint,steps,epoch) pk_metamon_begin(checkpoint,steps,epoch)
#define PUF_LIVE_EVAL_POLL() pk_metamon_poll()
#define PUF_LIVE_EVAL_LOG(log) pk_metamon_log(log)
#define PUF_LIVE_EVAL_ENABLED() pk_metamon.enabled
#define PUF_LIVE_EVAL_DASH(tty,rows) pk_metamon_dashboard(tty,rows)
#define PUF_LIVE_EVAL_COMPACT(out,n,steps,sps) pk_metamon_compact(out,n,steps,sps)
