#ifndef SWAT_ACOUSTICS_H
#define SWAT_ACOUSTICS_H
#include "world.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SWAT_SOUND_CAPACITY 128
#define SWAT_SOUND_LIFETIME 120
typedef enum SwatSoundKind {
    SWAT_SOUND_SHOT, SWAT_SOUND_STEP, SWAT_SOUND_RELOAD, SWAT_SOUND_HANDLE,
    SWAT_SOUND_DOOR, SWAT_SOUND_IMPACT, SWAT_SOUND_BREAK, SWAT_SOUND_COMMAND, SWAT_SOUND_KINDS
} SwatSoundKind;
typedef struct SwatSoundEvent {
    uint32_t id;
    int tick, source_actor;
    SwatSoundKind kind;
    b3Pos position;
    float strength, range;
} SwatSoundEvent;
typedef struct SwatSoundLog {
    SwatSoundEvent events[SWAT_SOUND_CAPACITY];
    uint32_t next_id;
    int head, count;
} SwatSoundLog;
typedef struct SwatAcousticPath {
    float bands[3]; // low, middle, high amplitude after distance/material loss
    float gain;
    b3Vec3 direction; // direction of arrival; a doorway can change this
    int delay_ticks;
    bool via_doorway;
} SwatAcousticPath;
typedef struct SwatHearingMemory {
    uint32_t consumed[SWAT_SOUND_CAPACITY], minimum_id;
    int head;
} SwatHearingMemory;
typedef struct SwatHeardSound {
    uint32_t id;
    SwatSoundKind kind;
    float gain, bearing; // coarse world bearing; no source position or identity
} SwatHeardSound;
typedef struct SwatRoomAcoustics {
    float rt60[3], wet, early_seconds[4], early_gain[4];
} SwatRoomAcoustics;
SwatRoomAcoustics swat_acoustic_room(const SwatWorld* world,b3Pos listener);

void swat_sound_emit(SwatSoundLog* log, int tick, int source, SwatSoundKind kind,
                     b3Pos position, float strength, float range);
void swat_sound_append(SwatSoundLog* log, SwatSoundEvent event);
const SwatSoundEvent* swat_sound_at(const SwatSoundLog* log, int index);
SwatAcousticPath swat_acoustic_path(const SwatWorld* world, const SwatSoundEvent* event,
                                   b3Pos listener);
bool swat_hearing_consumed(const SwatHearingMemory* memory, uint32_t id);
void swat_hearing_consume(SwatHearingMemory* memory, uint32_t id);
bool swat_hearing_next(const SwatWorld* world, const SwatSoundLog* log, int tick,
                       b3Pos listener, int actor, SwatHearingMemory* memory,
                       SwatHeardSound* heard);

#ifdef __cplusplus
}
#endif
#endif
