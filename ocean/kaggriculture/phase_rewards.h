#pragma once
#include "kaggriculture_core.h"

/* Training-only state curriculum, NOT potential-based/policy-invariant shaping.
 * Four triangular windows, each normalized to one unit of reward at perfect
 * readiness. Scale s therefore pays at most 4*s per root episode. No action
 * bonuses, purchase peaks, or permanent achievement latches. */
typedef struct {
    float cows, other_animals, plants, productive_plots;
} KagPhaseState;

KG_HD static inline float kag_phase_fraction(float value, float target) {
    float x = value / target;
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return x*x*(3.0f-2.0f*x);
}

KG_HD static inline KagPhaseState kag_phase_state(const KGState* game, int player_id) {
    KagPhaseState s = {0};
    float occupied[4] = {0};
    const KGPlayer* p = &game->players[player_id];
    for (int i=0; i<KG_MAX_TILES; i++) {
        int x=i%KG_MAX_BOARD_SIZE, y=i/KG_MAX_BOARD_SIZE;
        if (x>=game->config.board_size || y>=game->config.board_size) continue;
        const KGTile* t=&p->tiles[i];
        int plant=(p->plant_bits[i/64]>>(i%64))&1;
        int animal=(p->animal_bits[i/64]>>(i%64))&1;
        /* Healthy now or repaired today. Empty buildings/weeds never count. */
        plant = plant && t->kind==KG_TILE_PLANT
            && (unsigned)t->crop<KG_NUM_CROPS
            && (t->watered_today || t->consecutive_unwatered==0);
        /* animal_bits indexes housing, including empty buildings. */
        animal = animal && (t->kind==KG_TILE_COOP || t->kind==KG_TILE_PASTURE)
            && (unsigned)t->animal<KG_NUM_ANIMALS
            && (t->fed_today || t->consecutive_unfed==0);
        if (plant) s.plants += 1;
        if (animal) {
            if (t->animal==KG_COW) s.cows += 1;
            else s.other_animals += 1;
        }
        if (plant || animal) occupied[(y>=5)*2+(x>=5)] += 1;
    }
    for (int q=0;q<4;q++) {
        if ((p->unlocked_mask>>q)&1) s.productive_plots += kag_phase_fraction(occupied[q],12.0f);
    }
    return s;
}

KG_HD static inline float kag_phase_readiness(KagPhaseState s, int phase) {
    /* Establish; expand productively; build herd; sustain the mature farm.
     * Other animals can be sheep OR geese; no forced product/action sequence. */
    float cows=phase==0?4.0f:phase==1?7.0f:10.0f;
    float others=phase==0?2.0f:phase==1?3.0f:6.0f;
    float plants=phase==0?12.0f:phase==1?24.0f:40.0f;
    float plots=phase==0?1.0f:phase==1?2.0f:3.0f;
    float f[4]={kag_phase_fraction(s.cows,cows),kag_phase_fraction(s.other_animals,others),
                kag_phase_fraction(s.plants,plants),kag_phase_fraction(s.productive_plots,plots)};
    float sum=0, least=1;
    for (int i=0;i<4;i++) { sum+=f[i]; if (f[i]<least) least=f[i]; }
    return .5f*(sum*.25f+least);
}

KG_HD static inline float kag_phase_window(int step, int phase, int turns_per_day) {
    int width=2*turns_per_day, center=4*(phase+1)*turns_per_day;
    int distance=step-center; if (distance<0) distance=-distance;
    if (width<=0 || distance>=width) return 0;
    return (float)(width-distance)/((float)width*(float)width);
}

KG_HD static inline float kag_phase_reward(const KGState* game, int player_id, float scale) {
    if (scale==0.0f) return 0;
    float windows[4], total=0;
    for (int i=0;i<4;i++) { windows[i]=kag_phase_window(game->step,i,game->config.turns_per_day); total+=windows[i]; }
    if (total==0) return 0;
    KagPhaseState state=kag_phase_state(game,player_id);
    float reward=0;
    for (int i=0;i<4;i++) if (windows[i]>0) reward+=windows[i]*kag_phase_readiness(state,i);
    return scale*reward;
}
