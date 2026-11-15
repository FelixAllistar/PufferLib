// Game audio (raylib / miniaudio): ambience beds mixed from the world around
// the listener (forest by day and night, frogs near water, wind on heights,
// surf, rivers, rain), positional footsteps per species and ground, calls,
// attacks, hits, eating, drinking and deaths, and menu sounds.
// Reads the sim, never changes it; the trainer never links it. If there is no
// audio device or no sound files (assets/audio, from tools/fetch_audio.py),
// everything here is a silent no-op.
#ifndef CRET_AUDIO_H
#define CRET_AUDIO_H
#include <stdbool.h>
#include "sim.h"

enum { CRET_AUDIO_MASTER, CRET_AUDIO_AMBIENCE, CRET_AUDIO_EFFECTS, CRET_AUDIO_UI, CRET_AUDIO_BUSES };
enum { CRET_UI_MOVE, CRET_UI_CLICK };

typedef struct CretAudio CretAudio;

// Opens the device and loads the sounds; NULL never (check cret_audio_ok).
CretAudio* cret_audio_init(void);
bool cret_audio_ok(const CretAudio* a);
// Bus volumes 0..1 (master, ambience, effects, ui).
void cret_audio_set_volume(CretAudio* a, int bus, float v);
// Once per frame. listener: camera position (pan) and the point distances are
// measured from (the player's head in third person); yaw: camera heading.
// cloud_cover: the sky's cover (rain above ~0.9). paused: menus mute the world.
void cret_audio_update(CretAudio* a, const CretSim* sim, int me, const float cam[3], const float listener[3], float yaw,
                       float cloud_cover, bool paused, float dt);
void cret_audio_ui(CretAudio* a, int which);
// Hit confirm for a blow the player landed (not positional, over the world's
// own hit sound): a crunch for bites and claws, a low thud for rams and tail
// blows. kind: CRET_HIT_*; strength 0..1 (damage).
void cret_audio_hit(CretAudio* a, int kind, float strength);
// A blow of the player's that landed on nothing: a quiet whoosh (not
// positional), heavier for heavier blows. strength 0..1.
void cret_audio_whiff(CretAudio* a, float strength);
// CRET_AUDIO_RENDER=out.wav mixes in software instead of on the device and
// writes everything played to that file on close (tests, no speakers needed).
// CRET_AUDIO=0 turns audio off; CRET_AUDIO_LOG=1 prints every event.
void cret_audio_close(CretAudio* a);
#endif
