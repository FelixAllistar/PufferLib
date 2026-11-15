// CPU policy path (policy_cpu.c, docs/encoder.md "CPU inference"): checks it against the NumPy reference, its refusals
// and its determinism in the sim; with --bench, times it.
//
//   test_policy                     the stored fixtures (tests/policy_fixture_net{0,1}.bin), refusals, parsing, a sim run
//   test_policy FIXTURE...          only these fixtures (tools/policy_reference.py --out FIXTURE writes one)
//   test_policy --bench [--runs N]  ms per step for 1, 8 and 64 agents, both encoders, hidden 256 and 1024 (2 layers)
//
// A fixture holds the seed and shapes of its weights, which are regenerated here bit for bit (the sim's xorshift
// generator, uniform in +-sqrt(6 / cols)) and written as a checkpoint so the loader is part of the check, then the
// observations, resets and the reference's float64 encoder output, logits + value and actions per step.
#include "test_util.h"
#include "../policy_cpu.h"
#include <omp.h>
#include "../../../src/puffercpu.c"   // PufferNet: the trainer-matched CPU forward of the default policy (./puffer eval)

#define FX_MAGIC 0x43504F4C
#define FX_TENSORS 32
// float32 against the float64 reference: both encoders, the MinGRU and the decoder stay within 7.2e-7 of it on the
// stored fixtures (docs/encoder.md); the bars leave room for the summation order of other compilers and SIMD widths.
#define FX_ATOL 2e-4
#define FX_RTOL 1e-4

typedef struct Fixture {
    int net, hidden, layers, B, T, nt; uint32_t wseed, sseed;
    int rows[FX_TENSORS], cols[FX_TENSORS];
    int32_t* reset; float *obs, *enc, *out; int32_t *greedy, *sampled;   // per step, concatenated
} Fixture;

static bool fx_read(const char* path, Fixture* fx) {
    FILE* f = fopen(path, "rb"); if (!f) return false;
    int32_t h[8]; uint32_t seeds[2];
    bool ok = fread(h, 4, 8, f) == 8 && fread(seeds, 4, 2, f) == 2 && h[0] == FX_MAGIC && h[1] == 1 && h[7] <= FX_TENSORS;
    if (ok) {
        *fx = (Fixture){.net = h[2], .hidden = h[3], .layers = h[4], .B = h[5], .T = h[6], .nt = h[7], .wseed = seeds[0], .sseed = seeds[1]};
        for (int i = 0; i < fx->nt && ok; i++) { int32_t rc[2]; ok = fread(rc, 4, 2, f) == 2; fx->rows[i] = rc[0]; fx->cols[i] = rc[1]; }
    }
    if (ok) {
        long B = fx->B, T = fx->T, H = fx->hidden;
        fx->reset = malloc(T * B * 4); fx->obs = malloc(T * B * CRET_OBS_SIZE * 4); fx->enc = malloc(T * B * H * 4);
        fx->out = malloc(T * B * CRET_POLICY_OUT * 4); fx->greedy = malloc(T * B * CRET_ACTION_HEADS * 4); fx->sampled = malloc(T * B * CRET_ACTION_HEADS * 4);
        for (long t = 0; t < T && ok; t++)
            ok = fread(fx->reset + t * B, 4, B, f) == (size_t)B && fread(fx->obs + t * B * CRET_OBS_SIZE, 4, B * CRET_OBS_SIZE, f) == (size_t)(B * CRET_OBS_SIZE)
              && fread(fx->enc + t * B * H, 4, B * H, f) == (size_t)(B * H) && fread(fx->out + t * B * CRET_POLICY_OUT, 4, B * CRET_POLICY_OUT, f) == (size_t)(B * CRET_POLICY_OUT)
              && fread(fx->greedy + t * B * CRET_ACTION_HEADS, 4, B * CRET_ACTION_HEADS, f) == (size_t)(B * CRET_ACTION_HEADS)
              && fread(fx->sampled + t * B * CRET_ACTION_HEADS, 4, B * CRET_ACTION_HEADS, f) == (size_t)(B * CRET_ACTION_HEADS);
        ok = ok && fgetc(f) == EOF;
    }
    fclose(f);
    return ok;
}
static void fx_free(Fixture* fx) { free(fx->reset); free(fx->obs); free(fx->enc); free(fx->out); free(fx->greedy); free(fx->sampled); }

