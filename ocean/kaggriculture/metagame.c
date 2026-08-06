#define _GNU_SOURCE

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KG_META_MAX_POLICIES 128
#define KG_META_NAME_MAX 256

typedef struct {
    int size;
    char names[KG_META_MAX_POLICIES][KG_META_NAME_MAX];
    double score[KG_META_MAX_POLICIES][KG_META_MAX_POLICIES];
    double payoff[KG_META_MAX_POLICIES][KG_META_MAX_POLICIES];
} KGMetaGame;

static int kg_meta_fields(char* line, char** fields, int capacity) {
    int count = 0;
    char* save = NULL;
    for (char* token = strtok_r(line, "\t\r\n", &save);
            token && count < capacity;
            token = strtok_r(NULL, "\t\r\n", &save)) {
        fields[count++] = token;
    }
    return count;
}

static int kg_meta_load(const char* path, KGMetaGame* game) {
    FILE* file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno));
        return 0;
    }

    char* line = NULL;
    size_t capacity = 0;
    char* fields[KG_META_MAX_POLICIES + 1];
    if (getline(&line, &capacity, file) < 0) {
        fprintf(stderr, "%s is empty\n", path);
        free(line);
        fclose(file);
        return 0;
    }
    int count = kg_meta_fields(line, fields, KG_META_MAX_POLICIES + 1);
    if (count < 3 || strcmp(fields[0], "policy") != 0) {
        fprintf(stderr, "%s is not a Kaggriculture payoff matrix\n", path);
        free(line);
        fclose(file);
        return 0;
    }
    game->size = count - 1;
    for (int i = 0; i < game->size; i++) {
        snprintf(game->names[i], sizeof(game->names[i]), "%s", fields[i + 1]);
    }

    for (int row = 0; row < game->size; row++) {
        if (getline(&line, &capacity, file) < 0) {
            fprintf(stderr, "%s has only %d policy rows; expected %d\n",
                path, row, game->size);
            free(line);
            fclose(file);
            return 0;
        }
        count = kg_meta_fields(line, fields, KG_META_MAX_POLICIES + 1);
        if (count != game->size + 1 || strcmp(fields[0], game->names[row]) != 0) {
            fprintf(stderr, "%s row %d does not match its header\n", path, row + 2);
            free(line);
            fclose(file);
            return 0;
        }
        for (int col = 0; col < game->size; col++) {
            char* end = NULL;
            game->score[row][col] = strtod(fields[col + 1], &end);
            if (!end || *end || !isfinite(game->score[row][col])) {
                fprintf(stderr, "%s contains an invalid score at row %d column %d\n",
                    path, row + 2, col + 2);
                free(line);
                fclose(file);
                return 0;
            }
        }
    }
    free(line);
    fclose(file);

    /* Seat-balanced score matrices should satisfy s(i,j) = 1 - s(j,i).
     * Symmetrizing removes finite-evaluation drift and produces the exact
     * antisymmetric zero-sum meta-game required by one shared policy pool. */
    for (int row = 0; row < game->size; row++) {
        game->payoff[row][row] = 0.0;
        for (int col = row + 1; col < game->size; col++) {
            double value = 0.5 * (game->score[row][col] - game->score[col][row]);
            game->payoff[row][col] = value;
            game->payoff[col][row] = -value;
        }
    }
    return 1;
}

static double kg_meta_bernoulli_kl(double p, double q) {
    double value = 0.0;
    if (p > 0.0) value += p * log(p / q);
    if (p < 1.0) value += (1.0 - p) * log((1.0 - p) / (1.0 - q));
    return value;
}

static double kg_meta_payoff_jsd(const KGMetaGame* game, int a, int b) {
    double total = 0.0;
    int count = 0;
    for (int opponent = 0; opponent < game->size; opponent++) {
        if (opponent == a || opponent == b) continue;
        double p = fmin(1.0, fmax(0.0, game->score[a][opponent]));
        double q = fmin(1.0, fmax(0.0, game->score[b][opponent]));
        double mixture = 0.5 * (p + q);
        total += 0.5 * (kg_meta_bernoulli_kl(p, mixture)
            + kg_meta_bernoulli_kl(q, mixture));
        count++;
    }
    return count ? total / count : 0.0;
}

