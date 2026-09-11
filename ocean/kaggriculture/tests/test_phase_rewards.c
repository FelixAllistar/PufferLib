#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../phase_rewards.h"

static void phase_fixture(KGState* g) {
    memset(g,0,sizeof(*g));
    g->config.board_size=10; g->config.turns_per_day=24;
    g->players[0].unlocked_mask=15;
    for (int i=0;i<100;i++) {
        KGPlayer* p=&g->players[0]; KGTile* t=&p->tiles[i];
        if (i<10) { p->animal_bits[i/64]|=1ULL<<(i%64);t->animal=KG_COW;t->kind=KG_TILE_PASTURE; }
        else if (i<16) { p->animal_bits[i/64]|=1ULL<<(i%64);t->animal=KG_SHEEP;t->kind=KG_TILE_PASTURE; }
        else {p->plant_bits[i/64]|=1ULL<<(i%64);t->kind=KG_TILE_PLANT;t->crop=KG_WHEAT;}
    }
}

int main(void) {
    KGState* g=(KGState*)calloc(1,sizeof(*g)); assert(g); phase_fixture(g);
    double sum=0;
    for (int t=0;t<=720;t++) {
        g->step=t; float r=kag_phase_reward(g,0,2);
        assert(r>=0); assert(kag_phase_reward(g,0,0)==0);
        assert(kag_phase_reward(g,1,2)==0); sum+=r;
    }
    assert(fabs(sum-8)<1e-5);
    assert(kag_phase_window(48,0,24)==0 && kag_phase_window(144,0,24)==0);
    assert(kag_phase_window(96,0,24)>kag_phase_window(72,0,24));
    for (int i=0;i<4;i++) assert(kag_phase_readiness(kag_phase_state(g,0),i)==1);
    KagPhaseState partial={3,1,8,.8f};
    assert(kag_phase_readiness(partial,0)>0 && kag_phase_readiness(partial,0)<1);
    for (int i=0;i<100;i++) {g->players[0].tiles[i].consecutive_unwatered=1;g->players[0].tiles[i].consecutive_unfed=1;}
    g->step=384; assert(kag_phase_reward(g,0,2)==0);
    for (int i=0;i<100;i++) {g->players[0].tiles[i].watered_today=1;g->players[0].tiles[i].fed_today=1;}
    assert(kag_phase_reward(g,0,2)>0);
    memset(g->players[0].plant_bits,0,sizeof(g->players[0].plant_bits));
    memset(g->players[0].animal_bits,0,sizeof(g->players[0].animal_bits));
    assert(kag_phase_reward(g,0,2)==0); /* empty unlocked land / dead prior peak */
    phase_fixture(g); g->step=96;
    memset(g->players[0].plant_bits,0,sizeof(g->players[0].plant_bits));
    for (int i=0;i<16;i++) g->players[0].tiles[i].animal=KG_ANIMAL_INVALID;
    KagPhaseState empty=kag_phase_state(g,0);
    assert(empty.cows==0 && empty.other_animals==0 && empty.productive_plots==0);
    assert(kag_phase_reward(g,0,2)==0);
    g->players[0].tiles[0].animal=KG_COW;
    assert(kag_phase_state(g,0).cows==1 && kag_phase_reward(g,0,2)>0);
    g->players[0].tiles[0].animal=KG_NUM_ANIMALS;
    assert(kag_phase_reward(g,0,2)==0);
    free(g); puts("phase reward CPU tests passed"); return 0;
}
