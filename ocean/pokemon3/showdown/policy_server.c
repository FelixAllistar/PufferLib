// Policy server for the Gen 3 Showdown bridge (2026-11-06): one pokemon3 checkpoint run on the CPU, any number of
// concurrent battles, each with its own recurrent state. Two networks, told apart by the checkpoint's size:
// PufferLib's default (linear encoder, MinGRU, linear decoder; src/puffercpu.c) or the structured one
// (policy.pokemon3_net=1, 2026-11-08: ../pk3_net_cpu.h encoder and pointer decoder around the same MinGRU). The
// structured net's widths come from --embed E --mid M (the run's policy.pokemon_embed / pokemon_mid, default 32 / 64).
// Protocol (one line each, stdin -> stdout):
//   "ID new"   resets battle ID's memory; "ID end" frees it.
//   "ID TURN\tMASK\tSEEN\tSTATE" -> "ID ACTION". STATE is a poke-engine state with side one = us, SEEN what we have seen
//   of the opponent (see pk3_view in ../engine/src/lib.rs), MASK ten 0/1 characters (legal battle actions, from
//   Showdown's request; the network's other 118 actions are the draft's, never legal here), TURN the turn number. The observation is built by the env's own code, so it matches training.
// Argmax by default, --sample to sample like the training policy.
// Build: ocean/pokemon3/showdown/build.sh.
// Usage: policy_server CHECKPOINT.bin HIDDEN LAYERS [--sample] [--embed E] [--mid M]
#include "../../../src/puffercpu.c"
#include <stdint.h>
#include "../pk3_net_cpu.h"

#define OBS 5343
#define ACTIONS 128
#define BATTLE_ACTIONS 10
int pk3_view(const char* state, const char* seen, int turns, float* out);
int pk3_obs_size(void);

#define MAX_BATTLES 256
typedef struct { char id[256]; int used; Linear* enc; MinGRU* gru; Linear* dec; } Battle;
static Battle battles[MAX_BATTLES];
static char line[1 << 20];

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s CHECKPOINT.bin HIDDEN LAYERS [--sample] [--embed E] [--mid M]\n", argv[0]);
        return 2;
    }
    assert(pk3_obs_size() == OBS);
    int hidden = atoi(argv[2]), layers = atoi(argv[3]), sample = 0, E = 32, M = 64;
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "--sample")) sample = 1;
        else if (!strcmp(argv[i], "--embed") && i + 1 < argc) E = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mid") && i + 1 < argc) M = atoi(argv[++i]);
        else { fprintf(stderr, "unknown argument %s\n", argv[i]); return 2; }
    }
    Weights* w = load_weights(argv[1]);
    assert(w && "cannot read checkpoint");
    // Which network: every tensor size here is a multiple of 8, so the checkpoint is the tensors back to back.
    long gru = (long)layers * 3 * hidden * hidden;
    long flat = (long)OBS * hidden + (long)(ACTIONS + 1) * hidden + gru;
    long mlp_move = M * P3_AUG(P3_MOVE_IN) + E * P3_AUG(M), mlp_mon = M * P3_AUG(P3_MON_IN(E)) + E * P3_AUG(M);
    long enc_size = mlp_move + mlp_mon + hidden * P3_AUG(P3_FUSION(E)) + hidden * P3_AUG(hidden);
    long dec_size = (ACTIONS + 1) * hidden + hidden * P3_AUG(hidden) + 2 * E * P3_AUG(hidden) + mlp_move + mlp_mon;
    long floats = (long)w->size - 7;  // load_weights pads 7 floats past the file
    int structured = floats == enc_size + dec_size + gru;
    if (!structured && floats != flat) {
        fprintf(stderr, "checkpoint has %ld floats: not the default net (%ld) or the structured net (%ld, E %d, M %d)\n",
            floats, flat, enc_size + dec_size + gru, E, M);
        return 2;
    }
    Pk3CpuEnc penc; Pk3CpuDec pdec;
    if (structured) {
        const float* p = pk3_cpu_enc_bind(&penc, E, M, hidden, w->data);
        p = pk3_cpu_dec_bind(&pdec, E, M, hidden, p);
        assert(p == w->data + enc_size + dec_size);
    }
    fprintf(stderr, "policy_server: %s net, hidden %d, layers %d\n", structured ? "structured" : "default", hidden, layers);
    float* enc_out = (float*)calloc(hidden, sizeof(float));
    static float logits[ACTIONS + 1];
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
            if (battles[i].used && !strcmp(battles[i].id, id)) slot = i;
            if (!battles[i].used && empty < 0) empty = i;
        }
        if (!strcmp(arg, "end") || !strcmp(arg, "new")) {
            if (slot >= 0) {
                free(battles[slot].enc); free(battles[slot].dec); free_mingru(battles[slot].gru);
                battles[slot] = (Battle){0};
            }
            if (!strcmp(arg, "end")) continue;
            slot = -1;
            for (int i = 0; i < MAX_BATTLES && empty < 0; i++) if (!battles[i].used) empty = i;
        }
        if (slot < 0) {
            if (empty < 0) { printf("%s error too many battles\n", id); continue; }
            slot = empty;
            snprintf(battles[slot].id, sizeof battles[slot].id, "%s", id);
            battles[slot].used = 1;
            // Every battle reads the same weights, in the checkpoint's order: encoder, decoder, MinGRU.
            if (structured) {
                w->idx = enc_size + dec_size;
            } else {
                w->idx = 0;
                battles[slot].enc = make_linear(w, 1, OBS, hidden);
                battles[slot].dec = make_linear(w, 1, hidden, ACTIONS + 1);
            }
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
        float* out;
        if (structured) {
            pk3_cpu_encode(&penc, obs, enc_out);
            mingru(b->gru, enc_out);
            pk3_cpu_decode(&pdec, obs, b->gru->output, logits);
            out = logits;
        } else {
            linear(b->enc, obs);
            mingru(b->gru, b->enc->output);
            linear(b->dec, b->gru->output);
            out = b->dec->output;
        }
        float action = 0;
        multidiscrete(head, out, &action, !sample, mask);
        printf("%s %d\n", id, (int)action);
    }
    return 0;
}
