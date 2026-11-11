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
// Agent i used the ability in `slot` (0-2; abilities.c), and ate kg of meat from a carcass (sim.c).
void cret_quest_on_ability(CretSim* s, int i, int slot);
void cret_quest_on_scavenge(CretSim* s, int i, float kg);
// Growth stage for quests (0 hatchling below 0.5, 1 juvenile below 0.9, 2
// adult) and its band of quest distances in m.
int cret_quest_stage(float growth);
void cret_quest_band(float growth, float* lo, float* hi);
// Hunt rule: could hunters of hunters_mass in all (the hunter, or its group)
// plausibly kill animal b? And can herbivore a wound a predator (defend)?
bool cret_quest_prey_ok(const CretSim* s, const CretAgent* hunter, float hunters_mass, const CretAgent* b);
bool cret_quest_can_defend(const CretAgent* a);
// Short name of a quest kind ("drink", "companion"), for logs and probes.
const char* cret_quest_kind_name(int kind);
// Clears the slot's quest (a new one, of another kind if it can, is offered after a few seconds).
void cret_quest_reset(CretSim* s, int i);
// 0: the slot's own quest, 1: its group's (NULL if none).
const CretQuest* cret_quest_of(const CretSim* s, int i, int which);
// Where the quest leads now: the place or spot, the next patrol waypoint, or
// the animal a companion quest stays near. False for hunt, defend, ability and nest quests.
bool cret_quest_target(const CretSim* s, const CretQuest* q, float* x, float* z, float* r);
// The tracker's words: "Drink at Misty Lake" (title) and "drink 4 / 8 s" (progress).
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
#ifdef __cplusplus
}
#endif
#endif
