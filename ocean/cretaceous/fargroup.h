// Far groups (docs/ecosystem.md, Far groups; docs/ecosystem_next.md section 1).
// A herd, a pack or a lone animal of scripted bots with no person within
// CretConfig.far_range is folded into one record: it leaves the physics
// world and the per-tick sim, and fargroup.c moves it along what its drive
// wants once a second, at the pace of its slowest member. Its members eat,
// drink, burn, age, grow, starve and die of thirst by the full sim's own
// equations (sim.c survive_body, eco.c), graze the same biomass grid and
// drink at the same water. A predator record that reaches a herd record
// rolls the hunt with the odds measured by tools/ecoprobe.c (a herd at its
// nest, which never folds, is hunted by the same rule). When a person comes
// within far_range - CRET_FAR_HYST (or a policy's animal within
// CRET_FAR_MEET_M) the members are placed back around the record, in their
// formation, on dry ground clear of props and of each other.
//
// While folded a member keeps its whole CretAgent in its slot (every field
// the full sim needs back: species, growth, health, food, water, fat, age,
// sex, group, memory of water, abilities and the rest) with alive false, so
// every loop of the full sim skips it; CretFar.of says it is folded, not dead.
// Never folded: a person, a slot a policy drives (CretFar.learned), an
// animal on a nest task (building, guarding or raiding a nest, a group with a
// nest, carrying food home) and anything busy (hunting, fleeing, fighting,
// hurt, latched, swimming, down).
// Off unless far_range > 0: the trainer (cretaceous.h) leaves it off.
// sim.h includes this after it defines CRET_MAX_AGENTS.
#ifndef CRET_FARGROUP_H
#define CRET_FARGROUP_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CRET_FAR_RANGE 400.0f     // m: no person this close, a calm group is folded (CretConfig.far_range; 0: off)
#define CRET_FAR_HYST 50.0f       // m: a person within far_range minus this unfolds it
#define CRET_FAR_MEET_M 80.0f     // m: a record this close to a person or a policy's slot unfolds (what they meet is the full sim) ...
#define CRET_FAR_APART_M 120.0f   // ... and a group folds only with none within this
#define CRET_FAR_MEMBERS 8        // the largest group (the herds' cap)
#define CRET_FAR_SPREAD_M 80.0f   // a group folds only with every member within this of its middle ...
#define CRET_FAR_FORM_M 30.0f     // ... and keeps its formation within this
#define CRET_FAR_SENSE_M 120.0f   // m: a predator record notices a herd record this close (sight, hearing and scent together)
#define CRET_FAR_STRIKE_M 30.0f   // m: it rolls the hunt from this close
#define CRET_FAR_GIVE_UP_S 120    // s a herd it failed on (or that was not worth it) is left alone
#define CRET_FAR_FLEE_S 40        // s a herd that was hunted trots away
#define CRET_FAR_NEAR_M 20.0f     // m: blocked this close to where it goes, a record counts as there (a carcass or a shore at the water's edge)
#define CRET_FAR_STUCK_S 10       // s blocked farther away before it gives the target up
#define CRET_FAR_NEST_HOLD_S 120  // s a group unfolded to build a nest stays in the full sim
#define CRET_FAR_DEFEND_M 400.0f  // m: a herd's defender that kills a hunter (fargroup.c, FAR_DEFEND) is one this close

typedef struct CretFarGroup {
    uint8_t active, n, diet;      // n members; diet of the group (CRET_DIET_*)
    uint8_t group;                // the members' group id (0: a lone animal)
    uint8_t drive;                // BOT_* (eco.h): what the record does (wander, graze, seek water or drink, rest, hunt, feed, flee)
    uint8_t stuck;                // s it has been blocked short of its target
    uint8_t member[CRET_FAR_MEMBERS];
    float fx[CRET_FAR_MEMBERS], fz[CRET_FAR_MEMBERS], fyaw[CRET_FAR_MEMBERS];   // formation: forward and side offset from the record, yaw against its heading
    float x, z, heading, speed;   // where the record is, which way it faces, m/s it moves at now
    float tx, tz;                 // where its drive takes it
    int32_t target;               // hunt: prey agent + 1; feed: carcass slot + 1; flee: predator record + 1; seek water: 1 water known, -1 on a leg toward it (0: none)
    int32_t timer;                // s left in a timed drive (flee, a wander leg)
    int32_t lost, lost_until;     // prey agent + 1 it failed on (or found not worth it): it leaves that animal ...
    uint8_t lost_group;           // ... and its group alone until lost_until
    int32_t since;                // tick it was folded
} CretFarGroup;

typedef struct CretFar {
    uint32_t rng;                         // its own stream (CretConfig.seed): folding never draws from the sim's
    uint8_t of[CRET_MAX_AGENTS];          // agent -> record + 1 (0: in the full sim)
    uint8_t learned[CRET_MAX_AGENTS];     // slots a policy drives: never folded
    int32_t hold[CRET_MAX_AGENTS];        // tick before which an agent handed to the full sim to build a nest is not folded again
    CretFarGroup g[CRET_MAX_AGENTS];      // records (at most one per agent)
    int folds, unfolds, hunts, kills, deaths;   // counts since reset (logs, tests, ecoprobe)
} CretFar;

struct CretSim;
// Once a tick at the end of cret_sim_step: folds, steps and unfolds once a second.
void cret_far_step(struct CretSim* s);
// Agent i is folded into a far group (alive false, but not dead).
bool cret_far_folded(const struct CretSim* s, int i);
// Agent i is in the world: alive in the full sim or folded.
bool cret_far_present(const struct CretSim* s, int i);
// Folds the unit of agent i now if it may fold (tests, the debug console); returns its record or -1.
int cret_far_fold(struct CretSim* s, int i);
// Unfolds record r now: its members back in the full sim around it. Returns members placed.
int cret_far_unfold(struct CretSim* s, int r);
// Unfolds every record (far_range turned off, a reset).
void cret_far_unfold_all(struct CretSim* s);
// The folded record of diet (and species, -1: any) best to bring to (x, z) for
// the population director: the largest, then the nearest (-1: none).
int cret_far_pick(const struct CretSim* s, int diet, int species, float x, float z);
// Records active and members folded now.
int cret_far_count(const struct CretSim* s, int* members);
#ifdef __cplusplus
}
#endif
#endif
