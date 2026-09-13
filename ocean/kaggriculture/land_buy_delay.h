#pragma once

/* Policy constraint only: the simulator's purchase rules are unchanged.
 * A plot is full when every tile contains a live crop or a placed animal.
 * Remember the first observed full state of the newest owned plot; harvesting
 * or crop death afterwards does not restart the clock. */
KG_HD static inline void kag_reset_land_buy_delay(Env* env, int player_id) {
    env->land_fill_mask[player_id] = 0;
    env->land_fill_step[player_id] = -1;
}

KG_HD static inline void kag_update_land_buy_delay(Env* env, int player_id) {
    if (env->land_buy_min_days == 0) return;
    const KGState* game = &env->game_storage;
    const KGPlayer* player = &game->players[player_id];
    int owned = player->unlocked_mask;
    if (env->land_fill_mask[player_id] != owned) {
        env->land_fill_mask[player_id] = owned;
        env->land_fill_step[player_id] = -1;
    }
    if (owned == 15 || env->land_fill_step[player_id] >= 0) return;
    int quadrant = 1;
    for (int bit = 2; bit <= 8; bit <<= 1) {
        if (owned & bit) quadrant = bit;
    }
    int count = 0;
    for (int y = 0; y < game->config.board_size; y++) {
        for (int x = 0; x < game->config.board_size; x++) {
            if (kg_quadrant(x, y, game->config.board_size) != quadrant) continue;
            const KGTile* tile = &player->tiles[kg_tile_index(x, y)];
            if (!((tile->kind == KG_TILE_PLANT && (unsigned)tile->crop < KG_NUM_CROPS)
                    || kg_is_animal_tile(tile))) return;
            count++;
        }
    }
    if (count > 0) env->land_fill_step[player_id] = game->step;
}

KG_HD static inline int kag_land_buy_delay_ready(const Env* env, int player_id) {
    if (env->land_buy_min_days == 0) return 1;
    const KGState* game = &env->game_storage;
    if (env->land_fill_mask[player_id] != game->players[player_id].unlocked_mask
            || env->land_fill_step[player_id] < 0) return 0;
    return (int64_t)game->step - env->land_fill_step[player_id]
        >= (int64_t)env->land_buy_min_days * game->config.turns_per_day;
}
