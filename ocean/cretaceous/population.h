// Population director for solo and host play (never the trainer, whose
// config/cretaceous.ini world stays as it is).
//
// The island is 8 km across. With 16 animals, herds that walk 400 m legs to
// their quest places (progress.c) and spawn points measured from the mean of
// all herbivores (which, once herds split up, lies in empty country), a player
// spent about half of a session with nothing within 300 m (tools/popprobe.c).
// The director keeps the world populated around people:
//   - more animals: cret_pop_config raises num_agents to CRET_POP_AGENTS;
//   - a person who spawns fresh is placed near a real herd (prey for a
//     carnivore, its own kind for a herbivore), never next to a predator;
//   - a saved position is kept, and a herd is brought to it at once;
//   - a person with no prey (carnivore) or company (herbivore) within
//     CRET_POP_NEAR m for CRET_POP_LONELY_S seconds gets a herd or pack moved
//     to 130-230 m away, out of sight of everyone; a herbivore without any
//     predator within 600 m for a few minutes gets a predator at 300-420 m;
//   - a bot that respawns far from everyone joins its kind near a person who
//     has few animals around.
// Only calm groups (wandering, grazing or resting, nobody latched, hurt or
// hunting) that are at least CRET_POP_FAR m from every person are moved, and
// never to within 130 m of anyone, so nothing visibly pops in or out.
//
// Numbers follow the ecosystem (docs/ecosystem.md section 8): the target for
// herbivore biomass is CRET_POP_HERB_DENSITY kg a hectare of the island's
// plants (richness x biomass, eco.h), for predators CRET_POP_PRED_SHARE of the
// herbivores alive; herbivores share theirs by the adult mass of their
// slots, predators evenly by species. Each
// species' ratio of alive to target is smoothed over a game day; over 1, its
// dead bots wait (the slot stays empty), under CRET_POP_FAST they come back
// twice as fast. Every species keeps at least one animal alive, so none dies out.
// It uses its own random stream, so the sim's own streams are untouched.
#ifndef CRET_POPULATION_H
#define CRET_POPULATION_H
#include "sim.h"
#ifdef __cplusplus
extern "C" {
#endif

#define CRET_POP_AGENTS 48        // solo and host: animals on the island (the trainer keeps [env] num_agents)
#define CRET_POP_NEAR 300.0f      // "within reach": m
#define CRET_POP_FAR 600.0f       // groups this far from every person may be moved
#define CRET_POP_LONELY_S 20      // s with nothing within reach before a herd is brought
#define CRET_POP_THREAT_S 150     // s a herbivore goes without a predator within 600 m before one is brought
#define CRET_POP_HERB_DENSITY 30.0f   // kg of herbivore a hectare of full plants (richness 1, biomass 1)
#define CRET_POP_PRED_SHARE 0.08f     // predator biomass target: this share of the herbivore biomass alive
#define CRET_POP_FAST 0.8f            // a diet under this ratio of its target respawns twice as fast

typedef struct CretPop {
    bool on;
    uint32_t rng;
    int lonely[CRET_MAX_AGENTS];      // per person: ticks with nothing within reach
    int calm[CRET_MAX_AGENTS];        // per herbivore person: ticks without a predator within 600 m
    int moved[CRET_MAX_AGENTS];       // per agent: tick it was last moved (cooldown)
    uint8_t keep[CRET_MAX_AGENTS];    // person whose position was restored: keep it at the next spawn
    uint8_t exempt[CRET_MAX_AGENTS];  // never move (debug spawns, scenario actors)
    uint8_t was_alive[CRET_MAX_AGENTS];
    int pulls, joins, placed;         // counts, for logs and tests
    float plants_ha;                  // hectares of full plants on the island (richness summed, once)
    float ratio[CRET_SPECIES];        // per species: biomass alive over its target, smoothed over a game day
    int held;                         // dead bot slots waiting because their diet is over its target (now)
} CretPop;

// Solo and host play: more animals (keeps a larger num_agents).
void cret_pop_config(CretConfig* c);
void cret_pop_init(CretPop* p, CretSim* s, bool on);
// After every cret_sim_step.
void cret_pop_step(CretPop* p, CretSim* s);
// Person i's saved position was just restored: keep it, and bring a herd if
// it is lonely there (instead of moving the person at its next spawn check).
void cret_pop_player_placed(CretPop* p, CretSim* s, int i);
// Moves person i near a herd now (a fresh spawn). Returns false if no herd was found.
bool cret_pop_place_person(CretPop* p, CretSim* s, int i);
// Moves agent i's whole group (or i alone, if `whole` is false) so that i
// stands at (x, z); members keep their offsets. Returns members moved.
int cret_pop_move_group(CretSim* s, int i, float x, float z, bool whole);
// Herbivore and predator biomass targets now (kg), and what is alive (kg).
void cret_pop_targets(const CretPop* p, const CretSim* s, float target[2], float alive[2]);
// A species' share of its diet's target (kg).
float cret_pop_species_target(const CretPop* p, const CretSim* s, int species);
// Distance from (x, z) to the nearest live animal other than `skip` matching
// `diet` (-1: any), and its slot in *who (-1: none).
float cret_pop_nearest(const CretSim* s, float x, float z, int skip, int diet, int* who);
#ifdef __cplusplus
}
#endif
#endif
