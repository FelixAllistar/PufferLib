#ifndef SWAT_PROTOCOL_H
#define SWAT_PROTOCOL_H
#include "sim.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 53: commands are heard (swat_sim_command_heard). The wire is unchanged, but
// recorded inputs replay differently, so older replays and peers are refused.
// 54: netcode (NETWORK.md). Clients send SWAT_MSG_INPUTS: every unacknowledged
// command (XOR/zero-run packed), the newest snapshot revision held and each
// command's view tick for lag compensation. The server answers with
// SWAT_MSG_DELTA state packets against that revision. Replays are unchanged
// in content, but the shared version refuses older files and peers.
#define SWAT_NET_VERSION 54
#define SWAT_NET_PACKET_MAX 2097152
#define SWAT_NET_MAGIC 0x53474531u
#define SWAT_NET_SOUNDS 32
typedef enum SwatMessage { SWAT_MSG_INPUT=1, SWAT_MSG_MAP, SWAT_MSG_SNAPSHOT,
                          SWAT_MSG_WELCOME, SWAT_MSG_RESTART, SWAT_MSG_SCENARIO,
                          SWAT_MSG_DELTA, SWAT_MSG_INPUTS } SwatMessage;
// Most commands one SWAT_MSG_INPUTS packet repeats (about one second).
#define SWAT_INPUT_BATCH 64
typedef struct SwatMapObject {
    b3Pos center,hinge;
    b3Vec3 half;
    float yaw,max_health,closed_yaw;
    SwatMaterial material;
    SwatPart part;
    SwatSupportRole support_role;
    bool door,latch_broken;
    float pitch;
    bool fractured;
    float corners[4][2];
    int wall_group;
    int supports[SWAT_MAX_SUPPORTS];
    float structural_thickness;
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
    SwatMantle mantle;
    int last_shot_tick;
    SwatArsenal arsenal;
    SwatEquipment gear;
} SwatActorState;
typedef struct SwatObjectState {
    bool active,door_open,locked,latch_broken;
    bool fractured;
    float corners[4][2];
    float health,door_angle;
    int cut_count;
    SwatCut cuts[SWAT_MAX_CUTS];
    int breach_owner,breach_ticks,wedge_owner;
    bool peek,trapped;
    unsigned int trap_known;
    b3Pos breach_position;
    SwatDoorState door;
} SwatObjectState;
typedef struct SwatSnapshot {
    uint32_t epoch, revision, ack[SWAT_MAX_PLAYERS];
    int tick,actor_count,object_count,generation,leader_slot;
    unsigned int player_mask;
    unsigned int room_light_off_mask;
    SwatEnd end;
    SwatEvents totals;
    SwatDebrief debrief;
    SwatEvidence evidence[SWAT_MAX_ACTORS];
    SwatActorState actors[SWAT_MAX_ACTORS];
    SwatObjectState objects[SWAT_MAX_OBJECTS];
    uint32_t rubble_serial;
    int rubble_count;
    SwatRubbleState rubble[SWAT_RUBBLE_CAPACITY];
    SwatSoundEvent sounds[SWAT_NET_SOUNDS];
    int sound_count;
    SwatProjectile projectiles[SWAT_MAX_PROJECTILES];
    SwatSniper snipers[SWAT_SNIPERS];
    SwatDevice devices[SWAT_MAX_DEVICES];
    int commander_actor;
} SwatSnapshot;
// view_tick: the server tick the client was presenting remote actors at
// when it sampled this command (0: unknown). Only SWAT_MSG_INPUTS carries it.
typedef struct SwatCommand { uint32_t epoch,sequence; SwatInput input; uint32_t view_tick; } SwatCommand;
typedef struct SwatInputBatch {
    uint32_t epoch,snapshot_ack;
    int count;                       // consecutive sequences, oldest first
    SwatCommand commands[SWAT_INPUT_BATCH];
} SwatInputBatch;

// All wire integers and IEEE float32 values have explicit big-endian encoding.
// Decoders validate complete packets into temporary state before any mutation.
// ENet connect data, which the server compares exactly before giving a slot:
// magic + wire version + swat_weapons_table_id(). The id is 0 for the compiled
// table, so default peers send the same value as before weapon data existed.
uint32_t swat_net_handshake(void);
SwatMessage swat_message_type(const void* bytes, size_t size);
size_t swat_encode_command(void* bytes,size_t size,const SwatCommand* command);
bool swat_decode_command(SwatCommand* command,const void* bytes,size_t size);
size_t swat_encode_inputs(void* bytes,size_t size,const SwatInputBatch* batch);
bool swat_decode_inputs(SwatInputBatch* batch,const void* bytes,size_t size);
// Generic byte delta: XOR against a baseline, packed as (zero run, literal
// run) varint pairs. Returns the packed size, 0 if it does not fit.
size_t swat_delta_pack(void* out,size_t capacity,const void* base,size_t base_size,const void* target,size_t target_size);
bool swat_delta_unpack(void* out,size_t target_size,const void* base,size_t base_size,const void* packed,size_t packed_size);
// SWAT_MSG_DELTA: a complete SWAT_MSG_SNAPSHOT packet expressed against an
// earlier one the client acknowledged. Expanding yields the identical bytes,
// which then pass the unchanged swat_decode_snapshot validation.
size_t swat_encode_snapshot_delta(void* bytes,size_t size,uint32_t epoch,uint32_t base_revision,
    const void* base,size_t base_size,const void* full,size_t full_size);
bool swat_decode_snapshot_delta_header(const void* bytes,size_t size,uint32_t* epoch,uint32_t* base_revision);
size_t swat_expand_snapshot_delta(void* out,size_t capacity,const void* base,size_t base_size,const void* bytes,size_t size);
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
// Progress may service transport/UI only; it must not step or inspect the
// partially constructed replica. Hooks are cleared before this function returns.
void swat_apply_map_progress(SwatSim*,const SwatMap*,SwatBuildProgress,void* context);
bool swat_apply_snapshot(SwatSim* sim,const SwatSnapshot* state);

#ifdef __cplusplus
}
#endif
#endif
