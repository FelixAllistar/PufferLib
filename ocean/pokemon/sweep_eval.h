#pragma once
#include <errno.h>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;

/* Runs only after the final checkpoint is saved. Evaluation failures fail the
 * trial: the training win rate must never masquerade as an Oak result. */
static int pk_post_train_oak(Ini* ini, const char* checkpoint,
        const char* log_path, Dict* log, float* score) {
    if (!puf_ini_get(ini, "oak", "enabled")) return 0;
    char output[4096], games[32], budget[32], workers[32], seed[32];
    if (snprintf(output, sizeof(output), "%s.oak", log_path) >= (int)sizeof(output)) exit(1);
    snprintf(games, sizeof(games), "%.0f", puf_ini_get(ini, "oak", "games"));
    snprintf(budget, sizeof(budget), "%.0f", puf_ini_get(ini, "oak", "budget"));
    snprintf(workers, sizeof(workers), "%.0f", puf_ini_get(ini, "oak", "workers"));
    snprintf(seed, sizeof(seed), "%.0f", puf_ini_get(ini, "oak", "seed"));
    char* argv[] = {(char*)puf_ini_get_str(ini, "oak", "python"),
        (char*)"ocean/pokemon/benchmarks/evaluate_checkpoint.py", (char*)checkpoint,
        (char*)"--output", output, (char*)"--games", games, (char*)"--budget", budget,
        (char*)"--workers", workers, (char*)"--seed", seed, NULL};
    pid_t pid;
    fflush(NULL);
    int error = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);
    if (error) { fprintf(stderr, "Cannot start Oak evaluation: %s\n", strerror(error)); exit(1); }
    int status = 0;
    pid_t done;
    do { done = waitpid(pid, &status, 0); } while (done < 0 && errno == EINTR);
    if (done != pid || !WIFEXITED(status) || WEXITSTATUS(status)) {
        fprintf(stderr, "Oak evaluation failed for %s\n", checkpoint); exit(1);
    }
    char result[4128]; snprintf(result, sizeof(result), "%s/score.txt", output);
    FILE* fp = fopen(result, "r");
    float sampled;
    if (!fp || fscanf(fp, "%f %f", score, &sampled) != 2 ||
            !isfinite(*score) || *score < 0 || *score > 1 ||
            !isfinite(sampled) || sampled < 0 || sampled > 1) {
        fprintf(stderr, "Missing or invalid Oak score: %s\n", result); exit(1);
    }
    fclose(fp);
    dict_set(log, "env/oak_score", *score);
    dict_set(log, "env/oak_sampled_score", sampled);
    printf("Oak final: argmax=%.4f sampled=%.4f; reports=%s\n", *score, sampled, output);
    return 1;
}
#define PUF_POST_TRAIN_EVAL(ini, checkpoint, path, log, score) \
    pk_post_train_oak(ini, checkpoint, path, log, score)
