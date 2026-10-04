#ifndef SWAT_ENVIRONMENT_PROPS_H
#define SWAT_ENVIRONMENT_PROPS_H
#include "generation.h"

// Presentation recipe v1. These IDs never enter layout tokens or wire packets.
// New solid furniture/cover requires a separate authoritative gameplay design.
#define SWAT_ENV_PROP_RECIPE_VERSION 1
#define SWAT_ENV_PROP_MAX (2*SWAT_LAYOUT_FURNITURE)
typedef enum SwatEnvironmentPropKind {
    SWAT_ENV_PROP_MUG, SWAT_ENV_PROP_PLATES, SWAT_ENV_PROP_TOWEL,
    SWAT_ENV_PROP_WALLET, SWAT_ENV_PROP_REMOTE, SWAT_ENV_PROP_GLASSES,
    SWAT_ENV_PROP_KINDS
} SwatEnvironmentPropKind;
typedef struct SwatEnvironmentPropSpec {
    const char* file;
    // Conservative metre bounds, centred in X/Z, base at Y=0.
    b3Vec3 size;
} SwatEnvironmentPropSpec;
static const SwatEnvironmentPropSpec swat_environment_prop_specs[SWAT_ENV_PROP_KINDS]={
    {"prop_chipped_coffee_mug.glb", {.1110f,.0926f,.0769f}},
    {"prop_stacked_dinner_plates.glb", {.2601f,.0585f,.2601f}},
    {"prop_folded_hand_towel.glb", {.3000f,.0489f,.1930f}},
    {"prop_creased_leather_wallet.glb", {.1141f,.0364f,.0981f}},
    {"prop_television_remote.glb", {.0471f,.0236f,.1781f}},
    {"prop_brass_eyeglasses.glb", {.1208f,.0128f,.1242f}}
};
typedef struct SwatEnvironmentProp {
    int support, furniture, room;
    SwatEnvironmentPropKind kind;
    b3Vec3 local; // Support-local, so rendering inherits that object's transform.
    float yaw;
} SwatEnvironmentProp;

static inline uint32_t swat_environment_prop_mix(uint32_t x) {
    x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16);
}

static inline bool swat_environment_prop_support(const SwatObject* o,const SwatPlanFurniture* f) {
    // Bind only an exact accepted furnishing, never guess from a material alone.
    return o->active && !o->door && o->part==SWAT_PART_SOLID && o->material==f->material &&
        o->max_health>0 && fabsf(o->yaw)<1e-5f && fabsf(o->pitch)<1e-5f &&
        b3Distance(o->center,f->center)<1e-4f &&
        fabsf(o->half.x-f->half.x)<1e-5f && fabsf(o->half.y-f->half.y)<1e-5f &&
        fabsf(o->half.z-f->half.z)<1e-5f;
}

// Pure, bounded, allocation-free. layout must be the current accepted generated
// plan (NULL for hand-authored missions). Rebind each draw: no stale epoch IDs.
// Replica layout is reconstructed from accepted tokens, not the installed NN.
static inline int swat_environment_props(const SwatWorld* world,const SwatLayout* layout,
                                        SwatEnvironmentProp out[SWAT_ENV_PROP_MAX]) {
    if(!layout || layout->furniture_count<0 || layout->furniture_count>SWAT_LAYOUT_FURNITURE ||
       layout->room_count<0 || layout->room_count>SWAT_LAYOUT_ROOMS) return 0;
    int count=0;
    for(int f=0;f<layout->furniture_count;f++) {
        const SwatPlanFurniture* furniture=&layout->furniture[f];
        int room=-1,support=-1;
        for(int r=0;r<layout->room_count;r++) {
            const SwatPlanRoom* candidate=&layout->rooms[r];
            if(!candidate->hall && furniture->center.x-furniture->half.x>=candidate->x0 &&
               furniture->center.x+furniture->half.x<=candidate->x1 &&
               furniture->center.z-furniture->half.z>=candidate->z0 &&
               furniture->center.z+furniture->half.z<=candidate->z1) { room=r; break; }
        }
        if(room<0) continue;
        // An ambiguous or missing support fails closed. No art on floors/doors.
        bool duplicate=false;
        for(int i=0;i<world->count;i++) if(swat_environment_prop_support(&world->objects[i],furniture)) {
            if(support>=0) { duplicate=true; break; }
            support=i;
        }
        if(support<0 || duplicate) continue;
        const SwatObject* o=&world->objects[support];
        uint32_t recipe=swat_environment_prop_mix(layout->seed^layout->fingerprint^
            ((uint32_t)(f+1)*0x9e3779b9u));
        // Domestic tabletop pairs; the generator has no kitchen/bedroom labels.
        int pair=(int)(recipe%3);
        for(int slot=0;slot<2;slot++) {
            SwatEnvironmentPropKind kind=(SwatEnvironmentPropKind)(pair*2+slot);
            b3Vec3 size=swat_environment_prop_specs[kind].size;
            b3Vec3 local={slot ? .22f : -.22f,o->half.y+.001f,slot ? .045f : -.045f};
            // Entire visual footprint sits well inside already-solid furniture.
            // No new floor obstacle, doorway footprint or visual cover archetype.
            if(fabsf(local.x)+size.x*.5f>o->half.x-.08f ||
               fabsf(local.z)+size.z*.5f>o->half.z-.08f || size.y>.1f) continue;
            out[count++]=(SwatEnvironmentProp){support,f,room,kind,local,
                (recipe&(1u<<(slot+4))) ? SWAT_PI : 0};
        }
    }
    return count;
}
#endif
