#include "../kaggriculture.h"

void kg_top_bot_action(const KGState* game, int player, KGAction* action) {
    if (game->step < 26) {
        kag_script_action(game, player, KG_SCRIPT_TOP, action);
        kag_script_repair(game, player, KG_SCRIPT_TOP, action);
    } else {
        kag_bot_action(game, player, -1, action);
    }
}