// The fixture's weights: tools/policy_reference.py gen_weights, the same float32 roundings.
static float* fx_weights(const Fixture* fx, long* n) {
    *n = 0; for (int i = 0; i < fx->nt; i++) *n += (long)fx->rows[i] * fx->cols[i];
    float* w = malloc((size_t)*n * sizeof(float)); uint32_t s = fx->wseed; long k = 0;
    for (int i = 0; i < fx->nt; i++) {
        float scale = sqrtf(6.0f / (float)fx->cols[i]);
        for (long j = 0; j < (long)fx->rows[i] * fx->cols[i]; j++) w[k++] = ((float)(t_rand(&s) >> 8) * (1.0f / 16777216.0f) * 2.0f - 1.0f) * scale;
    }
    return w;
}

static bool write_floats(const char* path, const float* w, long n) {
    FILE* f = fopen(path, "wb"); if (!f) return false;
    bool ok = fwrite(w, sizeof(float), (size_t)n, f) == (size_t)n; fclose(f); return ok;
}

// Runs the C path over the fixture and compares every output; returns the largest errors.
static void test_fixture(const char* path) {
    Fixture fx;
    if (!fx_read(path, &fx)) { CHECK(0, "%s: missing or malformed fixture", path); return; }
    long n; float* w = fx_weights(&fx, &n);
    char ckpt[600], err[600]; snprintf(ckpt, sizeof ckpt, "build/cretaceous/test/policy_ckpt_net%d.bin", fx.net);
    CHECK(write_floats(ckpt, w, n), "cannot write %s", ckpt);
    CHECK(n == cret_policy_param_count(fx.net, fx.hidden, fx.layers), "%s: %ld weights, the C layout counts %ld", path, n,
        cret_policy_param_count(fx.net, fx.hidden, fx.layers));
    CretPolicy* p = cret_policy_load(ckpt, fx.net, fx.hidden, fx.layers, fx.B, err, sizeof err);
    CretPolicy* found = cret_policy_load(ckpt, -1, -1, -1, 1, err, sizeof err);   // the sizes found from the file
    CHECK(p && found, "%s: %s", ckpt, err);
    if (found) { char d[96]; cret_policy_describe(found, d, sizeof d); char want[96]; snprintf(want, sizeof want, "cretaceous_net %d, hidden %d x %d layers", fx.net, fx.hidden, fx.layers); CHECK(!strcmp(d, want), "found %s, want %s", d, want); }
    cret_policy_free(found);
    if (!p) { free(w); fx_free(&fx); return; }
    long B = fx.B, H = fx.hidden; int slots[256]; for (int k = 0; k < B; k++) slots[k] = k;
    double enc_err = 0, out_err = 0, enc_worst = 0, out_worst = 0; int greedy_bad = 0, sampled_bad = 0, checked = 0;
    uint32_t rng = fx.sseed;
    for (long t = 0; t < fx.T; t++) {
        for (int k = 0; k < B; k++) if (fx.reset[t * B + k]) cret_policy_reset(p, k);
        cret_policy_forward(p, fx.obs + t * B * CRET_OBS_SIZE, slots, (int)B);
        for (int k = 0; k < B; k++) {
            const float *e = cret_policy_encoded(p, k), *o = cret_policy_output(p, k);
            for (int j = 0; j < H; j++) {
                double ref = fx.enc[(t * B + k) * H + j], d = fabs(e[j] - ref);
                enc_err = fmax(enc_err, d); enc_worst = fmax(enc_worst, d / (FX_ATOL + FX_RTOL * fabs(ref)));
            }
            for (int j = 0; j < CRET_POLICY_OUT; j++) {
                double ref = fx.out[(t * B + k) * CRET_POLICY_OUT + j], d = fabs(o[j] - ref);
                out_err = fmax(out_err, d); out_worst = fmax(out_worst, d / (FX_ATOL + FX_RTOL * fabs(ref)));
            }
            float g[CRET_ACTION_HEADS], s[CRET_ACTION_HEADS];
            cret_policy_pick(p, k, true, &rng, g);
            cret_policy_pick(p, k, false, &rng, s);
            for (int h = 0; h < CRET_ACTION_HEADS; h++) {
                int rg = fx.greedy[(t * B + k) * CRET_ACTION_HEADS + h], rs = fx.sampled[(t * B + k) * CRET_ACTION_HEADS + h];
                if (rg >= 0) { checked++; greedy_bad += (int)g[h] != rg; }
                if (rs >= 0) { checked++; sampled_bad += (int)s[h] != rs; }
            }
        }
    }
    printf("policy: %s (cretaceous_net %d, hidden %d x %d, %d agents x %d steps): max abs error vs float64 NumPy: encoder %.2e, logits + value %.2e; %d of %d actions differ\n",
        path, fx.net, fx.hidden, fx.layers, fx.B, fx.T, enc_err, out_err, greedy_bad + sampled_bad, checked);
    CHECK(enc_worst <= 1 && out_worst <= 1, "%s: errors %.2e / %.2e over the tolerance (%.2f, %.2f of it)", path, enc_err, out_err, enc_worst, out_worst);
    CHECK(greedy_bad == 0 && sampled_bad == 0, "%s: %d greedy and %d sampled actions differ from the reference", path, greedy_bad, sampled_bad);
    cret_policy_free(p); free(w); fx_free(&fx);
}

