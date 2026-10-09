# Material destruction recordings

Nine unchanged original Ogg clips from rubberduck's
[75 CC0 breaking / falling / hit SFX](https://opengameart.org/content/75-cc0-breaking-falling-hit-sfx)
are installed directly in `assets/audio`. The source page identifies the pack
as CC0. Original names, archive/source/runtime SHA-256 digests, durations and
selection are recorded in `assets/audio/SOURCES.json`. No extra copy of the
75-clip archive or unpacked release is kept in the repository.

Three rock-breaking variants replace wood-breaking aliases for drywall and
plaster, and add brick breakage. Three glass-breaking variants cover shattered
glass; three metal-falling variants cover a failed steel assembly. Wood keeps
its existing wood recordings. These are material foley, not a claim that the
source captures real bullet hits or physically accurate building collapse.

All nine original 48 kHz stereo files are decoded through the existing production
bank loader to mono float, keeping the ordinary directional, room-tail and
occlusion processing. The bank has 76 variant rows within its existing 80 slots.
No new audio pipeline, Python execution or duplicate WAV conversion is needed.

A successful charge now emits one material debris event alongside its blast,
regardless of the number of wall fragments removed. Failed/unowned attempts
emit no debris. This supplies the recorded break layer without flooding voices
per fragment. The blast itself retains its current fallback and remains audio
polish work. Doors/handling also need further refinement.

Evidence lives in `build/swat/review/destruction-audio`: bank/source hash and
decode checks, material selection/finite mixed samples and tactical checks.
Native player assets receive the same bank and original clips. The normal
launcher now checks audio-file freshness alongside environment assets.
