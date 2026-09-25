// Opt-in real Raylib rendering. Hidden window; writes a screenshot for review.
#ifdef KAG_RENDER_GPU
#include "../kaggriculture.cu"
#else
#include "../kaggriculture.h"
#endif

int main(int argc, char** argv) {
    assert(argc == 2);
    Env* host = (Env*)calloc(1, sizeof(Env));
    KGConfig config;
    kg_config_default(&config);
    config.seed = 7;
    kg_init(&host->game, &config);
    KGAction pair[2];
    for (int step = 0; step < 240; step++) {
        for (int p = 0; p < 2; p++) {
            kg_rule_action(&host->game, p, pair + p);
        }
        kg_step(&host->game, pair);
    }
    // Populate a visual fixture for every crop and animal icon.
    KGPlayer* farm = &host->game.players[0];
    for (int crop = 0; crop < 5; crop++) {
        kg_set_player_tile(farm, crop, KG_TILE_PLANT);
        farm->tiles[crop].crop = crop;
        farm->tiles[crop].watered_today = 1;
        farm->tiles[crop].yield_units = crop + 1;
        farm->seeds[crop] = crop + 1;
    }
    for (int animal = 0; animal < 3; animal++) {
        int tile = 10 + animal;
        kg_set_player_tile(farm, tile, animal ? KG_TILE_PASTURE : KG_TILE_COOP);
        farm->tiles[tile].animal = animal;
        farm->tiles[tile].fed_today = 1;
    }
    KGState before = host->game;
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
#ifdef KAG_RENDER_GPU
    Env* env;
    assert(cudaMalloc(&env, sizeof(Env)) == cudaSuccess);
    assert(cudaMemcpy(env, host, sizeof(Env), cudaMemcpyHostToDevice) == cudaSuccess);
#else
    Env* env = host;
#endif
    puf_render(env);
    assert(IsWindowReady());
    puf_render(env);
    Image screenshot = LoadImageFromScreen();
    assert(screenshot.width == 1440 && screenshot.height == 900);
    assert(ExportImage(screenshot, argv[1]));
    UnloadImage(screenshot);
#ifdef KAG_RENDER_GPU
    assert(cudaMemcpy(host, env, sizeof(Env), cudaMemcpyDeviceToHost) == cudaSuccess);
#endif
    assert(memcmp(&before, &host->game, sizeof(KGState)) == 0);
#ifdef KAG_RENDER_GPU
    puf_close(env);
    free(host);
#else
    my_vec_close(env);
#endif
    assert(!IsWindowReady());
    puts("renderer PASS: game state unchanged, screenshot exported, window closed");
}
