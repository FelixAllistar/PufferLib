#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "../kag_spending_view.c"
int main(void) {
    KGConfig cfg;kg_config_default(&cfg);
    KGState* g=kg_create(&cfg);assert(g);
    KGPlayer* p=&g->players[0];
    p->unlocked_mask=3;
    kg_set_player_tile(p,0,KG_TILE_PASTURE);
    kg_set_player_tile(p,5,KG_TILE_COOP);
    kg_new_animal(p,1,KG_COW,0);
    double metrics[12]={0};
    struct {double before; double values[17]; double after;} out={123,{0},456};
    kg_experiment_metrics(g,0,metrics);
    kg_spending_metrics(g,0,out.values);
    assert(metrics[3]==1 && metrics[4]==1 && metrics[7]==0);
    assert(out.values[0]==0 && out.values[1]==1 && out.values[2]==0);
    assert(out.values[16]==2);
    assert(out.before==123 && out.after==456);
    kg_destroy(g);puts("spending occupancy/buffer tests passed");
}
