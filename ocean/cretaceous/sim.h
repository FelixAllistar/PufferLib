// Authoritative fixed-step simulation. Humans, network peers, scripted bots
// and policies all drive agents through CretInput; rendering never changes it.
#ifndef CRET_SIM_H
#define CRET_SIM_H
#include <stdbool.h>
#include <stdint.h>
#include "world.h"
#include "critters.h"
#include "scent.h"
#ifdef __cplusplus
extern "C" {
#endif

#define CRET_TICK_HZ 30
#define CRET_SNAP_TICKS 4                     // a bite catches small prey this early in the swing (0.13 s), see resolve_snap
#define CRET_GRAVITY 9.8f                     // m/s^2: falls, leaps, knockback
#define CRET_WINDED 0.25f                      // scripted animals: stamina under this slows the trot ...
#define CRET_WINDED_TROT 0.4f                 // ... down to this share of the way from a walk to a trot (at 0 stamina)
#define CRET_HEAL 0.002f                       // health regained a second (fraction of max) when fed and watered ...
#define CRET_HEAL_REST 0.008f                  // ... and when resting
#define CRET_HURT 0.6f                         // under this fraction of its health an animal slows ...
#define CRET_HURT_SLOW 0.2f                    // ... by up to this much (at no health left)
#define CRET_KNOCK 2.2f                        // knockback speed (m/s) x attacker's share of the two masses x the blow's knock ...
#define CRET_KNOCK_MAX 4.5f                    // ... at most this
#define CRET_KNOCK_LIFT 4.0f                   // a shove over this lifts the feet, a little (only the heaviest blows on a much smaller animal)
#define CRET_AIR_GRAVITY 1.6f                 // jumps and other airborne motion fall this much faster (snappy hops)
#define CRET_DT (1.0f / CRET_TICK_HZ)
#define CRET_MAX_AGENTS 128
#define CRET_KILL_CREDIT_TICKS (10 * CRET_TICK_HZ)   // a death this long after the last hit credits nobody, unless its wound still bleeds (sim.c, cret_killer)
#define CRET_WOUND_OPEN 0.002f                        // bleed (health per second, as a share of max health) at or above which a wound is open: the same line below which an animal starts to heal
#define CRET_DEFEND_TICKS (10 * CRET_TICK_HZ)        // a herbivore's blow pays only on a carnivore that struck it or its group this recently

// Policy contract (bump CRET_CONTRACT_VERSION when obs/actions change).
// 2: species, health, hunger, thirst, growth, bleed, forage and carcass in
//    the self obs; diet, size and health per neighbour; an action head
//    (none, attack, eat or drink).
// 3: six species; broken bones, group size and rally point in the self obs;
//    same-group and same-species flags per neighbour; call in the action head.
// 4: abilities. Appended after the contract 3 obs: status effects, ability
//    cooldowns and readiness, quest target and progress, nest (self, 22), then
//    stunned, afraid, latched and marked per neighbour (6 x 4). The action
//    head gains ability 1, 2 and 3 (7 choices).
// 5: aimed attacks. The action head gains the secondary attack (8 choices:
//    none, attack, eat or drink, call, ability 1, 2, 3, attack 2); a sixth
//    head aims the head (or the tail) in 5 bins {-1, -0.5, 0, 0.5, 1} of the
//    species' neck arc. Observations are unchanged (243).
// 6: wounds. Appended after the contract 4 obs: the animal's own wounds,
//    leg, body and head severity 0..1 (3 floats, wounds.c). Actions are
//    unchanged. The reward is unchanged (a body wound's bleeding counts as
//    damage taken, as every bleed does).
#define CRET_CONTRACT_VERSION 6
#define CRET_PATCH 7                          // egocentric terrain patch (PATCH x PATCH)
#define CRET_PATCH_SPACING 6.0f               // metres between patch samples
#define CRET_NEIGHBORS 6
#define CRET_NEIGHBOR_OBS 10
#define CRET_SELF_OBS 39
#define CRET_OBS_V3 (CRET_SELF_OBS + CRET_PATCH * CRET_PATCH * 2 + CRET_NEIGHBORS * CRET_NEIGHBOR_OBS)   // 197 (contract 3)
#define CRET_SELF_OBS4 22                     // contract 4 self block: status, abilities, quest, nest
#define CRET_NEIGHBOR_OBS4 4                  // contract 4 per neighbour: stunned, afraid, latched, marked
#define CRET_OBS_V5 (CRET_OBS_V3 + CRET_SELF_OBS4 + CRET_NEIGHBORS * CRET_NEIGHBOR_OBS4)   // 243 (contracts 4 and 5)
#define CRET_SELF_OBS6 3                      // contract 6 self block: leg, body and head wound severity
#define CRET_OBS_SIZE (CRET_OBS_V5 + CRET_SELF_OBS6)   // 246
// Offsets into the obs for structured encoders (cretaceous_encoder.cu); they name the layout above, never change it.
#define CRET_OBS_SPECIES 16                   // self obs: species one-hot (CRET_SPECIES floats)
#define CRET_OBS_PATCH CRET_SELF_OBS          // patch cells, forward-major (row = forward, col = lateral), {height, water} each
#define CRET_OBS_NEIGHBOR (CRET_SELF_OBS + CRET_PATCH * CRET_PATCH * 2)   // neighbour records; feature 0 = present
#define CRET_ACTION_HEADS 6
#define CRET_ACTION_SIZES {5, 5, 3, 2, 8, 5}  // throttle, turn, strafe, jump, action (none, attack, eat/drink, call, ability 1, 2, 3, attack 2), aim
#define CRET_MAX_CARCASSES 32
#define CRET_AB_SLOTS 3                       // ability keys 1-3
#define CRET_MAX_NESTS 16

// Species: adult body size, gaits, stamina, health and attack. Juveniles
// scale these with growth (see cret_growth_scale in sim.c). Assets
// (render/dino.c) are looked up by `asset`; a missing model draws as a
// capsule, so the sim never depends on the art.
enum { CRET_SP_RAPTOR, CRET_SP_STEGO, CRET_SP_TREX, CRET_SP_PACHY, CRET_SP_SAUROPELTA, CRET_SP_GALLI, CRET_SPECIES };
enum { CRET_DIET_HERBIVORE, CRET_DIET_CARNIVORE };
// Secondary attack (right mouse, CRET_ACT_ATTACK2). A tail swipe or sweep
// hits everything in a wide arc behind and beside the animal (reach from the
// body centre) and throws it; a slash is a quick pass-through rake at a
// sprint that does not slow the animal.
enum { CRET_ALT_NONE, CRET_ALT_TAIL, CRET_ALT_SLASH };
typedef struct CretAltAttack {
    int kind;                                    // CRET_ALT_*
    float damage, reach, arc;                    // adult hp; m (tail: from the body centre; slash: past the snout); half-angle of the arc (rad)
    float cooldown, hit_time, attack_time;       // s
    float knock, speed;                          // knockback multiplier (by mass ratio, like the bite); movement speed kept during the swing
} CretAltAttack;
typedef struct CretSpecies {
    const char* name; const char* asset; int diet;
    float length, height, radius, mass;          // m, m, capsule radius m, kg
    float walk, trot, sprint, swim;              // m/s
    float turn_rate, jump;                       // rad/s at rest, hop height m (0: cannot jump)
    float stamina_drain, stamina_regen;          // per second while sprinting / otherwise
    float health;                                // adult hit points
    float damage, reach, cooldown;               // attack: hp, m, s between attacks
    float hit_time, attack_time;                 // s into the attack when it lands (matches the clip), length of the attack
    int tail;                                    // 1: the attack hits behind (tail swing), 0: in front (bite, headbutt, bash)
    float knock;                                 // knockback multiplier (a headbutt throws)
    float armor;                                 // fraction of incoming damage the hide blocks
    float bleed_resist;                          // fraction of bleeding the hide prevents
    float thorns;                                // fraction of damage reflected onto an attacker in front contact (spikes)
    int group_max;                               // bots: pack or herd size (0: loners)
    float neck_arc;                              // rad the aim turns the attack (and the head) to either side; tail attacks aim the rear arc
    float swing_speed;                           // movement speed kept until the primary attack has landed (never rooted)
    CretAltAttack alt;                           // secondary attack
    // Mass: ground speed ramps toward the gait at `accel` (falling off near
    // the sprint), brakes at `decel`, and turns are held by `grip`: the yaw
    // rate at speed v is at most grip / v, a turning radius of v^2 / grip.
    float accel, decel, grip;                    // m/s^2 (adult)
} CretSpecies;
extern const CretSpecies CRET_SPECIES_TABLE[CRET_SPECIES];

// Actions. Eat grazes (herbivores) or eats a carcass in reach (carnivores),
// and turns into drink when facing water; attack bites or swings the tail.
// Call: rallies the caller's group, and forms or joins a group with the
// same species nearby.
// Nest: an adult builds one by holding it for 5 s (progress.c).
// Attack 2: the species' secondary attack (CretAltAttack). Attacks never
// root the animal, and an attack pressed up to CRET_ATTACK_BUFFER ticks
// before it is ready starts as soon as it is.
enum { CRET_ACT_NONE, CRET_ACT_EAT, CRET_ACT_DRINK, CRET_ACT_CALL, CRET_ACT_ATTACK, CRET_ACT_REST, CRET_ACT_NEST, CRET_ACT_ATTACK2 };
#define CRET_ACT_MAX CRET_ACT_ATTACK2                 // last valid action
#define CRET_ATTACK_BUFFER 9                          // ticks (0.3 s)
#define CRET_LUNGE 0.25f                              // body lengths a primary blow in front carries the attacker toward its target over the wind-up (sim.c, lunge_step)
#define CRET_SQUARE_AIM 0.98f                        // a bite, headbutt or bash aimed at least this far round (of the neck arc) turns a standing attacker's body that way (sim.c, move_agent)
static inline bool cret_act_is_attack(int act) { return act == CRET_ACT_ATTACK || act == CRET_ACT_ATTACK2; }

// Abilities (abilities.c). Each species has a pool of four: slots 1 and 2
// are fixed, slot 3 holds one of two that are unlocked with marks.
enum { CRET_AB_NONE, CRET_AB_POUNCE, CRET_AB_SPRINT_BURST, CRET_AB_HAMSTRING, CRET_AB_FEAR_ROAR, CRET_AB_CRUSH_BITE, CRET_AB_REND_BITE,
       CRET_AB_TAIL_SLAM, CRET_AB_DEF_STANCE, CRET_AB_SPIKE_SWEEP, CRET_AB_CHARGE, CRET_AB_HERD_BELLOW, CRET_AB_HUNKER, CRET_AB_SHOVE,
       CRET_AB_DODGE_LEAP, CRET_AB_ALERT_CALL, CRET_AB_MEND, CRET_AB_COUNT };
// Status effects, timers in seconds. Latched: raptors hold on to this
// animal; latching: this raptor holds on to one. Burst, evade and mend are
// ability buffs.
enum { CRET_ST_STUN, CRET_ST_FEAR, CRET_ST_SLOW, CRET_ST_LATCHED, CRET_ST_LATCHING, CRET_ST_DEFENSIVE, CRET_ST_MARKED,
       CRET_ST_BURST, CRET_ST_EVADE, CRET_ST_MEND, CRET_ST_COUNT };

typedef struct CretInput {
    float forward;   // -1 back .. 1 forward (0.5 walk, 1 trot; sprint flag for the top gait)
    float strafe;    // -1 left .. 1 right
    float turn;      // -1 left .. 1 right (yaw rate scale)
    uint8_t sprint, jump;
    uint8_t action;  // CRET_ACT_*: started when standing (eat, drink, call, attack)
    uint8_t ability; // 1..3: use that ability slot (0: none)
    float aim;       // -1 .. 1: where the attack points, as a fraction of the species' neck arc to the
                     // left (-) or right (+) of the head; species whose primary is the tail aim the rear
    uint8_t carry;   // 1: pick up what lies at the mouth, or put down what it holds (a press: one tick
                     // toggles once). Bots and people only, like nest building: not in the policy head.
} CretInput;

// What an animal knows of another (eco.c, docs/ecosystem.md section 3):
// where it was and how it moved when last seen, heard or smelled.
#define CRET_KNOWN 8
typedef struct CretKnown {
    uint8_t who;          // agent + 1 (0: an empty slot)
    uint8_t looking;      // it was seen looking this way (head up, this animal in its view)
    uint8_t alarm;        // learned from a fleeing group member: taken as coming for it
    uint8_t pad;
    float x, z, vx, vz;   // position and velocity when last sensed
    int32_t tick;         // when last sensed
    int32_t since;        // from when it counts (noticed after the reaction delay)
} CretKnown;

// Wounds (wounds.c): a hard blow leaves a wound that outlasts the bleeding.
// A blow of at least CRET_WOUND_MIN of the victim's max health (after armour)
// wounds the place it came at: the legs (from behind, or from an attacker
// under CRET_WOUND_SMALL of its height), the head (from in front), else the
// body; an attacker CRET_WOUND_TALL times its height bites down on the body.
// Severity is the blow's share over CRET_WOUND_FULL, at most 1, and blows on
// the same place add up. One wound per place, so at most three. It heals
// over about a game day fed and resting, slower on the move or hungry, and
// worsens while starving or parched. Effects (scaled by severity): a leg
// wound slows the top speed and the turn, a body wound bleeds a little
// (under the open-wound line, so it does not stop healing or credit a kill)
// and slows the stamina's recovery, a head wound shortens the senses (smell,
// sight, hearing). Wounded animals leave blood on their trail (scent.c), which
// hunting bots follow (bots.c), and a herd waits a while for a member with a
// fresh leg wound. The thresholds come from the species table (README, Combat:
// wounds): 8% is above a raptor's bite on a grown pachycephalosaur (7%) and
// the nibbling of a pack on a stegosaur, tyrannosaur or sauropelta (1-3%), and
// below a raptor's bite on a raptor (22%) or a gallimimus (16%), a raptor's
// slash on a raptor (9%), a stegosaur's tail on a raptor (50%) and every
// tyrannosaur bite (27% of a stegosaur or a sauropelta, more of the rest).
enum { CRET_WOUND_LEG, CRET_WOUND_BODY, CRET_WOUND_HEAD, CRET_WOUND_PLACES };
typedef struct CretWound { float sev; int32_t tick; } CretWound;   // severity 0..1 (0: none); the tick of the last blow on it
#define CRET_WOUND_MIN 0.08f          // a blow of at least this share of max health leaves a wound
#define CRET_WOUND_FULL 0.4f          // a blow of this share (or wounds adding up to it) is the worst wound, severity 1
#define CRET_WOUND_SMALL 0.6f         // an attacker under this share of the victim's height wounds a leg
#define CRET_WOUND_TALL 1.6f          // an attacker this many times the victim's height wounds the body (it bites down)
#define CRET_WOUND_FRONT 0.87f        // rad (50 degrees): a blow from within this of the heading is to the head
#define CRET_WOUND_REAR 2.27f         // rad (130 degrees): a blow from beyond this is to the hind legs
#define CRET_WOUND_HEAL_REST 1.0f     // severity healed a game day when fed, watered and resting
#define CRET_WOUND_HEAL_MOVE 0.5f     // ... fed and watered, awake
#define CRET_WOUND_HEAL_HUNGRY 0.15f  // ... hungry or thirsty (food or water under 0.3)
#define CRET_WOUND_WORSEN 0.4f        // severity gained a game day while starving (gut empty, fat under a tenth) or parched
#define CRET_WOUND_LEG_SLOW 0.3f      // share of the top speed a leg wound of severity 1 takes away
#define CRET_WOUND_LEG_TURN 0.3f      // share of the turn rate a leg wound of severity 1 takes away
#define CRET_WOUND_BODY_BLEED 0.0006f // health a second (share of max) a body wound of severity 1 bleeds: under a third of a fed animal's healing
#define CRET_WOUND_BODY_TIRE 0.4f     // share of the stamina recovery a body wound of severity 1 takes away
#define CRET_WOUND_HEAD_SENSE 0.4f    // share of the sense ranges (smell, sight, hearing) a head wound of severity 1 takes away
#define CRET_WOUND_DRIP 0.25f         // a wound this bad drips blood on the trail (a body wound drips as an open bleed does); a multiple of 0.05 (net.c sends twentieths)
#define CRET_WOUND_LIMP 0.2f          // a leg wound this bad shows as a limp (render/dino.c)
#define CRET_WOUND_HERD_DAY 0.15f     // share of a game day a herd keeps to a walk for a member with a fresh leg wound

typedef struct CretAgent {
    bool alive, on_ground, swimming, sprinting;
    uint8_t species;       // CRET_SP_*
    uint8_t act;           // CRET_ACT_* in progress (0: none)
    float x, y, z;         // feet position (m)
    float yaw;             // radians, 0 = +x
    float vx, vy, vz;
    float speed;           // horizontal speed (m/s)
    float radius, height;  // collision capsule
    float walk_speed, trot_speed, sprint_speed, swim_speed, turn_rate, jump_speed;
    float stamina;         // 0..1
    float distance;        // metres travelled this life
    int ticks_alive, act_ticks;
    // Survival.
    float health, max_health;   // hp
    float food, water;          // 0 starving / parched .. 1 full (food: the gut, eco.h)
    float fat;                  // reserve, 0..1 of ECO_FAT_FRAC x mass (eco.h): fills while the gut is well filled, burns once it is empty
    float meal;                 // gut fraction eaten in the current meal (fades once eating stops)
    uint8_t digest;             // a carnivore digesting a big meal: its bot does not hunt until the gut is half empty
    float growth;               // 0.35 hatchling .. 1 adult; scales size, speed, health and damage
    float shape_growth;         // growth the collision capsule was built for
    float bleed;                // hp per second, decays
    int attack_cd;              // ticks until the next attack
    int attack2_cd;             // ticks until the next secondary attack
    uint8_t buf_act, buf_ticks; // an attack pressed early: CRET_ACT_ATTACK or ATTACK2, ticks it stays queued
    uint8_t snapped;            // this bite already caught small prey at the snap (sim.c, CRET_SNAP_TICKS)
    float aim;                  // the input's aim (-1..1), held for the swing and the head
    int hurt_ticks;             // > 0 just after a hit (flinch)
    int dead_ticks;             // ticks since death (alive == false)
    int last_attacker;          // agent that hit last (-1)
    int last_hit_tick;          // when it hit (kill credit expires CRET_KILL_CREDIT_TICKS after it)
    int struck, struck_tick;    // the last agent this one hit (-1) and when: is a herbivore that hits back defending?
    uint8_t struck_group;       // that agent's group at the time
    float broken;               // s until a broken bone heals (0: sound): slow, no sprint or jump
    float fall_vy;              // downward speed at the last landing (m/s), for fall injuries
    CretWound wound[CRET_WOUND_PLACES];   // by CRET_WOUND_* (wounds.c)
    // Groups: packs and herds. group = founder slot + 1 (0: alone).
    uint8_t group;
    int rally_ticks; float rally_x, rally_z;   // a group member called: where (bots go there)
    // Abilities and status effects (abilities.c); timers in seconds.
    float ab_cd[CRET_AB_SLOTS];     // until each slot is ready
    uint8_t strike;                 // CRET_AB_* special attack in progress (0: a plain attack)
    uint8_t latch;                  // pounce: agent + 1 this raptor holds on to (0: none)
    uint8_t latchers;               // raptors holding on to this animal
    uint8_t fear_src;               // agent + 1 that frightened this one
    uint8_t mark_by;                // group (or 100 + agent + 1) whose call marked this animal
    float st[CRET_ST_COUNT];        // CRET_ST_* timers
    float slow_amt;                 // speed lost while slowed (0..1)
    float def_armor, def_flank, def_reflect, def_speed;   // defensive: armour all round, extra on the flanks, damage reflected onto biters, speed factor
    float shake;                    // latching raptor: 0..1, thrown off at 1
    float latch_t;                  // s to the next latch bite
    uint8_t leap;                   // CRET_AB_* of a pounce or leap in flight (0: none)
    float leap_t;                   // s of that flight left (a pounce latches on contact)
    float latch_side;               // -1 / 1: which flank it holds
    float heal_rate;                // hp per second while mending
    float in_turn; uint8_t in_sprint, in_jump, in_act;   // this tick's input (shaking off raptors)
    int nest_ticks;                 // nest building progress
    // Carrying (sim.c, cret_carry_*): a carcass, a caught critter or an egg held in the mouth.
    uint8_t carry;                  // carcass slot + 1 (0: nothing)
    float carry_kg;                 // its weight when picked up: the load that slows the carrier
    // Events this tick (cleared at the start of cret_sim_step), for rewards.
    float ev_food, ev_water, ev_dealt, ev_taken;   // fullness gained; damage as a fraction of the victim's / own max health
    int ev_died, ev_kills, ev_quest;               // ev_quest: quests completed
    // The rewarded share of ev_dealt and ev_kills (sim.c, reward_worth): a carnivore's scaled by the
    // victim's mass against its own, a herbivore's dealt only when defending, never a kill or thorns.
    float ev_dealt_paid, ev_kills_paid;
    b3ShapeId shape;
    // Scripted bot state (cret_bot_input).
    int bot_state, bot_timer, bot_target; float goal_x, goal_z;
    float stuck_x, stuck_z; int stuck_ticks, escape_ticks; float escape_yaw;   // unsticking
    float avoid;                    // bots: heading offset (rad) round a trunk or rock ahead (steer)
    // Bots: perception and fight-or-flight (bot_perceive). A new threat or a hit
    // is noticed only after a reaction delay; a hit leaves the bot alert to its
    // attacker for a while. Agents are stored as slot + 1 (0: none).
    int bot_cue, bot_react, bot_cue_hit;   // waiting to be noticed, ticks until it is, 1: it hit this bot
    int bot_seen;                          // the threat the bot has noticed
    int bot_foe, bot_alert;                // the attacker it is alert to, ticks of alert left
    int bot_hurt;                          // hurt_ticks at the last bot tick (a rise is a new hit)
    // Ecosystem AI (eco.c, bots.c; never observed): what it knows of others,
    // its vigilance while feeding, and where it last found water.
    CretKnown known[CRET_KNOWN];
    float vig_t;                           // s to the next look up while feeding
    float head_up;                         // s left of the current look up
    float water_x, water_z; int water_tick;   // the last water found (water_tick 0: none yet)
    int eco_hurt;                          // hurt_ticks at the last eco step (a rise is a new hit: a fear mark)
    int bot_next;                          // tick of an early decision (an alarm, a hit), 0: none
    // Drives (bots.c, docs/ecosystem.md section 5).
    float fatigue;                         // 0 rested .. 1: rises while awake, falls three times as fast at rest
    float intake_avg;                      // kg/s eaten while grazing, averaged over a game hour (the marginal value rule)
    float graze_x, graze_z, graze_val;     // the ground it grazes or heads for, and its intake there / c_max
    uint8_t hunt_phase;                    // 0 approach, 1 stalk, 2 charge
    uint8_t hunt_blood;                    // this hunt began on the prey's blood trail (bots.c, blood_prey)
    uint8_t flee_run;                      // fleeing: a threat is coming inside the flight distance (a sprint, not a walk)
    float hunt_gap; int hunt_tick;         // the closest gap of the charge, and when it last closed
    int lost, lost_until;                  // prey given up (agent + 1), left alone until this tick
    int kill;                              // carcass slot + 1 of the kill it fed on (it guards it)
    // Knockdown (damage(), CRET_KD_*): ticks left lying on its side (the fall
    // included), then ticks left getting up; ticks before it can be knocked
    // down again; how long this fall lies in all (ticks, for the pose); the
    // side it fell toward (1: its right, the +z side at yaw 0; -1: its left).
    int down_ticks, getup_ticks, kd_immune, down_len, down_side;
    b3BodyId body;
} CretAgent;
// Knocked down or getting up: no moving, attacking, eating, drinking, calling
// or abilities (getting up it may turn).
// Wound effects (wounds.c, sim.h CRET_WOUND_*): factors on the top speed, the
// turn rate, the stamina recovery and the sense ranges (1: unhurt).
static inline float cret_wound_speed(const CretAgent* a) { return 1 - CRET_WOUND_LEG_SLOW * a->wound[CRET_WOUND_LEG].sev; }
static inline float cret_wound_turn(const CretAgent* a) { return 1 - CRET_WOUND_LEG_TURN * a->wound[CRET_WOUND_LEG].sev; }
static inline float cret_wound_regen(const CretAgent* a) { return 1 - CRET_WOUND_BODY_TIRE * a->wound[CRET_WOUND_BODY].sev; }
static inline float cret_wound_sense(const CretAgent* a) { return 1 - CRET_WOUND_HEAD_SENSE * a->wound[CRET_WOUND_HEAD].sev; }
// The worst of its wounds (0: none).
static inline float cret_wound_worst(const CretAgent* a) {
    float w = 0; for (int p = 0; p < CRET_WOUND_PLACES; p++) w = a->wound[p].sev > w ? a->wound[p].sev : w; return w;
}
static inline bool cret_down(const CretAgent* a) { return a->down_ticks > 0 || a->getup_ticks > 0; }
// The input a knocked-down animal keeps: nothing while it lies, the turn while it gets up.
static inline void cret_down_input(const CretAgent* a, CretInput* in) { float t = a->down_ticks > 0 ? 0 : in->turn; *in = (CretInput){0}; in->turn = t; }

// Knockdowns. A blow throws the victim off its feet when its strength (force
// in damage(): ma / (ma + mb) x the attack's knock x the ability's, the
// knockback before CRET_KNOCK scales it and CRET_KNOCK_MAX caps it) is at
// least CRET_KD_FORCE and the attacker clearly outweighs it: by
// CRET_KD_MASS_BLUNT for a blunt blow (a tail swing or swipe, or an attack
// whose knock is at least CRET_KD_BLUNT: a headbutt, a charge, a shoulder
// bash), by CRET_KD_MASS_BITE for anything else (a bite). The rule reads the
// strength rather than the knockback so that retuning the shove (CRET_KNOCK,
// CRET_KNOCK_MAX) does not move who knocks down whom. With the species table:
//   - a stegosaur's tail (0.98 on a raptor, 0.92 on a gallimimus) or its
//     sweep, a sauropelta's tail or bash (1.2-1.5) and a pachycephalosaur's
//     headbutt (1.8 and more on a raptor, 1.16 on a gallimimus, which it
//     outweighs by 12%) or charge knock down a raptor or a gallimimus;
//   - a tyrannosaur's bite (0.72 at 2.5x the mass) knocks down anything up
//     to a stegosaur at growth 0.86 (3.2 t); a grown stegosaur (5 t) stays up;
//   - a raptor never outweighs anything bigger than itself, so it never knocks
//     one down (its bite on a raptor hatchling it outweighs 2.5x can).
// Never in water and never while already down. The animal lies for
// CRET_KD_MIN to CRET_KD_MAX seconds, longer for a harder blow (strength from
// CRET_KD_FORCE up to CRET_KD_FORCE_MAX), then gets up over CRET_KD_GETUP
// seconds (it may turn), then cannot be knocked down again for
// CRET_KD_IMMUNE seconds, so two attackers taking turns keep it down at most
// (1.0 + 0.4) / 5.4 = 26% of the time. The times are short on purpose: with
// 1.2-2.5 s lying and a 0.7 s get-up a pack of three raptors beat a
// pachycephalosaur in 58% of staged fights instead of 87% (tools/fightprobe.c;
// README, Knockdowns). Lying it drops what it carries and lets go of a latch;
// the fall does no damage, blows on it do no extra damage, and a knockdown
// pays no reward of its own (only the blow's damage does, cret_reward_worth).
#define CRET_KD_FORCE 0.7f            // least blow strength that knocks down
#define CRET_KD_FORCE_MAX 1.8f        // strength at which it lies longest
#define CRET_KD_BLUNT 1.5f            // attack knock multiplier (times the ability's) that counts as a blunt blow
#define CRET_KD_MASS_BLUNT 1.1f       // a blunt blow knocks down an animal it outweighs this many times
#define CRET_KD_MASS_BITE 2.5f        // a bite knocks down an animal it outweighs this many times
#define CRET_KD_MASS_SWIPE 10.0f      // a side swipe of the tail (the secondary attack of an animal that fights head first) only floors one this much lighter:
                                      // a sauropelta (19x) or tyrannosaur still throws a raptor, a pachycephalosaur (5.6x) fleeing a pack does not
#define CRET_KD_MIN 0.5f              // s lying after a blow of strength CRET_KD_FORCE (the fall included)
#define CRET_KD_MAX 1.0f              // s lying after a blow of strength CRET_KD_FORCE_MAX or more
#define CRET_KD_GETUP 0.4f            // s getting up
#define CRET_KD_IMMUNE 4.0f           // s after getting up before it can be knocked down again
#define CRET_KD_FALL 0.35f            // s the fall onto its side takes (presentation: render/dino.c)
#define CRET_KD_GETUP_TICKS ((int)(CRET_KD_GETUP * CRET_TICK_HZ + 0.5f))

typedef struct CretConfig {
    uint32_t world_seed;
    int world_n;
    float world_cell;
    int num_agents;
    int max_ticks;
    float day_seconds;     // real seconds per in-game day
    uint32_t seed;
    int player_species;    // slot 0 (CRET_SP_*); other slots follow a fixed mix
    int respawn_ticks;     // after death (the RL binding uses 1)
    float growth_seconds;  // fed hatchling to adult
    float spawn_growth;    // growth at spawn; < 0: random adult-ish (0.55..1)
    float player_growth;   // slot 0 growth at spawn (< 0: as spawn_growth)
    float hunger_seconds, thirst_seconds;   // full to empty at rest
    float carcass_seconds; // a carcass lasts at most this long
    int weather;           // 1: cloud cover drifts between fair, overcast and rain spells; 0: fixed at cloud_cover
    float cloud_cover;     // starting (or fixed) cloud cover 0..1; rain above ~0.9 (render/view)
} CretConfig;

// Remains of a dead animal: carnivores eat the meat; it rots away. The same
// slots hold the small food a carnivore can carry: the uneaten rest of a
// caught critter and an egg taken from a nest (item). Anything light enough
// can be carried in the mouth (carrier); a carried item keeps rotting, and
// nobody eats from it until it is put down.
// A piece (CRET_ITEM_MEAT) is meat a carnivore tore off a body too heavy to
// lift, to carry it to its young (cret_carry_tear).
enum { CRET_ITEM_BODY, CRET_ITEM_CRITTER, CRET_ITEM_EGG, CRET_ITEM_MEAT, CRET_ITEM_KINDS };
#define CRET_CARRY_RATIO 0.25f        // a carrier lifts up to this fraction of its own mass
#define CRET_CARRY_SLOW 0.8f          // speed lost per unit of load ratio (a full load: 20% slower)
#define CRET_CARRY_KNOCK 0.08f        // a blow of this fraction of max health makes the carrier drop it
typedef struct CretCarcass {
    bool active; uint8_t species;  // body: who died; egg: whose egg
    float x, y, z, yaw, growth;
    float meat, max_meat;  // kg
    int age;               // ticks since death
    int agent;             // who died (for the death animation), -1 unknown
    uint8_t item;          // CRET_ITEM_*
    uint8_t critter;       // CRET_CR_* of a caught critter
    uint8_t carrier;       // agent + 1 holding it in the mouth (0: on the ground)
} CretCarcass;

// Quests (progress.c): server-generated, pointing at named places (world.h
// CretPoi) or at a spot. A group quest is shared by the members of a group.
// Every kind is offered only to animals that can do it (progress.c says how):
//   drink, graze (herbivores), hunt (prey within a mass ratio), patrol,
//   defend (herbivores that can wound a predator), catch (small prey,
//   critters.h), companion (stay near your group or your own kind), ability
//   (use ability 1 near something), explore (reach a place not visited yet),
//   nest (build one), rest (stand still or rest at a place), scavenge (eat
//   meat from carcasses), raid (eat eggs from another group's nest), guard
//   (stay by your own nest while it has eggs).
// A quest with `spot` set leads to a point (spot_x, spot_z, within spot_r)
// instead of a named place: a drink or graze spot close by, a carcass, a nest.
// `chain` marks a step of a new character's first steps (progress.c).
enum { CRET_Q_NONE, CRET_Q_DRINK, CRET_Q_GATHER, CRET_Q_HUNT, CRET_Q_PATROL, CRET_Q_DEFEND, CRET_Q_CATCH,
       CRET_Q_COMPANION, CRET_Q_ABILITY, CRET_Q_EXPLORE, CRET_Q_NEST, CRET_Q_REST, CRET_Q_SCAVENGE, CRET_Q_RAID, CRET_Q_GUARD, CRET_Q_COUNT };
#define CRET_CHAIN_STEPS 6         // first steps: eat, drink, companion, ability 1, a place, a nest (adults)
typedef struct CretQuest {
    uint8_t kind;          // CRET_Q_*
    uint8_t group;         // group quest: the group id (0: personal)
    uint8_t poi[3], step, steps;   // target places (patrol: visited in order)
    uint8_t spot; float spot_x, spot_z, spot_r;   // a point instead of poi[0]
    uint8_t species;       // hunt: prey species; companion: the species to stay near
    uint8_t chain;         // first steps: step + 1 (0: an ordinary quest)
    uint8_t last;          // the kind of the quest before (kept while kind is NONE, so the next differs)
    float progress, goal;  // seconds, kills, kg or damage (see progress.c)
    float xp; int marks;   // reward: growth and marks
    int expires;           // tick it is withdrawn
    int wait;              // ticks until the next quest is offered (kind NONE)
} CretQuest;

// Per slot and kept across deaths: the character's progress.
typedef struct CretProgress {
    char name[24];         // character name ("" for bots)
    int marks;             // currency
    uint32_t unlocked;     // slot-3 choices bought: bit species * 2 + choice
    uint8_t loadout[CRET_AB_SLOTS];   // CRET_AB_* in each slot (0: empty)
    CretQuest quest;
    int quests_done;
    uint8_t chain;         // first steps done (CRET_CHAIN_STEPS: all; bots skip them)
    uint32_t visited;      // named places visited: bit = CretWorld.pois index
    uint8_t load_pending;  // a client sent its name: the host loads that character
} CretProgress;

// A nest: group members respawn there as juveniles while it has eggs;
// predators eat the eggs.
typedef struct CretNest {
    bool active; uint8_t species, group, owner;   // owner: agent + 1
    float x, y, z;
    int eggs;
    int egg_ticks, idle_ticks;
} CretNest;

// Counters for balance runs (tools/balance.c) and logs; kept across resets.
// A death's cause: predation (a blow or its bleeding with an attacker known),
// starvation, thirst, or anything else (falls, crushing, bleeding with no
// attacker).
enum { CRET_DEATH_PREDATION, CRET_DEATH_STARVED, CRET_DEATH_THIRST, CRET_DEATH_OTHER, CRET_DEATH_CAUSES };
typedef struct CretStats {
    int kills[CRET_SPECIES][CRET_SPECIES];   // [killer][victim]
    int deaths[CRET_SPECIES], deaths_env[CRET_SPECIES];   // all deaths; deaths with no attacker (hunger, thirst, falls)
    int ability_uses[CRET_AB_COUNT], ability_hits[CRET_AB_COUNT];
    int latches, shake_offs, stuns, fears, marked;
    int quests[CRET_Q_COUNT], group_quests, marks_earned, chain_steps;   // chain_steps: first steps completed (progress.c)
    int nests_built, eggs_laid, eggs_eaten, nest_spawns;
    int pickups, carried_home, forced_drops;   // carrying: items picked up; put down at the carrier's own nest; dropped by a blow, stun or death
    int death_cause[CRET_SPECIES][CRET_DEATH_CAUSES];   // deaths by CRET_DEATH_* (tools/ecoprobe.c)
    double eaten_kg[CRET_SPECIES], burned_kg[CRET_SPECIES];   // food eaten and burned, kg, all animals of a species
    int knockdowns[CRET_SPECIES][CRET_SPECIES];   // [attacker][victim]: blows that knocked the victim down
    int wounds[CRET_SPECIES][CRET_WOUND_PLACES];  // [victim][place]: blows that left (or deepened) a wound (wounds.c)
    int blood_hunts[CRET_SPECIES], blood_kills[CRET_SPECIES];   // [hunter]: bot hunts begun on a blood trail; of them, ended with the prey dead (bots.c)
    CretCritterStats critters;               // spawned, eaten and flushed small prey (critters.c)
} CretStats;

// Hit log: every landed blow, for presentation only (hit feedback, blood,
// flinches). The sim writes it and never reads it, so it changes no rule,
// reward or observation; clients fill theirs from snapshots (net.c, v10).
// A ring: entry k lives at hits[k % CRET_HIT_LOG], hit_count counts them all.
// CRET_HIT_DOWN follows the blow that knocked the victim down (dmg: the time
// it lies, as a fraction of CRET_KD_MAX).
enum { CRET_HIT_BITE, CRET_HIT_TAIL, CRET_HIT_SLASH, CRET_HIT_BUTT, CRET_HIT_ABILITY, CRET_HIT_THORNS, CRET_HIT_OTHER, CRET_HIT_CRUSH, CRET_HIT_DOWN, CRET_HIT_KINDS };
#define CRET_HIT_LOG 64
typedef struct CretHit {
    int32_t tick;
    uint8_t attacker, victim, kind;   // CRET_HIT_*
    uint8_t dmg;                      // damage as a fraction of the victim's max health, 0..255
} CretHit;

// Movable props (dynprops.c): boulders, big stones, logs and driftwood are
// Box3D dynamic bodies, asleep at their scattered pose until something
// pushes or hits them. A prop that has moved gets a record here (in the
// struct, so the renderer's copy of the sim carries the poses); props
// without one sit where the scatter put them. Clients fill theirs from
// snapshots (net.c, v11) and never step the physics.
#define CRET_MAX_DYN 256
#define CRET_DYN_AWAKE_MAX 32          // awake props per sim; the slowest beyond it are put to sleep
typedef struct CretDynProp {
    int32_t id;                        // collider id (CretWorld.props index)
    uint8_t awake;
    int8_t pusher;                     // agent that pushed it last (-1)
    uint8_t hit_agent[4];              // crush cooldowns: agent + 1 and the tick it was hit
    int32_t hit_tick[4];
    int32_t push_tick, touched;        // tick of the last push; of the last movement
    int32_t awake_ticks;               // physics steps awake
    float p[3], q[4], v[3];            // pose (body origin, rotation x y z w) and velocity after the last step
    float pp[3], pq[4];                // pose before the last step (renderer interpolation)
    b3BodyId body;                     // null while no agent is near
} CretDynProp;

// The landscape of fear (eco.c, docs/ecosystem.md section 4): places where a
// herbivore was attacked or a predator passed, fading over a game day.
#define CRET_FEAR_MARKS 128
typedef struct CretFearMark { float x, z, w; int32_t tick; } CretFearMark;   // w 0: an empty slot

// Grazed plant cells (eco.c, docs/ecosystem.md section 2): biomass b (a
// fraction of the cell's capacity) as of `tick`; cells not stored are full.
#define CRET_PLANT_SLOTS 1024          // an open-addressed table keyed by cell + 1 (a power of two)
typedef struct CretPlantCell { int32_t cell; float b; int32_t tick; } CretPlantCell;

// Lag compensation (the net server's, net.c and docs/net.md): a client sees
// other animals about a round trip and an interpolation delay in the past, so
// the server judges that client's swings against where their targets were
// then. It keeps the poses a hit test reads, per agent, for the last
// CRET_REWIND_HIST ticks, and per agent how many ticks back its swings look
// (back, which the server validates: net.h, CRET_REWIND_MAX; the sim never
// looks further back than its history, CRET_REWIND_HIST - 1 ticks). Without
// it (CretSim.rewind NULL: the trainer, bots, solo play, replays) or with
// back 0, every hit is judged at the present, exactly as before.
#define CRET_REWIND_HIST 16
typedef struct CretPose {
    float x, y, z, yaw, radius, height, growth;   // what body_dist, the arc and the height test read
    int32_t ticks_alive;                          // a respawn starts it again: the pose of an earlier life is not hit
    uint8_t species, alive;
} CretPose;
typedef struct CretRewind {
    CretPose pose[CRET_REWIND_HIST][CRET_MAX_AGENTS];   // by tick % CRET_REWIND_HIST: the state after that tick's step
    int32_t tick[CRET_REWIND_HIST];                     // the tick each row holds (-1: none yet)
    float back[CRET_MAX_AGENTS];                        // ticks an agent's swings judge targets in the past (0: the present)
} CretRewind;
// Clears the history (rows empty, every back 0).
void cret_rewind_init(CretRewind* r);
// Stores every agent's pose at s->tick (the state the next step starts from).
void cret_rewind_record(CretRewind* r, const CretSim* s);

typedef struct CretSim {
    CretConfig cfg;
    CretWorld* world;
    b3WorldId phys;
    CretAgent agents[CRET_MAX_AGENTS];
    int tick;
    float time_of_day;     // 0..1, 0.25 sunrise, 0.5 noon
    uint32_t rng;
    CretPropStream props;  // prop colliders near agents (world.c)
    CretCarcass carcasses[CRET_MAX_CARCASSES];
    uint8_t species_pick[CRET_MAX_AGENTS];   // species chosen for a slot + 1 (0: the default mix)
    float cloud_cover;     // 0..1, server-authoritative weather (clients get it in snapshots)
    float cloud_target; int weather_ticks; uint32_t weather_rng;   // the current weather spell
    uint8_t human[CRET_MAX_AGENTS];          // slot driven by a person (network peers; slot 0 always): spawns like the player
    uint8_t hold[CRET_MAX_AGENTS];           // skip this agent's movement this tick (a network player whose input has not arrived)
    CretRewind* rewind;                      // lag compensation (the net server's; NULL: every hit judged at the present)
    // Progression (abilities.c, progress.c).
    CretProgress prog[CRET_MAX_AGENTS];
    CretQuest gquest[CRET_MAX_AGENTS + 1];   // group quests by group id
    CretNest nests[CRET_MAX_NESTS];
    CretStats stats;
    CretHit hits[CRET_HIT_LOG]; uint32_t hit_count;   // presentation-only hit log (above)
    // Movable props (above): records of the props that moved, the ground body they roll on.
    CretDynProp dyn[CRET_MAX_DYN]; int ndyn, dyn_awake, dyn_step_tick; bool dyn_kick;
    b3BodyId ground;
    // Small prey around people (critters.h): the pool, how many are active, densities.
    CretCritter critters[CRET_MAX_CRITTERS]; int ncrit;
    CretCritterConfig crit_cfg;
    // Scent trails (scent.h): the last places each critter slot and each agent passed.
    CretTrail crit_trail[CRET_MAX_CRITTERS], agent_trail[CRET_MAX_AGENTS];
    CretSign signs[CRET_SIGNS]; int sign_head; uint8_t sign_step[CRET_MAX_AGENTS];   // footprints and dung (scent.h); each animal's trail points so far, for the spacing and the foot
    CretBlood blood[CRET_BLOOD_MARKS]; int blood_head; uint8_t blood_skip[CRET_MAX_AGENTS];   // blood marks (scent.h), and each animal's trail points since its last one
    // Plants as a resource (eco.c): the grazed cells.
    CretPlantCell plants[CRET_PLANT_SLOTS]; int nplants;
    // The landscape of fear (eco.c): a ring of marks.
    CretFearMark fear[CRET_FEAR_MARKS]; int fear_head;
    // Each group's living mass and size, from the last eco step (eco.c; derived: rebuilt every tick).
    float group_kg[CRET_MAX_AGENTS + 1]; uint8_t group_n[CRET_MAX_AGENTS + 1];
} CretSim;

CretConfig cret_default_config(void);
// Species of an agent slot (slot 0 is the player's choice).
int cret_slot_species(const CretConfig* cfg, int slot);
int cret_sim_init(CretSim* s, CretConfig cfg);
void cret_sim_reset(CretSim* s);
void cret_sim_step(CretSim* s, const CretInput* inputs);
void cret_sim_close(CretSim* s);
void cret_sim_respawn(CretSim* s, int i);
void cret_sim_observe(const CretSim* s, int i, float* obs);
// Every agent's observation, obs[i] for agent i (NULL: skipped): the same
// values as cret_sim_observe agent by agent, with what they all read about the
// others (positions, group sizes, masses) gathered once.
void cret_sim_observe_all(const CretSim* s, float* const* obs);
// Switches an agent's species (the player's choice) and respawns it.
void cret_sim_set_species(CretSim* s, int i, int species);
// One agent's movement for one tick and the timers that gate it (attack
// cooldown, flinch, rally), as cret_sim_step does it. The server uses it to
// catch up on a network player's queued inputs; clients use it with
// predict = true for client-side prediction (no effect on other agents:
// attacks, calls and fall injuries are left to the server).
void cret_sim_move(CretSim* s, int i, const CretInput* in, bool predict);
// Whether agent a's swing in progress (a->act: the primary or the secondary
// attack, with its special strike) reaches agent b where both stand: the test
// resolve_attack and resolve_alt make (reach, arc, height, packmates spared;
// the primary then lands on the nearest such body). Read-only: netbench and
// the lag-compensation test judge with it what a client saw.
bool cret_swing_reaches(const CretAgent* a, const CretAgent* b);
// Ground for presentation (foot placement): the terrain, or the top of a prop
// collider (a rock or a log) hit by a ray down from y_top; normal (optional)
// is the surface normal there. Read-only: no effect on the sim.
float cret_ground_at(const CretSim* s, float x, float z, float y_top, float normal[3]);
// Puts agent i's collision body where the agent is (parked when dead) and
// resizes its capsule to the species and growth (clients applying snapshots).
void cret_sim_place(CretSim* s, int i);
// A saved character's animal (character.c): growth, health/food/water
// fractions and, when walkable and clear, its position.
void cret_sim_restore(CretSim* s, int i, float growth, float health, float food, float water, bool has_pos, float x, float z);
// Size factor of a growth stage (1 at adult).
float cret_growth_scale(float growth);
// Take-off speed (m/s) for a hop of `height` metres (CretSpecies.jump).
float cret_jump_speed(float height);
// Sets size, speeds, turning, jump and max health from species and growth
// (keeps the health fraction).
void cret_apply_growth(CretAgent* a);
// Members of a group (0: none).
int cret_group_size(const CretSim* s, int group);
// An unused group id for agent i to found a group with (its slot + 1 when free).
int cret_group_new(const CretSim* s, int i);
// Edible ground plants at a point, 0..1: the static richness (the capacity
// of the ground; the observation reads it). What is there now is cret_graze_at (eco.h).
float cret_forage(const CretWorld* w, float x, float z);
CretInput cret_decode_action(const float* actions);
// Shared with abilities.c and progress.c.
void cret_damage(CretSim* s, int attacker, int victim, float hp);
void cret_kill(CretSim* s, int i);
// Who is credited if agent i dies now: its last attacker, if that hit landed within CRET_KILL_CREDIT_TICKS or the
// wound is still open (bleed at least CRET_WOUND_OPEN of max health); else -1.
int cret_killer(const CretSim* s, int i);
// What a blow on (or the death of) victim is worth to attacker's reward, 0..1 (sim.c says how).
float cret_reward_worth(const CretSim* s, int attacker, int victim);
float cret_body_dist(const CretAgent* b, float px, float pz);
void cret_strike_point(const CretAgent* a, float* px, float* pz);
// World yaw an attack points along: the head (or the tail, for tail attacks) turned by the aim.
float cret_attack_dir(const CretAgent* a, bool rear);
// The attack swinging now (or the species' primary) hits from behind: tail blows are not spiked or reflected.
bool cret_rear_attack(const CretAgent* a);
// Aim (-1..1) that turns agent a's primary attack toward world point (x, z).
float cret_aim_at(const CretAgent* a, float x, float z);
// The heading toward `want` (rad, `dist` m away) around trunks and rocks, as bots steer (sim.c).
float cret_avoid_heading(CretSim* s, int i, float want, float dist);
float cret_mass(const CretAgent* a);
// Knockdown pose for presentation (render/dino.c, render/view.c): false when
// agent a stands; else *since is the seconds since it fell, *up how far it has
// got up (0 lying .. 1 standing) and *side the side it fell toward (1 right, -1 left).
bool cret_down_pose(const CretAgent* a, float* since, float* up, float* side);
// Appends to the hit log (presentation only).
void cret_log_hit(CretSim* s, int attacker, int victim, int kind, float frac);
bool cret_water_ahead(const CretSim* s, const CretAgent* a);
bool cret_walkable(const CretWorld* w, float x, float z);
// The slot is a person (the local player or a network peer), not a bot.
bool cret_is_person(const CretSim* s, int i);
bool cret_predator_near(const CretSim* s, float x, float z, float r, int species);
// Seconds a carcass lies before it is gone (longer for a big body).
float cret_carcass_life(const CretSim* s, const CretCarcass* k);
// Movable props (dynprops.c). The record of collider id (-1: none; it sits at its scattered pose).
int cret_dyn_find(const CretSim* s, int id);
// Puts movable prop id at pose p, q (x, y, z, w): creates its record and
// moves its body. Clients apply snapshots with it; on the server (tests,
// debug scenarios) awake = true lets the physics take it from there.
void cret_dyn_set(CretSim* s, int id, const float p[3], const float q[4], bool awake);
// Drops prop id's record: back at its scattered pose (clients, when the server dropped it).
void cret_dyn_drop(CretSim* s, int id);
// The HUD's eat prompt: 1 food in reach, 2 a carcass within 20 m (*dist), 3 small
// prey in reach of a bite, 4 small prey within 20 m (*dist), 0 none (sim.c).
int cret_food_state(const CretSim* s, int i, float* dist);
// Brings a movable prop of `kind` (PROP_BOULDER, PROP_STONE, PROP_LOG) whose
// mass is nearest kg to rest at (x, z), turned to yaw: one nobody holds,
// scattered at least 150 m away (nothing in view vanishes). Returns its
// collider id, or -1. Debug console `prop` and scenario `prop =`.
int cret_dyn_place(CretSim* s, int kind, float kg, float x, float z, float yaw);
// Bodies for records near agents, none for asleep ones far away (the server's step does this).
void cret_dyn_stream(CretSim* s);
// Renderer: moved props a fraction alpha (0..1) of the way from their pose a
// tick ago to the current one (out is a copy of the sim).
void cret_dyn_lerp(CretSim* out, float alpha);
// Species push strength (multiplies mass x g; dynprops.c).
float cret_push_strength(int species);
// Carrying. The weight agent i would lift for carcass slot c (kg; a body
// weighs twice its meat, an item its meat).
float cret_item_kg(const CretCarcass* k);
// Most agent i can lift (kg): CRET_CARRY_RATIO of its mass, carnivores only (0 otherwise).
float cret_carry_limit(const CretAgent* a);
// What a carry press would do now (the HUD's prompt): 1 put down carcass slot
// *what, 2 pick up carcass slot *what, 3 take an egg from nest *what, 0
// nothing (*what -1, or -(slot + 2) when a carcass at the mouth is too heavy).
int cret_carry_state(const CretSim* s, int i, int* what);
// Agent i tears a piece it can lift (at most kg) off the body in carcass slot
// c and holds it in its mouth: a new CRET_ITEM_MEAT item, the body losing the
// meat. Returns the piece's slot, or -1 (nothing torn: a full mouth, too far,
// not a body, no room).
int cret_carry_tear(CretSim* s, int i, int c, float kg);
// Puts down what agent i carries, at its mouth on the ground (forced: a blow,
// stun or death made it drop). No effect when it carries nothing.
void cret_carry_drop(CretSim* s, int i, bool forced);
// The point at agent i's mouth where a carried item hangs (and pickups reach from).
void cret_mouth_point(const CretAgent* a, float* x, float* y, float* z);
// Renderer: puts every carried item at its carrier's mouth (s is a drawn,
// interpolated copy of the sim, so the item moves with the drawn animal).
void cret_carry_follow(CretSim* s);
// Simple scripted wanderer used for bots until policies take over.
CretInput cret_bot_input(CretSim* s, int i);
// Bots: agent i's herd keeps to a walk for a member with a fresh leg wound (bots.c, CRET_WOUND_HERD_DAY).
bool cret_herd_waits(const CretSim* s, int i);
#ifdef __cplusplus
}
#endif
#endif
