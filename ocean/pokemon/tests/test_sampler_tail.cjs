// Compile the actual production selection block with host float/mask helpers.
// Force rare RNG endpoints without depending on a lucky GPU draw.
const fs = require('node:fs');
const cp = require('node:child_process');
const source = fs.readFileSync('src/pufferl.cu', 'utf8');
const start = source.indexOf('            int sampled = 0;');
const end = source.indexOf('            float sampled_logit =', start);
if (start < 0 || end < 0) throw Error('Sampler changed; review test extraction');
const selection = source.slice(start, end);
const dir = fs.mkdtempSync('/tmp/pokemon-sampler-test-');
try {
    fs.writeFileSync(dir + '/test.cpp', `
#include <cmath>
#include <cassert>
#include <cstdio>
#include <initializer_list>
float curand_uniform(float* state) { return *state; }
float load_logit_masked_byte(const float* logits, int base, int offset,
        int a, const unsigned char* mask, int mask_base) {
    return mask[mask_base + offset + a] ? logits[base + offset + a] : -1e4f;
}
int select(const float* logits, const unsigned char* action_mask, int A,
        float state, bool deterministic, bool use_cache) {
    // Nonzero offsets catch accidental indexing into another row/head.
    int logits_base = 3, logits_offset = 2, mask_base = 7;
    float cache[160], max_val = -INFINITY, sum_exp = 0;
    for (int a = 0; a < A; a++) {
        cache[a] = load_logit_masked_byte(logits, logits_base, logits_offset,
            a, action_mask, mask_base);
        max_val = fmaxf(max_val, cache[a]);
    }
    for (int a = 0; a < A; a++) sum_exp += expf(cache[a] - max_val);
    float logsumexp = max_val + logf(sum_exp);
${selection}
    assert(action_mask[mask_base + logits_offset + sampled]);
    assert(std::isfinite(cache[sampled] - logsumexp));
    return sampled;
}
int main() {
    float logits[165] = {};
    unsigned char mask[169] = {};
    for (int A : {1, 12, 160}) {
        for (bool cached : {false, true}) {
            for (int a = 0; a < A; a++) mask[9+a] = 1;
            assert(select(logits, mask, A, 1.0f, false, cached) == A-1);
            for (int a = 0; a < A; a++) mask[9+a] = 0;
            mask[9] = 1;
            // Exact CDF equality: the old fallback returned illegal A-1.
            assert(select(logits, mask, A, 1.0f, false, cached) == 0);
            assert(select(logits, mask, A, 0.5f, true, cached) == 0);
            if (A > 2) {
                mask[11] = 1;
                assert(select(logits, mask, A, 1.0f, false, cached) == 2);
                assert(select(logits, mask, A, 0.25f, false, cached) == 0);
                assert(select(logits, mask, A, 0.75f, false, cached) == 2);
            }
        }
    }
    // A finite CDF slightly below 1, equal to the draw (observed failure).
    bool exercised = false;
    for (int a = 0; a < 160; a++) mask[9+a] = 0;
    mask[9] = mask[11] = 1;
    logits[5] = 10.625f;
    for (int i = 1; i < 1000 && !exercised; i++) {
        logits[7] = 10.625f - i / 128.0f;
        float lse = logits[5] + logf(1.0f + expf(logits[7] - logits[5]));
        float cdf = expf(logits[5] - lse) + expf(logits[7] - lse);
        if (cdf < 1.0f && cdf > 0.99f) {
            assert(select(logits, mask, 160, cdf, false, false) == 2);
            assert(select(logits, mask, 160, cdf, false, true) == 2);
            exercised = true;
        }
    }
    assert(exercised);
    puts("Production sampler tail regression passed");
}
`);
    cp.execFileSync('g++', ['-std=c++17', '-O2', dir+'/test.cpp', '-o', dir+'/test']);
    process.stdout.write(cp.execFileSync(dir+'/test'));
} finally {
    fs.rmSync(dir, {recursive: true, force: true});
}