// The linear policy against PufferNet (src/puffercpu.c), an independent CPU forward of the same checkpoint: the
// logits and value agree over several recurrent steps, terminals included.
static void test_puffernet(void) {
    Fixture fx; const char* path = "ocean/cretaceous/tests/policy_fixture_net0.bin";
    if (!fx_read(path, &fx)) { CHECK(0, "%s missing", path); return; }
    long n; float* w = fx_weights(&fx, &n); char err[600];
    const char* ckpt = "build/cretaceous/test/policy_ckpt_net0.bin";
    CHECK(write_floats(ckpt, w, n), "cannot write %s", ckpt);
    Weights* pw = load_weights(ckpt);
    CretPolicy* p = cret_policy_create(w, n, 0, fx.hidden, fx.layers, fx.B, err, sizeof err);
    if (!pw || !p) { CHECK(0, "load: %s", err); free(w); fx_free(&fx); return; }
    int sizes[] = CRET_ACTION_SIZES, slots[256];
    PufferNet* net = make_puffernet(pw, fx.B, CRET_OBS_SIZE, fx.hidden, fx.layers, sizes, CRET_ACTION_HEADS);
    float* actions = calloc((size_t)fx.B * CRET_ACTION_HEADS, sizeof(float)); float term[256];
    double worst = 0;
    for (int k = 0; k < fx.B; k++) slots[k] = k;
    for (int t = 0; t < fx.T; t++) {
        for (int k = 0; k < fx.B; k++) { term[k] = fx.reset[t * fx.B + k] ? 1.0f : 0.0f; if (term[k] > 0.5f) cret_policy_reset(p, k); }
        float* obs = fx.obs + (long)t * fx.B * CRET_OBS_SIZE;
        forward_puffernet(net, obs, actions, NULL, term);
        cret_policy_forward(p, obs, slots, fx.B);
        for (int k = 0; k < fx.B; k++)
            for (int j = 0; j < CRET_POLICY_OUT; j++) worst = fmax(worst, fabs(cret_policy_output(p, k)[j] - net->decoder->output[k * CRET_POLICY_OUT + j]));
    }
    printf("policy: cretaceous_net 0 against PufferNet (src/puffercpu.c), %d agents x %d steps: logits + value within %.2e\n", fx.B, fx.T, worst);
    CHECK(worst < 1e-5, "the linear policy differs from PufferNet by %.2e", worst);
    free_puffernet(net); free(pw); free(actions); cret_policy_free(p); free(w); fx_free(&fx);
}

