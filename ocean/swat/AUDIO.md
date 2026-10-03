# Shared game audio and hearing

An officer and an AI listener should hear through the same changing building.
This implementation establishes event timing, shared propagation and a
device-independent PCM mixer. It uses game-tuned acoustic approximations and
procedural placeholder sounds; a realistic production mix is still to come.

## Current path

Authority emits accepted shots, impacts, breakage, grounded footsteps,
reload/equip/selector handling and door interactions into a bounded ring.
Events contain stable IDs, emission tick, source position, kind, strength and
range. Rejected shots produce no shot event. Snapshots repeat recent sounds;
listener deduplication prevents repeats. Late join starts above old event IDs.

`acoustics.c` computes distance attenuation and low/mid/high absorption through
live oriented material boxes with thickness. Destroyed cover is excluded. A
single bend through an authored open/destroyed doorway can provide a stronger
path and bearing toward that opening. Arrival delay is path length at 343 m/s,
rounded to simulation ticks. Coefficients are gameplay tuning, not measured
materials. There is no full wave simulation, general diffraction mesh, room
impulse response or multi-bounce reverb.

`audio_dsp.c` generates deterministic 48 kHz stereo float PCM with 32 bounded
voices, equal-power pan, absorption-driven low-pass filtering and a soft output
limit. Kind chooses an original synthesized placeholder envelope/tone.
`sound_view.c` streams through Raylib and updates directional gain/filtering as
the listener/camera moves. Saved master volume affects player output only;
headless authority and training open no audio device.

The scripted guard consumes delayed kind/gain/16-sector world-bearing cues.
`SwatHeardSound` excludes hidden source coordinates and identity. Loud movement,
shots, doors and breakage create brief turning memory; firing still requires
visible acquisition and reaction time. NPC hearing ignores its own events.

## Next work

Replace synthesis with licensed/original recordings and authored mechanisms,
material impacts and stance/surface footsteps, retaining event IDs and shared
propagation. Add voice priority, room/reverb transitions, broader diffraction,
headphone HRTF and spatial calibration. Test recognition/localization with
players and agents on the same scenes.

For RL, version an audible-cue observation with temporal memory and explicit
noise/timing first. Contract v1 has no hearing fields. A later waveform listener
can use headless PCM and the same per-listener paths. Source metadata remains
internal world truth. The mixer exists; audio policy adapters, datasets, trained
audio models and recording/export tooling do not.

Tests cover distance/delay, material bands, changing occlusion, doorway bearing,
deduplication, guard reaction, deterministic PCM, stereo orientation, mute,
voice limits/expiry and audible low-pass behavior.
