#include "audio.h"
#include "raylib.h"
#include "world.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_DIR "ocean/cretaceous/assets/audio/"
#define MIX_RATE 44100

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float sstep(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static float frand(uint32_t* s) { *s = *s * 1664525u + 1013904223u; return (*s >> 8) / 16777216.0f; }

// ---------------------------------------------------------------- sound table
// Ambience beds (looped) and one-shot banks (variants picked at random).
enum { BED_DAY, BED_INSECTS, BED_NIGHT, BED_FROGS, BED_WIND, BED_SURF, BED_RIVER, BED_RAIN, BEDS };
static const char* BED_FILE[BEDS] = {"amb_day", "amb_insects", "amb_night", "amb_frogs", "amb_wind", "amb_surf", "amb_river", "amb_rain"};
// Loudness trims: the loops are peak-normalised, so these bring their RMS to
// about the same level (measured on the built files) before the mix weights.
static const float BED_TRIM[BEDS] = {0.95f, 0.9f, 0.75f, 0.75f, 0.38f, 1.0f, 1.25f, 1.9f};

enum {
    S_STEP_GRASS, S_STEP_DIRT, S_STEP_ROCK, S_STEP_SAND, S_STEP_WATER, S_STEP_HEAVY, S_STEP_THUD,
    S_CALL_RAPTOR, S_CALL_RAPTOR2, S_CALL_STEGO, S_CALL_TREX, S_CALL_PACHY, S_CALL_SAUROPELTA, S_CALL_GALLI, S_GROWL,
    S_BITE, S_SWING, S_HIT, S_CRUNCH, S_CHEW, S_DRINK, S_FALL, S_SPLASH, S_UI_CLICK, S_UI_MOVE, BANKS
};
static const struct { const char* file; int variants; } BANK[BANKS] = {
    {"step_grass", 6}, {"step_dirt", 6}, {"step_rock", 6}, {"step_sand", 6}, {"step_water", 6}, {"step_heavy", 1}, {"step_thud", 1},
    {"call_raptor", 1}, {"call_raptor2", 1}, {"call_stego", 1}, {"call_trex", 1}, {"call_pachy", 1}, {"call_sauropelta", 1}, {"call_galli", 1}, {"growl", 1},
    {"sfx_bite", 1}, {"sfx_swing", 1}, {"sfx_hit", 1}, {"sfx_crunch", 2}, {"sfx_chew", 2}, {"sfx_drink", 2}, {"sfx_fall", 1}, {"sfx_splash", 1},
    {"ui_click", 1}, {"ui_move", 1},
};
#define MAX_VARIANTS 6
#define ALIASES 6          // simultaneous plays of one variant (device backend)
#define SOFT_VOICES 48     // software mixer voices

// Per-species voice: the call bank and how the shared sounds are pitched.
static int call_bank(int sp, uint32_t* rng) {
    switch (sp) {
    case CRET_SP_RAPTOR: return frand(rng) < 0.5f ? S_CALL_RAPTOR : S_CALL_RAPTOR2;
    case CRET_SP_STEGO: return S_CALL_STEGO;
    case CRET_SP_TREX: return S_CALL_TREX;
    case CRET_SP_PACHY: return S_CALL_PACHY;
    case CRET_SP_SAUROPELTA: return S_CALL_SAUROPELTA;
    default: return S_CALL_GALLI;
    }
}

typedef struct {
    bool on; int bank, var; double pos; float gain, pan, pitch;
} SoftVoice;

typedef struct {
    float* data; int frames;   // stereo float at MIX_RATE
} SoftWave;

typedef struct {
    bool alive, swimming; uint8_t act, species; int hurt; float act_t, step_acc, chew_t; bool hit_done;
} Track;

struct CretAudio {
    bool ok, soft, log;
    float bus[CRET_AUDIO_BUSES];
    // Device backend.
    Music bed[BEDS]; bool bed_ok[BEDS];
    Sound snd[BANKS][MAX_VARIANTS][ALIASES]; int nvar[BANKS];
    // Software backend (CRET_AUDIO_RENDER).
    SoftWave sbed[BEDS]; double sbed_pos[BEDS];
    SoftWave swave[BANKS][MAX_VARIANTS];
    SoftVoice voice[SOFT_VOICES];
    float* out; size_t out_frames, out_cap; char out_path[512];
    // Mix state.
    float bed_vol[BEDS], bed_target[BEDS], env_timer;
    Track track[CRET_MAX_AGENTS];
    bool primed;
    uint32_t rng;
    float lis[3], cam[3], yaw;
    int plays;
};

bool cret_audio_ok(const CretAudio* a) { return a && a->ok; }

static bool load_soft(SoftWave* w, const char* path) {
    if (!FileExists(path)) return false;
    Wave wv = LoadWave(path); if (!wv.data) return false;
    WaveFormat(&wv, MIX_RATE, 32, 2);
    w->frames = (int)wv.frameCount; w->data = malloc(sizeof(float) * 2 * w->frames);
    memcpy(w->data, wv.data, sizeof(float) * 2 * w->frames);
    UnloadWave(wv); return true;
}

CretAudio* cret_audio_init(void) {
    CretAudio* a = calloc(1, sizeof *a);
    for (int b = 0; b < CRET_AUDIO_BUSES; b++) a->bus[b] = 1;
    a->bus[CRET_AUDIO_UI] = 0.6f;
    a->rng = 0x9e3779b9u; a->log = getenv("CRET_AUDIO_LOG") && atoi(getenv("CRET_AUDIO_LOG"));
    const char* off = getenv("CRET_AUDIO");
    if (off && !strcmp(off, "0")) return a;
    char path[512]; snprintf(path, sizeof path, AUDIO_DIR "%s.ogg", BED_FILE[0]);
    if (!FileExists(path)) { fprintf(stderr, "cretaceous: no sounds (run tools/fetch_audio.py); audio off\n"); return a; }
    const char* render = getenv("CRET_AUDIO_RENDER");
    if (render && *render) {
        a->soft = true; snprintf(a->out_path, sizeof a->out_path, "%s", render);
        for (int b = 0; b < BEDS; b++) { snprintf(path, sizeof path, AUDIO_DIR "%s.ogg", BED_FILE[b]); load_soft(&a->sbed[b], path); }
        for (int k = 0; k < BANKS; k++) for (int v = 0; v < BANK[k].variants; v++) {
            if (BANK[k].variants > 1) snprintf(path, sizeof path, AUDIO_DIR "%s_%d.ogg", BANK[k].file, v);
            else snprintf(path, sizeof path, AUDIO_DIR "%s.ogg", BANK[k].file);
            if (load_soft(&a->swave[k][a->nvar[k]], path)) a->nvar[k]++;
        }
        a->ok = true; return a;
    }
    SetTraceLogLevel(LOG_ERROR);
    InitAudioDevice();
    SetTraceLogLevel(LOG_WARNING);
    if (!IsAudioDeviceReady()) { fprintf(stderr, "cretaceous: no audio device; audio off\n"); return a; }
    for (int b = 0; b < BEDS; b++) {
        snprintf(path, sizeof path, AUDIO_DIR "%s.ogg", BED_FILE[b]);
        if (!FileExists(path)) continue;
        a->bed[b] = LoadMusicStream(path); a->bed_ok[b] = IsMusicValid(a->bed[b]);
        if (a->bed_ok[b]) { a->bed[b].looping = true; SetMusicVolume(a->bed[b], 0); PlayMusicStream(a->bed[b]); }
    }
    for (int k = 0; k < BANKS; k++) for (int v = 0; v < BANK[k].variants; v++) {
        if (BANK[k].variants > 1) snprintf(path, sizeof path, AUDIO_DIR "%s_%d.ogg", BANK[k].file, v);
        else snprintf(path, sizeof path, AUDIO_DIR "%s.ogg", BANK[k].file);
        if (!FileExists(path)) continue;
        Sound s = LoadSound(path); if (!IsSoundValid(s)) continue;
        int n = a->nvar[k]++;
        a->snd[k][n][0] = s;
        for (int c = 1; c < ALIASES; c++) a->snd[k][n][c] = LoadSoundAlias(s);
    }
    a->ok = true;
    return a;
}

void cret_audio_set_volume(CretAudio* a, int bus, float v) {
    if (!a || bus < 0 || bus >= CRET_AUDIO_BUSES) return;
    a->bus[bus] = clampf(v, 0, 1);
    if (a->ok && !a->soft && bus == CRET_AUDIO_MASTER) SetMasterVolume(a->bus[bus]);
}

// ---------------------------------------------------------------- playing
// raylib 5.5 pan: 0.5 centre, 1 full left, 0 full right. `side` is -1 left .. 1 right.
static float pan_of(float side) {
    static int swap = -1; if (swap < 0) swap = getenv("CRET_AUDIO_SWAP_PAN") ? 1 : 0;
    float p = 0.5f - 0.5f * clampf(side, -1, 1);
    return swap ? 1 - p : p;
}

static void play(CretAudio* a, int bank, float gain, float side, float pitch, int bus) {
    if (!a->ok || a->nvar[bank] == 0) return;
    gain *= a->bus[bus] * (a->soft ? a->bus[CRET_AUDIO_MASTER] : 1.0f);
    if (gain < 0.003f) return;
    int var = (int)(frand(&a->rng) * a->nvar[bank]) % a->nvar[bank];
    pitch = clampf(pitch, 0.25f, 3.0f);
    a->plays++;
    if (a->soft) {
        int best = -1; float quiet = 1e9f;
        for (int i = 0; i < SOFT_VOICES; i++) { if (!a->voice[i].on) { best = i; break; } if (a->voice[i].gain < quiet) { quiet = a->voice[i].gain; best = i; } }
        a->voice[best] = (SoftVoice){true, bank, var, 0, gain, clampf(side, -1, 1), pitch};
        return;
    }
    Sound* s = NULL;
    for (int c = 0; c < ALIASES; c++) if (!IsSoundPlaying(a->snd[bank][var][c])) { s = &a->snd[bank][var][c]; break; }
    if (!s) s = &a->snd[bank][var][(int)(frand(&a->rng) * ALIASES) % ALIASES];   // all busy: retrigger one
    SetSoundVolume(*s, clampf(gain, 0, 1)); SetSoundPan(*s, pan_of(side)); SetSoundPitch(*s, pitch);
    PlaySound(*s);
}

// A sound at a world point: inverse-distance falloff from `ref` metres to
// `range`, panned by its bearing from the camera.
static void play_at(CretAudio* a, int bank, float x, float y, float z, float gain, float ref, float range, float pitch) {
    float dx = x - a->lis[0], dy = y - a->lis[1], dz = z - a->lis[2], d = sqrtf(dx * dx + dy * dy + dz * dz);
    if (d > range) return;
    float g = gain * fminf(1.0f, ref / fmaxf(d, 0.01f)) * (1 - sstep(range * 0.6f, range, d));
    float cx = x - a->cam[0], cz = z - a->cam[2], cl = sqrtf(cx * cx + cz * cz);
    float side = 0;
    if (cl > 0.5f) {   // right of the camera heading is +side (world: yaw from +x toward +z)
        float rx = -sinf(a->yaw), rz = cosf(a->yaw);
        side = (cx * rx + cz * rz) / cl * fminf(1.0f, cl / 4.0f) * 0.85f;
    }
    g *= 0.8f;   // headroom: a big animal's own steps, call and hit can coincide
    if (a->log) fprintf(stderr, "audio: %-14s d %5.1f m gain %.2f pitch %.2f side %+.2f\n", BANK[bank].file, d, g, pitch, side);
    play(a, bank, g, side, pitch, CRET_AUDIO_EFFECTS);
}

void cret_audio_ui(CretAudio* a, int which) {
    if (!a || !a->ok) return;
    play(a, which == CRET_UI_CLICK ? S_UI_CLICK : S_UI_MOVE, which == CRET_UI_CLICK ? 0.7f : 0.45f, 0, 1, CRET_AUDIO_UI);
}

// ---------------------------------------------------------------- world sampling
static int ground_bank(const CretWorld* w, float x, float z) {
    if (cret_water(w, x, z) > cret_height(w, x, z) + 0.08f) return S_STEP_WATER;
    const TerraMap* m = &w->map; int n = m->n;
    int gx = (int)clampf(x / m->cell + 0.5f, 0, n - 1), gz = (int)clampf(z / m->cell + 0.5f, 0, n - 1);
    const uint8_t* s = &m->splat[((size_t)gz * n + gx) * TERRA_MAT_COUNT];
    int rock = s[TERRA_MAT_ROCK] + s[TERRA_MAT_SCREE], sand = s[TERRA_MAT_SAND], dirt = s[TERRA_MAT_DIRT] + s[TERRA_MAT_MUD];
    int soft = s[TERRA_MAT_FOREST] + s[TERRA_MAT_FERN] + s[TERRA_MAT_MOSS];
    if (rock >= sand && rock >= dirt && rock >= soft) return S_STEP_ROCK;
    if (sand >= dirt && sand >= soft) return S_STEP_SAND;
    if (dirt >= soft) return S_STEP_DIRT;
    return S_STEP_GRASS;
}

// Bed targets from the world around the listener (every 0.25 s).
static void sample_env(CretAudio* a, const CretSim* sim, float cloud) {
    const CretWorld* w = sim->world; const TerraMap* m = &w->map; int n = m->n;
    float x = a->lis[0], z = a->lis[2];
    float trees = 0, nt = 0, sea = 1e9f, river = 1e9f, lake = 1e9f;
    static const float RADII[5] = {8, 30, 70, 140, 260};
    for (int r = 0; r < 5; r++) for (int k = 0; k < 16; k++) {
        float ang = k * 0.3927f + r * 0.2f, px = x + cosf(ang) * RADII[r], pz = z + sinf(ang) * RADII[r];
        int gx = (int)clampf(px / m->cell, 0, n - 1), gz = (int)clampf(pz / m->cell, 0, n - 1); size_t i = (size_t)gz * n + gx;
        int kind = m->water_kind[i];
        if (kind == TERRA_WATER_SEA && RADII[r] < sea) sea = RADII[r];
        if (kind == TERRA_WATER_RIVER && RADII[r] < river) river = RADII[r];
        if (kind == TERRA_WATER_LAKE && RADII[r] < lake) lake = RADII[r];
        if (r < 3) {
            const uint8_t* v = &m->veg[i * TERRA_VEG_COUNT];
            trees += clampf((v[TERRA_VEG_CONIFER] + v[TERRA_VEG_BROADLEAF] + v[TERRA_VEG_TREEFERN] + v[TERRA_VEG_PALM] + 0.5f * v[TERRA_VEG_CYCAD]) / 200.0f, 0, 1); nt++;
        }
    }
    trees /= fmaxf(nt, 1);
    float ground = cret_height(w, x, z), alt = fmaxf(ground, 0);
    float tod = sim->time_of_day;
    float day = sstep(0.22f, 0.29f, tod) * (1 - sstep(0.73f, 0.80f, tod));
    float dusk = sstep(0.70f, 0.76f, tod) * (1 - sstep(0.80f, 0.86f, tod)) + sstep(0.20f, 0.24f, tod) * (1 - sstep(0.26f, 0.30f, tod));
    float night = 1 - day;
    float rain = sstep(0.86f, 0.94f, cloud), overcast = sstep(0.65f, 0.85f, cloud);
    float underwater = cret_water(w, x, z) > a->lis[1] ? 1.0f : 0.0f;
    float wet = fmaxf(1 - sstep(20, 160, river), 1 - sstep(20, 160, lake));
    float exposed = sstep(120, 450, alt) * (1 - 0.5f * trees);
    float* t = a->bed_target;
    t[BED_DAY] = day * (0.25f + 0.75f * trees) * (1 - 0.75f * rain) * (1 - 0.6f * exposed);
    t[BED_INSECTS] = day * (0.35f + 0.3f * trees) * (1 - rain) * (1 - overcast * 0.5f) * (1 - sstep(150, 500, alt)) * 0.55f;
    t[BED_NIGHT] = (night + dusk * 0.4f) * (0.45f + 0.55f * trees) * (1 - 0.6f * rain) * (1 - 0.5f * exposed) * 0.8f;
    t[BED_FROGS] = (night + dusk * 0.6f) * wet * (1 - 0.3f * rain) * 0.8f;
    t[BED_WIND] = clampf(0.08f + 0.6f * exposed + 0.25f * (1 - trees) * sstep(30, 200, alt) + 0.35f * rain + 0.15f * overcast, 0, 0.9f);
    t[BED_SURF] = (1 - sstep(10, 260, sea)) * 0.9f;
    t[BED_RIVER] = (1 - sstep(6, 70, river)) * 0.8f;
    t[BED_RAIN] = rain * (0.7f + 0.3f * trees);
    if (underwater > 0) for (int b = 0; b < BEDS; b++) t[b] *= 0.25f;
}

// ---------------------------------------------------------------- per-agent events
static float voice_pitch(const CretAgent* g) {
    // Smaller (younger) animals sound higher.
    return 1.0f / sqrtf(fmaxf(cret_growth_scale(g->growth > 0 ? g->growth : 1), 0.3f));
}

static void agent_sounds(CretAudio* a, const CretSim* sim, int i, int me, float dt) {
    const CretAgent* g = &sim->agents[i]; Track* T = &a->track[i];
    const CretSpecies* S = &CRET_SPECIES_TABLE[g->species];
    float mass = S->mass * powf(cret_growth_scale(g->growth > 0 ? g->growth : 1), 3);
    float big = sstep(80, 4000, mass);                       // 0 raptor .. 1 tyrannosaur
    float loud = 0.55f + 0.45f * big;
    float range = 50 + 200 * big;                            // footsteps carry further for heavy animals
    float hy = g->y + g->height * 0.6f;
    if (!T->alive && g->alive) { T->step_acc = 0; T->act = g->act; T->hurt = g->hurt_ticks; T->swimming = g->swimming; }
    if (T->alive && !g->alive) {   // death: a cry and the body hitting the ground
        play_at(a, call_bank(g->species, &a->rng), g->x, hy, g->z, 0.8f * loud, 6 + 10 * big, 160 + 300 * big, voice_pitch(g) * 0.78f);
        play_at(a, S_FALL, g->x, g->y, g->z, 0.9f * loud, 5 + 8 * big, 90 + 120 * big, clampf(1.25f - 0.45f * big, 0.6f, 1.3f));
    }
    T->alive = g->alive; T->species = g->species;
    if (!g->alive) return;
    // Footsteps: one per stride (both feet for bipeds, the pair for quadrupeds).
    if (g->on_ground && !g->swimming && g->speed > 0.4f) {
        float stride = clampf(S->length * cret_growth_scale(g->growth) * (S->length > 6 ? 0.22f : 0.3f), 0.45f, 3.2f);
        stride *= 1 + 0.35f * sstep(S->trot, S->sprint, g->speed);   // longer strides at a run
        T->step_acc += g->speed * dt;
        if (T->step_acc >= stride) {
            T->step_acc -= stride;
            if (T->step_acc > stride) T->step_acc = 0;
            int gb = ground_bank(sim->world, g->x, g->z);
            float run = sstep(S->walk, S->sprint, g->speed);
            float pitch = clampf(1.35f - 0.5f * big, 0.55f, 1.4f) * (0.94f + 0.12f * frand(&a->rng));
            float gain = (0.35f + 0.35f * run) * (i == me ? 0.8f : 1.0f) * (0.6f + 0.6f * big);
            play_at(a, gb, g->x, g->y, g->z, gain, 3 + 4 * big, range, pitch);
            if (big > 0.15f) play_at(a, big > 0.6f ? S_STEP_HEAVY : S_STEP_THUD, g->x, g->y, g->z, big * (0.6f + 0.4f * run), 6 + 12 * big, range * 1.3f,
                                     clampf(1.1f - 0.45f * big, 0.5f, 1.2f) * (0.95f + 0.1f * frand(&a->rng)));
        }
    } else T->step_acc *= 0.9f;
    // Into the water.
    if (g->swimming && !T->swimming) play_at(a, S_SPLASH, g->x, g->y, g->z, 0.7f * loud, 4 + 6 * big, 80 + 80 * big, clampf(1.2f - 0.4f * big, 0.6f, 1.3f));
    T->swimming = g->swimming;
    // Actions: started this frame, or continuing.
    if (g->act != T->act) { T->act = g->act; T->act_t = 0; T->hit_done = false; T->chew_t = 0.3f;
        if (g->act == CRET_ACT_CALL) play_at(a, call_bank(g->species, &a->rng), g->x, hy, g->z, 1.0f * loud, 8 + 14 * big, 300 + 600 * big, voice_pitch(g));
        if (g->act == CRET_ACT_ATTACK && (S->diet == CRET_DIET_CARNIVORE || g->species == CRET_SP_PACHY))
            play_at(a, S_GROWL, g->x, hy, g->z, 0.55f * loud, 5 + 8 * big, 100 + 150 * big, voice_pitch(g) * clampf(1.4f - 0.6f * big, 0.6f, 1.5f));
    } else T->act_t += dt;
    bool alt = g->act == CRET_ACT_ATTACK2;
    if (cret_act_is_attack(g->act) && !T->hit_done && T->act_t >= (alt ? S->alt.hit_time : S->hit_time) * 0.85f) {
        T->hit_done = true;
        float dir = cret_attack_dir(g, cret_rear_attack(g));   // aimed: the sound comes from where the blow lands
        float hx = g->x + cosf(dir) * S->length * 0.4f, hz = g->z + sinf(dir) * S->length * 0.4f;
        if (alt && S->alt.kind == CRET_ALT_SLASH)   // a quick slash or kick: a light, high whoosh
            play_at(a, S_SWING, hx, hy, hz, 0.45f * loud, 3 + 4 * big, 60, clampf(1.5f - 0.4f * big, 0.9f, 1.6f) * (0.95f + 0.1f * frand(&a->rng)));
        else if (alt || S->tail || S->knock > 1.5f) play_at(a, S_SWING, hx, hy, hz, (alt ? 0.8f : 0.7f) * loud, 4 + 6 * big, 90, clampf(1.15f - 0.45f * big, 0.55f, 1.3f));
        else play_at(a, S_BITE, hx, hy, hz, 0.8f * loud, 4 + 6 * big, 90 + 60 * big, clampf(1.25f - 0.5f * big, 0.6f, 1.3f));
    }
    if (g->act == CRET_ACT_EAT || g->act == CRET_ACT_DRINK) {
        T->chew_t -= dt;
        if (T->chew_t <= 0) {
            int bank = g->act == CRET_ACT_DRINK ? S_DRINK : S->diet == CRET_DIET_CARNIVORE ? S_CRUNCH : S_CHEW;
            T->chew_t = 1.3f + 0.6f * frand(&a->rng);
            play_at(a, bank, g->x + cosf(g->yaw) * S->length * 0.4f, g->y + 0.5f, g->z + sinf(g->yaw) * S->length * 0.4f, 0.55f + 0.2f * big, 2 + 3 * big, 35 + 20 * big,
                    clampf(1.2f - 0.4f * big, 0.6f, 1.3f) * (0.95f + 0.1f * frand(&a->rng)));
        }
    }
    // Hit taken.
    if (g->hurt_ticks > T->hurt) play_at(a, S_HIT, g->x, hy, g->z, 0.85f * loud, 5 + 6 * big, 110, clampf(1.2f - 0.4f * big, 0.6f, 1.3f) * (0.95f + 0.1f * frand(&a->rng)));
    T->hurt = g->hurt_ticks;
}

// ---------------------------------------------------------------- software mixer
static void soft_mix(CretAudio* a, float dt) {
    int frames = (int)(dt * MIX_RATE + 0.5f);
    if (a->out_frames + frames > a->out_cap) {
        a->out_cap = (a->out_frames + frames) * 2 + MIX_RATE;
        a->out = realloc(a->out, sizeof(float) * 2 * a->out_cap);
    }
    float* o = a->out + a->out_frames * 2; memset(o, 0, sizeof(float) * 2 * frames);
    float amb = a->bus[CRET_AUDIO_AMBIENCE] * a->bus[CRET_AUDIO_MASTER];
    for (int b = 0; b < BEDS; b++) {
        SoftWave* w = &a->sbed[b]; if (!w->frames || a->bed_vol[b] < 0.002f) { continue; }
        float g = a->bed_vol[b] * BED_TRIM[b] * amb;
        for (int f = 0; f < frames; f++) {
            size_t p = (size_t)a->sbed_pos[b] % w->frames;
            o[2 * f] += w->data[2 * p] * g; o[2 * f + 1] += w->data[2 * p + 1] * g;
            a->sbed_pos[b] += 1;
        }
    }
    for (int v = 0; v < SOFT_VOICES; v++) {
        SoftVoice* s = &a->voice[v]; if (!s->on) continue;
        SoftWave* w = &a->swave[s->bank][s->var];
        // Same pan law as raylib 5.5 (sine approximation).
        float l = 0.5f - 0.5f * s->pan, r = 1 - l;
        float gl = s->gain * 0.5f * l * (3 - l * l), gr = s->gain * 0.5f * r * (3 - r * r);
        for (int f = 0; f < frames; f++) {
            int p = (int)s->pos; if (p >= w->frames - 1) { s->on = false; break; }
            float t = (float)(s->pos - p);
            float sl = w->data[2 * p] * (1 - t) + w->data[2 * p + 2] * t, sr = w->data[2 * p + 1] * (1 - t) + w->data[2 * p + 3] * t;
            o[2 * f] += sl * gl; o[2 * f + 1] += sr * gr;
            s->pos += s->pitch;
        }
    }
    a->out_frames += frames;
}

static void write_wav(const CretAudio* a) {
    if (!a->out_frames) return;
    short* pcm = malloc(sizeof(short) * 2 * a->out_frames);
    for (size_t i = 0; i < 2 * a->out_frames; i++) {   // soft knee above 0.8 (the device mixer just clips)
        float v = a->out[i], m = fabsf(v);
        if (m > 0.8f) v = copysignf(0.8f + 0.2f * tanhf((m - 0.8f) / 0.2f), v);
        pcm[i] = (short)(clampf(v, -1, 1) * 32767);
    }
    Wave w = {(unsigned int)a->out_frames, MIX_RATE, 16, 2, pcm};
    ExportWave(w, a->out_path);
    fprintf(stderr, "cretaceous: wrote %.1f s of audio to %s (%d sounds)\n", a->out_frames / (float)MIX_RATE, a->out_path, a->plays);
    free(pcm);
}

// ---------------------------------------------------------------- update
void cret_audio_update(CretAudio* a, const CretSim* sim, int me, const float cam[3], const float listener[3], float yaw,
                       float cloud_cover, bool paused, float dt) {
    if (!a || !a->ok || !sim || !sim->world) return;
    if (dt > 0.1f) dt = 0.1f;
    memcpy(a->cam, cam, sizeof a->cam); memcpy(a->lis, listener, sizeof a->lis); a->yaw = yaw;
    a->env_timer -= dt;
    if (a->env_timer <= 0 || !a->primed) { sample_env(a, sim, cloud_cover); a->env_timer = 0.25f; }
    if (!a->primed) { memcpy(a->bed_vol, a->bed_target, sizeof a->bed_vol); a->primed = true; }
    float k = 1 - expf(-dt * 0.8f);   // beds cross-fade over a couple of seconds
    for (int b = 0; b < BEDS; b++) {
        float target = paused ? a->bed_target[b] * 0.35f : a->bed_target[b];
        a->bed_vol[b] += (target - a->bed_vol[b]) * k;
        if (!a->soft && a->bed_ok[b]) { SetMusicVolume(a->bed[b], a->bed_vol[b] * BED_TRIM[b] * a->bus[CRET_AUDIO_AMBIENCE]); UpdateMusicStream(a->bed[b]); }
    }
    if (!paused) for (int i = 0; i < sim->cfg.num_agents && i < CRET_MAX_AGENTS; i++) agent_sounds(a, sim, i, me, dt);
    if (a->soft) soft_mix(a, dt);
}

void cret_audio_close(CretAudio* a) {
    if (!a) return;
    if (a->ok && a->soft) {
        write_wav(a); free(a->out);
        for (int b = 0; b < BEDS; b++) free(a->sbed[b].data);
        for (int k = 0; k < BANKS; k++) for (int v = 0; v < a->nvar[k]; v++) free(a->swave[k][v].data);
    } else if (a->ok) {
        for (int b = 0; b < BEDS; b++) if (a->bed_ok[b]) UnloadMusicStream(a->bed[b]);
        for (int k = 0; k < BANKS; k++) for (int v = 0; v < a->nvar[k]; v++) {
            for (int c = 1; c < ALIASES; c++) UnloadSoundAlias(a->snd[k][v][c]);
            UnloadSound(a->snd[k][v][0]);
        }
        CloseAudioDevice();
    }
    free(a);
}
