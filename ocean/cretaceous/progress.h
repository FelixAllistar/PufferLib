// Progression in the spirit of Path of Titans: named places built from the
// world data, server-generated quests that pay growth and marks, group
// quests, and nests where group members respawn as juveniles.
#ifndef CRET_PROGRESS_H
#define CRET_PROGRESS_H
#include "sim.h"
#ifdef __cplusplus
extern "C" {
#endif

// Named places: lakes, river fords, fern meadows, cycad groves, horsetail
// marshes, peaks and beaches. Built once per world (deterministic, so
// clients derive the same list as the server).
void cret_pois_build(CretWorld* w);
const char* cret_poi_kind_name(int kind);

// Quests and nests, once per tick after survival (sim.c).
void cret_progress_step(CretSim* s);
// Hooks from combat (sim.c): a kill, and damage as a fraction of the victim's max health.
void cret_quest_on_kill(CretSim* s, int killer, int victim);
void cret_quest_on_damage(CretSim* s, int attacker, int victim, float frac);
// Agent i caught a critter of `kind` (critters.c).
void cret_quest_on_catch(CretSim* s, int i, int kind);
// Clears the slot's quest (a new one is offered after a few seconds).
void cret_quest_reset(CretSim* s, int i);
// 0: the slot's own quest, 1: its group's (NULL if none).
const CretQuest* cret_quest_of(const CretSim* s, int i, int which);
// Where the quest leads now: the place (or a hatchling's spot), or the next patrol waypoint. False for hunts and defence.
bool cret_quest_target(const CretSim* s, const CretQuest* q, float* x, float* z, float* r);
// One line for the tracker: "Drink at Misty Lake" (title) and "4 / 8 s" (progress).
void cret_quest_text(const CretSim* s, const CretQuest* q, char* title, int nt, char* prog, int np);

// Nests.
int cret_nest_of(const CretSim* s, int i);                    // the agent's (or its group's) nest, -1
int cret_nest_near(const CretSim* s, float x, float z, float r, int except_group);   // nearest with eggs, -1
// Respawn at a nest of `species` (group >= 0, a person: its own nest or its
// group's; -1, a bot: any whose group has room): hatches an egg and returns
// the nest, or -1.
int cret_nest_spawn(CretSim* s, int i, int species, int group);
// A predator eating at (hx, hz): eats an egg every 1.5 s. Returns fullness gained.
float cret_nest_eat(CretSim* s, int i, float hx, float hz);
// Food in one egg (kg): what eating one at a nest gives, and what a carried egg holds.
#define CRET_EGG_KG 8.0f
// The nest with eggs agent i could raid with its mouth at (hx, hz): not its
// own or its group's (-1 if none).
int cret_nest_egg_at(const CretSim* s, int i, float hx, float hz);
// Radius of a species' nest (m): a tyrannosaur's is several metres across, a gallimimus's small.
float cret_nest_radius(int species);
#ifdef __cplusplus
}
#endif
#endif
