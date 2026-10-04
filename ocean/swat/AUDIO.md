# Shared game audio and hearing

An officer and an AI listener should hear through the same changing building.
The current implementation combines shared live-geometry propagation, a
device-independent PCM mixer, approximate room reflections and optional Steam
Audio headphone rendering. Sounds are still procedural placeholders. Material
coefficients are authored game tuning, not measured building assemblies.

## Current path

Authority emits accepted shots, impacts, breakage, grounded footsteps,
reload/equip/selector handling, compliance commands, canister contacts,
flash/CS/taser effects and door interactions into a bounded ring.
Events contain stable IDs, emission tick, source position, kind, strength and
range and surface material. Rejected shots produce no shot event. Snapshots repeat recent sounds;
listener deduplication prevents repeats. Late join starts above old event IDs.

`materials.c` separates surface absorption from low/mid/high transmission loss,
and supplies contact density/friction/restitution/rolling resistance, impact
pitch/decay and footstep gain from the same material identity. This is the
collider-surface concept described by [Unity's Physics Material reference](https://docs.unity.com/en-us/engine/6000.7/manual/physics-section/physics-overview/collision-section/collider-surfaces/class-physics-material),
implemented here using Box3D and our own authored tables. The coefficients are
game tuning; sound absorption, transmission and mechanical bounce remain
separate properties rather than being inferred from one another.
Concrete, gypsum board, timber, glass, steel, brick, plaster, fibrous insulation,
tile, carpet and earth have editable definitions. `acoustics.c` intersects live
oriented boxes, adds each crossed layer's loss in dB, and uses a logarithmic
thickness correction for a homogeneous slab. Gypsum faces and timber studs are
separate geometry. There is no mass-air-mass resonance or measured STC model.

Destroyed cover is excluded. A single bend through an open/destroyed doorway
can provide a stronger path and arrival bearing toward that opening. Three
points across each opening avoid treating a swung leaf as a closed doorway.
Arrival delay is path length at 343 m/s, rounded to simulation ticks. This is
a bounded game approximation; it does not solve arbitrary diffraction or
multiple-room portal chains.

`audio_dsp.c` generates deterministic 48 kHz stereo float PCM with 32 bounded
voices, smoothed gain/filter changes and a soft output limit. A four-delay
feedback network plus four early taps supplies a stereo room tail. Room size,
wall/floor absorption, actual roof material, exposed furniture and open
doors/holes determine approximate decay times;
the mixer uses the middle-band RT60 with high-frequency damping. A broken board
only adds escape area when the opposite face no longer blocks the opening.
Outdoor space has no added local-room reverb. A source in another room also
has a small two-delay feedback tail that passes through its live material
filter, attenuation and arrival direction before HRTF processing. An indoor
shot therefore retains source-room decay for an outdoor listener. Sources in
the listener's room use the shared room bus to avoid applying that room twice.
This remains an approximate room/path model, not measured impulse responses
or arbitrary coupled-room wave simulation.

Footsteps query the actual ground material; its gain changes authority sound
events and NPC hearing as well as player output. Canister contact sounds use
the struck surface. Carpet is quieter and stops bouncing quickly; tile and
steel have more energetic contacts and distinct synthesized ringing. The
canister mass/contact/CCD tests are separate from PCM timbre and room-tail tests.

`spatial_audio.c` dynamically loads Steam Audio 4.8.1 and uses one binaural
effect per voice, bilinear HRTF interpolation, 256-frame processing, and
explicit voice/round reset. It models headphone cues for front/back, elevation,
interaural delay and spectrum. This integration uses Steam Audio's HRTF engine;
its scene simulation/reflection APIs are not enabled. The game owns propagation
so actors and players share destruction/door rules. Missing runtime falls back
to equal-power stereo panning. `SWAT_HRTF_SOFA` accepts an optional compatible
personal HRTF; `SWAT_STEAM_AUDIO_LIBRARY` overrides the runtime library path.

`sound_view.c` streams through Raylib. Listener orientation updates every frame;
active acoustic paths and room parameters refresh at 10 Hz. The listening point
stays at the officer's ears while using the optiwand or planning cameras;
direct scope control listens at the selected sniper. Saved
master volume affects player output only; authority/training opens no device.

The scripted guard consumes delayed kind/gain/16-sector world-bearing cues.
`SwatHeardSound` excludes hidden source coordinates and identity. Loud movement,
shots, doors and breakage create brief turning memory; firing still requires
visible acquisition and reaction time. NPC hearing ignores its own events.

## Enable and compare

From the repository root:

```sh
python3 ocean/swat/setup_audio.py
make -C ocean/swat viewer audio-lab spatial-test
build/swat/test_spatial_audio --require
build/swat/audio_lab build/swat/audio-lab-stereo
build/swat/audio_lab build/swat/audio-lab-hrtf --hrtf
./swat play
```

The setup script verifies the pinned official SDK archive hash and installs
only local Linux/Windows runtimes and license notices under ignored `build/`.
No Steam client/account is involved. The Windows build copies an installed DLL
beside the executable. The startup log reports HRTF or stereo fallback. Other
build locations can use the explicit library environment variable. Headers and
notices are in `vendor/steam_audio`; binaries are not committed.

The lab writes 26 stereo PCM16 WAVs and `comparison.csv`: open air,
wood/brick/glass/steel, two/one/no gypsum faces, front/back/left/right/above,
dry/carpet/tile rooms, closed/open doors, an indoor source heard outside,
carpet/tile steps and wood/steel/tile/carpet impacts. Propagation comparisons
keep source gain fixed; footstep comparisons include the surface's authored gain.
Compare on headphones without normalizing each file's loudness. Waveforms,
band gains and late energy are repeatable; perceptual realism still needs
listening tests and better source recordings.

In the current six-metre fixture, calculated gain is 0.429 without a partition,
0.029 through two gypsum faces, 0.101 after one face breaks, and 0.429 after both
break between studs. The house doorway case rises from 0.029 to 0.197 and changes
arrival direction to the opening. The carpet/tile room fixture requests 0.674 s
versus 1.337 s middle-band RT60 and produces more late energy for tile. These
numbers describe this tuning and these fixtures, not measurements of real homes.

## Research and decisions

- [Steam Audio C programmer's guide](https://valvesoftware.github.io/steam-audio/doc/capi/guide.html):
  native float-PCM HRTF processing, optional SOFA data and smooth interpolation
  fit this C engine without replacing Raylib's device output. It also exposes
  transmission, occlusion and reflection simulation for later experiments.
- [Stepan Boev, Sound Propagation in Hitman, GDC Europe 2015](https://media.gdcvault.com/gdceurope2015/Boev_Stepan_Sound%20Propagation%20in.pdf):
  room/portal graphs, apparent direction at openings and runtime geometry
  changes are useful for a destructible house. Its 10–15 Hz propagation update
  is a useful scheduling reference. The talk also explains why listener-only
  reverb can misrepresent a remote source; our source tails now retain their
  room decay along the arrival path. The current three-point, one-bend routes are much
  smaller than Hitman's graph system.
- [Audio Propagation Through the Ears of VERA, GDC 2018](https://www.gdcvault.com/play/1025063/Audio-Propagation-Through-the-Ears):
  the session describes a voxel approach covering occlusion, obstruction,
  early reflections and portals. Treat that as a research direction for
  general dynamic topology; no VERA implementation is included here.
- [FFmpeg's `afir` filter](https://ffmpeg.org/ffmpeg-filters.html#afir):
  useful for offline impulse-response comparisons and asset processing. A
  filter can render a supplied response; the changing game geometry still
  has to supply propagation parameters/responses. The current lab exports WAV
  directly and has no FFmpeg dependency.

Raylib was the original output layer because the game already used it. A device
and mixing library does not by itself decide how a demolished wall changes a
sound path. The present split keeps that decision in shared simulation code and
uses a specialist library for headphone rendering.

## Next work

Replace synthesis with licensed/original recordings and authored mechanisms,
material impacts and stance/surface footsteps, retaining event IDs and shared
propagation. Add better voice priority, coupled-room calibration, broader diffraction,
measured material assemblies and spatial calibration. Test recognition/localization with
players and agents on the same scenes.

For RL, version an audible-cue observation with temporal memory and explicit
noise/timing first. Contract v1 has no hearing fields. A later waveform listener
can use headless PCM and the same per-listener paths. Source metadata remains
internal world truth. The mixer and WAV comparisons exist; audio policy adapters,
datasets and trained audio models do not.

Tests cover distance/delay, material bands, changing occlusion, doorway bearing,
deduplication, guard reaction, deterministic PCM, stereo orientation, mute,
voice limits/expiry, low-pass behavior, persistent listener/source room tails,
floor/roof/furniture absorption and native HRTF
left/right, front/back and elevation differences. These checks validate behavior,
not production audio quality.
