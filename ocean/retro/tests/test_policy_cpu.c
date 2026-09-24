#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <unistd.h>
#ifdef RETRO_LEGACY_POLICY
#include RETRO_LEGACY_POLICY
#else
#include "../retro_policy_cpu.h"
#endif

#ifdef RETRO_CUDA_REFERENCE
void* retro_reference_create(float* data, int hidden, int layers) {
    Weights cursor = {data, (int)retro_policy_weights(hidden, layers), 0};
    RetroPolicy* p = make_retro_policy(&cursor, hidden, layers);
    assert(cursor.idx == cursor.size);
    return p;
}

const float* retro_reference_encode(void* policy, const float* obs) {
    return retro_policy_encode(policy, obs);
}

const float* retro_reference_logits(void* policy, const float* obs) {
    return retro_policy_logits(policy, obs);
}

const float* retro_reference_image(void* policy, int layer) {
    return ((RetroPolicy*)policy)->image[layer];
}

void retro_reference_reset(void* policy) {
    RetroPolicy* p = policy;
    memset(p->mingru->state, 0,
        p->mingru->hidden_size*p->mingru->num_layers*sizeof(float));
}

void retro_reference_free(void* policy) {
    free_retro_policy(policy);
}
#else
int main(void) {
    uint64_t hash = UINT64_C(14695981039346656037);
    const int hidden[] = {16, 128}, layers[] = {1, 2};
    for (int kind = 0; kind < 2; kind++) {
        int H = hidden[kind], L = layers[kind];
        size_t count = retro_policy_weights(H, L);
        float* data = (float*)calloc(count, sizeof(float));
        float* restored = (float*)calloc(count, sizeof(float));
        float* obs = (float*)calloc(RETRO_POLICY_INPUTS, sizeof(float));
        for (size_t i = 0; i < count; i++) {
            data[i] = 0.03f*sinf((float)i*0.731f + 0.2f);
        }
        char path[] = "/tmp/retro-policy-test-XXXXXX";
        int fd = mkstemp(path);
        assert(fd >= 0);
        FILE* file = fdopen(fd, "w+b");
        assert(file && fwrite(data, sizeof(float), count, file) == count);
        rewind(file);
        assert(fread(restored, sizeof(float), count, file) == count);
        assert(!memcmp(data, restored, count*sizeof(float)));
        assert(fclose(file) == 0 && unlink(path) == 0);
        Weights cursor = {restored, (int)count, 0};
        RetroPolicy* p = make_retro_policy(&cursor, H, L);
        assert(cursor.idx == (int)count);
        float previous[65] = {0};
        int varied = 0;
        for (int step = 0; step < 64; step++) {
            if (step % 13 == 0) {
                memset(p->mingru->state, 0, H*L*sizeof(float));
            }
            for (int i = 0; i < RETRO_POLICY_INPUTS; i++) {
                obs[i] = 0.5f + 0.49f*sinf(i*0.073f + step*0.2f);
            }
            const float* out = retro_policy_logits(p, obs);
            varied |= memcmp(previous, out, sizeof(previous)) != 0;
            memcpy(previous, out, sizeof(previous));
            for (int i = 0; i < 65; i++) {
                assert(isfinite(out[i]));
                uint32_t bits;
                memcpy(&bits, out + i, sizeof(bits));
                hash = (hash ^ bits)*UINT64_C(1099511628211);
            }
            int expected = 0;
            for (int i = 1; i < 64; i++) {
                if (out[i] > out[expected]) expected = i;
            }
            float action = -1;
#ifdef RETRO_LEGACY_POLICY
            argmax_multidiscrete(p->multidiscrete, p->decoder->output, &action);
#else
            multidiscrete(p->multidiscrete, p->decoder->output, &action, 1, NULL);
#endif
            assert(action == expected);
        }
        assert(varied);
        // A reset must reproduce the output of a fresh instance on the same input.
        memset(p->mingru->state, 0, H*L*sizeof(float));
        memcpy(previous, retro_policy_logits(p, obs), sizeof(previous));
        cursor.idx = 0;
        RetroPolicy* fresh = make_retro_policy(&cursor, H, L);
        assert(!memcmp(previous, retro_policy_logits(fresh, obs), sizeof(previous)));
        free_retro_policy(fresh);
        free_retro_policy(p);
        free(data);
        free(restored);
        free(obs);
    }
    printf("Retro CPU scale=%d trace=%016llx\n", RETRO_OBS_SCALE, (unsigned long long)hash);
}
#endif
