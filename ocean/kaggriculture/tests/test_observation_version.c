#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../kaggriculture.h"
#include "kag_observation_contract.h"

static Env* fixture(obs_t out[2][OBS_SIZE]) {
    Env* e = calloc(1, sizeof(*e));
    KGConfig config; kg_config_default(&config); config.weed_spawn_chance = 0;
    kg_init(&e->game_storage, &config);
    e->macro_mode = KAG_MACRO_MODE_TASKS;
    e->observation_version = KAG_OBSERVATION_ENTITIES;
    e->frozen_macro_mode = e->frozen_observation_version = e->frozen_macro_executor_version = -1;
    e->macro_decision_interval = 1;
    e->policy_market_slots = 10; e->policy_max_hands = 16;
    e->reward.gamma = 1;
    e->game_storage.players[0].money = 245678;
    e->game_storage.players[1].money = 345678;
    kg_new_plant(&e->game_storage.players[0], 0, KG_WHEAT, 0, 24);
    kg_new_plant(&e->game_storage.players[1], 0, KG_STRAWBERRY, 0, 24);
    for (int p = 0; p < 2; p++) {
        e->agents[p].observations = out[p]; e->agents[p].policy = p;
        kag_reward_reset(e, p); kag_reset_land_buy_delay(e, p);
    }
    return e;
}
static void write_pair(Env* e) {
    for (int p = 0; p < 2; p++) kag_write_observation(e, p);
}
static void layout(void) {
    obs_t out[2][OBS_SIZE], prior[2][OBS_SIZE];
    Env* e = fixture(out); write_pair(e); memcpy(prior, out, sizeof(out));
    assert(sizeof(obs_t) == sizeof(float) && OBS_SIZE == 1424);
    assert(out[0][0] > 2 && out[1][0] > 3); /* No 100k saturation. */
    assert(fabsf(out[0][KAG_PRODUCT_OFFSET + 14] - 0.04f) < 1e-6f);
    assert(fabsf(out[1][KAG_PRODUCT_OFFSET + KG_STRAWBERRY * 56 + 14] - 0.04f) < 1e-6f);
    assert(out[0][KAG_PLOT_OFFSET] == 1 && out[0][KAG_PLOT_OFFSET + 4 * 24] == 0);
    assert(out[0][KAG_WORKER_OFFSET] == 1 && out[0][KAG_WORKER_OFFSET + 3] == 1);
    assert(out[0][KAG_WORKER_OFFSET + 32] == 0);
    assert(out[0][KAG_WORKER_OFFSET + 29] >= 0 && out[0][KAG_WORKER_OFFSET + 30] >= 0);
    for (int i = 0; i < OBS_SIZE; i++) assert(isfinite(out[0][i]) && isfinite(out[1][i]));
    KGPlayer player = e->game_storage.players[0];
    e->game_storage.players[0] = e->game_storage.players[1]; e->game_storage.players[1] = player;
    KagRewardState reward = e->reward_state[0]; e->reward_state[0] = e->reward_state[1]; e->reward_state[1] = reward;
    write_pair(e);
    assert(memcmp(out[0], prior[1], sizeof(out[0])) == 0);
    assert(memcmp(out[1], prior[0], sizeof(out[1])) == 0);
    for (int learner_seat = 0; learner_seat < 2; learner_seat++) {
        e->agents[learner_seat].policy = 0; e->agents[1 - learner_seat].policy = 7;
        write_pair(e); assert(memcmp(out[0], prior[1], sizeof(out[0])) == 0);
    }
    memcpy(prior, out, sizeof(out));
    e->game_storage.players[1].seeds[KG_MELON] = 987;
    e->game_storage.players[1].shed[KG_ITEM_COW] = 999;
    e->game_storage.players[1].units[0].inventory[KG_ITEM_MILK] = 345;
    write_pair(e); assert(memcmp(out[0], prior[0], sizeof(out[0])) == 0);
    e->game_storage.players[0].money += 17;
    write_pair(e); assert(out[0][0] != prior[0][0] && out[0][27] != prior[0][27]);
    e->game_storage.step = 360; e->game_storage.day = 15; e->reset_source = 1;
    write_pair(e); assert(out[0][3] == 0.5f && out[0][KAG_OBS_RESET_SOURCE_INDEX] == 1);
    free(e);
}
static void expect_rejected(const char* checkpoint, KagObservationContract c) {
    pid_t pid = fork(); assert(pid >= 0);
    if (!pid) { kag_executor_check_load(checkpoint, c, 0); _exit(0); }
    int status; waitpid(pid, &status, 0); assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
}
static void metadata(void) {
    Ini ini = {0};
    puf_ini_set(puf_ini_section(&ini, "base", 1), "env_name", "kaggriculture");
    puf_ini_set(puf_ini_section(&ini, "policy", 1), "hidden_size", "256");
    puf_ini_set(puf_ini_section(&ini, "policy", 1), "num_layers", "3");
    puf_ini_set(puf_ini_section(&ini, "env", 1), "observation_version", "3");
    puf_ini_set(puf_ini_section(&ini, "env", 1), "frozen_observation_version", "3");
    puf_ini_set(puf_ini_section(&ini, "env", 1), "macro_executor_version", "0");
    puf_ini_set(puf_ini_section(&ini, "env", 1), "frozen_macro_executor_version", "-1");
    KagObservationContract c = kag_observation_contract(&ini);
    assert(c.enabled && !kag_observation_mixed(c));
    for (int seat = 0; seat < 2; seat++) {
        kag_observation_pair(&ini, c, 1, seat);
        assert(puf_ini_get_int(&ini, "env", "observation_version") == KAG_OBSERVATION_ENTITIES);
        assert(puf_ini_get_int(&ini, "env", "frozen_observation_version") == KAG_OBSERVATION_ENTITIES);
    }
    kag_observation_restore(&ini, c);
    char directory[] = "/tmp/kag-entity-contract-XXXXXX"; assert(mkdtemp(directory));
    char checkpoint[1024]; snprintf(checkpoint, sizeof(checkpoint), "%s/new.bin", directory);
    expect_rejected(checkpoint, c);
    kag_observation_save(checkpoint, &ini);
    kag_executor_check_load(checkpoint, c, 0); kag_executor_check_load(checkpoint, c, 1);
    assert(kag_checkpoint_integer(checkpoint, ".hidden_size") == 256);
    c.hidden = 128; expect_rejected(checkpoint, c); c.hidden = 256;
    char path[1100]; snprintf(path, sizeof(path), "%s.policy_version", checkpoint);
    FILE* f = fopen(path, "w"); assert(f); fputs("1\n", f); fclose(f); expect_rejected(checkpoint, c);
    const char* suffixes[] = {".obs_version", ".executor_version", ".policy_version", ".hidden_size", ".num_layers", ".param_alignment",
        ".macro_mode", ".macro_decision_interval", ".macro_score_features"};
    for (int mode = 0; mode <= 3; mode++) for (int executor = 0; executor <= 1; executor++) {
        if (!kag_controller_valid(mode, executor)) continue;
        c.mode = mode; c.executor = executor; c.interval = mode == 1 ? 4 : 1;
        c.score_features = mode % 2;
        kag_observation_save_contract(checkpoint, c);
        KagObservationContract loaded = kag_checkpoint_contract(checkpoint);
        assert(loaded.mode == mode && loaded.executor == executor && loaded.interval == c.interval
            && loaded.score_features == c.score_features);
        kag_executor_check_load(checkpoint, c, 0);
        c.mode = (mode + 1) % 4; expect_rejected(checkpoint, c); c.mode = mode;
    }
    for (int i = 0; i < 9; i++) { snprintf(path, sizeof(path), "%s%s", checkpoint, suffixes[i]); assert(unlink(path) == 0); }
    assert(rmdir(directory) == 0); puf_ini_free(&ini);
}
int main(void) {
    layout(); metadata();
    puts("entity observations: float precision, layout, seat symmetry, privacy, bank semantics, strict checkpoints PASS");
}