static int kg_meta_diversity(int argc, char** argv) {
    if (argc < 6 || argc > 7) {
        fprintf(stderr, "usage: %s diversity PAYOFF.tsv BEHAVIOR.tsv "
            "CANDIDATE_PREFIX COUNT [quality_gap]\n", argv[0]);
        return 2;
    }
    int wanted = (int)strtol(argv[5], NULL, 10);
    double quality_gap = argc == 7 ? strtod(argv[6], NULL) : 0.15;
    if (wanted < 1 || wanted > KG_META_MAX_POLICIES
            || quality_gap < 0.0 || quality_gap > 1.0) {
        fprintf(stderr, "COUNT must be positive and quality_gap must be in [0,1]\n");
        return 2;
    }

    KGMetaGame payoff = {0};
    KGMetaGame behavior = {0};
    if (!kg_meta_load(argv[2], &payoff) || !kg_meta_load(argv[3], &behavior)) return 1;
    if (payoff.size != behavior.size) {
        fprintf(stderr, "payoff and behavior matrices differ in size\n");
        return 1;
    }
    for (int i = 0; i < payoff.size; i++) {
        if (strcmp(payoff.names[i], behavior.names[i]) != 0) {
            fprintf(stderr, "payoff and behavior policy %d differ\n", i);
            return 1;
        }
    }

    double quality[KG_META_MAX_POLICIES] = {0};
    int candidate[KG_META_MAX_POLICIES] = {0};
    int selected[KG_META_MAX_POLICIES] = {0};
    int selected_order[KG_META_MAX_POLICIES] = {0};
    int candidate_count = 0;
    double best_quality = -INFINITY;
    size_t prefix_len = strlen(argv[4]);
    for (int i = 0; i < payoff.size; i++) {
        double total = 0.0;
        for (int j = 0; j < payoff.size; j++) {
            if (i != j) total += payoff.score[i][j];
        }
        quality[i] = total / (payoff.size - 1);
        candidate[i] = strncmp(payoff.names[i], argv[4], prefix_len) == 0;
        if (candidate[i]) {
            candidate_count++;
            if (quality[i] > best_quality) best_quality = quality[i];
        }
    }
    if (!candidate_count) {
        fprintf(stderr, "no policy starts with candidate prefix %s\n", argv[4]);
        return 1;
    }
    if (wanted > candidate_count) wanted = candidate_count;

    printf("policy\tquality\tpayoff_jsd\tbehavior_jsd\tcombined_jsd\n");
    int selected_count = 0;
    while (selected_count < wanted) {
        int best = -1;
        double best_payoff = 0.0;
        double best_behavior = 0.0;
        double best_combined = -INFINITY;
        for (int i = 0; i < payoff.size; i++) {
            if (!candidate[i] || selected[i]
                    || quality[i] < best_quality - quality_gap) continue;
            double payoff_novelty = 0.0;
            double behavior_novelty = 0.0;
            if (selected_count) {
                payoff_novelty = INFINITY;
                behavior_novelty = INFINITY;
                for (int k = 0; k < selected_count; k++) {
                    int prior = selected_order[k];
                    payoff_novelty = fmin(payoff_novelty,
                        kg_meta_payoff_jsd(&payoff, i, prior));
                    behavior_novelty = fmin(behavior_novelty,
                        behavior.score[i][prior]);
                }
            }
            double combined = selected_count
                ? 0.7 * payoff_novelty + 0.3 * behavior_novelty
                : quality[i];
            if (combined > best_combined
                    || (combined == best_combined
                        && (best < 0 || quality[i] > quality[best]))) {
                best = i;
                best_payoff = payoff_novelty;
                best_behavior = behavior_novelty;
                best_combined = combined;
            }
        }
        if (best < 0) break;
        selected[best] = 1;
        selected_order[selected_count++] = best;
        printf("%s\t%.9f\t%.9f\t%.9f\t%.9f\n",
            payoff.names[best], quality[best], best_payoff,
            best_behavior, selected_count == 1 ? 0.0 : best_combined);
    }
    return 0;
}

static double kg_meta_solve(const KGMetaGame* game, long iterations,
        double* mixture, double* response) {
    int size = game->size;
    double logits[KG_META_MAX_POLICIES] = {0};
    double policy[KG_META_MAX_POLICIES] = {0};
    double average[KG_META_MAX_POLICIES] = {0};
    double utility[KG_META_MAX_POLICIES] = {0};
    double eta = sqrt(8.0 * log((double)size) / (double)iterations);

    for (long step = 0; step < iterations; step++) {
        double maximum = logits[0];
        for (int i = 1; i < size; i++) {
            if (logits[i] > maximum) maximum = logits[i];
        }
        double total = 0.0;
        for (int i = 0; i < size; i++) {
            policy[i] = exp(logits[i] - maximum);
            total += policy[i];
        }
        for (int i = 0; i < size; i++) {
            policy[i] /= total;
            average[i] += policy[i];
        }
        for (int i = 0; i < size; i++) {
            double value = 0.0;
            for (int j = 0; j < size; j++) {
                value += game->payoff[i][j] * policy[j];
            }
            utility[i] = value;
        }
        for (int i = 0; i < size; i++) logits[i] += eta * utility[i];
    }

    double exploitability = -INFINITY;
    for (int i = 0; i < size; i++) mixture[i] = average[i] / iterations;
    for (int i = 0; i < size; i++) {
        double value = 0.0;
        for (int j = 0; j < size; j++) {
            value += game->payoff[i][j] * mixture[j];
        }
        response[i] = value;
        if (value > exploitability) exploitability = value;
    }
    return exploitability;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "diversity") == 0) {
        return kg_meta_diversity(argc, argv);
    }
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: %s MATRIX.tsv [iterations]\n", argv[0]);
        return 2;
    }
    long iterations = argc == 3 ? strtol(argv[2], NULL, 10) : 2000000;
    if (iterations < 1000) {
        fprintf(stderr, "iterations must be at least 1000\n");
        return 2;
    }

    KGMetaGame game = {0};
    if (!kg_meta_load(argv[1], &game)) return 1;
    double mixture[KG_META_MAX_POLICIES] = {0};
    double response[KG_META_MAX_POLICIES] = {0};
    double exploitability = kg_meta_solve(&game, iterations, mixture, response);

    printf("policy\tweight\tresponse\n");
    for (int i = 0; i < game.size; i++) {
        printf("%s\t%.9f\t%.9f\n", game.names[i], mixture[i], response[i]);
    }
    printf("# exploitability\t%.9f\n", exploitability);
    printf("# iterations\t%ld\n", iterations);
    return 0;
}
