/* Regression for Kaggle 1.32.7 PLACE-to-shed on locked access tiles. */
#include <assert.h>
#include <stdio.h>
#include "../kaggriculture_core.c"

static void place(KGState* game, int x, int y, int n) {
    game->players[0].units[0].x = x;
    game->players[0].units[0].y = y;
    KGUnitAction action = {KG_OP_PLACE, KG_ITEM_COW, n};
    kg_apply_unit_action(game, &game->players[0], 0, &action);
}

int main(void) {
    KGConfig cfg;
    kg_config_default(&cfg);
    cfg.weed_spawn_chance = 0;
    KGState game;
    for (int x = 4; x <= 5; x++) for (int y = 4; y <= 5; y++) {
        kg_init(&game, &cfg);
        KGPlayer* player = &game.players[0];
        kg_inventory_add(&player->units[0], KG_ITEM_COW, 2);
        int kind = player->tiles[kg_tile_index(x, y)].kind;
        place(&game, x, y, 1);
        assert(player->shed[KG_ITEM_COW] == 1);
        assert(player->units[0].inventory[KG_ITEM_COW] == 1);
        assert(player->tiles[kg_tile_index(x, y)].kind == kind);
    }
    kg_init(&game, &cfg);
    KGPlayer* player = &game.players[0];
    kg_inventory_add(&player->units[0], KG_ITEM_COW, 3);
    player->shed[KG_ITEM_WHEAT] = cfg.shed_capacity - 1;
    place(&game, 5, 5, 3);
    assert(player->shed[KG_ITEM_COW] == 1);
    assert(player->units[0].inventory[KG_ITEM_COW] == 2);
    place(&game, 5, 5, 1);
    assert(player->units[0].inventory[KG_ITEM_COW] == 2);

    kg_init(&game, &cfg);
    player = &game.players[0];
    kg_inventory_add(&player->units[0], KG_ITEM_COW, 1);
    place(&game, 6, 6, 1);
    assert(player->shed[KG_ITEM_COW] == 0);
    assert(player->units[0].inventory[KG_ITEM_COW] == 1);
    kg_set_player_tile(player, kg_tile_index(4, 4), KG_TILE_PASTURE);
    place(&game, 4, 4, 1);
    assert(player->shed[KG_ITEM_COW] == 0);
    assert(player->tiles[kg_tile_index(4, 4)].animal == KG_COW);
    assert(game.placed_animals[0] == 1);

    kg_init(&game, &cfg);
    player = &game.players[0];
    player->units[0].x = 5;
    player->units[0].y = 5;
    player->seeds[KG_WHEAT] = 1;
    KGUnitAction plant = {KG_OP_PLANT, KG_WHEAT, 0};
    kg_apply_unit_action(&game, player, 0, &plant);
    assert(player->seeds[KG_WHEAT] == 1);
    assert(player->tiles[kg_tile_index(5, 5)].kind == KG_TILE_LOCKED);
    puts("PLACE/shed regression passed (all corners, capacity, placement priority, locked tile guard)");
    return 0;
}
