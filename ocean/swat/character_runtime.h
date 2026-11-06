#ifndef SWAT_CHARACTER_RUNTIME_H
#define SWAT_CHARACTER_RUNTIME_H
#include "character_view.h"
#include "weapon_art.h"
#define SWAT_CHARACTER_MESHES 16
typedef enum SwatCharacterBank {
    SWAT_CHARACTER_READY, SWAT_CHARACTER_WALK, SWAT_CHARACTER_LEFT,
    SWAT_CHARACTER_RIGHT, SWAT_CHARACTER_CROUCH_READY,
    SWAT_CHARACTER_CROUCH_WALK, SWAT_CHARACTER_BACKWARD, SWAT_CHARACTER_BANKS
} SwatCharacterBank;
typedef struct SwatCharacterActorPose {
    float* matrices;
    float* first_person_matrices;
    float* transition_locals;
    size_t transition_count;
    double transition_elapsed;
    size_t count;
    Matrix root;
    Matrix first_person_inverse_gun;
    b3Pos feet;
    Vector3 muzzle; // presented muzzle (world), see swat_character_runtime_muzzle
    double distance,phase,source_time;
    int tick,episode,bank;
    uint64_t signature;
    bool valid,reloading,crouching;
    unsigned char visible[SWAT_CHARACTER_MESHES];
} SwatCharacterActorPose;
#define SWAT_BODY_SETS 8 // body texture sets (glTF primitives of the full-body mesh)
typedef struct SwatCharacterRuntime {
    SwatCharacterView banks[SWAT_CHARACTER_BANKS];
    Matrix reload_inverse_gun; // Ready entry frame; retain authored working carry.
    Matrix grip_local[2];      // Ready entry left/right hand in the rifle frame.
    Texture2D diffuse[SWAT_BODY_SETS],normal[SWAT_BODY_SETS],specular[SWAT_BODY_SETS],gloss[SWAT_BODY_SETS],emissive,orm;
    bool cutout[SWAT_BODY_SETS]; // alpha-tested body sets (hair, lashes)
    // Officer runtime: -1. Armed-suspect body runtime: this body's variant
    // index among `suspect_variants` (see swat_body_variant).
    int suspect_variant,suspect_variants;
    SwatCharacterActorPose actors[SWAT_MAX_ACTORS];
    unsigned int preparations,draws;
    double prepare_seconds,prepare_max_seconds;
} SwatCharacterRuntime;
// Presentation-only third-person aim stance. Tuned offline against the
// skinned-mesh fit score in tools/officer_review.c; never affects gameplay.
typedef struct SwatAimTuning {
    float pocket[3];        // stock pad, chest frame (forward, up, right) from Spine2, m
    float eye_weight;       // 0 pocket only .. 1 sight line through the mesh eye
    float eye_relief;       // eye behind the stock pad along the barrel, m
    float blade_deg;        // chest turn toward the firing side
    float torso_share,torso_max_deg;
    float head_tip_deg,head_cant_deg,neck_share;
    float clavicle_share;   // right clavicle lift toward the stock pad
    float elbow_hint[2][3]; // left/right elbow hint, chest frame (forward, up, right)
    float slide_max;        // support hand handguard slide, m
    float stride_scale;     // moving banks: foot travel along the walk direction
    float walk_raise;       // moving standing banks: max hip raise within leg reach, m
} SwatAimTuning;
extern SwatAimTuning swat_aim_tuning;
SwatCharacterRuntime* swat_character_runtime_open(const SwatWeaponArt* weapons);
// Same officer rig, clips and props with another conformed body (a bank
// directory written by tools/mixamo); poses armed suspects of one variant.
SwatCharacterRuntime* swat_character_runtime_open_body(const SwatWeaponArt* weapons,const char* directory,int variant,int variants);
// Presentation-only body choice per actor: the actor's rank among present
// actors of its role plus a per-episode offset, so no body repeats in a scene
// while there are enough of them.
static inline int swat_body_variant(const SwatSim* sim,int actor,int count) {
    if(count<=0 || actor<0 || actor>=sim->actor_count) return -1;
    int rank=0; for(int j=0;j<actor;j++) rank+=sim->actors[j].present && sim->actors[j].role==sim->actors[actor].role;
    uint32_t h=sim->config.layout_seed*40503u ^ (uint32_t)sim->episode*2654435761u ^ (uint32_t)sim->config.mission*7919u;
    h^=h>>15; h*=2246822519u; h^=h>>13;
    return (int)((h+(uint32_t)rank)%(uint32_t)count);
}
void swat_character_runtime_prepare(SwatCharacterRuntime* runtime,const SwatSim* sim);
bool swat_character_runtime_draw(SwatCharacterRuntime* runtime,int actor,SwatLighting* lighting);
// Third-person officer: world muzzle of the drawn rifle (presentation only).
bool swat_character_runtime_muzzle(const SwatCharacterRuntime* runtime,int actor,Vector3* muzzle);
bool swat_character_runtime_draw_first_person(SwatCharacterRuntime* runtime,int actor,SwatLighting* lighting,const SwatPose* presentation);
void swat_character_runtime_close(SwatCharacterRuntime* runtime);
double swat_character_reload_time(const SwatWeapon* weapon);
// Presentation-only carbine grip, applied to a freshly sampled source pose.
bool swat_character_support_grip(SwatCharacterAsset* asset,float amount);

