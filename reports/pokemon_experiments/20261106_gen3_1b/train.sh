#!/usr/bin/env bash
# Gen 3 random battles, first real run (2026-11-06): ocean/pokemon3, 1024x2, 1B steps of mirror self-play.
# 4096 agents: 8192 does not fit the 16 GB GPU with this observation (5,171 values x horizon 64).
. /workspace/env.sh
cd /workspace/PufferLib
export OMP_NUM_THREADS=11
D=reports/pokemon_experiments/20261106_gen3_1b
./puffer train --base.env_name=pokemon3 --policy.hidden_size=1024 --train.total_timesteps=1000000000 \
    --vec.num_threads=11 --vec.total_agents=4096 --base.checkpoint_dir=$D/checkpoints 2>&1 | tee -a $D/train.log
