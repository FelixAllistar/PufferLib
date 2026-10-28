#define _GNU_SOURCE
// Policy server for the Showdown bridge: loads one checkpoint (its saved config.ini gives the architecture) and answers
// observation requests for any number of concurrent battles, each with its own recurrent state, on the CPU.
// Protocol (one line each, stdin -> stdout):  "ID new" resets battle ID's memory; "ID HEX" (648 observation bytes as hex,
// mask included) -> "ID ACTION"; "ID end" frees it. Argmax by default, --sample to sample like the training policy.
// Build: ocean/pokemon/showdown/build.sh. Usage: policy_server CHECKPOINT.bin [--sample]
#include "../pokemon.h"
#include "../policy_view.h"

#define MAX_BATTLES 256
typedef struct { char id[64]; PKCpuPolicy* net; } Battle;
static Battle battles[MAX_BATTLES];

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s CHECKPOINT.bin [--sample]\n", argv[0]); return 2; }
    int sample = argc > 2 && !strcmp(argv[2], "--sample");
    PKPolicy base = {0};
    pk_load_policy(&base, argv[1], 0, 0, NULL);
    pk_reset_policy(&base, 20261027);
    Dict* p = puf_ini_section(&base.ini, "policy", 0);
    DictItem *embed = dict_find(p, "pokemon_embed"), *mid = dict_find(p, "pokemon_mid");
    int h = puf_ini_get(&base.ini, "policy", "hidden_size"), layers = puf_ini_get(&base.ini, "policy", "num_layers");
    char line[4096];
    setvbuf(stdout, NULL, _IOLBF, 0);
    while (fgets(line, sizeof line, stdin)) {
        char id[64], arg[2048];
        if (sscanf(line, "%63s %2047s", id, arg) != 2) continue;
        int slot = -1, empty = -1;
        for (int i = 0; i < MAX_BATTLES; i++) {
            if (battles[i].net && !strcmp(battles[i].id, id)) slot = i;
            if (!battles[i].net && empty < 0) empty = i;
        }
        if (!strcmp(arg, "end")) { if (slot >= 0) { pk_cpu_free(battles[slot].net); battles[slot].net = NULL; } continue; }
        if (slot < 0) {
            if (empty < 0) { printf("%s error too many battles\n", id); continue; }
            slot = empty; snprintf(battles[slot].id, sizeof battles[slot].id, "%s", id);
            base.weights->idx = 0;  // pk_cpu_make reads the weights through this cursor; every battle shares them
            battles[slot].net = pk_cpu_make(base.weights, 1, h, layers, embed ? embed->value : 32, mid ? mid->value : 64,
                pk_attn_off, pk_window_off);
        }
        if (!strcmp(arg, "new")) { pk_cpu_reset_memory(battles[slot].net); continue; }
        if (strlen(arg) != 2 * PK_OBS) { printf("%s error bad observation\n", id); continue; }
        uint8_t obs[PK_OBS];
        for (int i = 0; i < PK_OBS; i++) sscanf(arg + 2 * i, "%2hhx", &obs[i]);
        base.net = battles[slot].net;
        printf("%s %d\n", id, pk_policy_action(&base, obs, obs + 480, !sample));
    }
    base.net = NULL;
    return 0;
}
