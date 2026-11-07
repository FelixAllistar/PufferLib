#ifndef SWAT_REPLAY_H
#define SWAT_REPLAY_H
#include "protocol.h"
#include <stdio.h>
typedef struct SwatReplay {
    FILE* file;
    SwatConfig config;
    uint32_t seed,count,frame;
    bool writing,failed;
    // swat_weapons_table_id() of the recording. Journals made with the
    // compiled table keep the version 1 header byte for byte. Other tables
    // write version 2, which adds this id after the flags word. Opening
    // fails, with weapon_mismatch set, unless the running table matches.
    uint32_t weapon_table;
    bool weapon_mismatch;
} SwatReplay;
bool swat_replay_record(SwatReplay* replay,const char* path,const SwatSim* sim);
bool swat_replay_append(SwatReplay* replay,const SwatInput* input,const SwatSim* after);
bool swat_replay_open(SwatReplay* replay,const char* path);
// 1 complete frame, 0 clean end, -1 malformed/truncated/incompatible file.
int swat_replay_next(SwatReplay* replay,SwatInput* input,uint32_t* expected);
bool swat_replay_close(SwatReplay* replay);
bool swat_replay_checkpoint(SwatReplay* replay,const char* path);
bool swat_replay_restore(SwatSim* sim,const char* path,char* error,size_t capacity);
bool swat_replay_continue(SwatReplay* replay,const char* path,const char* checkpoint,const SwatSim* sim);
uint32_t swat_replay_digest(const SwatSim* sim);
#endif
