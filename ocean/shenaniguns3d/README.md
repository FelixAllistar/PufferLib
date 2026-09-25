# Shenaniguns3D conversion

The CPU navigation simulator is preserved from `5c` at `036cf4251`, with its
391 float observations and five action heads (5/3/3/2/2). The only adapter
change moves `obs_t` before the upstream environment API include. Native
FP32 training uses the custom encoder; the generic MLP is not checkpoint-compatible.

## Dependencies and tests

Use the same Box3D revision as ARPG:

```sh
git clone https://github.com/FelixAllistar/box3d.git ../box3d
git -C ../box3d checkout --detach c4a414fcfe612a704dcd06ce921348d441271fc7
cmake -S ../box3d -B ../box3d/build -DCMAKE_BUILD_TYPE=Release \
  -DBOX3D_SAMPLES=OFF -DBOX3D_UNIT_TESTS=OFF
cmake --build ../box3d/build --parallel 2
make -C ocean/shenaniguns3d test
make -C ocean/shenaniguns3d sanitize
make -C ocean/shenaniguns3d encoder-test
```

Do not clone over or change an existing dirty dependency. Set `BOX3D` for a
different checkout location. Tests run paired seeded worlds across all three
course modes and difficulties, checking observation/reward determinism,
finite outputs and automatic resets over 9,216 transitions.
Both optimized and ASan/UBSan builds pass. The sanitizer target instruments
the environment/controller, not the separately built Box3D archive.

The custom sensor encoder is now under `encoder.cu`, registered through the
standard `src/ocean.cu` extension point. It preserves the circular depth-map
convolution, 3D occupancy convolution and scalar branch. The FP32 CUDA test
passes CPU-reference encoder/full recurrent-policy forward comparisons,
eleven encoder parameter probes and four decoder/MinGRU numerical-gradient
probes. Training-chain tests use upstream network forward/backward with
explicit output cotangents, not a custom PPO loss. BF16 remains unqualified.

## Native training

```sh
bash build.sh shenaniguns3d --float
./puffer train
```

Box3D/controller simulation is CPU-side; the sensor network and trainer run
on GPU. The config preserves the legacy 32×2 network, course stage and reward
settings, but starts fresh (`load_model_path=None`) instead of assuming an
external latest checkpoint. It omits retired priority-replay/`use_rnn` options.
The ordinary upstream recurrent network is active.

Qualification completed 2,048 FP32 steps with two async buffers, CUDA graphs
and forced episode timeouts, then loaded that checkpoint for another 1,024
steps with graphs off. Both runs had finite losses and saved checkpoints.
These small runs establish execution, not learning quality or production speed.

The configured sweep varies learning rate, time cost and progress reward.
Other inherited sweep dimensions have fixed min/max bounds, replacing the
unsupported legacy `sweep_only` setting. If overriding a fixed parameter for a
sweep, update its bounds too. No production sweep was launched for this port.

The formerly unversioned sibling `pd64/character.c` and `character.h` are now
preserved here byte-for-byte. Their original attribution identifies the C
port of Box3D's RigidbodyCharacter sample and s&box PlayerController lineage.
Source snapshot SHA-256:

- `character.c`: `0db4023c191f649532de6ef343754b6cb98e4d92cd657c67fc9595231a24c01b`
- `character.h`: `411954928be6557ae29276133bf655e4275d25148a80f2964156c85c83517d26`

The simulator uses one physics substep while the original game uses four;
sharing the controller does not establish exact deployment parity. The
legacy GPU simulator uses specialized physics rather than general Box3D.
Its port, differential qualification, standalone policy
viewer remain unfinished.
