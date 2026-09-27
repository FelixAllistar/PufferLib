// Jensen-Shannon behavior distances on the exact reachable Goofspiel tree.
#define GS_EXPLOIT_NO_MAIN
#include "goofspiel_exploit.cu"

#include <math.h>

static double gs_jsd(const GSBehavior* a, const GSBehavior* b) {
    if (a->count != b->count || a->num_cards != b->num_cards) {
        fprintf(stderr, "behavior checkpoints have incompatible state shapes\n");
        exit(1);
    }
    double value = 0.0;
    uint64_t count = a->count * (uint64_t)a->num_cards;
    for (uint64_t i = 0; i < count; i++) {
        double p = a->probabilities[i];
        double q = b->probabilities[i];
        double mean = 0.5 * (p + q);
        if (p > 0.0) value += 0.5 * p * log(p / mean);
        if (q > 0.0) value += 0.5 * q * log(q / mean);
    }
    double states = (double)a->count;
    return value / (states * log(2.0));
}

static double gs_total_variation(const GSBehavior* a, const GSBehavior* b) {
    if (a->count != b->count || a->num_cards != b->num_cards) {
        fprintf(stderr, "behavior checkpoints have incompatible state shapes\n");
        exit(1);
    }
    double value = 0.0;
    uint64_t count = a->count * (uint64_t)a->num_cards;
    for (uint64_t i = 0; i < count; i++) {
        value += 0.5 * fabs(a->probabilities[i] - b->probabilities[i]);
    }
    return value / (double)a->count;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr,
            "usage: %s CHECKPOINT CHECKPOINT... [section.key=value ...]\n",
            argv[0]);
        return 1;
    }

    int count = 1;
    while (count + 1 < argc && !strchr(argv[count + 1], '=')) count++;

    Ini ini = {0};
    puf_ini_load_env(&ini, "goofspiel", argc - count - 1,
        argv + count + 1);
    GSBehavior* behaviors = (GSBehavior*)calloc(
        (size_t)count, sizeof(*behaviors));
    double* exploitability = (double*)malloc((size_t)count * sizeof(double));
    if (!behaviors || !exploitability) {
        fprintf(stderr, "failed to allocate behavior population\n");
        return 1;
    }

    for (int i = 0; i < count; i++) {
        exploitability[i] = gs_cuda_behavior(argv[i + 1], &ini, behaviors + i);
    }

    printf("# cards=%d turns=%d states=%llu\n",
        (int)puf_ini_get(&ini, "env", "num_cards"),
        (int)puf_ini_get(&ini, "env", "num_turns"),
        (unsigned long long)behaviors[0].count);
    printf("policy\texact_exploitability\tstates\n");
    for (int i = 0; i < count; i++) {
        printf("%s\t%.9f\t%llu\n", argv[i + 1], exploitability[i],
            (unsigned long long)behaviors[i].count);
    }

    printf("policy_a\tpolicy_b\tjsd\tjs_distance\ttotal_variation\n");
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            double jsd = gs_jsd(behaviors + i, behaviors + j);
            printf("%s\t%s\t%.9f\t%.9f\t%.9f\n",
                argv[i + 1], argv[j + 1], jsd, sqrt(jsd),
                gs_total_variation(behaviors + i, behaviors + j));
        }
    }

    for (int i = 0; i < count; i++) free(behaviors[i].probabilities);
    free(behaviors);
    free(exploitability);
    puf_ini_free(&ini);
    return 0;
}
