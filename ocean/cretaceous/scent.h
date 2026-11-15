// Scent: the hunting sense of carnivores (scent.c, included by sim.c).
//
// Every animal and critter near a person leaves a trail: a ring of the last
// CRET_TRAIL_POINTS places it passed, recorded by the sim at the end of each
// tick (a point when it has moved CRET_TRAIL_STEP m from the last one, or
// moved at all after CRET_TRAIL_IDLE s). A smell is a source (a trail point
// laid `age` s ago, a carcass, a critter where it is) carried downwind: the
// wind (cret_wind) moves the scent along at CRET_SCENT_DRIFT x its strength,
// a source is smelled furthest straight downwind of it, a little across the
// wind and almost not at all upwind, and it fades over CRET_SCENT_LIFE s
// (rain washes it out sooner and dampens the range).
//
// Nothing here draws a random number or changes the state the sim steps on:
// trails are written from positions the sim already has, and every query is
// a const read. The HUD, the renderer (wisps along trails, a soft pulse on
// the nearest smelled prey) and bots use the queries; the RL observation and
// action contract are unchanged (docs/progression.md says how it would be
// added). The sim only uses the wind for one thing: a critter downwind of an
// animal smells it and notices it further away (critters.c).
//
// Clients record trails too, from the critters and animals in each snapshot
// (netclient.c), so the scent view works the same in multiplayer.
#ifndef CRET_SCENT_H
#define CRET_SCENT_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CRET_TRAIL_POINTS 8
#define CRET_TRAIL_STEP 1.5f     // m between trail points
#define CRET_TRAIL_IDLE 6.0f     // s: a slow animal still leaves a point this often
#define CRET_SCENT_LIFE 45.0f    // s a trail point can be smelled in dry weather
#define CRET_SCENT_DRIFT 0.25f   // m/s a smell drifts downwind at wind strength 1
#define CRET_SCENT_MIN_AGE 3.0f  // s: another animal's trail is smelled only behind it, never where it stands
#define CRET_SCENT_HITS 8

typedef struct CretTrail {
    float x[CRET_TRAIL_POINTS], z[CRET_TRAIL_POINTS];
    int32_t tick[CRET_TRAIL_POINTS];
    uint8_t head, n;             // next slot to write; points held
} CretTrail;

// Blood: a bleeding animal drips a mark every CRET_BLOOD_STEP m of its trail, a
// badly hurt one (under half its health) every third of those. Marks last
// CRET_BLOOD_LIFE s (rain: a third of it) in one ring for the whole island;
// the sniff view shows them in red (render/scentfx.c). Wound it, follow it,
// finish it.
#define CRET_BLOOD_MARKS 192
#define CRET_BLOOD_STEP 3.0f
#define CRET_BLOOD_LIFE 240.0f
typedef struct CretBlood { float x, z; int32_t tick; uint8_t agent, used; } CretBlood;

// Signs: what animals leave on the ground for a tracker to read. A footprint
// every CRET_SIGN_STEP m of an animal's trail (left and right in turn,
// pointing the way it went), dung from a herbivore every CRET_DUNG_S s. One
// ring for the island, recorded with the trails (only while a person plays);
// prints last CRET_PRINT_LIFE s (a third of it in rain), dung CRET_DUNG_LIFE.
// Drawn on the ground near the camera (render/fx.c).
#define CRET_SIGNS 384
#define CRET_SIGN_STEP 3.0f
#define CRET_PRINT_LIFE 300.0f
#define CRET_DUNG_LIFE 900.0f
#define CRET_DUNG_S 120.0f
enum { CRET_SIGN_NONE, CRET_SIGN_PRINT, CRET_SIGN_DUNG };
typedef struct CretSign { float x, z, yaw; int32_t tick; uint8_t kind, species, growth, agent; } CretSign;   // growth: 0..255 for 0..1

enum { CRET_SCENT_CRITTER, CRET_SCENT_CARCASS, CRET_SCENT_ANIMAL };

// One thing smelled. Critters and carcasses come with their position; another
// animal only with the freshest point of its trail that reaches the nose (x, z),
// the direction it was going there (dir, radians) and how old it is.
typedef struct CretScentHit {
    uint8_t what;                // CRET_SCENT_*
    int16_t index;               // critter slot, carcass slot or agent
    float x, z, dir, age, dist;  // dist: from the sniffer to (x, z)
    float strength;              // 0..1: range left and freshness
} CretScentHit;

typedef struct CretScent {
    float range[3];              // m straight downwind, by CRET_SCENT_* (0: this animal has no nose for it)
    float wind_x, wind_z, wind;  // direction the wind blows toward, and its strength
    float rain;                  // 0..1
    int n;                       // hits, nearest critter first, then carcasses, then animals
    CretScentHit hit[CRET_SCENT_HITS];
} CretScent;

typedef struct CretSim CretSim;
// Wind over the island (direction it blows toward, unit; strength 0.75-1.65,
// stronger under an overcast sky). Veers slowly with the sim clock; the plant
// wind in render/veg.c uses the same formula on the render clock.
void cret_wind(const CretSim* s, float* wx, float* wz, float* strength);
// How far downwind of a source its smell carries, as a fraction of the full
// range, for a nose at direction (dx, dz) from it (any length): 1.25 straight
// downwind, 0.55 across the wind, 0.1 upwind.
float cret_scent_lobe(const CretSim* s, float dx, float dz);
// Full ranges (m) agent i smells critters, carcasses and other animals' trails
// at while sniffing (0 for herbivores); species and growth set them. Not
// sniffing, a carnivore still smells within CRET_SCENT_PASSIVE of them.
#define CRET_SCENT_PASSIVE 0.35f
void cret_scent_ranges(const CretSim* s, int i, bool sniffing, float out[3]);
// 0..1: how strongly agent i smells a source of type `what` laid `age` s ago at
// (x, z) (its smell drifted downwind since); size scales the range (1 for a
// critter or a trail point, more for a big carcass). 0: not smelled.
float cret_scent_strength(const CretSim* s, int i, bool sniffing, int what, float x, float z, float age, float size);
// Everything agent i smells now (out->n hits; ranges and wind filled even when none).
int cret_scent_query(const CretSim* s, int i, bool sniffing, CretScent* out);
// Records trails: the sim once a tick at the end of cret_sim_step, only
// while someone in it is a person (the trainer never pays for it); a client
// after each snapshot with `always` (netclient.c; its sim marks no people).
void cret_scent_record(CretSim* s, bool always);
void cret_scent_clear(CretSim* s);
// 0..1: how fresh blood mark k still is (0: none there, or washed out).
float cret_blood_fresh(const CretSim* s, int k);
// 0..1: how fresh sign k still is (0: none, or gone).
float cret_sign_fresh(const CretSim* s, int k);

// How agent i notices critter k: 0 not at all, 1 seen (in front, closer when
// it is still, in plants or in the dark), 2 heard (moving close by), 3
// smelled (cret_scent_strength). Senses only: no walls or line of sight.
enum { CRET_SENSE_NONE, CRET_SENSE_SEEN, CRET_SENSE_HEARD, CRET_SENSE_SMELLED };
int cret_critter_sensed(const CretSim* s, int i, int k, bool sniffing);
// The nearest critter agent i can catch and has noticed (-1: none), its
// distance and how it was noticed (CRET_SENSE_*). Fish only from the water.
int cret_prey_noticed(const CretSim* s, int i, bool sniffing, float range, float* dist, int* how);
#ifdef __cplusplus
}
#endif
#endif