// A checkpoint of another size, encoder or contract is refused, naming both counts.
static void test_refusals(void) {
    char err[600]; long n0 = cret_policy_param_count(0, 32, 2), n1 = cret_policy_param_count(1, 32, 2);
    long c3 = 256L * 197 + 20L * 256 + 2L * 3 * 256 * 256;   // contract 3's linear encoder at 256 x 2: 197 inputs, 19 logits
    float* w = calloc((size_t)(c3 > n1 ? c3 : n1), sizeof(float));
    CretPolicy* p = cret_policy_create(w, n0, 0, 64, 2, 4, err, sizeof err);
    char a[32], b[32]; snprintf(a, sizeof a, "%ld", n0); snprintf(b, sizeof b, "%ld", cret_policy_param_count(0, 64, 2));
    CHECK(!p && strstr(err, a) && strstr(err, b), "wrong hidden: '%s' should name %s and %s", err, a, b);
    p = cret_policy_create(w, n0, 1, 32, 2, 4, err, sizeof err);
    snprintf(b, sizeof b, "%ld", n1);
    CHECK(!p && strstr(err, a) && strstr(err, b), "wrong encoder: '%s' should name %s and %s", err, a, b);
    p = cret_policy_create(w, c3, -1, -1, -1, 4, err, sizeof err);
    CHECK(!p && strstr(err, "no policy"), "a contract 3 checkpoint is found: '%s'", err);
    snprintf(a, sizeof a, "%ld", c3); snprintf(b, sizeof b, "%ld", cret_policy_param_count(0, 256, 2));
    p = cret_policy_create(w, c3, 0, 256, 2, 4, err, sizeof err);
    CHECK(!p && strstr(err, a) && strstr(err, b), "contract 3 at 256 x 2: '%s' should name %s and %s", err, a, b);
    p = cret_policy_create(w, n0 - 8, 0, 32, 2, 4, err, sizeof err);
    CHECK(!p, "a truncated checkpoint loads");
    p = cret_policy_load("build/cretaceous/test/no_such_policy.bin", -1, -1, -1, 4, err, sizeof err);
    CHECK(!p && strstr(err, "cannot read"), "a missing file: '%s'", err);
    p = cret_policy_create(w, n1, -1, -1, -1, 4, err, sizeof err);
    CHECK(p, "the structured size at 32 x 2 is not found: %s", err);
    cret_policy_free(p); free(w);
}

static void test_parse(void) {
    CretPolicyArg a;
    CHECK(cret_policy_parse_arg("ck/a.bin", &a) && !strcmp(a.who, "all") && !strcmp(a.path, "ck/a.bin") && a.net < 0, "plain path");
    CHECK(cret_policy_parse_arg("carnivores=ck/a.bin:1:1024:2", &a) && !strcmp(a.who, "carnivores") && !strcmp(a.path, "ck/a.bin")
        && a.net == 1 && a.hidden == 1024 && a.layers == 2, "who and sizes: %s %s %d %d %d", a.who, a.path, a.net, a.hidden, a.layers);
    CHECK(cret_policy_parse_arg("slot3=C:\\ck\\a.bin", &a) && !strcmp(a.path, "C:\\ck\\a.bin") && a.net < 0, "a drive letter is not a size");
    CHECK(cret_policy_parse_arg("ck/run:7/a.bin", &a) && !strcmp(a.path, "ck/run:7/a.bin"), "a colon inside the path");
    CHECK(!cret_policy_parse_arg("dragons=a.bin", &a), "an unknown WHO is accepted");
    CHECK(!cret_policy_parse_arg("a.bin:2:256:2", &a), "encoder 2 is accepted");
    CHECK(cret_policy_who_matches("raptor", 4, CRET_SP_RAPTOR) && !cret_policy_who_matches("herbivores", 0, CRET_SP_RAPTOR)
        && cret_policy_who_matches("slot5", 5, CRET_SP_STEGO) && !cret_policy_who_matches("slot5", 6, CRET_SP_STEGO), "who matching");
}

