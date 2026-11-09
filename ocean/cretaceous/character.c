#include "character.h"
#include "abilities.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }

static void char_dir(char* out, size_t n, bool make) {
    char base[400];
#ifdef _WIN32
    const char* appdata = getenv("APPDATA");   // %APPDATA%\Cretaceous\characters
    snprintf(base, sizeof base, "%s\\Cretaceous", appdata && *appdata ? appdata : ".");
    if (make) mkdir(base, 0755);
#else
    const char* xdg = getenv("XDG_CONFIG_HOME"); const char* home = getenv("HOME");
    if (xdg && *xdg) snprintf(base, sizeof base, "%s/cretaceous", xdg);
    else snprintf(base, sizeof base, "%s/.config/cretaceous", home ? home : ".");
    if (make) {
        char parent[400]; snprintf(parent, sizeof parent, "%s", base);
        char* sl = strrchr(parent, '/'); if (sl) { *sl = 0; mkdir(parent, 0755); }
        mkdir(base, 0755);
    }
#endif
    snprintf(out, n, "%s/characters", base);
    if (make) mkdir(out, 0755);
}

bool cret_character_clean(const char* in, char* out, int n) {
    int k = 0;
    for (const char* p = in; *p && k < n - 1 && k < 23; p++) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') out[k++] = c;
        else if (c == ' ' && k > 0) out[k++] = '_';
    }
    while (k > 0 && out[k - 1] == '_') k--;
    out[k] = 0;
    return k > 0;
}

static int ability_by_id(const char* id) {
    for (int k = 1; k < CRET_AB_COUNT; k++) if (!strcmp(CRET_ABILITIES[k].id, id)) return k;
    return 0;
}
static int species_by_asset(const char* id) {
    for (int k = 0; k < CRET_SPECIES; k++) if (!strcmp(CRET_SPECIES_TABLE[k].asset, id)) return k;
    return -1;
}

void cret_character_new(CretCharacter* c, const char* name, int species, float growth) {
    memset(c, 0, sizeof *c); cret_character_clean(name, c->name, sizeof c->name);
    c->species = species >= 0 && species < CRET_SPECIES ? species : CRET_SP_RAPTOR;
    c->growth = growth; c->health = c->food = c->water = 1;
    const CretSpeciesAbilities* P = &CRET_SPECIES_ABILITIES[c->species];
    c->loadout[0] = P->ab[0]; c->loadout[1] = P->ab[1];
}

bool cret_character_load(const char* name, CretCharacter* c) {
    char clean[24]; if (!cret_character_clean(name, clean, sizeof clean)) return false;
    char dir[480], path[600]; char_dir(dir, sizeof dir, false); snprintf(path, sizeof path, "%s/%s.ini", dir, clean);
    FILE* f = fopen(path, "r"); if (!f) return false;
    cret_character_new(c, clean, CRET_SP_RAPTOR, 0.35f);
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char* nl = strpbrk(line, "\r\n"); if (nl) *nl = 0;
        if (line[0] == '#' || !line[0]) continue;
        char* eq = strchr(line, '='); if (!eq) continue;
        *eq = 0; char* key = line; char* val = eq + 1;
        while (*key == ' ') key++;
        for (char* e = key + strlen(key); e > key && e[-1] == ' '; ) *--e = 0;
        while (*val == ' ') val++;
        if (!strcmp(key, "species")) { int sp = species_by_asset(val); if (sp >= 0) c->species = sp; }
        else if (!strcmp(key, "growth")) c->growth = clampf((float)atof(val), 0.35f, 1.0f);
        else if (!strcmp(key, "health")) c->health = clampf((float)atof(val), 0.05f, 1.0f);
        else if (!strcmp(key, "food")) c->food = clampf((float)atof(val), 0, 1);
        else if (!strcmp(key, "water")) c->water = clampf((float)atof(val), 0, 1);
        else if (!strcmp(key, "marks")) c->marks = atoi(val) < 0 ? 0 : atoi(val);
        else if (!strcmp(key, "quests_done")) c->quests_done = atoi(val);
        else if (!strcmp(key, "position")) { if (sscanf(val, "%f %f", &c->x, &c->z) == 2) c->has_pos = true; }
        else if (!strcmp(key, "unlocked")) {   // species/ability pairs
            for (char* t = strtok(val, " ,"); t; t = strtok(NULL, " ,")) {
                char* sl = strchr(t, '/'); if (!sl) continue; *sl = 0;
                int sp = species_by_asset(t), ab = ability_by_id(sl + 1); if (sp < 0 || !ab) continue;
                for (int ch = 0; ch < 2; ch++) if (CRET_SPECIES_ABILITIES[sp].ab[2 + ch] == ab) c->unlocked |= 1u << (sp * 2 + ch);
            }
        } else if (!strcmp(key, "loadout")) {
            int k = 0; for (char* t = strtok(val, " ,"); t && k < CRET_AB_SLOTS; t = strtok(NULL, " ,")) c->loadout[k++] = (uint8_t)ability_by_id(t);
        }
    }
    fclose(f);
    return true;
}

