#!/usr/bin/env bash
# Gen 3 OU run (2026-11-06): fine-tune of the Gen 3 randbat 1B model (20261106_gen3_1b) on Gen 3 OU positions.
# Teams: Metamon replay-based gen3ou team sets (172,594 training teams; 1,000 held out for head-to-head tests; the
# 20-team competitive set held out for Kakuna), 300K turn-1 positions from random team pairs, mixed 75/25 with the 100K
# random-battle positions so it keeps randbat play. Observation: opponent stats now estimated (base stats, 85 EVs,
# 31 IVs, neutral), as the Showdown bridge sees them. 1024x2, 2B steps, lr 0.0005 (fine-tune), replay_ratio 1.
. /workspace/env.sh
cd /workspace/PufferLib
export OMP_NUM_THREADS=11
D=reports/pokemon_experiments/20261106_gen3_ou
START=reports/pokemon_experiments/20261106_gen3_1b/checkpoints/pokemon3/1793956256918/0000000999817216.bin
./puffer train --base.env_name=pokemon3 --policy.hidden_size=1024 --train.total_timesteps=2000000000 \
    --vec.num_threads=11 --vec.total_agents=4096 --env.positions=build/pokemon3/ou/mix.txt \
    --base.load_model_path=$START --train.learning_rate=0.0005 --base.checkpoint_dir=$D/checkpoints 2>&1 | tee -a $D/train.log
