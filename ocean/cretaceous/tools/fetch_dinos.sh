#!/bin/bash
# Converts the dinosaur models (see assets/dinos/CREDITS.md) into the game's
# .dino files and textures in ocean/cretaceous/assets/dinos with Blender
# (tools/dino_convert.py). Run from the repository root.
#   DINO_SRC=/path/to/dino-models ocean/cretaceous/tools/fetch_dinos.sh [raptor|stego|trex|pachy|sauropelta|galli ...]
# Sketchfab and animalanimator downloads need a browser login, so this script
# does not download: put the GLBs named below into DINO_SRC first (links in
# CREDITS.md). The GLBs are untrusted data: nothing in them is executed
# (Blender runs with --disable-autoexec), and only the files named here are read.
# It never reads from a "_PLACEHOLDER_RIPS_do_not_ship" folder.
set -e
BLENDER=${BLENDER:-$HOME/tools/blender-4.2.3-linux-x64/blender}
SRC=${DINO_SRC:-$HOME/Downloads/dino-models}
OUT=${DINO_OUT:-ocean/cretaceous/assets/dinos}
HERE=$(cd "$(dirname "$0")" && pwd)
case "$SRC" in *_PLACEHOLDER_RIPS_do_not_ship*) echo "refusing to convert placeholder rips" >&2; exit 1;; esac
SPECIES=("$@"); [ ${#SPECIES[@]} -eq 0 ] && SPECIES=(raptor stego trex pachy sauropelta galli)
mkdir -p "$OUT"
for sp in "${SPECIES[@]}"; do
    case "$sp" in
    raptor) glb=velociraptor_ferocious.glb
        args=(--length 3.0 --speed walk=1.8,trot=3.6,run=6.0
              --map "idle=Idle_01,idle2=Idle_02,walk=Walk,trot=Jog,run=Sprint,jump=Leap_01,attack=Bite_01,roar=Roar_01,eat=Eat Prey,drink=Drink,sleep=Sleep_Loop,death=Death_01,hurt=Hurt_01,call=Call_Alert,sniff=Sniff");;
    stego) glb=stegosaurus_ferocious.glb
        args=(--length 8.5 --speed walk=2.0,run=4.2
              --map "idle=IdleA,idle2=IdleB,walk=Walk,run=Run,eat=Eat,drink=Drink,attack=TailWhip,roar=Roar,death=Death,hurt=HurtA,stomp=Stomp");;
    trex) glb=trex_animalanimator.glb
        args=(--length 12.0 --map "idle=01_Idle,walk=02_Walk,run=03_Run,attack=04_Attack");;
    pachy) glb=pachycephalosaurus_ferocious.glb
        args=(--length 4.5
              --map "idle=IdleA,idle2=IdleB,walk=Walk,run=Run,attack=Headbutt,eat=EatLow,drink=Drink,roar=Roar,call=Bark,hurt=HurtLow,death=KnockedOut,sleep=SleepLoop,charge=Charge");;
    sauropelta) glb=sauropelta_anees.glb   # one walk clip; generic bone names
        args=(--length 5.2 --forward "0 0 1" --feet 'Bone\.[LR]\.00[25]_' --map "walk=Armature|ArmatureAction");;
    galli) glb=gallimimus_skinny.glb       # one run clip
        args=(--length 6.0 --map "run=ArmatureAction");;
    *) echo "unknown species $sp (raptor, stego, trex, pachy, sauropelta, galli)" >&2; exit 1;;
    esac
    if [ ! -f "$SRC/$glb" ]; then echo "missing $SRC/$glb (see $OUT/CREDITS.md)" >&2; exit 1; fi
    "$BLENDER" -b --factory-startup --disable-autoexec --python "$HERE/dino_convert.py" -- "$SRC/$glb" "$sp" "$OUT" "${args[@]}" 2>&1 \
        | grep -E "^dino: (wrote|clip|LOD|material|attack)|Error" || true
    [ -f "$OUT/$sp.dino" ] || { echo "conversion failed for $sp" >&2; exit 1; }
done
