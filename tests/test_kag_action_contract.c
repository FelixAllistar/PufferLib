#include <assert.h>
#include <stdio.h>
#include "kag_action_contract.h"
#include "kag_observation_contract.h"

static void check(int mode, int frozen_mode, int executor, int frozen_executor,
        int expected, int frozen_expected) {
    kag_resolve_executor_versions(mode, frozen_mode, &executor, &frozen_executor);
    assert(executor == expected);
    assert(frozen_executor == frozen_expected);
}
int main(void) {
    check(3, -1, 1, 1, 0, 0);
    check(3, -1, 1, -1, 0, -1);
    check(3, 3, 1, 1, 0, 0);
    check(3, 2, 1, 1, 0, 1);
    check(2, 3, 1, 1, 1, 0);
    check(2, 3, 1, -1, 1, 0);
    check(2, -1, 1, 1, 1, 1);
    check(0, -1, 1, 1, 1, 1); /* Still rejected by adapter validation. */
    check(3, -1, 2, 2, 2, 2); /* Do not hide unsupported version values. */
    Ini ini = {0};
    dict_set_str(puf_ini_section(&ini, "base", 1), "env_name", "kaggriculture");
    Dict* env = puf_ini_section(&ini, "env", 1);
    dict_set(env, "macro_mode", 3);
    dict_set(env, "frozen_macro_mode", -1);
    dict_set(env, "macro_executor_version", 1);
    dict_set(env, "frozen_macro_executor_version", 1);
    KagObservationContract c = kag_observation_contract(&ini);
    assert(c.executor == 0 && c.frozen_executor == 0);
    assert(kag_observation_pool_compatible(c, 1, 0));
    puf_ini_put(&ini, "env.frozen_macro_mode", "2");
    c = kag_observation_contract(&ini);
    assert(c.executor == 0 && c.frozen_executor == 1);
    assert(!kag_observation_pool_compatible(c, 1, 0));
    assert(kag_observation_pool_compatible(c, 1, 1));
    puts("Kaggriculture action contract: PASS");
}
