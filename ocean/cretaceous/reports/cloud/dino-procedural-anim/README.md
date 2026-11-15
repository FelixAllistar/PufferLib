# Procedural animation layers: preview stills

Stills from `./cretaceous capture DIR --shot NAME --size 960 540` under
`xvfb-run -a` with software GL (llvmpipe). The licensed dinosaur models are
not in the repository (`tools/fetch_dinos.sh` needs browser downloads), so
these show the **synthetic test rigs** from `tools/dino_testrig.py` (box
limbs, procedural clips) standing in for the raptor and the stegosaur. They
show that the layers run in the real render path; they are not a review of
how the real models look.

Re-rendered after merging 5.0 (foot IK in render/dino_ik.c). Each image: top = `CRET_ANIM=none` (every procedural layer off, as on 5.0
except that the foot IK is off too), bottom = `CRET_ANIM=all` (default).

| File | Shot |
|---|---|
| `testrig_raptor_run_off_on.jpg` | `dino_raptor_run`: tail lift and trail, head stabilisation |
| `testrig_raptor_idle_off_on.jpg` | `dino_raptor_idle`: tail sway, breathing, look-at, foot IK |
| `testrig_stego_walk_off_on.jpg` | `dino_stego_walk`: quadruped foot IK and locking, spine |
| `testrig_herd_off_on.jpg` | `dino_herd`: several animals looking at each other |

Reproduce from the repository root:

    python3 ocean/cretaceous/tools/dino_testrig.py ocean/cretaceous/assets/dinos raptor stego
    CRET_ANIM=none xvfb-run -a build/cretaceous/cretaceous capture /tmp/off --shot 'dino_raptor_run' --size 960 540
    CRET_ANIM=all  xvfb-run -a build/cretaceous/cretaceous capture /tmp/on  --shot 'dino_raptor_run' --size 960 540
    rm ocean/cretaceous/assets/dinos/raptor.dino ocean/cretaceous/assets/dinos/stego.dino
