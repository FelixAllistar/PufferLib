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

// ---------------------------------------------------------------- 2. plants
// Biomass B per ECO_CELL_M cell, a fraction of the cell's capacity K
// (cret_forage, the static richness at the point). Regrowth is logistic,
// dB/dt = r B (1 - B), evaluated in closed form when a cell is read, so
// nothing is stepped. Intake is Holling type II on K x B.
#define ECO_CELL_M 32.0f          // m: side of a biomass cell
#define ECO_PLANT_SLOTS CRET_PLANT_SLOTS   // grazed cells tracked at once; every other cell is full
#define ECO_REGROW_FROM 0.2f      // regrowth: a cell eaten down to this ...
#define ECO_REGROW_TO 0.8f        // ... is back at this in one game day (r = ln 16 / day_seconds)
#define ECO_SEED_BANK 0.05f       // biomass never falls below this
#define ECO_REGROWN 0.99f         // a cell grown back this far counts as full and leaves the table
#define ECO_CELL_KG 320.0f        // kg of plants in a full cell of richness 1 (8 pachycephalosaurs take one to 0.3 in about 4 min)
#define ECO_INTAKE_X 4.0f         // c_max, the most a herbivore eats a second, in multiples of its basal burn
#define ECO_B_HALF 0.25f          // Holling half-saturation: richness x biomass at which intake is half of c_max

// What a herbivore can graze at (x, z) now: richness x biomass, 0..1.
float cret_graze_at(const CretSim* s, float x, float z);
// Biomass of the cell holding (x, z), 0.05..1.
float cret_eco_biomass(const CretSim* s, float x, float z);
// Sets the biomass of the cell holding (x, z) as of now (grazing; clients from snapshots).
void cret_eco_set_biomass(CretSim* s, float x, float z, float b);
// Logistic regrowth from b0 over `seconds` at rate (1/s).
float cret_eco_regrow(float b0, float rate, float seconds);
// Regrowth rate r (1/s) for the config's day.
float cret_eco_regrow_rate(const CretConfig* c);
// Holling type II intake (kg/s) on plants bk (richness x biomass) for a c_max of cmax kg/s.
float cret_eco_intake(float cmax, float bk);
// c_max (kg/s) of a herbivore of `mass` kg.
float cret_eco_cmax(const CretConfig* c, float mass);
// One tick of grazing by agent a with its mouth at (x, z), at most room_kg:
// eats and takes it off the cell. Returns kg eaten.
float cret_eco_graze(CretSim* s, CretAgent* a, float x, float z, float room_kg);

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
