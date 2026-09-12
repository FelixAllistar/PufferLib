#pragma once
#include <math.h>
#include <stdint.h>

/* Species-blind state potentials, not bonuses for actions or winning teams.
 * Layout follows bridge.zig (ABI 2). Sleep includes Rest: the observation
 * intentionally does not expose its cause. These are occupancy measures,
 * NEVER counts of successfully inflicted statuses/heals/attacks.
 */
#define PK_PERSONALITY_DIM 5
static const char* const pk_personality_names[PK_PERSONALITY_DIM] = {
    "paralysis", "sleep", "offense", "defense", "reserve"
};
static inline double pk_personality_clip(double x, double lo, double hi) {
    return x < lo ? lo : x > hi ? hi : x;
}
/* One side's own information only. All components are bounded in [-1,1].
 * No IDs, types, moveset values, usage statistics, or opponent private data.
 */
static inline void pk_personality_features(const uint8_t* obs, double out[5]) {
    for (int k=0;k<5;k++) out[k]=0;
    if (!obs[0]) return; /* Nothing is rewarded during species/set selection. */
    for (int i=0;i<6;i++) {
        const uint8_t* mon=obs+16+32*i;
        if (mon[3]) continue;
        out[0] -= (mon[2]==5)/6.0;
        out[1] -= (mon[2]==1)/6.0;
        out[4] += mon[1]/(6.0*255.0);
    }
    /* Positive active boosts; neutral stages are encoded as 6. No benefit
     * from repeatedly using a capped setup move. Fainted actives contribute 0.
     */
    int active=obs[4]-1;
    if (active<0 || active>=6 || obs[16+32*active+3]) return;
    out[2]=(pk_personality_clip(obs[403]-6,0,6)
           +pk_personality_clip(obs[405]-6,0,6)
           +pk_personality_clip(obs[406]-6,0,6))/18.0;
    out[3]=(pk_personality_clip(obs[404]-6,0,6)/6.0
           +(obs[422]!=0)+(obs[423]!=0))/3.0; /* LightScreen, Reflect */
}
static inline double pk_personality_potential(const uint8_t* a,
        const uint8_t* b, const double weights[5]) {
    double fa[5],fb[5],phi=0;
    pk_personality_features(a,fa); pk_personality_features(b,fb);
    for (int k=0;k<5;k++) phi+=weights[k]*(fa[k]-fb[k]);
    return phi;
}
static inline double pk_personality_shaping(double before, double after,
        double gamma, int terminal) {
    return gamma*(terminal?0.0:after)-before;
}
