#define GS_EXPLOIT_NO_MAIN
#include "../goofspiel_exploit.cu"

void compare_pools(GSExactTable* a, GSExactTable* b, int count) {
    for (int i = 0; i < count; i++) {
        assert(a[i].decisions == b[i].decisions);
        assert(a[i].num_cards == b[i].num_cards);
        assert(a[i].prize_order == b[i].prize_order);
        assert(memcmp(a[i].counts, b[i].counts, sizeof(a[i].counts)) == 0);
        for (int d = 0; d < a[i].decisions; d++) {
            assert(memcmp(a[i].actions[d], b[i].actions[d], a[i].counts[d]) == 0);
        }
    }
}

int main(int argc, char** argv) {
    assert(argc == 3);
    char* options[] = {(char*)"--env.num_cards=3", (char*)"--env.num_turns=3",
        (char*)"--policy.hidden_size=32", (char*)"--policy.num_layers=2"};
    Ini ini = {0};
    puf_ini_load_env(&ini, "goofspiel", 4, options);
    GSConfig cfg = gs_solver_config(&ini);
    GSExactTable original[3] = {}, restored[3] = {};
    int count = 0, loaded_count = 0;
    uint64_t seen = 0, loaded_seen = 0;
    assert(!gs_exact_pool_load(argv[2], restored, 3, cfg.num_cards,
        cfg.prize_order, &loaded_count, &loaded_seen));
    for (int i = 0; i < 7; i++) {
        gs_cuda_pool_response(i % 2 ? argv[1] : "uniform", &ini,
            original, &count, 3, &seen, NULL, NULL);
    }
    assert(count == 3 && seen == 7);
    gs_exact_pool_save(argv[2], original, count, 3, seen);
    assert(gs_exact_pool_load(argv[2], restored, 3, cfg.num_cards,
        cfg.prize_order, &loaded_count, &loaded_seen));
    assert(loaded_count == count && loaded_seen == seen);
    compare_pools(original, restored, count);
    // Continuing a loaded reservoir must make the same replacement decisions.
    for (int i = 0; i < 8; i++) {
        const char* model = i % 2 ? "uniform" : argv[1];
        double a = gs_cuda_pool_response(model, &ini, original, &count,
            3, &seen, NULL, NULL);
        double b = gs_cuda_pool_response(model, &ini, restored, &loaded_count,
            3, &loaded_seen, NULL, NULL);
        assert(a == b && count == loaded_count && seen == loaded_seen);
        compare_pools(original, restored, count);
    }
    // Reload over populated tables as the checkpoint-resume path does.
    gs_exact_pool_save(argv[2], original, count, 3, seen);
    assert(gs_exact_pool_load(argv[2], restored, 3, cfg.num_cards,
        cfg.prize_order, &loaded_count, &loaded_seen));
    compare_pools(original, restored, count);
    assert(loaded_count == count && loaded_seen == seen);
    for (int i = 0; i < count; i++) {
        gs_exact_table_clear(original + i);
        gs_exact_table_clear(restored + i);
    }
    puf_ini_free(&ini);
    puts("exact pool save/load and deterministic continuation passed");
}
