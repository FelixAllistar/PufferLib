/* Separate diagnostic library: does not replace the trainer or old probe. */
#include "kag_experiment_view.c"

void kg_spending_metrics(const KGState* state, int player, double* out) {
    const KGPlayer* farm = &state->players[player];
    for (int k = 0; k < 17; k++) out[k] = 0;
    for (int i = 0; i < KG_MAX_TILES; i++) {
        const KGTile* t = &farm->tiles[i];
        if ((t->kind==KG_TILE_COOP || t->kind==KG_TILE_PASTURE)
                && !kg_is_animal_tile(t)) out[16]++;
        if (((farm->animal_bits[i/64] >> (i%64)) & 1)
                && t->animal >= 0 && t->animal < KG_NUM_ANIMALS) out[t->animal]++;
        if (((farm->plant_bits[i/64] >> (i%64)) & 1)
                && t->crop >= 0 && t->crop < KG_NUM_CROPS) out[3+t->crop]++;
    }
    for (int a=0; a<KG_NUM_ANIMALS; a++) {
        int item=KG_ITEM_GOOSE+a;
        out[8+a]=farm->shed[item];
        for (int u=0; u<farm->unit_count; u++) out[8+a]+=farm->units[u].inventory[item];
    }
    for (int c=0; c<KG_NUM_CROPS; c++) out[11+c]=farm->seeds[c];
}
