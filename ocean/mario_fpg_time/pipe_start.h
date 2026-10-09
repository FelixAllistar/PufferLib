#pragma once
#include "../mario_sim/bank_io.h"

// This experiment starts in the original 1-1 pipe-exit neighborhood. The
// untouched level continues to its flagpole; no terrain is spliced or moved.
enum {FPT_PIPE_MIN_X=2584,FPT_PIPE_MAX_X=2680,FPT_PIPE_POLE_X=3168};
SMB_HD bool fpt_is_pipe_start(const SmbLogic* s) {
    const auto* m=s->ram;int x=m[0x6d]*256+m[0x86],y=((int)m[0xb5]-1)*256+m[0xce];
    return !s->fault&&s->timing.enabled==1&&m[0x770]==1&&m[0x772]==3&&m[0xe]==8
        &&m[0x75f]==0&&m[0x75c]==0&&m[0x74e]==1&&m[0x754]==1&&m[0x1d]==0
        &&x>=FPT_PIPE_MIN_X&&x<=FPT_PIPE_MAX_X&&(y==144||y==176);
}
static void fpt_require_pipe_bank(const SmbBank& bank) {
    if(bank.header.worlds!=1||smb_bank_hash(1469598103934665603ull,bank.worlds.data(),bank.worlds.size())!=0x0df85300a23f25f8ull)
        throw std::runtime_error("pipe-exit experiment requires the original unmodified 1-1 world data");
    for(const auto& e:bank.entries)if(e.stage!=0||e.scene.mask||(e.flags&SMB_BANK_CONSTRUCTED)
        ||!(e.flags&SMB_BANK_FLAGPOLE)||!fpt_is_pipe_start(&e.scene.initial))
        throw std::runtime_error("FPG reset is outside the natural 1-1 pipe-exit band; rebuild the pipe bank");
}
