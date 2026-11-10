// Critters: a cheap layer of small prey around people (critters.c).
//
// Most animals in any ecosystem are small. Population density scales roughly
// with body mass^-0.75 (Damuth's law), juvenile tyrannosaurs ate small
// dinosaurs (a juvenile Gorgosaurus died with two yearling dinosaurs in its
// stomach), and Cretaceous mammals such as Repenomamus (badger-sized, about
// 12 kg) even ate baby dinosaurs. So a hatchling predator lives on mice,
// lizards, birds and fish, not on stegosaurs.
//
// Critters are not agents: no Box3D bodies, no RL slots, nothing in the
// observations. They are a pool of small structs snapped to the terrain that
// exists only in a ring around each person (cret_is_person), so the trainer,
// which has no people, never runs them. They use the sim rng, so a sim with
// people stays deterministic.
//
// Kinds: a small mammal (multituberculate-like, skitters off and hides), a
// large mammal (Repenomamus-like, bites back at small attackers or when
// cornered), a lizard (fast short bursts), a small bird (flushes and is gone)
// and a fish (river and lake shallows, caught from the water).
// Eating: the primary attack (bite) catches a critter at the strike point
// (sim.c resolve_attack); food comes from the carcass formula, kg / (0.25 x
// body mass), so a mouse fills a hatchling and is nothing to an adult.
#ifndef CRET_CRITTERS_H
#define CRET_CRITTERS_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CRET_MAX_CRITTERS 384        // the pool, shared by every person in the sim
enum { CRET_CR_MOUSE, CRET_CR_BADGER, CRET_CR_LIZARD, CRET_CR_BIRD, CRET_CR_FISH, CRET_CR_KINDS };
// States: standing, wandering, running from a threat, hiding (still), fighting (the
// large mammal), flying off (a flushed bird).
enum { CRET_CRS_IDLE, CRET_CRS_WANDER, CRET_CRS_FLEE, CRET_CRS_HIDE, CRET_CRS_FIGHT, CRET_CRS_FLY, CRET_CRS_COUNT };

typedef struct CretCritterKind {
    const char* name;
    float kg_min, kg_max;            // body mass range
    float walk, flee;                // m/s
    float detect;                    // m at which it notices a running animal (0.4 of that for one creeping up)
    float react;                     // s before it reacts to what it noticed
    float fear;                      // runs from animals at least this many times its mass
    float hp;                        // bites to kill, for a bite of power 1 (see critter_bite_power)
    float density;                   // relative density: Damuth's law, (mean mass)^-0.75, before habitat
} CretCritterKind;
extern const CretCritterKind CRET_CRITTER_KINDS[CRET_CR_KINDS];

typedef struct CretCritter {
    uint8_t active, kind, state;     // CRET_CR_*, CRET_CRS_*
    uint8_t owner;                   // the person it lives around (agent slot)
    int8_t threat;                   // agent it is reacting to (-1: none)
    uint8_t react;                   // ticks until it reacts to `threat`
    uint16_t timer;                  // ticks left in this state
    float x, y, z, yaw, speed;       // feet on the ground (fish: under the surface); heading; ground speed
    float alt;                       // m above the ground (a bird flying off)
    float kg, hp;                    // body mass; bites left
    float bite_cd;                   // s until the large mammal can bite again
    float hop;                       // gait phase in metres walked (the renderer's bob)
} CretCritter;

// Densities and ranges ([critters] in config/critters.ini; defaults in critters.c).
typedef struct CretCritterConfig {
    int enabled;
    int per_person;                  // critters around one person
    int fresh_per_person;            // ... around a person who spawned as a hatchling, for the first fresh_seconds
    float fresh_seconds;
    float ring_min, ring_max;        // spawn ring around a person (m)
    float fresh_ring_min, fresh_ring_max;
    float despawn;                   // m from every person
    float density[CRET_CR_KINDS];    // multipliers on CretCritterKind.density
} CretCritterConfig;

// Counters for tests and probes (in CretStats, kept across resets).
typedef struct CretCritterStats { int spawned[CRET_CR_KINDS], eaten[CRET_CR_KINDS], bites_taken, flushed; } CretCritterStats;

typedef struct CretSim CretSim;
struct CretInput;
CretCritterConfig cret_critter_defaults(void);
// Reads [critters] from an ini file over *c; false if the file is missing (c unchanged).
bool cret_critter_load(CretCritterConfig* c, const char* path);
// Once per tick, at the end of cret_sim_step (sim.c): spawn, despawn, behaviour, the large mammal's bites.
void cret_critters_step(CretSim* s);
void cret_critters_clear(CretSim* s);
// Body length (m) of a critter of `kg` (also the renderer's scale).
float cret_critter_length(int kind, float kg);
// How suitable (x, z) is for a kind, 0..1 (density before Damuth and the config).
float cret_critter_habitat(const CretSim* s, int kind, float x, float z);
// Nearest critter that agent i can catch with a bite landing at (px, pz)
// within `reach` (the attack's reach, as sim.c resolve_attack); -1 if none.
// *dist: its distance, to compare with the nearest animal's body.
int cret_critter_in_reach(const CretSim* s, int i, float px, float pz, float reach, float* dist);
// Agent i bites critter c: it dies (eaten whole by carnivores) or, if large, is hurt and turns or runs.
void cret_critter_bite(CretSim* s, int i, int c);
// Nearest catchable critter within `range` of agent i (-1), and its distance.
int cret_critter_nearest(const CretSim* s, int i, float range, float* dist);
// Bots: a hungry juvenile carnivore (growth below about 0.5) with nothing
// better to do stalks and bites the nearest critter. Returns true if it took
// over the input (sim.c cret_bot_input, when calm).
bool cret_critter_bot(CretSim* s, int i, struct CretInput* in);
#ifdef __cplusplus
}
#endif
#endif
