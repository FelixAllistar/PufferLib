#ifndef SWAT_PROTOCOL_H
#define SWAT_PROTOCOL_H
#include "sim.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SWAT_NET_VERSION 7
#define SWAT_NET_MAGIC 0x53474531u
#define SWAT_NET_PACKET_MAX 98304
#define SWAT_NET_SOUNDS 32
typedef enum SwatMessage { SWAT_MSG_INPUT=1, SWAT_MSG_MAP, SWAT_MSG_SNAPSHOT,
                          SWAT_MSG_WELCOME, SWAT_MSG_RESTART, SWAT_MSG_SCENARIO } SwatMessage;
typedef struct SwatMapObject {
    b3Pos center,hinge;
    b3Vec3 half;
    float yaw,max_health,closed_yaw;
    SwatMaterial material;
    SwatPart part;
    bool door;
    float pitch;
} SwatMapObject;
typedef struct SwatMap {
    uint32_t epoch, sound_floor;
    SwatConfig config;
    int count;
    b3Pos extraction;
    int room_count;
    SwatRoom rooms[SWAT_MAX_ROOMS];
    SwatMapObject objects[SWAT_MAX_OBJECTS];
    SwatMissionDef mission;
    int layout_tokens[SWAT_LAYOUT_TOKENS];
    uint32_t layout_policy_id;
} SwatMap;
typedef struct SwatActorState {
    bool present,alive,crouched,grounded,sprinting,muzzle_blocked;
    SwatRole role;
    SwatMind mind;
    bool rescued;
    b3Pos position,tracer_start,tracer_end;
    b3Vec3 velocity,upper_offset;
    float health,yaw,pitch,ads,stamina,eye_height,recoil_pitch,recoil_yaw,lean,ready_blend;
    SwatReady ready;
    int last_shot_tick;
    SwatArsenal arsenal;
    SwatEquipment gear;
} SwatActorState;
typedef struct SwatObjectState {
    bool active,door_open,locked;
    float health,door_angle;
    int breach_owner,breach_ticks,wedge_owner;
    bool peek,trapped;
    unsigned int trap_known;
} SwatObjectState;
typedef struct SwatSnapshot {
    uint32_t epoch, revision, ack[SWAT_MAX_PLAYERS];
    int tick,actor_count,object_count,generation,leader_slot;
    unsigned int player_mask;
    SwatEnd end;
    SwatEvents totals;
    SwatDebrief debrief;
    SwatEvidence evidence[SWAT_MAX_ACTORS];
    SwatActorState actors[SWAT_MAX_ACTORS];
    SwatObjectState objects[SWAT_MAX_OBJECTS];
    SwatSoundEvent sounds[SWAT_NET_SOUNDS];
    int sound_count;
    SwatProjectile projectiles[SWAT_MAX_PROJECTILES];
    SwatSniper snipers[SWAT_SNIPERS];
    SwatDevice devices[SWAT_MAX_DEVICES];
    int commander_actor;
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
size_t swat_encode_scenario(void* bytes,size_t size,uint32_t epoch,const SwatConfig* config);
bool swat_decode_scenario(const void* bytes,size_t size,uint32_t* epoch,SwatConfig* config);
void swat_capture_map(const SwatSim* sim,uint32_t epoch,SwatMap* map);
void swat_capture_snapshot(const SwatSim* sim,uint32_t epoch,SwatSnapshot* state);
void swat_apply_map(SwatSim* sim,const SwatMap* map);
bool swat_apply_snapshot(SwatSim* sim,const SwatSnapshot* state);

#ifdef __cplusplus
}
#endif
#endif
