#pragma once
#include "cuda_replay.h"
enum {SMB_CUDA_REPLAY_BATCH=256};
void smb_cuda_replay_init(const char* module_path);
void smb_cuda_replay_shutdown();
