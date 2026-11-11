// Saved characters, as in Path of Titans: species, growth, marks, unlocked
// abilities and where the animal stood, one ini file per name in
// ~/.config/cretaceous/characters/<name>.ini ($XDG_CONFIG_HOME first).
// The player picks one on the title screen; a server keeps one per client
// name. Death keeps the marks and unlocks but the next life hatches young
// (growth back to the respawn growth, no position). The first steps (a new
// character's first quests) and the places seen carry over too.
#ifndef CRET_CHARACTER_H
#define CRET_CHARACTER_H
#include "sim.h"

#define CRET_MAX_CHARACTERS 32

typedef struct CretCharacter {
    char name[24];
    int species;
    float growth, health, food, water;   // fractions
    bool has_pos; float x, z;            // where it last stood (alive)
    int marks, quests_done;
    uint32_t unlocked;                   // bit species*2 + choice (abilities.c)
    uint8_t loadout[CRET_AB_SLOTS];      // CRET_AB_* ids
    int first_steps;                     // first-steps quests done (CRET_CHAIN_STEPS: all)
    uint32_t visited;                    // named places seen: bit = index in the world's place list
} CretCharacter;

// File-safe name: letters, digits, '-', '_' and spaces become '_'; at most 23 chars.
// False when nothing usable is left.
bool cret_character_clean(const char* in, char* out, int n);
bool cret_character_load(const char* name, CretCharacter* c);
bool cret_character_save(const CretCharacter* c);
// Saved names, sorted; returns the count.
int cret_character_list(char names[][24], int max);
// A new character of the species at the respawn growth.
void cret_character_new(CretCharacter* c, const char* name, int species, float growth);

// From the sim slot (a dead animal saves as a fresh hatchling at respawn_growth).
void cret_character_capture(const CretSim* s, int i, float respawn_growth, CretCharacter* c);
// Onto the sim slot: respawns it as the species, then growth, vitals,
// position, marks, unlocks and loadout. Keeps the slot's quest.
void cret_character_apply(CretSim* s, int i, const CretCharacter* c);
#endif
