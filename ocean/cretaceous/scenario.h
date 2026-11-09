// Scripted test scenarios (no raylib): a small ini format in
// ocean/cretaceous/config/scenarios/NAME.ini that places the player and a
// few animals with behaviours, sets the time and weather, and optionally
// drives the player with a scripted controller. The game AI plays every
// animal whose behaviour is `bot`. The scenario sim holds only these animals
// (no ambient population, no respawns), on the normal island.
//   ./cretaceous play scenario NAME          watch (or take over with M)
//   ./cretaceous capture DIR --scenario NAME  windowless mp4 + stills + summary
//
// Format (keys in any order; # or ; start a comment):
//   [scenario]
//   title = Raptor against a stegosaur
//   seconds = 40                 # length (the end condition may stop it sooner)
//   hour = 10.5                  # time of day, 0..24
//   weather = clear              # clear | overcast | rain
//   place = open                 # open flat ground near the island centre, or "X Z"
//   camera = side                # follow (behind the player) | side | orbit | high
//   end = death stego            # time | death NAME (one of them dies) | wipe NAME (all of them dead)
//   [player]                     # slot 0 (optional; default an idle raptor off to the side)
//   species = raptor
//   at = -14 0                   # metres from the origin (x, z)
//   facing = stego               # degrees (0 = +x) or an actor name to face
//   growth = 1
//   control = attack stego       # see below
//   abilities = on               # the controller uses abilities 1 and 2 when they fit
//   god = off
//   [actor stego]                # any number of named groups (up to 12)
//   species = stego
//   count = 1
//   at = 6 0
//   spread = 12                  # count > 1: scattered within this radius
//   facing = 180
//   behaviour = bot              # bot | dummy | passive | hostile | script
//   target = player              # hostile: who it attacks (default the player)
//   control = ...                # script: a controller, as for the player
//   group = on                   # count > 1: a pack or herd (default on)
// Controllers: idle | bot | walk X Z | attack NAME | circle NAME [radius] |
// flee NAME | swipe NAME | hitrun NAME [away_seconds]
// (circle: strafes around the target and strikes in reach; swipe: keeps the
// target beside or behind and uses the secondary attack; hitrun: strikes,
// runs off for away_seconds once a blow lands, comes back).
#ifndef CRET_SCENARIO_H
#define CRET_SCENARIO_H
#include "sim.h"
#include "cheats.h"
#ifdef __cplusplus
extern "C" {
#endif

#define CRET_SCN_MAX_ACTORS 12
#define CRET_SCN_MAX_COUNT 16
#define CRET_SCN_DIR "ocean/cretaceous/config/scenarios"
enum { SCN_CTRL_IDLE, SCN_CTRL_BOT, SCN_CTRL_WALK, SCN_CTRL_ATTACK, SCN_CTRL_CIRCLE, SCN_CTRL_FLEE, SCN_CTRL_SWIPE, SCN_CTRL_HITRUN, SCN_CTRL_KINDS };
enum { SCN_END_TIME, SCN_END_DEATH, SCN_END_WIPE };
enum { SCN_CAM_FOLLOW, SCN_CAM_SIDE, SCN_CAM_ORBIT, SCN_CAM_HIGH, SCN_CAM_KINDS };
extern const char* const CRET_SCN_CTRL_NAMES[SCN_CTRL_KINDS];
extern const char* const CRET_SCN_CAM_NAMES[SCN_CAM_KINDS];

typedef struct CretScnActor {
    char name[24];
    int species, count, behaviour;      // CRET_SP_*, 1..16, CRET_ACTOR_*
    float x, z, spread, growth;
    float facing; char face[24];        // degrees, unless face names an actor
    char target[24];                    // hostile target ("player" or an actor)
    int ctrl; char ctrl_target[24]; float p0, p1;   // controller and its numbers (walk: x z; circle: radius; hitrun: away s)
    bool abilities, god, group;
} CretScnActor;

typedef struct CretScenario {
    char name[48], title[96];
    float seconds, hour; int weather;   // weather -1: as the sim has it
    bool open; float px, pz;            // origin: open ground near the centre, or this point
    int camera, end; char end_target[24];
    bool has_player; CretScnActor player;
    CretScnActor actor[CRET_SCN_MAX_ACTORS]; int nactor;
    struct { int kind; float x, z, kg, yaw; } prop[8]; int nprop;   // prop = boulder|stone|log X Z [KG] [YAW degrees]
} CretScenario;

typedef struct CretScnStats { int n, hits, deaths, kills, abilities, attacks; float dealt, taken; } CretScnStats;

typedef struct CretScnRun {
    CretScenario sc;
    float ox, oz;                       // origin on the island
    int nslot[CRET_SCN_MAX_ACTORS + 1], slot[CRET_SCN_MAX_ACTORS + 1][CRET_SCN_MAX_COUNT];   // [0]: player
    int group_of[CRET_MAX_AGENTS];      // scenario group per agent slot (0 player, k + 1 actor k, -1 none)
    CretScnStats st[CRET_SCN_MAX_ACTORS + 1];
    CretCheats cheats;
    int tick, end_tick; bool done; char ended[64];
    uint32_t hits_seen; uint8_t was_alive[CRET_MAX_AGENTS]; float prev_cd[CRET_MAX_AGENTS][CRET_AB_SLOTS]; int prev_atk_cd[CRET_MAX_AGENTS];
    int hr_phase[CRET_MAX_AGENTS]; float hr_t[CRET_MAX_AGENTS]; int hr_hits[CRET_MAX_AGENTS];   // hitrun controller state
    bool manual;                        // the player is driven by a person (play scenario, M)
    float stuck_t[CRET_MAX_AGENTS], side_t[CRET_MAX_AGENTS];   // unstick: seconds pushing without moving, seconds left sidestepping
} CretScnRun;

// Parses scenario text (name: for messages). False with a message in err.
bool cret_scenario_parse(const char* text, const char* name, CretScenario* sc, char* err, int nerr);
// Loads CRET_SCN_DIR/NAME.ini (or a path ending in .ini).
bool cret_scenario_load(const char* name, CretScenario* sc, char* err, int nerr);
// Names of the scenarios in CRET_SCN_DIR (sorted); returns the count.
int cret_scenario_list(char names[][48], int max);
// The sim config for a scenario: base world, only its animals, no respawns, no weather drift.
CretConfig cret_scenario_config(const CretScenario* sc, CretConfig base);
// Open, flat, dry ground near the island centre (as the dinosaur capture shots use).
bool cret_scenario_find_open(const CretWorld* w, float* x, float* z);
// Places everyone on a sim built from cret_scenario_config. False on failure.
bool cret_scenario_start(CretScnRun* r, const CretScenario* sc, CretSim* s);
// Inputs for every slot (actors, controllers; the player's unless manual).
void cret_scenario_inputs(CretScnRun* r, CretSim* s, CretInput* inputs);
// After cret_sim_step: statistics and the end condition (r->done).
void cret_scenario_after_step(CretScnRun* r, CretSim* s);
// Short summary table; returns its length.
int cret_scenario_summary(const CretScnRun* r, char* out, int n);
// Centre of the live scenario animals (for cameras); false if none.
bool cret_scenario_centre(const CretScnRun* r, const CretSim* s, float* x, float* z, float* radius);
#ifdef __cplusplus
}
#endif
#endif
