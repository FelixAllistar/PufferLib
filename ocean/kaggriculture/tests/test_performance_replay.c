/* Compile this same driver against baseline and candidate source roots.
 * Byte-compare outputs; covers mature replay states as well as fresh rollouts.
 * No renderer, GPU, network, training or source mutation is involved. */
#include <assert.h>
#include "ocean/kaggriculture/kaggriculture.h"

typedef struct {
    char magic[8];
    uint32_t version, state_version, state_size, count;
    uint64_t reserved;
} BankHeader;
static void emit(FILE* out, const void* data, size_t size) {
    assert(fwrite(data, 1, size, out) == size);
}
int main(int argc, char** argv) {
    assert(argc == 3);
    FILE* bank = fopen(argv[1], "rb"), *out = fopen(argv[2], "wx");
    assert(bank && out);
    BankHeader h;
    assert(sizeof(h) == 32 && fread(&h, sizeof(h), 1, bank) == 1);
    assert(!memcmp(h.magic, "KGRSTB1\0", 8) && h.version == 1 && h.reserved == 0
        && h.state_version == kg_state_serialization_version() && h.state_size == sizeof(KGState) && h.count);
    Env* env = calloc(1, sizeof(Env));
    float actions[2][NUM_ATNS] = {{0}}, logits[KG_POLICY_ACTION_MASK_SIZE];
    unsigned char masks[2][KG_POLICY_ACTION_MASK_SIZE];
    env->num_agents = 2; env->macro_mode = 2; env->macro_executor_version = 2;
    env->macro_decision_interval = 1; env->observation_version = 3;
    env->frozen_macro_mode = env->frozen_macro_executor_version = -1;
    env->frozen_macro_decision_interval = env->frozen_macro_score_features = -1;
    env->frozen_observation_version = -1;
    env->policy_market_slots = 10; env->policy_max_hands = 16;
    for (int p = 0; p < 2; p++) {
        env->agents[p].actions = actions[p]; env->agents[p].action_mask = masks[p];
    }
    for (int fixture = 0; fixture < 1025; fixture++) {
        if (fixture == 1024) {
            KGConfig cfg; kg_config_default(&cfg); kg_init(&env->game_storage, &cfg);
        } else {
            uint64_t index = (uint64_t)fixture * (h.count - 1) / 1023;
            assert(!fseek(bank, sizeof(h) + index * sizeof(KGState), SEEK_SET));
            KGState state;
            assert(fread(&state, sizeof(state), 1, bank) == 1);
            assert(kg_state_deserialize(&env->game_storage, &state, sizeof(state)));
        }
        env->rng = 17 + fixture;
        for (int turn = 0; turn < (fixture == 1024 ? 720 : 4); turn++) {
            KGAction decoded[2];
            for (int p = 0; p < 2; p++) {
                for (int a = 0; a < KG_POLICY_ACTION_MASK_SIZE; a++)
                    logits[a] = sinf((a * 17 + fixture * 7 + turn * 13 + p) * 0.73f);
                kag_write_mask(env, p); emit(out, masks[p], sizeof(masks[p]));
                kag_sample_cpu_logits(env, p, logits, turn % 2);
                emit(out, actions[p], sizeof(actions[p])); emit(out, masks[p], sizeof(masks[p]));
                kag_decode_policy_action(&decoded[p], &env->agents[p], &env->game_storage, p, env);
                emit(out, &decoded[p], sizeof(decoded[p]));
            }
            kg_step(&env->game_storage, decoded);
            emit(out, &env->rng, sizeof(env->rng));
            emit(out, &env->game_storage, sizeof(KGState));
        }
    }
    fclose(bank); assert(!fclose(out)); free(env);
    puts("performance replay dump: 1024 reset states x 4 turns + 720-turn fresh episode complete");
    return 0;
}