// The policy driving every slot of two sims gives the same game, tick for tick, and the same outputs with 1 or 4
// threads; it moves the animals (inputs are not all zero).
static void test_sim_run(void) {
    Fixture fx; const char* path = "ocean/cretaceous/tests/policy_fixture_net1.bin";
    if (!fx_read(path, &fx)) { CHECK(0, "%s missing", path); return; }
    long n; float* w = fx_weights(&fx, &n); char err[600];
    const int A = 12, TICKS = 240;
    CretSim* s[2]; CretPolicy* p[2]; uint32_t rng[2] = {7, 7};
    for (int k = 0; k < 2; k++) {
        s[k] = calloc(1, sizeof(CretSim)); CretConfig c = t_config(A);
        CHECK(cret_sim_init(s[k], c) == 0, "sim init");
        p[k] = cret_policy_create(w, n, fx.net, fx.hidden, fx.layers, A, err, sizeof err);
        CHECK(p[k], "%s", err);
    }
    if (!p[0] || !p[1]) return;
    bool mask[CRET_MAX_AGENTS]; for (int i = 0; i < A; i++) mask[i] = i % 3 != 2;   // bots keep every third slot
    int moving = 0, differ = 0, driven = 0; for (int i = 0; i < A; i++) driven += mask[i];
    for (int t = 0; t < TICKS; t++) {
        for (int k = 0; k < 2; k++) {
            omp_set_num_threads(k ? 4 : 1);
            CretInput in[CRET_MAX_AGENTS];
            for (int i = 0; i < A; i++) in[i] = cret_bot_input(s[k], i);
            cret_policy_act(p[k], s[k], mask, false, &rng[k], in);
            if (k == 0) for (int i = 0; i < A; i++) moving += mask[i] && (in[i].forward != 0 || in[i].turn != 0);
            cret_sim_step(s[k], in);
        }
        differ += t_sim_hash(s[0]) != t_sim_hash(s[1]);
    }
    omp_set_num_threads(4);
    printf("policy: %d of %d slots on the fixture policy for %d ticks, 1 vs 4 threads: %d ticks differ, %d moving inputs\n", driven, A, TICKS, differ, moving);
    CHECK(differ == 0, "%d ticks differ between the two sims", differ);
    CHECK(moving > TICKS, "the policy hardly moves the animals (%d moving inputs)", moving);
    for (int k = 0; k < 2; k++) { cret_policy_free(p[k]); cret_sim_close(s[k]); free(s[k]); }
    free(w); fx_free(&fx);
}

// At a size where the dense layers split over threads (hidden 256, 64 agents), 1 and 4 threads give the same bits.
static void test_threads(void) {
    for (int net = 0; net <= 1; net++) {
        const int B = 64, H = 256; long n = cret_policy_param_count(net, H, 2); char err[600];
        float* w = malloc((size_t)n * sizeof(float)); uint32_t s = 3;
        for (long i = 0; i < n; i++) w[i] = (t_frand(&s) * 2 - 1) * 0.08f;
        float* obs = malloc((size_t)B * CRET_OBS_SIZE * sizeof(float)); int slots[B];
        for (long i = 0; i < (long)B * CRET_OBS_SIZE; i++) obs[i] = t_frand(&s) * 2 - 1;
        for (int k = 0; k < B; k++) slots[k] = k;
        float* out[2];
        for (int k = 0; k < 2; k++) {
            omp_set_num_threads(k ? 4 : 1);
            CretPolicy* p = cret_policy_create(w, n, net, H, 2, B, err, sizeof err);
            for (int step = 0; step < 3; step++) cret_policy_forward(p, obs, slots, B);   // the recurrent state too
            out[k] = malloc((size_t)B * CRET_POLICY_OUT * sizeof(float));
            for (int r = 0; r < B; r++) memcpy(out[k] + (long)r * CRET_POLICY_OUT, cret_policy_output(p, r), CRET_POLICY_OUT * sizeof(float));
            cret_policy_free(p);
        }
        omp_set_num_threads(4);
        CHECK(!memcmp(out[0], out[1], (size_t)B * CRET_POLICY_OUT * sizeof(float)), "cretaceous_net %d: 1 and 4 threads differ", net);
        free(out[0]); free(out[1]); free(w); free(obs);
    }
}

// A slot handed back to the policy after a call without it starts from a zero state: its output equals a fresh
// policy's on the same observation, while a slot driven throughout keeps its memory.
static void test_act_reset(void) {
    Fixture fx; const char* path = "ocean/cretaceous/tests/policy_fixture_net1.bin";
    if (!fx_read(path, &fx)) { CHECK(0, "%s missing", path); return; }
    long n; float* w = fx_weights(&fx, &n); char err[600];
    const int A = 4; CretSim* s = calloc(1, sizeof(CretSim));
    CHECK(cret_sim_init(s, t_config(A)) == 0, "sim init");
    CretPolicy* p = cret_policy_create(w, n, fx.net, fx.hidden, fx.layers, A, err, sizeof err);
    CretPolicy* fresh = cret_policy_create(w, n, fx.net, fx.hidden, fx.layers, A, err, sizeof err);
    bool all[CRET_MAX_AGENTS] = {1, 1, 1, 1}, but0[CRET_MAX_AGENTS] = {0, 1, 1, 1};
    CretInput in[CRET_MAX_AGENTS]; uint32_t rng = 9;
    cret_policy_act(p, s, all, true, &rng, in);
    cret_policy_act(p, s, but0, true, &rng, in);
    cret_policy_act(p, s, all, true, &rng, in);   // slot 0 is back: row 0
    float back[CRET_POLICY_OUT], kept[CRET_POLICY_OUT];
    memcpy(back, cret_policy_output(p, 0), sizeof back); memcpy(kept, cret_policy_output(p, 1), sizeof kept);
    cret_policy_act(fresh, s, all, true, &rng, in);
    CHECK(!memcmp(back, cret_policy_output(fresh, 0), sizeof back), "a slot handed back kept its old state");
    CHECK(memcmp(kept, cret_policy_output(fresh, 1), sizeof kept), "a slot driven throughout lost its state");
    cret_policy_free(p); cret_policy_free(fresh); cret_sim_close(s); free(s); free(w); fx_free(&fx);
}

