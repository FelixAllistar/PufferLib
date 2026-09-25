# Shenaniguns3D conversion

The CPU navigation simulator is preserved from `5c` at `036cf4251`, with its
391 float observations and five action heads (5/3/3/2/2). The only adapter
change moves `obs_t` before the upstream environment API include. Native
training is explicitly blocked until the custom sensor encoder is ported;
the generic MLP is not checkpoint-compatible.

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
```

Do not clone over or change an existing dirty dependency. Set `BOX3D` for a
different checkout location. Tests run paired seeded worlds across all three
course modes and difficulties, checking observation/reward determinism,
finite outputs and automatic resets over 9,216 transitions.
Both optimized and ASan/UBSan builds pass. The sanitizer target instruments
the environment/controller, not the separately built Box3D archive.

The formerly unversioned sibling `pd64/character.c` and `character.h` are now
preserved here byte-for-byte. Their original attribution identifies the C
port of Box3D's RigidbodyCharacter sample and s&box PlayerController lineage.
Source snapshot SHA-256:

- `character.c`: `0db4023c191f649532de6ef343754b6cb98e4d92cd657c67fc9595231a24c01b`
- `character.h`: `411954928be6557ae29276133bf655e4275d25148a80f2964156c85c83517d26`

The simulator uses one physics substep while the original game uses four;
sharing the controller does not establish exact deployment parity. The
legacy GPU simulator uses specialized physics rather than general Box3D.
Its port, differential qualification, custom network, standalone policy
viewer and training configuration remain unfinished.
