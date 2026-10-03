#ifndef SWAT_PROTOCOL_H
#define SWAT_PROTOCOL_H
#include "sim.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SWAT_NET_VERSION 1
#define SWAT_NET_MAGIC 0x53474531u
#define SWAT_NET_PACKET_MAX 24576
#define SWAT_NET_SOUNDS 32
typedef enum SwatMessage { SWAT_MSG_INPUT=1, SWAT_MSG_MAP, SWAT_MSG_SNAPSHOT,
                          SWAT_MSG_WELCOME, SWAT_MSG_RESTART } SwatMessage;
typedef struct SwatMapObject {
    b3Pos center,hinge;
    b3Vec3 half;
    float yaw,max_health;
    SwatMaterial material;
    bool door;
} SwatMapObject;
typedef struct SwatMap {
    uint32_t epoch, sound_floor;
    SwatConfig config;
    int count;
    b3Pos extraction;
    SwatMapObject objects[SWAT_MAX_OBJECTS];
} SwatMap;
typedef struct SwatActorState {
    bool present,alive,crouched,grounded,sprinting,muzzle_blocked;
    SwatRole role;
    b3Pos position,tracer_start,tracer_end;
    b3Vec3 velocity,upper_offset;
    float health,yaw,pitch,ads,stamina,eye_height,recoil_pitch,recoil_yaw,lean;
    int last_shot_tick;
    SwatArsenal arsenal;
} SwatActorState;
typedef struct SwatObjectState { bool active,door_open; float health,door_angle; } SwatObjectState;
typedef struct SwatSnapshot {
    uint32_t epoch, revision, ack[SWAT_MAX_PLAYERS];
    int tick,actor_count,object_count,generation,leader_slot;
    unsigned int player_mask;
    SwatEnd end;
    SwatEvents totals;
    SwatActorState actors[SWAT_MAX_ACTORS];
    SwatObjectState objects[SWAT_MAX_OBJECTS];
    SwatSoundEvent sounds[SWAT_NET_SOUNDS];
    int sound_count;
} SwatSnapshot;
typedef struct SwatCommand { uint32_t epoch,sequence; SwatInput input; } SwatCommand;

// All wire integers and IEEE float32 values have explicit big-endian encoding.
// Decoders validate complete packets into temporary state before any mutation.
SwatMessage swat_message_type(const void* bytes, size_t size);
size_t swat_encode_command(void* bytes,size_t size,const SwatCommand* command);
bool swat_decode_command(SwatCommand* command,const void* bytes,size_t size);
size_t swat_encode_map(void* bytes,size_t size,const SwatMap* map);
bool swat_decode_map(SwatMap* map,const void* bytes,size_t size);
size_t swat_encode_snapshot(void* bytes,size_t size,const SwatSnapshot* state);
bool swat_decode_snapshot(SwatSnapshot* state,const void* bytes,size_t size);
size_t swat_encode_control(void* bytes,size_t size,SwatMessage type,uint32_t epoch,int slot);
bool swat_decode_control(const void* bytes,size_t size,SwatMessage type,uint32_t* epoch,int* slot);
void swat_capture_map(const SwatSim* sim,uint32_t epoch,SwatMap* map);
void swat_capture_snapshot(const SwatSim* sim,uint32_t epoch,SwatSnapshot* state);
void swat_apply_map(SwatSim* sim,const SwatMap* map);
bool swat_apply_snapshot(SwatSim* sim,const SwatSnapshot* state);

#ifdef __cplusplus
}
#endif
#endif
