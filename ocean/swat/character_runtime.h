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
    size_t count;
    Matrix root;
    Matrix first_person_inverse_gun;
    b3Pos feet;
    double distance,phase,source_time;
    int tick,episode,bank;
    uint64_t signature;
    bool valid;
    unsigned char visible[SWAT_CHARACTER_MESHES];
} SwatCharacterActorPose;
typedef struct SwatCharacterRuntime {
    SwatCharacterView banks[SWAT_CHARACTER_BANKS];
    Texture2D diffuse[2],normal[2],specular[2],gloss[2],emissive,orm;
    SwatCharacterActorPose actors[SWAT_MAX_ACTORS];
    unsigned int preparations,draws;
    double prepare_seconds,prepare_max_seconds;
} SwatCharacterRuntime;
SwatCharacterRuntime* swat_character_runtime_open(const SwatWeaponArt* weapons);
void swat_character_runtime_prepare(SwatCharacterRuntime* runtime,const SwatSim* sim);
bool swat_character_runtime_draw(SwatCharacterRuntime* runtime,int actor,SwatLighting* lighting);
bool swat_character_runtime_draw_first_person(SwatCharacterRuntime* runtime,int actor,SwatLighting* lighting,const SwatPose* presentation);
void swat_character_runtime_close(SwatCharacterRuntime* runtime);
double swat_character_reload_time(const SwatWeapon* weapon);
// Presentation-only carbine grip, applied to a freshly sampled source pose.
bool swat_character_support_grip(SwatCharacterAsset* asset,float amount);
#endif