// ms per step (all agents) and per agent-step, median and spread over runs.
static int cmp_d(const void* a, const void* b) { double x = *(const double*)a, y = *(const double*)b; return x < y ? -1 : x > y; }
static void bench(int runs) {
    const int agents[] = {1, 8, 64}, hidden[] = {256, 1024};
    printf("| encoder | hidden | agents | ms per step (median, min-max over %d runs) | ms per agent step |\n|---|---|---|---|---|\n", runs);
    for (int net = 0; net <= 1; net++) for (int hi = 0; hi < 2; hi++) {
        int H = hidden[hi]; long n = cret_policy_param_count(net, H, 2);
        float* w = malloc((size_t)n * sizeof(float)); uint32_t s = 1;
        for (long i = 0; i < n; i++) w[i] = (t_frand(&s) * 2 - 1) * 0.05f;
        for (int ai = 0; ai < 3; ai++) {
            int B = agents[ai]; char err[600];
            CretPolicy* p = cret_policy_create(w, n, net, H, 2, B, err, sizeof err);
            float* obs = malloc((size_t)B * CRET_OBS_SIZE * sizeof(float)); int slots[64];
            for (long i = 0; i < (long)B * CRET_OBS_SIZE; i++) obs[i] = t_frand(&s) * 2 - 1;
            for (int k = 0; k < B; k++) { slots[k] = k; for (int nb = 0; nb < CRET_NEIGHBORS; nb++) obs[k * CRET_OBS_SIZE + CRET_OBS_NEIGHBOR + nb * CRET_NEIGHBOR_OBS] = nb < 4; }
            int iters = (int)fmax(5, fmin(400, 2e8 / ((double)B * n)));
            double ms[64];
            for (int r = 0; r < runs; r++) {
                cret_policy_forward(p, obs, slots, B);   // warm
                double t0 = t_now();
                for (int it = 0; it < iters; it++) cret_policy_forward(p, obs, slots, B);
                ms[r] = (t_now() - t0) * 1e3 / iters;
            }
            qsort(ms, (size_t)runs, sizeof ms[0], cmp_d);
            printf("| %s | %d | %d | %.3f (%.3f-%.3f) | %.4f |\n", net ? "structured (1)" : "linear (0)", H, B, ms[runs / 2], ms[0], ms[runs - 1], ms[runs / 2] / B);
            fflush(stdout);
            cret_policy_free(p); free(obs);
        }
        free(w);
    }
}

int main(int argc, char** argv) {
    double t0 = t_now();
    int runs = 5, nfx = 0; bool do_bench = false; const char* fxs[16];
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bench")) do_bench = true;
        else if (!strcmp(argv[i], "--runs") && i + 1 < argc) runs = atoi(argv[++i]);
        else if (nfx < 16) fxs[nfx++] = argv[i];
    }
    if (runs < 1) runs = 1;
    if (runs > 64) runs = 64;
    if (do_bench) { bench(runs); return 0; }
    if (nfx) { for (int i = 0; i < nfx; i++) test_fixture(fxs[i]); return t_finish("test_policy", t0); }
    t_hold_world();
    test_fixture("ocean/cretaceous/tests/policy_fixture_net0.bin");
    test_fixture("ocean/cretaceous/tests/policy_fixture_net1.bin");
    test_puffernet();
    test_refusals();
    test_parse();
    test_threads();
    test_act_reset();
    test_sim_run();
    return t_finish("test_policy", t0);
}
