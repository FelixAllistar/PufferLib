#pragma once
#include "scene.h"

enum {SMB_BANK_MAGIC=0x53424b31,SMB_BANK_VERSION=1};
typedef struct {
    uint32_t magic,version,scene_size,entry_size,scenes,worlds;
    uint64_t rom_fingerprint,payload_hash;
} SmbBankHeader;
typedef struct {
    SmbScene scene;
    uint32_t stage,source_frame,source_id,flags;
    uint64_t actor_mask;
} SmbBankEntry;
enum {SMB_BANK_FLAGPOLE=1,SMB_BANK_STAGE_START=2,SMB_BANK_CONSTRUCTED=4,SMB_BANK_FPG_TEACHER=8};

// The checksum catches truncated/corrupted or mismatched local bank assets.
// ROM-derived scene and world bytes stay in the ignored build directory.
static inline uint64_t smb_bank_hash(uint64_t hash,const void* ptr,size_t size) {
    const auto* bytes=(const uint8_t*)ptr;
    for(size_t i=0;i<size;i++){hash^=bytes[i];hash*=1099511628211ull;}
    return hash;
}
