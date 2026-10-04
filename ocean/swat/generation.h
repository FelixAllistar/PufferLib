#ifndef SWAT_GENERATION_H
#define SWAT_GENERATION_H
#include "mission.h"
#include <stddef.h>
#define SWAT_LAYOUT_VERSION 1
#define SWAT_LAYOUT_TOKENS 12
#define SWAT_LAYOUT_ROOMS 5
#define SWAT_LAYOUT_WALLS 32
#define SWAT_LAYOUT_FURNITURE 12
#define SWAT_LAYOUT_SPAWNS 6
#define SWAT_LAYOUT_INPUT 49
#define SWAT_LAYOUT_HIDDEN 64
#define SWAT_LAYOUT_OUTPUT 3
#define SWAT_LAYOUT_PARAMETERS ((SWAT_LAYOUT_INPUT+1)*SWAT_LAYOUT_HIDDEN+(SWAT_LAYOUT_HIDDEN+1)*SWAT_LAYOUT_OUTPUT)
typedef enum SwatGenerator { SWAT_LAYOUT_UNIFORM, SWAT_LAYOUT_NEURAL, SWAT_GENERATORS } SwatGenerator;
typedef struct SwatPlanRoom { float x0,x1,z0,z1; bool hall; SwatMaterial floor; } SwatPlanRoom;
typedef struct SwatPlanWall {
    b3Pos origin;
    float yaw,length,opening,width,sill;
    int a,b;
    bool exterior,door;
} SwatPlanWall;
typedef struct SwatPlanFurniture { b3Pos center; b3Vec3 half; SwatMaterial material; } SwatPlanFurniture;
typedef struct SwatPlanSpawn { int actor,role; b3Pos feet; float yaw; } SwatPlanSpawn;
typedef struct SwatLayout {
    uint32_t seed,policy_id,fingerprint;
    int tokens[SWAT_LAYOUT_TOKENS],difficulty,entry_room;
    int room_count,wall_count,furniture_count,spawn_count,object_count;
    float width,depth,quality,path_length;
    SwatPlanRoom rooms[SWAT_LAYOUT_ROOMS];
    SwatPlanWall walls[SWAT_LAYOUT_WALLS];
    SwatPlanFurniture furniture[SWAT_LAYOUT_FURNITURE];
    SwatPlanSpawn spawns[SWAT_LAYOUT_SPAWNS];
    SwatMissionDef mission;
} SwatLayout;
const int* swat_layout_categories(void);
void swat_layout_logits(const int* tokens,int step,int difficulty,float logits[SWAT_LAYOUT_OUTPUT]);
bool swat_layout_load_policy(const char* path);
uint32_t swat_layout_policy_id(void);
bool swat_layout_plan(SwatLayout* plan,const int* tokens,int difficulty);
bool swat_layout_validate(SwatLayout* plan);
bool swat_layout_generate(SwatLayout* plan,uint32_t seed,int difficulty,SwatGenerator generator);
void swat_layout_build(SwatWorld* world,const SwatLayout* plan);
#endif
