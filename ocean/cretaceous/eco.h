// Ecosystem and animal AI (docs/ecosystem.md). eco.c holds the arithmetic
// (energy, plants, fear, the value of a hunt), bots.c the scripted animals
// that act on it; sim.c includes both. Every rule treats a person's animal
// exactly like a bot's. Nothing here enters cret_sim_observe: the policy
// contract is unchanged.
//
// The constants are in one table per section, each with its unit. Rates are
// per second of game time; masses in kg; food in kg or as a fraction of the
// gut (CretAgent.food, which the observation and the HUD read).
#ifndef CRET_ECO_H
#define CRET_ECO_H
#include "sim.h"
#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------- 1. energy
// Gut capacity G = ECO_GUT_* x mass (kg of food). Kleiber basal burn
// B = k_b M^0.75 kg/s, k_b set so a resting adult raptor burns a full gut
// in hunger_seconds; movement adds k_m M^0.7 v (k_m: a trotting raptor burns
// ECO_TROT_BURN x basal; a sprint, a fight or swimming counts as moving at
// ECO_SPRINT_MULT x the speed). Fat: a store of ECO_FAT_FRAC x mass kg.
#define ECO_REF_MASS 80.0f        // kg: the adult raptor the rates are set from
#define ECO_REF_TROT 4.6f         // m/s: its trot
#define ECO_GUT_CARN 0.25f        // carnivore gut, fraction of body mass (they gorge)
#define ECO_GUT_HERB 0.12f        // herbivore gut, fraction of body mass
#define ECO_BASAL_EXP 0.75f       // Kleiber exponent of the basal burn
#define ECO_MOVE_EXP 0.70f        // exponent of the cost of moving (cost of transport falls with size)
#define ECO_TROT_BURN 2.0f        // a raptor trotting burns this many times its basal rate
#define ECO_SPRINT_MULT 1.6f      // sprinting, fighting and swimming: the movement term at this many times the speed (a sprinting raptor ~4x basal)
#define ECO_FAT_FRAC 0.15f        // fat store, fraction of body mass (kg)
#define ECO_FAT_ABOVE 0.6f        // gut fullness above which the surplus goes to fat
#define ECO_FAT_RATE 0.3f         // ... the gut then drains this much faster, the extra stored as fat (kg for kg)
#define ECO_THIRST_EXP 0.1f       // water lasts (M / 80)^0.1 times longer than thirst_seconds
#define ECO_HEAT 0.5f             // water loss this much faster in the open in the hours around noon
#define ECO_HEAT_HOURS 4.0f       // ... this many hours centred on noon
#define ECO_SHADE 0.35f           // canopy cover (0..1) that counts as shade
#define ECO_GROW_FOOD 0.25f       // growth needs gut fullness above this ...
#define ECO_GROW_FAT 0.1f         // ... and fat above this
#define ECO_DIGEST_MEAL 0.5f      // a carnivore that ate this much of its gut in one meal digests: no hunting ...
#define ECO_DIGEST_END 0.5f       // ... until its gut is below this
#define ECO_MEAL_FADE 30.0f       // s: a meal counts as over this long after the last bite (the running total decays)
#define ECO_SPAWN_FAT 0.5f        // fat of a newly spawned animal

// Gut capacity of agent a (kg).
float cret_eco_gut_kg(const CretAgent* a);
// Basal burn (kg/s) of an animal of `mass` kg, for a hunger clock of hunger_seconds.
float cret_eco_basal(float hunger_seconds, float mass);
// Food agent a burns this tick (kg/s): basal plus movement.
float cret_eco_burn(const CretConfig* c, const CretAgent* a);
// Takes burn_kg of food from agent a: from the gut (with the surplus above
// ECO_FAT_ABOVE moved to fat), from fat once the gut is empty. Returns the
// kg burned from the gut and fat together.
float cret_eco_spend(CretAgent* a, float burn_kg);
// Seconds a full gut (and no fat) lasts agent a at its basal rate.
float cret_eco_gut_seconds(const CretConfig* c, const CretAgent* a);
// Water lost per second (fraction of a full drink): thirst_seconds stretched
// by mass, faster when sprinting, slower at rest, faster in the open at midday.
float cret_eco_thirst(const CretSim* s, const CretAgent* a);
// Canopy cover over (x, z), 0 (open) .. 1 (closed forest).
float cret_eco_canopy(const CretWorld* w, float x, float z);

// ---------------------------------------------------------------- activities
// What a scripted animal is doing (CretAgent.bot_state). Wander, graze and
// rest are the calm activities the population director may move.
enum { BOT_WANDER, BOT_GRAZE, BOT_FLEE, BOT_HUNT, BOT_REST, BOT_DRINK, BOT_SEEK_WATER, BOT_FEED, BOT_DEFEND, BOT_NEST, BOT_RAID, BOT_CARRY,
       BOT_STATES };
extern const char* const CRET_BOT_STATE_NAMES[BOT_STATES];

#ifdef __cplusplus
}
#endif
#endif
