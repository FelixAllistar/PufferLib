// Policy server for the Gen 3 Showdown bridge (2026-11-06): one pokemon3 checkpoint (PufferLib's default network, run
// on the CPU by src/puffercpu.c), any number of concurrent battles, each with its own recurrent state.
// Protocol (one line each, stdin -> stdout):
//   "ID new"   resets battle ID's memory; "ID end" frees it.
//   "ID TURN\tMASK\tSEEN\tSTATE" -> "ID ACTION". STATE is a poke-engine state with side one = us, SEEN what we have seen
//   of the opponent (see pk3_view in ../engine/src/lib.rs), MASK ten 0/1 characters (legal battle actions, from
//   Showdown's request; the network's other 118 actions are the draft's, never legal here), TURN the turn number. The observation is built by the env's own code, so it matches training.
// Argmax by default, --sample to sample like the training policy.
// Build: ocean/pokemon3/showdown/build.sh. Usage: policy_server CHECKPOINT.bin HIDDEN LAYERS [--sample]
#include "../../../src/puffercpu.c"
#include <stdint.h>

#define OBS 5343
#define ACTIONS 128
#define BATTLE_ACTIONS 10
int pk3_view(const char* state, const char* seen, int turns, float* out);
int pk3_obs_size(void);

#define MAX_BATTLES 256
typedef struct { char id[256]; Linear* enc; MinGRU* gru; Linear* dec; } Battle;
static Battle battles[MAX_BATTLES];
static char line[1 << 20];

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s CHECKPOINT.bin HIDDEN LAYERS [--sample]\n", argv[0]); return 2; }
    assert(pk3_obs_size() == OBS);
    int hidden = atoi(argv[2]), layers = atoi(argv[3]), sample = argc > 4 && !strcmp(argv[4], "--sample");
    Weights* w = load_weights(argv[1]);
    assert(w && "cannot read checkpoint");
    int act_sizes[] = {ACTIONS};
    Multidiscrete* head = make_multidiscrete(1, act_sizes, 1);
    static float obs[OBS];
    setvbuf(stdout, NULL, _IOLBF, 0);
    srand(20261106);
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\n")] = 0;
        char* sp = strchr(line, ' ');
        if (!sp) continue;
        *sp = 0;
        const char *id = line, *arg = sp + 1;
        int slot = -1, empty = -1;
        for (int i = 0; i < MAX_BATTLES; i++) {
            if (battles[i].enc && !strcmp(battles[i].id, id)) slot = i;
            if (!battles[i].enc && empty < 0) empty = i;
        }
        if (!strcmp(arg, "end") || !strcmp(arg, "new")) {
            if (slot >= 0) {
                free(battles[slot].enc); free(battles[slot].dec); free_mingru(battles[slot].gru);
                battles[slot].enc = NULL;
            }
            if (!strcmp(arg, "end")) continue;
            slot = -1;
            for (int i = 0; i < MAX_BATTLES && empty < 0; i++) if (!battles[i].enc) empty = i;
        }
        if (slot < 0) {
            if (empty < 0) { printf("%s error too many battles\n", id); continue; }
            slot = empty;
            snprintf(battles[slot].id, sizeof battles[slot].id, "%s", id);
            w->idx = 0;  // every battle reads the same weights, in the checkpoint's order
            battles[slot].enc = make_linear(w, 1, OBS, hidden);
            battles[slot].dec = make_linear(w, 1, hidden, ACTIONS + 1);
            battles[slot].gru = make_mingru(w, 1, hidden, layers);
        }
        if (!strcmp(arg, "new")) continue;
        // TURN \t MASK \t SEEN \t STATE
        char* f[4]; char* cur = (char*)arg;
        for (int k = 0; k < 4; k++) { f[k] = cur; char* t = strchr(cur, '\t'); if (k < 3) { if (!t) { f[0] = NULL; break; } *t = 0; cur = t + 1; } }
        if (!f[0] || strlen(f[1]) != BATTLE_ACTIONS) { printf("%s error bad request\n", id); continue; }
        unsigned char mask[ACTIONS] = {0};
        for (int a = 0; a < BATTLE_ACTIONS; a++) mask[a] = f[1][a] == '1';
        if (pk3_view(f[3], f[2], atoi(f[0]), obs) != 0) { printf("%s error bad state\n", id); continue; }
        // The trainer feeds bf16 observations; round the same way (truncation, as pk3_observe_bf16 does).
        for (int i = 0; i < OBS; i++) { uint32_t u; memcpy(&u, &obs[i], 4); u &= 0xffff0000u; memcpy(&obs[i], &u, 4); }
        Battle* b = &battles[slot];
        linear(b->enc, obs);
        mingru(b->gru, b->enc->output);
        linear(b->dec, b->gru->output);
        float action = 0;
        multidiscrete(head, b->dec->output, &action, !sample, mask);
        printf("%s %d\n", id, (int)action);
    }
    return 0;
}
