#ifndef SWAT_CHARACTER_RUNTIME_H
#define SWAT_CHARACTER_RUNTIME_H
#include "character_view.h"
#include "weapon_art.h"
#define SWAT_CHARACTER_MESHES 16
typedef struct SwatCharacterActorPose {
    float* matrices;
    size_t count;
    Matrix root;
    b3Pos feet;
    double distance,source_time;
    int tick,episode,bank;
    uint64_t signature;
    bool valid;
    unsigned char visible[SWAT_CHARACTER_MESHES];
} SwatCharacterActorPose;
typedef struct SwatCharacterRuntime {
    SwatCharacterView banks[2];
    Texture2D diffuse[2],normal[2],emissive,orm;
    SwatCharacterActorPose actors[SWAT_MAX_ACTORS];
    unsigned int preparations,draws;
    double prepare_seconds,prepare_max_seconds;
} SwatCharacterRuntime;
SwatCharacterRuntime* swat_character_runtime_open(const SwatWeaponArt* weapons);
void swat_character_runtime_prepare(SwatCharacterRuntime* runtime,const SwatSim* sim);
bool swat_character_runtime_draw(SwatCharacterRuntime* runtime,int actor,SwatLighting* lighting);
bool swat_character_runtime_draw_first_person(SwatCharacterRuntime* runtime,int actor,SwatLighting* lighting,const SwatActor* source,const SwatController* displayed);
void swat_character_runtime_close(SwatCharacterRuntime* runtime);
double swat_character_reload_time(const SwatWeapon* weapon);
#endif
