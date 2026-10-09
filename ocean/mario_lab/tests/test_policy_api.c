#include "../ml_policy_api.c"

int main(int argc, char** argv) {
    assert(argc == 2);
    void* cpu = ml_cpu_load(argv[1], 64, 2); assert(cpu);
    Weights* weights = load_weights(argv[1]); int sizes[] = {64};
    PufferNet* reference = make_puffernet(weights, 1, ML_OBS_SIZE, 64, 2, sizes, 1);
    MLConfig c = ml_default_config(); MLState s; ml_reset(&s, &c, 927);
    for (int t = 0; t < 256; t++) {
        float obs[ML_OBS_SIZE], reference_action, probabilities[6], entropy, terminal = t == 128;
        if (terminal) ml_cpu_reset(cpu);
        ml_observe(&s, &c, obs);
        srand(771 + t); forward_puffernet(reference, obs, &reference_action, NULL, &terminal);
        srand(771 + t); int action = ml_cpu_action(cpu, obs, 0, probabilities, &entropy);
        assert(action == (int)reference_action);
        MLCpu* wrapped = cpu;
        assert(!memcmp(reference->decoder->output, wrapped->net->decoder->output, 65 * sizeof(float)));
        assert(isfinite(entropy) && entropy >= 0 && entropy <= logf(64) + 1e-5f);
        for (int b = 0; b < 6; b++) assert(isfinite(probabilities[b]) && probabilities[b] >= 0 && probabilities[b] <= 1.00001f);
        if (s.status) ml_reset(&s, &c, 927 + t); else ml_step(&s, &c, action);
    }
    ml_cpu_free(cpu); free_puffernet(reference); free(weights);
    puts("ROM policy wrapper: recurrent logits/actions and episode reset match Puffer inference over 256 decisions PASS");
}
