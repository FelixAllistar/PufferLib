#pragma once
#include <cuda_runtime.h>
#include "trace.h"
struct SmbReplayResult {int frame,field,fault;};
void smb_cuda_replay_launch(SmbLogic*,const uint8_t*,const SmbTraceCase*,const int*,
    const SmbTraceFrame*,SmbReplayResult*,int,int,int,cudaStream_t);
