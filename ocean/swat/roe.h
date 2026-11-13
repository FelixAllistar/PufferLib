#ifndef SWAT_ROE_H
#define SWAT_ROE_H
#include <stdbool.h>

// Rules-of-engagement state of one suspect or civilian, as the police team
// could perceive it. Only what an officer or sniper sees (pose, a raised
// weapon, muzzle flash, movement, hands) or does with their own hands (cuffs)
// changes it; hidden intent, ammunition and private AI state never do. A
// suspect feigning surrender is therefore SURRENDERED until the weapon comes
// out.
//
// Sight of any substantial part of the body (head, chest or hips,
// swat_sim_actor_visible) is sight of the person: enough to see posture,
// movement and muzzle flash. Whether the person is armed is decided by the
// hands alone (swat_sim_hands_seen): a weapon seen in hand, or both hands
// seen empty. A person whose hands cannot be seen stays unidentified, which
// is never on its own grounds for lethal force. The state is authority-only: never observed by policies, never
// replicated, and input replay rebuilds it exactly.
typedef enum SwatRoeState {
    SWAT_ROE_UNKNOWN,        // Not yet assessed, hands never seen, or seen armed but not yet challenged or threatening.
    SWAT_ROE_COMPLIANT,      // Hands seen empty, not running from the police.
    SWAT_ROE_SURRENDERED,    // Seen kneeling with hands shown.
    SWAT_ROE_RESTRAINED,     // Cuffed by an officer.
    SWAT_ROE_FLEEING,        // Seen running from the police without a shown lethal threat.
    SWAT_ROE_HOSTILE_ARMED,  // Weapon seen raised at a person, or seen armed after ignoring a command.
    SWAT_ROE_HOSTILE_FIRING, // Seen firing within the last SWAT_ROE_FIRING_TICKS.
    SWAT_ROE_DOWN,           // Dead. Terminal.
    SWAT_ROE_STATES
} SwatRoeState;

// A shot seen within 2 s keeps the shooter HOSTILE_FIRING; afterwards an
// armed shooter stays HOSTILE_ARMED until they surrender or are disarmed.
#define SWAT_ROE_FIRING_TICKS 120
// A person seen armed who was ordered to comply and has not, within 1.5 s of
// the latest command they heard, is a noncompliant armed threat.
#define SWAT_ROE_CHALLENGE_TICKS 90
// Horizontal speed away from the nearest observing officer that reads as flight.
#define SWAT_ROE_FLEE_SPEED 1.6f

typedef struct SwatRoePerson {
    SwatRoeState state;
    int since;          // Tick of the latest transition.
    int seen_tick;      // Tick + 1 when an officer or sniper last saw this person.
    int challenged_tick;// Tick + 1 of the latest command this person heard.
    // The police have seen a weapon in this person's hand (or seen them fire)
    // and have not since seen the hands empty.
    bool armed_seen;
} SwatRoePerson;

struct SwatSim;
const char* swat_roe_state_name(SwatRoeState state);
// The documented transition table. Every perceived change must be legal;
// the step refuses (and counts) an illegal one rather than applying it.
bool swat_roe_transition_legal(SwatRoeState from,SwatRoeState to);
// Lethal force is authorised only against these perceived states.
bool swat_roe_lethal_threat(SwatRoeState state);
// Officers/snipers perceive every person once per tick (tactical rules only).
void swat_roe_step(struct SwatSim* sim);
// An officer's command reached this person: a challenge on the record.
void swat_roe_challenge(struct SwatSim* sim,int person);
// Judge one hit before it is applied. Lethal (not less-lethal) force by an
// officer or sniper against a person who is not a perceived lethal threat is a
// lethal ROE violation, merged into engagements like roe_violations.
void swat_roe_force(struct SwatSim* sim,int victim,int shooter,bool less_lethal);
#endif
