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

The October 10 follow-up adds nine unchanged Kenney CC0 plate, tin and soft
contact recordings (67,619 bytes) with original/runtime hashes in
`impact-bank.json` and the production `SOURCES.json`. The bank now has 88 rows
within 96 slots. Exact surface entries override documented family fallbacks;
all 16 impact/break materials select three recorded variants. Ceramic breakage
uses glass and aluminum uses steel fall recordings; these remain foley families,
not captured ballistic or explosive failures. A redundant same-contact hit is
suppressed when its recorded break plays. Authority and NPC hearing are unchanged.

The production loader/selector check is now a maintained C test,
`tests/test_sound_bank.c`, rather than only a temporary review tool. Linux and
Windows decode checks, complete material coverage, selection priority, source
identity, bounded directional mixing, expiry and cleanup are validated in
`build/swat/review/breach-feedback`. `material-review.wav` is a compact listening
proof; realistic blast recordings and voice/door polish remain unfinished.
