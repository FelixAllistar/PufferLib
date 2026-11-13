#ifndef SWAT_SIM_H
#define SWAT_SIM_H
#include "controller.h"
#include "weapons.h"
#include "pose.h"
#include "world.h"
#include "acoustics.h"
#include "equipment.h"
#include "mission.h"
#include "tactical.h"
#include "overwatch.h"
#include "generation.h"
#include "encounter.h"
#include "devices.h"
#include "roe.h"

#ifdef __cplusplus
extern "C" {
#endif

// Annex v4: v3's physical pane depth, optical actor presence and room-light
// limits, with obs 29 counting only suspects the team has seen at large.
// Movement keeps its separate v2 contract.
#define SWAT_CONTRACT_VERSION 4
#define SWAT_MAX_ACTORS 12
#define SWAT_MAX_PLAYERS 4
#define SWAT_PROPRIO_SIZE 32
#define SWAT_SENSOR_ROWS 5
#define SWAT_SENSOR_COLS 9
#define SWAT_SENSOR_CHANNELS 3
#define SWAT_OBS_SIZE (SWAT_PROPRIO_SIZE + SWAT_SENSOR_ROWS*SWAT_SENSOR_COLS*SWAT_SENSOR_CHANNELS)
#define SWAT_ACTION_HEADS 14
#define SWAT_ACTION_SIZES {5,5,3,3,3,2,3,2,2,2,3,2,2,2}
#define SWAT_PHYSICS_SUBSTEPS 4

typedef enum SwatRole { SWAT_OFFICER, SWAT_SUSPECT, SWAT_CIVILIAN, SWAT_SNIPER } SwatRole;
typedef enum SwatEnd { SWAT_RUNNING, SWAT_SUCCESS, SWAT_OFFICER_DOWN,
                       SWAT_CIVILIAN_HARMED, SWAT_TIMEOUT, SWAT_FALL } SwatEnd;
typedef struct SwatActor {
    SwatTag tag;
    SwatController controller;
    SwatArsenal arsenal;
    SwatEquipment gear;
    SwatRole role;
    SwatMind mind;
    bool rescued;
    float health;
    bool present, alive, last_interact;
    int visible_ticks, last_shot_tick;
    int target_actor, hearing_ticks;
    float heard_yaw, foot_distance;
    SwatHearingMemory hearing;
    b3Pos last_foot_position;
    b3Pos last_seen, tracer_start, tracer_end;
} SwatActor;
// Authoritative hit reaction state (gear.flinch_ticks, replicated). The weight
// is 1 on the impact tick and falls linearly to 0; presentation may use it.
static inline bool swat_actor_flinching(const SwatActor* a) { return a->alive && a->gear.flinch_ticks>0; }
static inline float swat_actor_flinch_weight(const SwatActor* a) {
    return swat_actor_flinching(a) ? (float)a->gear.flinch_ticks/SWAT_FLINCH_TICKS : 0;
}

typedef struct SwatConfig {
    int max_ticks;
    bool randomize, hostile_fire,tactical_rules;
    int mission;
    uint32_t layout_seed;
    int generator,difficulty,squad_bots;
    int motel_occupancy;
    // Rounds in each officer's seated primary magazine at spawn, for
    // starting part-used (held-out reload cases); 0 keeps it full. The
    // chambered round and the spare magazines stay full.
    int officer_magazine;
} SwatConfig;

#define SWAT_STAGING_RADIUS 2.5f
// One force violation is one engagement: protected-person hits by the same
// officer/sniper on the same victim merge while each follows the previous one
// within this window (1 s). A pause of a full second, another shooter or
// another victim starts a new violation; unlawful_damage still sums every hit.
#define SWAT_ROE_ENGAGEMENT_TICKS 60
typedef struct SwatScenarioProgress {
    int threats,unsecured,evacuees,evidence,officers_away;
    int civilian_casualties,officer_casualties;
    bool secured;
    // Dead suspects, for the debrief. Derived from replicated actor state.
    int suspect_casualties;
} SwatScenarioProgress;

typedef struct SwatEvents {
    float hostile_damage, civilian_damage, officer_damage;
    int shots, destroyed, hostile_down;
} SwatEvents;

// Per-actor force ledger for one tick, cleared with SwatEvents. Damage is
// attributed by the pre-impact state of the victim: protected means a civilian
// or a surrendered/restrained person. Reward bookkeeping only; never observed,
// never replicated, and it does not change any rule. downed counts kills the
// actor attributed; downed_protected marks the actor itself going down while
// protected, so rewards can tell a neutralised threat from an executed one.
typedef struct SwatForce {
    float taken, threat_damage, officer_damage, protected_damage;
    int downed;
    bool downed_protected;
    // Lethal hits the actor dealt, as an officer or sniper, on a person who
    // was not a perceived lethal threat (roe.h): every such hit, unmerged.
    int lethal;
} SwatForce;

typedef struct SwatSim {
    SwatWorld world;
    SwatActor actors[SWAT_MAX_ACTORS];
    int actor_count, tick, episode;
    uint32_t rng;
    uint32_t reset_seed;
    SwatConfig config;
    SwatEnd end;
    SwatEvents events, totals;
    SwatForce force[SWAT_MAX_ACTORS];
    SwatSoundLog sounds;
    b3Pos extraction;
    SwatProjectile projectiles[SWAT_MAX_PROJECTILES];
    SwatSniper snipers[SWAT_SNIPERS];
    int commander_actor,last_sniper_order[SWAT_MAX_ACTORS];
    SwatMissionDef mission;
    SwatLayout layout;
    SwatDevice devices[SWAT_MAX_DEVICES];
    // Built on the first route request. Solo movement courses need no grid.
    SwatNavigation* navigation;
    SwatEvidence evidence[SWAT_MAX_ACTORS];
    SwatDebrief debrief;
    // Authority-only ROE engagement record: tick+1 of the latest protected-person
    // hit by [shooter][victim], 0 for none. Never replicated; replay rebuilds it.
    int roe_hit_tick[SWAT_MAX_ACTORS][SWAT_MAX_ACTORS];
    int last_squad_order;
    // Authority-only perceived ROE state per person (roe.h) and the lethal
    // engagement record by [shooter][victim], tick + 1 as above. roe_refused
    // counts perceived changes the transition table rejected (expected zero).
    // Never replicated; input replay rebuilds them.
    SwatRoePerson roe[SWAT_MAX_ACTORS];
    int roe_lethal_tick[SWAT_MAX_ACTORS][SWAT_MAX_ACTORS];
    int roe_refused;
    // Authority-only team record per suspect: an officer or sniper has seen
    // them at large, and the team has not since secured them (cuffed, or
    // killed by police fire). Annex obs 29 counts it. Never replicated;
    // input replay rebuilds it.
    bool sighted[SWAT_MAX_ACTORS];
} SwatSim;

SwatConfig swat_default_config(void);
void swat_config_human(SwatConfig* config);
SwatScenarioProgress swat_sim_progress(const SwatSim* sim);
void swat_sim_init(SwatSim* sim, SwatConfig config, uint32_t seed);
void swat_sim_reset(SwatSim* sim);
void swat_sim_close(SwatSim* sim);
int swat_player_actor(int slot);
// Stable officer slots: slot zero is actor zero; NPC indices remain unchanged.
bool swat_sim_set_player(SwatSim* sim, int slot, bool present);
void swat_sim_spawn_actor(SwatSim* sim, int index, SwatRole role, b3Pos feet, float yaw);
void swat_sim_bot_inputs(SwatSim* sim, SwatInput inputs[SWAT_MAX_ACTORS]);
// Sight of a person: any of three body probes (head, chest, hips, tried in
// that order) within range and half_fov of the observer's aim reaches the
// person first and is lit well enough to see. The head alone decides nothing.
bool swat_sim_actor_visible(const SwatSim*,int observer,int target,float range,float half_fov);
// What an observer sees of a person's hands. HIDDEN when the person is not
// seen (swat_sim_actor_visible) or the hands cannot be seen; WEAPON when the
// person holds a weapon (swat_actor_shows_weapon) and either hand or the
// muzzle of the drawn pose is in sight; EMPTY when both empty hands are in
// sight, and always for a person seen with hands up or cuffed. The target's
// own body never hides their hands; other people and the world do.
typedef enum SwatHands { SWAT_HANDS_HIDDEN, SWAT_HANDS_EMPTY, SWAT_HANDS_WEAPON } SwatHands;
SwatHands swat_sim_hands_seen(const SwatSim*,int observer,int target,float range,float half_fov);
// The hands test alone, for callers that have just established sight of the
// person with swat_sim_actor_visible (indices must be valid).
SwatHands swat_sim_hands_seen_visible(const SwatSim*,int observer,int target);
// A currently aimed sight ray must reach the target before any other person.
// This authorizes an attempt; the actual shot still traverses physical materials.
bool swat_sim_firing_line_clear(const SwatSim*,int observer,int target,float range);
// Visible suspect weapon, seen in hand (swat_sim_hands_seen is WEAPON), raised
// and aimed at the observer or another visible protected person. Uses current
// pose/optical clearance, never private target or ammo.
bool swat_sim_observed_weapon_threat(const SwatSim*,int observer,int target,float range);
// Whether a squad bot officer sees a lawful target, by the rule roe_classify
// applies (roe.h): a weapon seen in the person's hand and raised at someone
// (*aimed, when given), the person seen firing within SWAT_ROE_FIRING_TICKS
// with hands not seen empty (the muzzle flash proves the weapon even behind a
// chest-high wall), or a weapon seen in hand SWAT_ROE_CHALLENGE_TICKS after
// the latest command that reached the person, when this officer challenged
// them (mind.challenge_actor) and they are not running away from it. Hidden
// or empty hands are otherwise never a lethal threat; the head never decides.
bool swat_sim_officer_threat(const SwatSim*,int officer,int target,bool* aimed);
// Whether this officer, aimed as it is now, has the person in hand reach or in
// sight close to the aim: the person it is focused on within 9 m, or one with
// the head, chest or hips in sight within 9 m and 15 degrees of the aim. A
// squad bot asks this before it challenges its target; the training annex
// (no tactical rules) delivers an officer's command to exactly these people.
bool swat_sim_command_reaches(const SwatSim*,int officer,int person);
// A spoken command is heard, not aimed. It reaches every present, alive,
// unrestrained suspect or civilian not yet surrendered whose acoustic path
// (swat_acoustic_path) from the emitted command sound to their eye carries at
// least this gain. Measured with the shout's strength 1.2: a closed door at
// 6 m passes 0.017 or more and an open doorway at 12 m 0.05 or more, while two
// closed doors pass at most 0.007 and an exterior wall at 10 m at most 0.008.
#define SWAT_COMMAND_HEARD_GAIN 0.01f
// Delivers the command sound commander has just emitted (the newest event in
// the sound log, read back as emitted so hearing and delivery agree) to
// everyone who hears it, through the surrender model and the ROE record.
// ball is the communication device that spoke it, or NULL for a voice.
void swat_sim_command_heard(SwatSim*,int commander,const SwatDevice* ball);
void swat_sim_step_inputs(SwatSim* sim, const SwatInput inputs[SWAT_MAX_ACTORS]);
// Solo locomotion courses: same controller/door/physics step, no combat or
// presentation bookkeeping. Inputs are movement plus automatic door interact.
void swat_sim_step_movement(SwatSim* sim, const SwatInput inputs[SWAT_MAX_ACTORS]);
void swat_sim_step(SwatSim* sim, const SwatInput* player);
void swat_sim_observe(const SwatSim* sim, int actor, float out[SWAT_OBS_SIZE]);
SwatInput swat_decode_action(const float actions[SWAT_ACTION_HEADS]);
int swat_sim_hostiles(const SwatSim* sim);
// Suspects the team has seen at large and not yet secured (SwatSim.sighted).
int swat_sim_sighted_threats(const SwatSim* sim);
// Whether a person visibly holds a weapon in hand, as the game draws them:
// an officer or sniper, or a suspect who has neither dropped the weapon nor
// put their hands up (a feigned surrender hides it). Dead people hold none.
bool swat_actor_shows_weapon(const SwatActor* actor);
void swat_sim_shoot(SwatSim* sim, int actor, b3Pos origin, b3Vec3 direction, SwatShot shot);
void swat_sim_damage_actor(SwatSim* sim, int victim, int shooter, float damage);
void swat_sim_damage_region(SwatSim* sim,int victim,int shooter,float damage,SwatHitRegion region,bool less_lethal);
SwatHitRegion swat_sim_hit_region(const SwatActor* actor,b3Pos point);
b3Pos swat_sim_inspection_camera(const SwatSim* sim,int actor);
b3Vec3 swat_sim_inspection_direction(const SwatSim* sim,int actor);
// A weapon pose as drawn. The clearance cast runs from the eye to the
// full-reach muzzle (swat_pose); when it is blocked the weapon is pulled
// back to the cast's contact (swat_pose_retract), so the drawn muzzle stays on
// the near side of the blocking geometry. blocked, when given, receives the
// cast result: the simulation stores it as muzzle_blocked, which stops firing,
// so every shot leaves the full-reach muzzle and the clamp never moves one.
SwatPose swat_sim_weapon_pose(const SwatSim* sim,const SwatController* controller,const SwatArsenal* arsenal,bool* blocked);
int swat_sim_unsecured(const SwatSim* sim);
const char* swat_end_name(SwatEnd end);
const SwatMissionDef* swat_sim_mission(const SwatSim* sim);

#ifdef __cplusplus
}
#endif
#endif
