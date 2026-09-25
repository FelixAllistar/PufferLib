/* Full native state is a test oracle, never part of a submission. */
#include "entity_bridge.c"

Export* oracle_create(int seed) {
    Export* x = export_create();
    KGConfig config;
    kg_config_default(&config);
    config.seed = seed;
    kg_init(&x->game, &config);
    kag_policy_reset(&x->policy, &x->game, 0);
    return x;
}

const char* oracle_snapshot(Export* x) { return kg_snapshot_json(&x->game); }
void oracle_string_free(const char* x) { kg_free_string(x); }
void oracle_rng(Export* x, uint32_t rng) { x->rng = rng; }

void oracle_view(Export* x, int p, float* out) {
    kag_write_observation(&x->policy, &x->game, p, out);
}

void oracle_step(Export* x, KGAction* actions) {
    kg_step(&x->game, actions);
    kag_policy_step(&x->policy, &x->game);
}