bool cret_character_save(const CretCharacter* c) {
    char clean[24]; if (!cret_character_clean(c->name, clean, sizeof clean)) return false;
    char dir[480], path[600], tmp[620]; char_dir(dir, sizeof dir, true);
    snprintf(path, sizeof path, "%s/%s.ini", dir, clean); snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE* f = fopen(tmp, "w"); if (!f) return false;
    fprintf(f, "# Cretaceous character (written by the game)\n");
    fprintf(f, "name = %s\nspecies = %s\ngrowth = %.4f\nhealth = %.3f\nfood = %.3f\nwater = %.3f\n", clean,
            CRET_SPECIES_TABLE[c->species % CRET_SPECIES].asset, c->growth, c->health, c->food, c->water);
    if (c->has_pos) fprintf(f, "position = %.1f %.1f\n", c->x, c->z);
    fprintf(f, "marks = %d\nquests_done = %d\nunlocked =", c->marks, c->quests_done);
    for (int sp = 0; sp < CRET_SPECIES; sp++)
        for (int ch = 0; ch < 2; ch++)
            if (c->unlocked >> (sp * 2 + ch) & 1) fprintf(f, " %s/%s", CRET_SPECIES_TABLE[sp].asset, CRET_ABILITIES[CRET_SPECIES_ABILITIES[sp].ab[2 + ch]].id);
    fprintf(f, "\nloadout =");
    for (int k = 0; k < CRET_AB_SLOTS; k++) fprintf(f, " %s", CRET_ABILITIES[c->loadout[k] < CRET_AB_COUNT ? c->loadout[k] : 0].id);
    fprintf(f, "\n");
    bool ok = fclose(f) == 0;
    return ok && rename(tmp, path) == 0;
}

static int cmp_name(const void* a, const void* b) { return strcmp((const char*)a, (const char*)b); }
int cret_character_list(char names[][24], int max) {
    char dir[480]; char_dir(dir, sizeof dir, false);
    DIR* d = opendir(dir); if (!d) return 0;
    int n = 0; struct dirent* e;
    while ((e = readdir(d)) && n < max) {
        size_t L = strlen(e->d_name);
        if (L < 5 || L > 27 || strcmp(e->d_name + L - 4, ".ini")) continue;
        memcpy(names[n], e->d_name, L - 4); names[n][L - 4] = 0; n++;
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof names[0], cmp_name);
    return n;
}

void cret_character_capture(const CretSim* s, int i, float respawn_growth, CretCharacter* c) {
    const CretAgent* a = &s->agents[i]; const CretProgress* p = &s->prog[i];
    char name[24]; memcpy(name, c->name, sizeof name);
    memset(c, 0, sizeof *c); memcpy(c->name, name, sizeof name);
    if (!c->name[0]) memcpy(c->name, p->name, sizeof c->name);
    c->species = a->species; c->marks = p->marks; c->quests_done = p->quests_done; c->unlocked = p->unlocked;
    memcpy(c->loadout, p->loadout, sizeof c->loadout);
    if (a->alive) {
        c->growth = a->growth; c->health = a->max_health > 0 ? a->health / a->max_health : 1; c->food = a->food; c->water = a->water;
        c->has_pos = true; c->x = a->x; c->z = a->z;
    } else {   // dead: the next life hatches
        c->growth = respawn_growth; c->health = c->food = c->water = 1;
    }
}

void cret_character_apply(CretSim* s, int i, const CretCharacter* c) {
    CretProgress* p = &s->prog[i];
    memcpy(p->name, c->name, sizeof p->name);
    p->marks = c->marks; p->quests_done = c->quests_done; p->unlocked = c->unlocked;
    memcpy(p->loadout, c->loadout, sizeof p->loadout);
    cret_sim_set_species(s, i, c->species);   // respawns (keeps the loadout when it fits the species)
    cret_sim_restore(s, i, c->growth, c->health, c->food, c->water, c->has_pos, c->x, c->z);
}
