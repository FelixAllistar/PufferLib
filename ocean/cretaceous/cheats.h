// Developer cheats at the sim level (no raylib, no GL): the debug console's
// command table, parser and tab completion, and the commands that change the
// sim (spawn, kill, heal, god, tp, time, weather, speed, freeze, grow,
// species). debug.c draws the console and runs the view commands (overlays,
// graphics); scenario.c reuses the actors. Tests: tests/test_debug.c.
//
// Spawned animals take over a bot slot (the one farthest from every person,
// or one waiting to respawn), so the slot count and the network protocol are
// unchanged; when a spawned animal dies its slot goes back to the normal mix.
// Actors (what drives a slot):
//   bot      the game AI (cret_bot_input)
//   dummy    stands still, never attacks; turns slowly toward a blow and flinches
//   passive  wanders slowly; flees for a few seconds when hurt; never attacks
//   hostile  hunts the player (or a set target) and attacks in reach
//   script   inputs come from a scenario controller (scenario.c)
// Debug actors never starve or thirst, and the population director leaves them alone.
#ifndef CRET_CHEATS_H
#define CRET_CHEATS_H
#include "sim.h"
#include "population.h"
#ifdef __cplusplus
extern "C" {
#endif

enum { CRET_ACTOR_NONE, CRET_ACTOR_BOT, CRET_ACTOR_DUMMY, CRET_ACTOR_PASSIVE, CRET_ACTOR_HOSTILE, CRET_ACTOR_SCRIPT, CRET_ACTOR_KINDS };
extern const char* const CRET_ACTOR_NAMES[CRET_ACTOR_KINDS];

// ---------------------------------------------------------------- commands
enum {
    CMD_HELP, CMD_CLEAR, CMD_SPAWN, CMD_KILL, CMD_HEAL, CMD_GOD, CMD_TP, CMD_TIME, CMD_WEATHER, CMD_SPEED, CMD_FREEZE, CMD_GROW, CMD_SPECIES, CMD_POP, CMD_PROP,
    // view commands (debug.c)
    CMD_COLLIDERS, CMD_LABELS, CMD_NAVDEBUG, CMD_FPS, CMD_PROFILE, CMD_NEAR,
    CMD_PRESET, CMD_SHADOWS, CMD_CLOUDS, CMD_VEG, CMD_GRASS, CMD_WATER, CMD_FOG, CMD_WIREFRAME,
    CMD_COUNT
};
#define CRET_CMD_MAX_ARGS 4
typedef struct CretCmd {
    int id;                        // CMD_*
    int argc;                      // arguments given
    char arg[CRET_CMD_MAX_ARGS][24];   // lower-cased words
    float num[CRET_CMD_MAX_ARGS];      // numeric value of each argument (NAN if not a number)
    int species;                   // spawn, tp nearest, species: CRET_SP_* (-1: random / none)
    int actor;                     // spawn: CRET_ACTOR_* (default dummy)
    int count;                     // spawn n (1..16)
    int toggle;                    // on/off arguments: 1 on, 0 off, -1 toggle
} CretCmd;
typedef struct CretCmdInfo { const char* name; const char* usage; const char* help; bool cheat, view; } CretCmdInfo;
extern const CretCmdInfo CRET_CMDS[CMD_COUNT];

// Parses one console line. False (with a message in err) on an unknown
// command, a missing or bad argument or a value out of range.
bool cret_cmd_parse(const char* line, CretCmd* out, char* err, int nerr);
// Tab completion of the line's last word: commands, then each command's
// words (species, actors, on/off, presets...). Writes the completed line to
// out (the longest common prefix when several match) and returns how many
// candidates matched; their names go to list (up to maxlist) if given.
int cret_cmd_complete(const char* line, char* out, int nout, char list[][24], int maxlist);
// Species by asset or full name (case-insensitive), "random" -> -2; -1 if unknown.
int cret_species_by_name(const char* name);

// ---------------------------------------------------------------- actors and sim cheats
typedef struct CretCheats {
    uint8_t actor[CRET_MAX_AGENTS];     // CRET_ACTOR_* driving the slot (NONE: as the game drives it)
    int target[CRET_MAX_AGENTS];        // hostile: who to attack (-1: the nearest person)
    float flee_t[CRET_MAX_AGENTS];      // passive: s left fleeing
    float wander[CRET_MAX_AGENTS];      // passive: heading it drifts along
    int last_hurt[CRET_MAX_AGENTS];     // hurt_ticks seen last tick (a new blow)
    bool god, freeze;
    float speed;                        // sim time scale (the caller steps the sim this much faster)
    uint32_t rng;
    CretPop* pop;                       // the director, told to leave debug actors alone (may be NULL)
    // Counters (scenario summaries).
    int spawned;
} CretCheats;

// Where the console's player stands and looks (filled by the caller).
typedef struct CretCheatEnv {
    int me;                 // the local player's slot
    bool authority;         // solo or host (cheats allowed)
    bool has_aim; float aim[3];   // the ground point under the crosshair
    int target;             // the animal under the crosshair (-1: none)
    float cam_yaw;          // facing (spawns turn toward the player)
    bool cut;               // out: the player moved (the camera should cut)
} CretCheatEnv;

void cret_cheats_init(CretCheats* c, CretPop* pop, uint32_t seed);
// Takes a bot slot for a new animal of `species` driven by `actor`, standing
// at (x, z) facing yaw, adult. Returns the slot or -1 if none is free.
int cret_cheat_spawn(CretCheats* c, CretSim* s, int species, int actor, float x, float z, float yaw, int me);
// Hands a slot back to the game (its next life is a normal bot).
void cret_cheat_release(CretCheats* c, CretSim* s, int i);
// Overrides inputs for debug actors and freeze (call after the bots' inputs, before cret_sim_step).
void cret_cheats_inputs(CretCheats* c, CretSim* s, CretInput* inputs, int me);
// After cret_sim_step: god mode, feeds debug actors, releases dead ones.
void cret_cheats_after_step(CretCheats* c, CretSim* s, int me);
// Runs a sim command; writes a reply into out. False if it was not a sim
// command (a view command for debug.c) or failed.
bool cret_cheat_exec(CretCheats* c, CretSim* s, CretCheatEnv* env, const CretCmd* cmd, char* out, int nout);
// Hostile actor input toward agent t (also used by scenario controllers).
CretInput cret_actor_attack_input(CretSim* s, int i, int t);
#ifdef __cplusplus
}
#endif
#endif
