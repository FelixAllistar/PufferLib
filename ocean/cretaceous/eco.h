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

// ---------------------------------------------------------------- 3. what each animal knows
// Seen: within its sight range, in its cone, not hidden in cover; with its
// head down (eating or drinking) only close by. Heard: anything moving
// within ECO_HEAR_M (v / 1 m/s)^2 (M / 80)^0.3, and calls. Smelled
// (carnivores): another animal's scent carried downwind (scent.h). The
// memory holds CRET_KNOWN others: herbivores keep the predators, carnivores
// their prey and the bigger carnivores.
#define ECO_SIGHT_M 60.0f         // sight range, plus ...
#define ECO_SIGHT_PER_L 4.0f      // ... this many times its body length (m)
#define ECO_NIGHT_SIGHT 0.5f      // diurnal species see this much as far at night
#define ECO_DOZE_SIGHT 0.5f       // resting animals this much
#define ECO_CONE 1.92f            // rad: half the 220 degree view
#define ECO_HEAD_DOWN_M 15.0f     // head down: sees only this far
#define ECO_HEAR_M 8.0f           // heard within this x (speed in m/s)^2 x (M / 80)^0.3 ...
#define ECO_HEAR_MASS_EXP 0.3f
#define ECO_HEAR_MAX 150.0f       // ... at most this far (rain halves it)
#define ECO_CALL_M 200.0f         // a call is heard this far
#define ECO_HIDE_COVER 0.6f       // hidden: its height under this fraction of the cover on the sight line, moving slower than a walk
#define ECO_MEMORY_S 20.0f        // s a sensed animal is remembered ...
#define ECO_PRED_MEMORY_S 60.0f   // ... a predator for this long, at ...
#define ECO_PRED_FADED 0.5f       // ... this weight once ECO_MEMORY_S has passed

// Height of the low cover at (x, z) (m): ferns, cycads, horsetails, scrub, tree ferns.
float cret_eco_cover_h(const CretWorld* w, float x, float z);
// Sight range of agent a now (m).
float cret_eco_sight_m(const CretSim* s, const CretAgent* a);
// How far an animal of `mass` kg moving at `speed` m/s is heard (m).
float cret_eco_hear_m(const CretSim* s, float speed, float mass);
// Agent a sees agent b now (d: their distance).
bool cret_eco_sees(const CretSim* s, const CretAgent* a, const CretAgent* b, float d);
// Updates agent i's memory: what it senses now, what it forgets.
void cret_eco_perceive(CretSim* s, int i);
// Agent i's memory of agent j (NULL: none, or not noticed yet).
const CretKnown* cret_eco_known(const CretSim* s, int i, int j);
// Puts j into i's memory as coming for it (an alarm from a group member), noticed after delay_ticks.
void cret_eco_alarm(CretSim* s, int i, int j, int delay_ticks);

// ---------------------------------------------------------------- 4. fear
// risk(p) = sum over known threats t of D(t) exp(-|p - p_t| / R_t) A(t),
// plus the landscape of fear, lowered in cover for small animals. A threat
// coming for the animal inside its flight initiation distance makes it run.
#define ECO_THREAT_MIN 0.15f      // a carnivore of at least this fraction of my mass (a pack: their sum) is a threat
#define ECO_THREAT_OFS 1.5f       // D = clamp(log2(M_t / M_me) + ECO_THREAT_OFS, ECO_THREAT_FLOOR, 3)
#define ECO_THREAT_FLOOR 0.3f
#define ECO_THREAT_MAX 3.0f
#define ECO_PACK_M 60.0f          // pack members within this of each other count together
#define ECO_RISK_R0 25.0f         // R_t = ECO_RISK_R0 + ECO_RISK_RV x its sprint speed (m)
#define ECO_RISK_RV 6.0f
#define ECO_A_COMING 1.0f         // A: coming toward it (closing faster than ECO_CLOSING) or an alarm
#define ECO_A_IDLE 0.3f           // A: about, not coming
#define ECO_A_BUSY 0.15f          // A: eating or resting
#define ECO_CLOSING 0.5f          // m/s toward it that counts as coming
#define ECO_FEAR_MARK 0.3f        // the landscape: a herbivore attacked here ...
#define ECO_FEAR_SCENT 0.1f       // ... a predator passed (each ECO_FEAR_SCENT_S s it moves) ...
#define ECO_FEAR_SCENT_S 20.0f
#define ECO_FEAR_R 64.0f          // ... within this many metres, fading to nothing over a game day
#define ECO_COVER_SMALL 0.5f      // risk in cover taller than the animal
#define ECO_COVER_BIG 1.3f        // risk for a large animal (over 2.5 m) in thick cover it cannot see out of
#define ECO_FID0 20.0f            // flight initiation distance: (ECO_FID0 + ECO_FID_L x L_t) x D / (1 + 0.5 cover) x (1.3 - 0.6 food) ...
#define ECO_FID_L 5.0f
#define ECO_FID_MAX 80.0f         // ... at most this (m)
#define ECO_VIG_T 6.0f            // vigilance: a look up every ECO_VIG_T sqrt(n) s (n: its group within ECO_VIG_GROUP_M) ...
#define ECO_VIG_MAX 20.0f         // ... at most this ...
#define ECO_VIG_UP 2.0f           // ... for this long (s)
#define ECO_VIG_GROUP_M 30.0f
#define ECO_ALARM_M 60.0f         // a fleeing animal alarms its group within this ...
#define ECO_ALARM_DELAY 0.03f     // ... each one after this many seconds per metre (0.3 s per 10 m)

// D: how much a carnivore of m_threat kg (a pack's total) frightens an animal of m_me kg (0: not at all).
float cret_eco_threat_d(float m_threat, float m_me);
// Flight initiation distance (m) from a threat of body length l_threat and fear D, in cover 0..1, at gut fullness food.
float cret_eco_fid(float l_threat, float d, float cover, float food);
// Seconds between looks up while feeding, for n animals of its group close by (itself included).
float cret_eco_vigil_interval(int n);
// D of agent t (with its pack) to agent a (0: no threat).
float cret_eco_threat_of(const CretSim* s, const CretAgent* a, const CretAgent* t);
// Risk at (x, z) for agent i, from what it knows and the landscape of fear.
float cret_eco_risk(const CretSim* s, int i, float x, float z);
// The landscape of fear at (x, z), 0..
float cret_eco_fear_at(const CretSim* s, float x, float z);
// The known threat coming for agent i inside its flight initiation distance (-1: none); *d its distance.
int cret_eco_flight(const CretSim* s, int i, float* d);
// Once a tick, from cret_sim_step: the landscape of fear (hits on herbivores, predators passing).
void cret_eco_step(CretSim* s);

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
