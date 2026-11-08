// Abilities and status effects, in the spirit of Path of Titans: each
// species has a pool of four abilities on keys 1-3 with cooldowns and
// stamina costs. Slots 1 and 2 are the species' own; slot 3 holds one of two
// choices unlocked with marks (bots and policies have every choice). The sim
// calls the hooks below; the data table is CRET_ABILITIES.
#ifndef CRET_ABILITIES_H
#define CRET_ABILITIES_H
#include "sim.h"
#ifdef __cplusplus
extern "C" {
#endif

// Strike: a special attack (the species' attack swing with multipliers).
// Area: an effect on animals around the caster. Self: a buff. Leap: a jump.
enum { CRET_ABK_STRIKE, CRET_ABK_AREA, CRET_ABK_SELF, CRET_ABK_LEAP };

typedef struct CretAbility {
    const char* id;            // save files
    const char* name;          // HUD
    const char* desc;          // HUD tooltip and docs
    uint8_t kind;              // CRET_ABK_*
    uint8_t anim;              // CRET_ACT_* the animal plays (attack swing, call); 0: none
    float cooldown, stamina;   // s; stamina spent (0..1)
    // Strike: multipliers on the species' attack and what a hit inflicts
    // (bone: chance of a break; stun s; slow fraction for slow_s s; dash:
    // runs at this many times its sprint speed until the hit lands).
    float dmg, reach, bleed, knock, bone, stun, slow, slow_s, dash;
    // Area and self effects: radius (m), duration (s), speed factor, armour
    // all round, extra armour on the flanks, damage reflected onto biters,
    // healing (fraction of max health over the duration).
    float radius, duration, speed, armor, flank, reflect, heal;
} CretAbility;
extern const CretAbility CRET_ABILITIES[CRET_AB_COUNT];

// Per species: slot 1, slot 2, then the two slot-3 choices and their prices in marks.
typedef struct CretSpeciesAbilities { uint8_t ab[4]; int cost[2]; } CretSpeciesAbilities;
extern const CretSpeciesAbilities CRET_SPECIES_ABILITIES[CRET_SPECIES];

// Progress of a slot: no marks or quests; persons start with slots 1 and 2,
// bots and policies with everything unlocked.
void cret_prog_reset(CretSim* s, int i, bool person);
// Slots 1 and 2 from the species, slot 3 the first unlocked choice (kept if still valid).
void cret_loadout_default(CretSim* s, int i);
// CRET_AB_* in slot 0..2 (0: empty).
int cret_ability_of(const CretSim* s, int i, int slot);
bool cret_ability_ready(const CretSim* s, int i, int slot);
// Slot-3 choice 0 or 1 of the agent's species: unlocked (bought already or
// free), price, and whether it is equipped.
bool cret_choice_unlocked(const CretSim* s, int i, int choice);
// Buys (spending marks) and equips slot-3 choice 0 or 1; equips it if owned. False if unaffordable.
bool cret_choice_take(CretSim* s, int i, int choice);

// Sim hooks (sim.c).
void cret_status_input(CretSim* s, int i, CretInput* in);   // before movement: stun, latch, casting
void cret_status_step(CretSim* s);                           // after movement: latches, timers, mending
float cret_status_speed(const CretAgent* a);                 // movement speed factor
float cret_strike_dash(const CretAgent* a);                  // > 0: charging at this times sprint speed
// Damage modifiers before armour: returns the scaled damage, 0 for a dodge;
// *extra_armor gets the defensive armour against this attacker.
float cret_status_damage(const CretSim* s, int attacker, int victim, float hp, float* extra_armor);
// After a hit lands (hp dealt): strike effects, reflection, latch knock-off.
void cret_status_hit(CretSim* s, int attacker, int victim, float hp, float raw, float knock);
// A latching raptor lets go (thrown: shaken off, a short stun).
void cret_release_latch(CretSim* s, int i, bool thrown);
#ifdef __cplusplus
}
#endif
#endif