// Civilian (BC_CIV01, stylized candidate art): one shared GPU view, a cached
// pose per civilian actor. Presentation only: picks the authored loop for the
// actor's existing state (idle, walk, kneeling surrender, restrained kneel /
// stand, escort walk) and plays the authored transition clips between them.
// Gameplay timing, collision and eye height are unchanged.
typedef enum SwatCivilianLoop {
    SWAT_CIV_IDLE,SWAT_CIV_WALK,SWAT_CIV_SURRENDER,SWAT_CIV_RESTRAINED_KNEEL,
    SWAT_CIV_RESTRAINED_STAND,SWAT_CIV_ESCORT,SWAT_CIV_LOOPS
} SwatCivilianLoop;
typedef struct SwatCivilianPose {
    float* matrices; size_t count;
    int loop,next,transition;   // transition: clip index or -1
    double clip_time,distance,last_seconds;
    b3Pos feet; Matrix root; int tick,episode,body; // body: SwatCivilianRuntime.bodies index or -1
    bool valid,suspect,armed;
    SwatPose gun; // armed suspects: presented weapon frame (stock in the chest pocket)
} SwatCivilianPose;
// Conformed Mixamo bodies on the same BC_CIV01 skeleton and clips (written
// by tools/mixamo); listed in assets/characters/mixamo/index.txt.
#define SWAT_CIV_BODIES 24
typedef struct SwatCivilianBody {
    SwatCharacterView view;
    Texture2D diffuse[SWAT_BODY_SETS],normal[SWAT_BODY_SETS];
    bool cutout[SWAT_BODY_SETS]; // alpha-tested sets (hair, lashes)
    bool suspect,loaded; // loaded: first-use attempt made (view.asset set on success)
    size_t nodes;        // base rig node count the body must match
    char name[64],dir[2048];
} SwatCivilianBody;
typedef struct SwatCivilianRuntime {
    SwatCharacterView view;
    Color* palette[2]; // per mesh albedo: [0] civilian, [1] suspect
    SwatCivilianBody bodies[SWAT_CIV_BODIES];
    int body_count,civilian_bodies,suspect_bodies;
    SwatCivilianPose actors[SWAT_MAX_ACTORS];
} SwatCivilianRuntime;
// The conformed-body index (assets/characters/mixamo/index.txt): name and
// role per line, in a stable order shared by suspect bank runtimes.
int swat_mixamo_index(char names[][64],bool suspects[],int capacity,char* directory,size_t directory_size);
SwatCivilianRuntime* swat_civilian_runtime_open(void);
void swat_civilian_runtime_prepare(SwatCivilianRuntime* runtime,const SwatSim* sim,double seconds);
bool swat_civilian_runtime_draw(SwatCivilianRuntime* runtime,int actor,SwatLighting* lighting);
// Armed suspects: the presented weapon pose to draw with the weapon art.
bool swat_civilian_runtime_weapon(const SwatCivilianRuntime* runtime,int actor,SwatPose* pose);
void swat_civilian_runtime_close(SwatCivilianRuntime* runtime);
#endif
