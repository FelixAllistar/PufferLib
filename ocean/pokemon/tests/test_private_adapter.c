#include "../benchmarks/private_adapter.c"

int main(void) {
    Benchmark a = {0}, b = {0};
    a.game.phase = b.game.phase = PK_PHASE_BATTLE;
    PKMon teams[12];
    int species[6] = {121,103,112,113,143,128};
    for (int p=0;p<2;p++) for (int i=0;i<6;i++) {
        int set = pk_species_set(species[i],0);
        teams[p*6+i].species = species[i];
        for (int m=0;m<4;m++) teams[p*6+i].moves[m]=pk_set_move(set,m);
    }
    assert(pk_start_free(&a.game.battle,42,teams)==0);
    // Change an unrevealed enemy reserve's moves, leaving both visible leads
    // and the observer's own team untouched. Actual engine bytes must differ.
    assert(pk_species_count(113)>1);
    int variant=pk_species_set(113,1);
    for(int m=0;m<4;m++)teams[3].moves[m]=pk_set_move(variant,m);
    assert(pk_start_free(&b.game.battle,42,teams)==0);
    assert(memcmp(&a.game.battle,&b.game.battle,sizeof(PKBattle)));
    pk_game_observe(&a.game);pk_game_observe(&b.game);
    uint8_t x[PK_OBS],y[PK_OBS];
    assert(private_observation(&a,x)==0 && private_observation(&b,y)==0);
    assert(!memcmp(x,y,PK_OBS));
    assert(!memcmp(x,a.game.obs[1],PK_OBS));
    assert(!memcmp(x+480,a.game.masks[1],PK_ACTIONS));
    // The hidden reserve is not assigned an observable identity.
    for(int i=1;i<6;i++)assert(x[208+32*i]==0);
    a.game.phase=PK_PHASE_MOVES;
    assert(private_observation(&a,x)==-1);
    puts("Private benchmark boundary: hidden-state mutation, mask and phase checks passed");
}
