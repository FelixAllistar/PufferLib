#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    OBS_SIZE = 1156,
    UNIT_HEADS = 12,
    UNIT_ACTIONS = 44,
    MARKET_ACTIONS = 22,
    MARKET_HEADS = 1,
    MASK_SIZE = UNIT_HEADS * UNIT_ACTIONS + MARKET_HEADS * MARKET_ACTIONS,
};

static float gate_offset = 0.08f;
static int hidden_size;

static void gate_row(float* decoder, int row, int pass_row) {
    for (int hidden = 0; hidden < hidden_size; hidden++) {
        decoder[row * hidden_size + hidden] =
            decoder[pass_row * hidden_size + hidden] - gate_offset;
    }
}

static size_t expected_floats(int hidden, int layers) {
    return (size_t)hidden * (OBS_SIZE + MASK_SIZE + 1)
        + (size_t)3 * layers * hidden * hidden;
}

static int parse_positive(const char* text) {
    char* end = NULL;
    long value = strtol(text, &end, 10);
    return end != text && *end == '\0' && value > 0 && value <= 4096
        ? (int)value : 0;
}

int main(int argc, char** argv) {
    if ((argc != 4 && argc != 6)
            || (strcmp(argv[3], "land") && strcmp(argv[3], "land-safe")
            && strcmp(argv[3], "full") && strcmp(argv[3], "full-safe")
            && strcmp(argv[3], "safe"))) {
        fprintf(stderr,
            "usage: %s INPUT OUTPUT land|land-safe|full|full-safe|safe [HIDDEN LAYERS]\n",
            argv[0]);
        return 2;
    }
    gate_offset = strstr(argv[3], "safe") ? 0.50f : 0.08f;
    FILE* input = fopen(argv[1], "rb");
    if (!input) return perror(argv[1]), 1;
    if (fseek(input, 0, SEEK_END) || ftell(input) < 0) {
        return perror(argv[1]), fclose(input), 1;
    }
    long bytes = ftell(input);
    if (bytes % (long)sizeof(float) || fseek(input, 0, SEEK_SET)) {
        fprintf(stderr, "checkpoint is not a raw float array: %s\n", argv[1]);
        fclose(input);
        return 1;
    }
    size_t float_count = (size_t)bytes / sizeof(float);
    int layers = 0;
    if (argc == 6) {
        hidden_size = parse_positive(argv[4]);
        layers = parse_positive(argv[5]);
    } else {
        int matches = 0;
        for (int candidate_layers = 1; candidate_layers <= 16;
                candidate_layers++) {
            for (int candidate_hidden = 8; candidate_hidden <= 4096;
                    candidate_hidden++) {
                if (expected_floats(candidate_hidden, candidate_layers)
                        == float_count) {
                    hidden_size = candidate_hidden;
                    layers = candidate_layers;
                    matches++;
                }
            }
        }
        if (matches != 1) {
            fprintf(stderr,
                "could not uniquely infer architecture from %zu floats; pass HIDDEN LAYERS\n",
                float_count);
            fclose(input);
            return 1;
        }
    }
    if (!hidden_size || !layers
            || expected_floats(hidden_size, layers) != float_count) {
        fprintf(stderr, "checkpoint shape does not match hidden=%d layers=%d\n",
            hidden_size, layers);
        fclose(input);
        return 1;
    }

    float* weights = malloc(float_count * sizeof(float));
    if (!weights) return 1;
    size_t count = fread(weights, sizeof(float), float_count, input);
    int extra = fgetc(input);
    fclose(input);
    if (count != float_count || extra != EOF) {
        fprintf(stderr, "unexpected checkpoint size: %zu floats\n", count);
        free(weights);
        return 1;
    }

    float* decoder = weights + (size_t)OBS_SIZE * hidden_size;
    int market_base = UNIT_HEADS * UNIT_ACTIONS;
    if (!strcmp(argv[3], "land") || !strcmp(argv[3], "land-safe")) {
        for (int head = 0; head < MARKET_HEADS; head++) {
            int market_pass = market_base + head * MARKET_ACTIONS;
            gate_row(decoder, market_pass + 75, market_pass);
        }
    } else {
        static const int new_unit_actions[] = {
            25, 26, 27, 29, 30, 31, 41, 42, 43,
        };
        for (int head = 0; head < UNIT_HEADS; head++) {
            int pass = head * UNIT_ACTIONS;
            for (size_t i = 0;
                    i < sizeof(new_unit_actions) / sizeof(new_unit_actions[0]);
                    i++) {
                gate_row(decoder, pass + new_unit_actions[i], pass);
            }
        }
        for (int head = 0; head < MARKET_HEADS; head++) {
            int market_pass = market_base + head * MARKET_ACTIONS;
            for (int action = 21; action < 38; action++) {
                gate_row(decoder, market_pass + action, market_pass);
            }
            gate_row(decoder, market_pass + 75, market_pass);
        }
    }

    FILE* output = fopen(argv[2], "wb");
    if (!output) return perror(argv[2]), free(weights), 1;
    count = fwrite(weights, sizeof(float), float_count, output);
    int failed = count != float_count || fclose(output) != 0;
    free(weights);
    if (failed) return perror(argv[2]), 1;
    printf("Gated %s actions (hidden=%d layers=%d): %s -> %s\n",
        argv[3], hidden_size, layers, argv[1], argv[2]);
    return 0;
}
