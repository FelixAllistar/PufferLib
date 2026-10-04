#ifndef SWAT_REPLAY_H
#define SWAT_REPLAY_H
#include "protocol.h"
#include <stdio.h>
typedef struct SwatReplay {
    FILE* file;
    SwatConfig config;
    uint32_t seed,count,frame;
    bool writing,failed;
} SwatReplay;
bool swat_replay_record(SwatReplay* replay,const char* path,const SwatSim* sim);
bool swat_replay_append(SwatReplay* replay,const SwatInput* input,const SwatSim* after);
bool swat_replay_open(SwatReplay* replay,const char* path);
// 1 complete frame, 0 clean end, -1 malformed/truncated/incompatible file.
int swat_replay_next(SwatReplay* replay,SwatInput* input,uint32_t* expected);
bool swat_replay_close(SwatReplay* replay);
uint32_t swat_replay_digest(const SwatSim* sim);
#endif
