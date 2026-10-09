#pragma once
#include "scene.h"

enum {SMB_TRACE_MAGIC=0x534d4231,SMB_TRACE_VERSION=2};
typedef struct {
    uint32_t magic,version,scene_size,case_size,frame_size,cases,frames;
    uint32_t scope; // 1: stable gameplay clips, explicitly not a full-game gate.
    uint64_t rom_fingerprint;
} SmbTraceHeader;
typedef struct {
    SmbScene scene;
    uint32_t frames,stage,source_frame,kind,seed;
} SmbTraceCase;
typedef struct {
    uint8_t buttons,ram[SMB_RAM],a,x,y,p,sp,control,mask;
    uint16_t pc;
    int32_t timestamp,video_frame;
} SmbTraceFrame;
