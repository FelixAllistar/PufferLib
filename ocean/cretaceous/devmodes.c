// Developer modes (included by cretaceous.c just before main, so they share
// its camera rig and input helpers):
//   ./cretaceous play gallery                 the "Matrix room": every plant, prop and dinosaur on flat ground
//   ./cretaceous capture DIR --gallery        each gallery row, windowless, as PNGs
//   ./cretaceous play scenario NAME           watch a scenario (config/scenarios/NAME.ini); M takes over the player
//   ./cretaceous capture DIR --scenario NAME  windowless mp4, stills and the summary
//
// The gallery is a real world (a flat 1 km plain baked into the world cache
// under its own seed) drawn by the real renderer: the same LODs, shadows,
// materials and streaming. Trees and props are added to the renderer's tiles
// one per variant; ground cover grows from painted patches of the map layers
// (so it is scattered and bends as in play); dinosaurs are sim agents whose
// state is set each frame to play one clip.
// Gallery keys: WASD fly, Q/E down/up, Shift faster, hold the right mouse
// button (or arrows) to look; Tab or N/P jump between items; L cycles the LOD
// (auto, full, reduced, impostor/lowest); T/G time of day +-1 h, [ ] +-10 min
// (the sun's angle); R weather; C/V next/previous dinosaur clip; Z slow
// motion; X dinosaur wireframe; K skeleton; H labels; F10 screenshot (to
// CRET_SHOT_DIR, default screenshots/); F2 terrain wireframe; F5 shaders; Esc quits.
#include "scenario.h"

enum { DEV_NONE, DEV_GALLERY, DEV_SCENARIO, DEV_NEST_SMOKE };
typedef struct { int kind; char name[64]; } DevArgs;

// Consumes "play gallery", "play scenario [NAME]", "--gallery", "--scenario NAME", "--nest-smoke" at argv[*i].
static bool cret_devmode_arg(int argc, char** argv, int* i, DevArgs* d) {
    const char* a = argv[*i];
    if (!strcmp(a, "play") && *i + 1 < argc && !strcmp(argv[*i + 1], "gallery")) { d->kind = DEV_GALLERY; *i += 1; return true; }
    if (!strcmp(a, "play") && *i + 2 < argc && !strcmp(argv[*i + 1], "scenario")) { d->kind = DEV_SCENARIO; snprintf(d->name, sizeof d->name, "%s", argv[*i + 2]); *i += 2; return true; }
    if (!strcmp(a, "play") && *i + 1 < argc && !strcmp(argv[*i + 1], "scenario")) { d->kind = DEV_SCENARIO; d->name[0] = 0; *i += 1; return true; }   // opens on the picker
    if (!strcmp(a, "--gallery")) { d->kind = DEV_GALLERY; return true; }
    if (!strcmp(a, "--nest-smoke")) { d->kind = DEV_NEST_SMOKE; return true; }
    if (!strcmp(a, "--scenario") && *i + 1 < argc) { d->kind = DEV_SCENARIO; snprintf(d->name, sizeof d->name, "%s", argv[*i + 1]); *i += 1; return true; }
    return false;
}

static void dev_mkdirs(const char* path) {   // mkdir -p
    char b[1024]; snprintf(b, sizeof b, "%s", path);
    for (char* p = b + 1; *p; p++) if (*p == '/' || *p == '\\') { char c = *p; *p = 0; mkdir(b, 0755); *p = c; }
    mkdir(b, 0755);
}
static void dev_text(const char* t, float x, float y, int fs, Color c) { DrawText(t, (int)x + 1, (int)y + 1, fs, (Color){0, 0, 0, 180}); DrawText(t, (int)x, (int)y, fs, c); }

// ================================================================ gallery
#define GAL_SEED 0x6A11E7u   // the gallery's world in the cache (s7213799_n512_*.terra)
#define GAL_N 512
#define GAL_CELL 2.0f
#define GAL_GROUND 12.0f
enum { GI_TREE, GI_COVER, GI_PROP, GI_DINO, GI_NEST, GI_ROWS };
static const char* const GAL_ROW[GI_ROWS] = {"trees", "cover", "props", "dinos", "nests"};
typedef struct { int type, kind, variant; float x, y, z, r, h; char name[64]; } GalItem;
// Ground-cover patches: a veg layer (or -1) and the splat painted under it.
typedef struct { const char* name; int layer, mat; } GalPatch;
static const GalPatch GAL_PATCH[] = {
    {"fern", TERRA_VEG_FERN, TERRA_MAT_FOREST}, {"horsetail", TERRA_VEG_HORSETAIL, TERRA_MAT_FOREST}, {"shrub", TERRA_VEG_SHRUB, TERRA_MAT_FOREST},
    {"carpet (moss sward)", -1, TERRA_MAT_MOSS}, {"grass (meadow)", -1, TERRA_MAT_DIRT}, {"grass (wet: sedge, reeds)", -1, TERRA_MAT_MUD}};
#define GAL_NPATCH ((int)(sizeof GAL_PATCH / sizeof GAL_PATCH[0]))
#define GAL_PATCH_HALF 8.0f
static const char* const VEG_NAME[VEG_KINDS] = {"conifer", "broadleaf", "cycad", "fern", "treefern", "horsetail", "palm", "shrub", "carpet", "grass"};
static const char* const PROP_NAME[PROP_KINDS] = {"boulder", "outcrop", "stone", "log", "stump", "roots", "debris", "driftwood", "coast", "cliff"};
// Gallery clips: what each one sets on the agent.
enum { GC_IDLE, GC_WALK, GC_TROT, GC_RUN, GC_JUMP, GC_EAT, GC_DRINK, GC_CALL, GC_ATTACK, GC_ATTACK2, GC_REST, GC_HURT, GC_DEATH, GC_COUNT };
static const char* const GC_NAME[GC_COUNT] = {"idle", "walk", "trot", "run", "jump", "eat", "drink", "call", "attack", "attack2 (tail/slash)", "rest", "hurt", "death"};
static const int GC_ROLE[GC_COUNT] = {DCLIP_IDLE, DCLIP_WALK, DCLIP_TROT, DCLIP_RUN, DCLIP_JUMP, DCLIP_EAT, DCLIP_DRINK, DCLIP_CALL, DCLIP_ATTACK, -1, DCLIP_REST, DCLIP_HURT, DCLIP_DEATH};

// The locomotion clip a gait plays: walk the slowest, run the fastest, trot the middle one (if the model has three).
static int gal_loco(const DinoModel* m, int clip) {
    if (!m || m->nloco <= 0) return -1;
    if (clip == GC_WALK) return m->loco[0];
    if (clip == GC_RUN) return m->loco[m->nloco - 1];
    return m->nloco >= 3 ? m->loco[1] : -1;
}
static float gal_centre(void) { return (GAL_N - 1) * GAL_CELL * 0.5f; }
static float gal_row_z(int row) { static const float Z[GI_ROWS] = {-75, -25, 20, 95, 140}; return gal_centre() + Z[row]; }

// The flat world, baked once into the world cache (world.c loads it like any other).
static bool gallery_world(void) {
#ifdef _WIN32
    _putenv("CRET_TERRA=");   // the cache key must be the default parameters
#else
    unsetenv("CRET_TERRA");
#endif
    TerraParams P = terra_default_params(GAL_SEED); P.n = GAL_N; P.cell = GAL_CELL;
    char path[512];
    mkdir("build", 0755); mkdir("build/cretaceous", 0755); mkdir("build/cretaceous/worlds", 0755);
    snprintf(path, sizeof path, "build/cretaceous/worlds/s%u_n%d_%016llx.terra", GAL_SEED, GAL_N, (unsigned long long)terra_params_hash(&P));
    TerraMap m; memset(&m, 0, sizeof m);
    if (terra_load(&m, &P, path) == 0) { terra_free(&m); return true; }
    size_t N = (size_t)GAL_N * GAL_N; m.n = GAL_N; m.cell = GAL_CELL; m.seed = GAL_SEED;
    m.height = malloc(N * 4); m.water = malloc(N * 4); m.flow = calloc(N, 4); m.rain = malloc(N * 4); m.soil = malloc(N * 4); m.temp = malloc(N * 4);
    m.water_kind = calloc(N, 1); m.flow_dir = calloc(N * 2, 1); m.splat = calloc(N * TERRA_MAT_COUNT, 1); m.veg = calloc(N * TERRA_VEG_COUNT, 1); m.hardness = malloc(N);
    for (size_t i = 0; i < N; i++) {
        m.height[i] = GAL_GROUND; m.water[i] = TERRA_DRY; m.rain[i] = 1.2f; m.soil[i] = 0.6f; m.temp[i] = 22; m.hardness[i] = 128;
        uint8_t* s = m.splat + i * TERRA_MAT_COUNT; s[TERRA_MAT_SCREE] = 150; s[TERRA_MAT_SAND] = 105;   // neutral grey-buff gravel: no grass, no sward
    }
    // Ground-cover patches along their row.
    float zc = gal_row_z(GI_COVER), x0 = gal_centre() - (GAL_NPATCH - 1) * 15.0f;
    for (int k = 0; k < GAL_NPATCH; k++) {
        float xc = x0 + k * 30.0f;
        for (int gz = (int)((zc - GAL_PATCH_HALF) / GAL_CELL); gz <= (int)((zc + GAL_PATCH_HALF) / GAL_CELL); gz++)
            for (int gx = (int)((xc - GAL_PATCH_HALF) / GAL_CELL); gx <= (int)((xc + GAL_PATCH_HALF) / GAL_CELL); gx++) {
                size_t i = (size_t)gz * GAL_N + gx; uint8_t* s = m.splat + i * TERRA_MAT_COUNT;
                memset(s, 0, TERRA_MAT_COUNT); s[GAL_PATCH[k].mat] = 255;
                if (GAL_PATCH[k].layer >= 0) m.veg[i * TERRA_VEG_COUNT + GAL_PATCH[k].layer] = 255;
            }
    }
    int rc = terra_save(&m, &P, path);
    terra_free(&m);
    if (rc != 0) { fprintf(stderr, "gallery: cannot write %s\n", path); return false; }
    fprintf(stderr, "gallery: baked the flat world into %s\n", path);
    return true;
}

typedef struct {
    GalItem item[256]; int n;
    int only_lo, only_hi;      // capture: label only items [lo, hi) (hi 0: all)
    int focus;                 // item the camera went to last (-1: none)
    int lod_mode;              // 0 auto, 1 full, 2 reduced, 3 impostor / lowest
    int clip, dino_clip[CRET_SPECIES];   // clip for all dinosaurs; per dinosaur when it is focused
    float clip_t; float slow; bool wire, skel, labels;
    float veg_near, veg_tree_lod, veg_lod, prop_lod;   // the renderer's defaults
    int dino_slot[CRET_SPECIES]; int ndino;
} Gallery;

static void tile_add_tree(CretVeg* v, VegInstance in) {
    int tx = (int)(in.x / VEG_TILE), tz = (int)(in.z / VEG_TILE); if (tx < 0 || tz < 0 || tx >= v->tiles_n || tz >= v->tiles_n) return;
    VegTile* t = &v->tiles[tz * v->tiles_n + tx];
    t->trees = realloc(t->trees, sizeof(VegInstance) * (t->ntrees + 1)); t->trees[t->ntrees++] = in;
    float h = v->mesh[in.kind][in.variant].height * in.scale;
    if (t->ntrees == 1) { t->ymin = in.y - 2; t->ymax = in.y + h + 2; } else { t->ymin = fminf(t->ymin, in.y - 2); t->ymax = fmaxf(t->ymax, in.y + h + 2); }
}
static void tile_add_prop(CretProps* p, PropInstance in) {
    int tx = (int)(in.x / PROP_TILE), tz = (int)(in.z / PROP_TILE); if (tx < 0 || tz < 0 || tx >= p->tiles_n || tz >= p->tiles_n) return;
    PropTile* t = &p->tiles[tz * p->tiles_n + tx];
    float r = p->var[in.variant].radius * in.scale, hh = p->var[in.variant].height * in.scale;
    t->items = realloc(t->items, sizeof(PropInstance) * (t->n + 1)); t->items[t->n++] = in;
    if (t->n == 1) { t->ymin = in.y - r; t->ymax = in.y + hh + r; } else { t->ymin = fminf(t->ymin, in.y - r); t->ymax = fmaxf(t->ymax, in.y + hh + r); }
}

static void gallery_build(Gallery* g, CretSim* sim, CretView* view) {
    memset(g, 0, sizeof *g); g->focus = -1; g->slow = 1; g->labels = true;
    float C = gal_centre();
    // Trees: every tall kind, every variant, one each.
    static const int TALL[] = {VEG_CONIFER, VEG_BROADLEAF, VEG_CYCAD, VEG_TREEFERN, VEG_PALM};
    if (view->veg_ok) {
        CretVeg* v = &view->veg; g->veg_near = view->veg_base[0]; g->veg_lod = view->veg_base[2]; g->veg_tree_lod = view->veg_base[3];
        // Nothing else grows here: the static scatter is empty (no veg layers outside the patches).
        float x = C - 2 * 4 * 26.0f - 1.5f * 26.0f;
        for (size_t k = 0; k < sizeof TALL / sizeof *TALL; k++) for (int var = 0; var < VEG_VARIANTS; var++, x += 26.0f) {
            int kind = TALL[k]; if (!v->mesh[kind][var].tris) continue;
            float z = gal_row_z(GI_TREE);
            VegInstance in = {x, GAL_GROUND, z, 1.0f, 0.6f, 0.5f, 0.37f * var, (uint16_t)kind, (uint16_t)var, cosf(0.6f), sinf(0.6f)};
            tile_add_tree(v, in);
            GalItem* it = &g->item[g->n++]; *it = (GalItem){GI_TREE, kind, var, x, GAL_GROUND, z, v->mesh[kind][var].radius, v->mesh[kind][var].height, ""};
            snprintf(it->name, sizeof it->name, "%s v%d", VEG_NAME[kind], var);
        }
        float x0 = C - (GAL_NPATCH - 1) * 15.0f;
        for (int k = 0; k < GAL_NPATCH; k++) {
            GalItem* it = &g->item[g->n++]; *it = (GalItem){GI_COVER, k, -1, x0 + k * 30.0f, GAL_GROUND, gal_row_z(GI_COVER), GAL_PATCH_HALF, 1.5f, ""};
            snprintf(it->name, sizeof it->name, "%s patch", GAL_PATCH[k].name);
        }
    }
    // Props: every catalog variant at a mid size of its first class, two rows if long.
    if (view->props_ok) {
        CretProps* p = &view->props; g->prop_lod = view->settings.prop_lod;
        // Only the gallery's props: drop the static scatter and switch off the near field (stones, debris).
        for (int t = 0; t < p->tiles_n * p->tiles_n; t++) p->tiles[t].n = 0;
        p->near_range = -1e4f; p->near_valid = false; p->nnear = 0;
        float x = C - 230, z = gal_row_z(GI_PROP); int in_row = 0;
        for (int vi = 0; vi < p->nvar && g->n < 250; vi++) {
            int kind = p->cat.kind[vi]; if (kind < 0) kind = PROP_BOULDER;
            const PropClass* PC = &PROP_CLASS[kind];
            float r = 0.5f * (PC->r_lo + PC->r_hi), sc = r / fmaxf(p->cat.radius[vi], 0.01f);
            if (x + r > C + 230 && in_row) { x = C - 230; z += 34; in_row = 0; }
            x += r;
            float y = GAL_GROUND - PC->sink * p->cat.height[vi] * sc;
            PropInstance in = {x, y, z, sc, {0, 0.38268f, 0, 0.92388f}, 0.15f, kind == PROP_DRIFTWOOD ? 1.0f : 0.0f, (uint16_t)vi, (uint16_t)kind};
            tile_add_prop(p, in);
            GalItem* it = &g->item[g->n++]; *it = (GalItem){GI_PROP, kind, vi, x, GAL_GROUND, z, r, p->cat.height[vi] * sc, ""};
            snprintf(it->name, sizeof it->name, "%s: %s", PROP_NAME[kind], p->cat.name[vi]);
            x += r + fmaxf(1.5f, r * 0.8f); in_row++;
        }
    }
    // Dinosaurs: one adult of each species, facing the camera row.
    float x = C - (CRET_SPECIES - 1) * 9.0f;
    for (int sp = 0; sp < CRET_SPECIES && sp < sim->cfg.num_agents; sp++, x += 18) {
        sim->species_pick[sp] = (uint8_t)(sp + 1); cret_sim_respawn(sim, sp);
        CretAgent* a = &sim->agents[sp]; a->growth = 1; a->max_health = 0; cret_apply_growth(a); a->health = a->max_health;
        a->x = x; a->z = gal_row_z(GI_DINO); a->y = GAL_GROUND; a->yaw = -PI / 2 + 0.5f; a->on_ground = true; a->vx = a->vz = 0;
        g->dino_slot[g->ndino++] = sp;
        GalItem* it = &g->item[g->n++]; *it = (GalItem){GI_DINO, sp, sp, x, GAL_GROUND, a->z, CRET_SPECIES_TABLE[sp].length * 0.5f, a->height, ""};
        snprintf(it->name, sizeof it->name, "%s", CRET_SPECIES_TABLE[sp].name);
    }
    // Nests (render/nest.c): one row per species behind the dinosaurs, with 0
    // to 5 eggs, then the same nest a third and two thirds built.
    CretNestDraw* nd = &view->nests; nd->nshow = 0;
    for (int sp = 0; sp < CRET_SPECIES; sp++) {
        float R = cret_nest_radius(sp), step = 2.9f * R + 1.5f, z = gal_row_z(GI_NEST) + sp * 12.0f, x = C - 3.5f * step;
        for (int col = 0; col < 8 && nd->nshow < NEST_SHOW_MAX && g->n < 256; col++, x += step) {
            int eggs = col < 6 ? col : 0; float built = col < 6 ? 1.0f : col == 6 ? 0.33f : 0.67f;
            nd->show[nd->nshow++] = (CretNestShow){sp, eggs, x, z, built};
            GalItem* it = &g->item[g->n++]; *it = (GalItem){GI_NEST, sp, col, x, GAL_GROUND, z, R * 1.3f, 0.5f, ""};
            if (col < 6) snprintf(it->name, sizeof it->name, "%s nest, %d egg%s", CRET_SPECIES_TABLE[sp].name, eggs, eggs == 1 ? "" : "s");
            else snprintf(it->name, sizeof it->name, "%s nest, %d%% built", CRET_SPECIES_TABLE[sp].name, (int)(built * 100 + 0.5f));
        }
    }
    for (int i = CRET_SPECIES; i < sim->cfg.num_agents; i++) sim->agents[i].alive = false;
    memset(sim->carcasses, 0, sizeof sim->carcasses);
    for (int s = 0; s < CRET_SPECIES; s++) g->dino_clip[s] = -1;
}

// Sets each dinosaur's state for its clip (the animation reads only the agent).
static void gallery_pose_dinos(Gallery* g, CretSim* sim, const CretView* view, float dt) {
    g->clip_t += dt;
    for (int k = 0; k < g->ndino; k++) {
        int i = g->dino_slot[k]; CretAgent* a = &sim->agents[i]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
        int clip = g->dino_clip[k] >= 0 ? g->dino_clip[k] : g->clip;
        const DinoModel* m = view->dinos_ok ? &view->dinos.model[a->species] : NULL;
        float period = 1.6f; if (m && GC_ROLE[clip] >= 0 && m->slot[GC_ROLE[clip]] >= 0) period = fmaxf(m->clip[m->slot[GC_ROLE[clip]]].duration + 0.4f, 1.0f);
        float t = fmodf(g->clip_t + k * 0.21f, clip == GC_DEATH ? 5.0f : period);
        a->alive = true; a->act = CRET_ACT_NONE; a->speed = 0; a->on_ground = true; a->swimming = false; a->y = GAL_GROUND;
        if (a->hurt_ticks > 0) a->hurt_ticks--;
        sim->carcasses[k].active = false;
        switch (clip) {
        case GC_WALK: case GC_TROT: case GC_RUN: {
            int li = gal_loco(m, clip);
            float sp = li >= 0 ? m->clip[li].speed : 0;
            if (sp <= 0) sp = clip == GC_WALK ? S->walk : clip == GC_TROT ? S->trot : S->sprint;
            a->speed = sp; break;
        }
        case GC_JUMP: a->on_ground = t > 0.8f; a->y = GAL_GROUND + (t < 0.8f ? sinf(t / 0.8f * PI) * 1.2f : 0); break;
        case GC_EAT: a->act = CRET_ACT_EAT; break;
        case GC_DRINK: a->act = CRET_ACT_DRINK; break;
        case GC_CALL: a->act = t < period - 0.3f ? CRET_ACT_CALL : CRET_ACT_NONE; break;
        case GC_ATTACK: a->act = t < 0.9f ? CRET_ACT_ATTACK : CRET_ACT_NONE; break;
        case GC_ATTACK2: a->act = t < 0.9f ? CRET_ACT_ATTACK2 : CRET_ACT_NONE; break;
        case GC_REST: a->act = CRET_ACT_REST; break;
        case GC_HURT: if (t < dt * 1.01f) a->hurt_ticks = 12; break;
        case GC_DEATH: {   // a carcass at its feet, replayed every 5 s
            a->alive = false; CretCarcass* c = &sim->carcasses[k];
            *c = (CretCarcass){.active = true, .species = (uint8_t)a->species, .x = a->x, .y = GAL_GROUND, .z = a->z, .yaw = a->yaw, .growth = 1,
                               .meat = 100, .max_meat = 100, .age = (int)(t * CRET_TICK_HZ), .agent = i};
            break;
        }
        default: break;
        }
    }
}

// The renderer's LOD knobs for a forced mode (relative to the focused item for props).
static void gallery_apply_lod(Gallery* g, CretView* view, const CretCamera* cam) {
    if (view->veg_ok) {   // through the view's base ranges: it rescales them by the preset every frame
        view->settings.veg_range = 1;
        view->veg_base[0] = g->lod_mode == 0 ? g->veg_near : g->lod_mode == 3 ? 1.0f : 1e4f;
        view->veg_base[3] = g->lod_mode == 0 ? g->veg_tree_lod : g->lod_mode == 1 ? 1e4f : 0.5f;
        view->veg_base[2] = g->lod_mode == 0 ? g->veg_lod : g->lod_mode == 1 ? 1e4f : 0.5f;
    }
    if (view->props_ok) {
        CretProps* p = &view->props; float k = g->prop_lod;
        if (g->lod_mode) {
            // Distance over radius of the focused prop (else the nearest), placed inside the wanted LOD band.
            int f = g->focus; float best = 1e9f;
            if (f < 0 || g->item[f].type != GI_PROP)
                for (int i = 0; i < g->n; i++) if (g->item[i].type == GI_PROP) { float d = hypotf(g->item[i].x - cam->pos[0], g->item[i].z - cam->pos[2]); if (d < best) { best = d; f = i; } }
            if (f >= 0) {
                const GalItem* it = &g->item[f]; float d = sqrtf(powf(it->x - cam->pos[0], 2) + powf(it->y - cam->pos[1], 2) + powf(it->z - cam->pos[2], 2)) / fmaxf(it->r, 0.05f);
                k = g->lod_mode == 1 ? 1e3f : g->lod_mode == 2 ? d / 50.0f : d / 150.0f;
            }
        }
        p->lod_scale = k; view->settings.prop_lod = k;
    }
}

static const char* gallery_lod_name(int mode) { static const char* N[4] = {"auto (by distance)", "full", "reduced", "impostor / lowest"}; return N[mode & 3]; }

// What an item shows now: its tris and the LOD in use.
static void gallery_item_info(const Gallery* g, const CretView* view, const GalItem* it, const CretCamera* cam, char* out, int n) {
    float d = sqrtf(powf(it->x - cam->pos[0], 2) + powf(it->y + it->h * 0.5f - cam->pos[1], 2) + powf(it->z - cam->pos[2], 2));
    if (it->type == GI_TREE && view->veg_ok) {
        const CretVeg* v = &view->veg; int full = v->mesh[it->kind][it->variant].tris, lo = v->mesh_lo[it->kind][it->variant].tris;
        float n_eff = v->near_range * (it->kind == VEG_CYCAD ? 0.6f : 1.0f);
        const char* lod = d >= n_eff ? "impostor" : (!lo || d < v->tree_lod_dist) ? "full" : "reduced";
        snprintf(out, n, "%s  |  %d tris full, %d reduced  |  LOD %s  |  %.0f m", it->name, full, lo, lod, d);
    } else if (it->type == GI_COVER && view->veg_ok) {
        const CretVeg* v = &view->veg; const GalPatch* P = &GAL_PATCH[it->kind];
        int kind = P->layer == TERRA_VEG_FERN ? VEG_FERN : P->layer == TERRA_VEG_HORSETAIL ? VEG_HORSETAIL : P->layer == TERRA_VEG_SHRUB ? VEG_SHRUB : P->mat == TERRA_MAT_MOSS ? VEG_CARPET : VEG_GRASS;
        int full = 0, lo = 0; for (int q = 0; q < VEG_VARIANTS; q++) { full += v->mesh[kind][q].tris; lo += v->mesh_lo[kind][q].tris; }
        snprintf(out, n, "%s  |  %d variants mixed, %d / %d tris (all variants)  |  LOD %s  |  %.0f m", it->name, VEG_VARIANTS, full, lo, d < v->lod_dist ? "full" : "reduced", d);
    } else if (it->type == GI_PROP && view->props_ok) {
        const CretProps* p = &view->props; const PropVariant* V = &p->var[it->variant];
        float k = p->lod_scale * PROP_CLASS[it->kind].lod_mul, dr = d / fmaxf(it->r, 0.05f); int l = dr < 22 * k ? 0 : dr < 90 * k ? 1 : 2;
        snprintf(out, n, "%s  |  %d / %d / %d tris  |  LOD%d  |  r %.1f m  |  %.0f m", it->name, V->tris[0], V->tris[1], V->tris[2], l, it->r, d);
    } else if (it->type == GI_NEST) {
        float el, ew; cret_egg_size(it->kind, &el, &ew);
        snprintf(out, n, "%s  |  nest %.1f m across  |  eggs %.0f x %.0f cm  |  %.0f m", it->name, 2.3f * cret_nest_radius(it->kind), el * 100, ew * 100, d);
    } else if (it->type == GI_DINO && view->dinos_ok) {
        const CretDinos* D = &view->dinos; const DinoModel* m = &D->model[it->kind]; int l = D->lod[it->variant];
        int clip = g->dino_clip[it->variant] >= 0 ? g->dino_clip[it->variant] : g->clip, role = GC_ROLE[clip];
        int ci = clip >= GC_WALK && clip <= GC_RUN ? gal_loco(m, clip) : role >= 0 ? m->slot[role] : -1;
        snprintf(out, n, "%s  |  %d / %d / %d tris  |  LOD%d  |  clip %s (%s)  |  %.0f m", it->name, m->lod[0].tris, m->lod[1].tris, m->lod[2].tris, l < 0 ? 0 : l,
                 GC_NAME[clip], ci >= 0 ? m->clip[ci].name : "procedural", d);
    } else snprintf(out, n, "%s", it->name);
}

// Skeleton: each joint's world position from its skinning matrix (model * global * inverse bind).
static void gallery_skeleton(const CretView* view, const CretSim* sim, int slot) {
    if (!view->dinos_ok || slot < 0 || !sim->agents[slot].alive) return;
    const CretDinos* D = &view->dinos; const DinoModel* m = &D->model[sim->agents[slot].species]; if (!m->ok) return;
    const float* row = D->pose + (size_t)slot * DINO_MAX_JOINTS * 12; static float P[DINO_MAX_JOINTS][3];
    for (int j = 0; j < m->njoint; j++) {
        const float* B = m->inv_bind[j]; const float* S = row + j * 12;
        // bind origin = -A^-1 t for inv_bind = [A | t]
        float a = B[0], b = B[1], c = B[2], d = B[4], e = B[5], f = B[6], gg = B[8], h = B[9], k = B[10];
        float det = a * (e * k - f * h) - b * (d * k - f * gg) + c * (d * h - e * gg); if (fabsf(det) < 1e-12f) det = 1e-12f;
        float inv[9] = {(e * k - f * h) / det, (c * h - b * k) / det, (b * f - c * e) / det, (f * gg - d * k) / det, (a * k - c * gg) / det, (c * d - a * f) / det,
                        (d * h - e * gg) / det, (b * gg - a * h) / det, (a * e - b * d) / det};
        float o[3]; for (int r = 0; r < 3; r++) o[r] = -(inv[r * 3] * B[3] + inv[r * 3 + 1] * B[7] + inv[r * 3 + 2] * B[11]);
        for (int r = 0; r < 3; r++) P[j][r] = S[r * 4] * o[0] + S[r * 4 + 1] * o[1] + S[r * 4 + 2] * o[2] + S[r * 4 + 3];
    }
    for (int j = 0; j < m->njoint; j++) {
        Vector2 p0; if (!dbg_project(view->viewproj, P[j][0], P[j][1], P[j][2], &p0)) continue;
        DrawCircleV(p0, 2.5f, (Color){255, 230, 80, 255});
        int q = m->parent[j]; if (q < 0) continue;
        line3(view->viewproj, P[q], P[j], 2.0f, (Color){255, 200, 60, 230});
    }
}

// Wireframe: the dinosaurs once more as additive lines, inside the scene pass (view.fx_draw).
typedef struct { Gallery* g; const CretSim* sim; } GalFx;
static void gallery_fx_draw(void* ctx, const CretView* v, const VegFrameDesc* fd) {
    GalFx* gf = ctx; if (!gf->g->wire || !v->dinos_ok) return;
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
    cret_dino_draw((CretDinos*)&v->dinos, gf->sim, fd);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST);
}

// Camera at an item: in front of it (rows face -z), framed by its size.
static void gallery_frame(const GalItem* it, CretCamera* cam) {
    float size = fmaxf(it->h, it->r * 2), d = fmaxf(size * 1.25f, 5.0f);
    if (it->type == GI_COVER) d = 16;
    cam->pos[0] = it->x - d * 0.35f; cam->pos[2] = it->z - d; cam->pos[1] = GAL_GROUND + fmaxf(1.7f, fminf(size * 0.45f, 9.0f)) + (it->type == GI_COVER ? 3 : 0);
    float ty = it->y + fminf(it->h * 0.45f, 12.0f);
    cam->yaw = atan2f(it->z - cam->pos[2], it->x - cam->pos[0]);
    cam->pitch = atan2f(ty - cam->pos[1], hypotf(it->x - cam->pos[0], it->z - cam->pos[2]));
    cam->fov = 60;
}

static void gallery_labels(const Gallery* g, const CretView* view, const CretCamera* cam, int fs) {
    for (int i = 0; i < g->n; i++) {
        if (g->only_hi && (i < g->only_lo || i >= g->only_hi)) continue;
        const GalItem* it = &g->item[i]; float d = hypotf(it->x - cam->pos[0], it->z - cam->pos[2]); if (d > 140) continue;
        Vector2 p; if (!dbg_project(view->viewproj, it->x, it->y + it->h + 0.8f, it->z, &p)) continue;
        p.y -= (i & 1) * (fs + 10);   // neighbours alternate heights
        char t[256]; gallery_item_info(g, view, it, cam, t, sizeof t);
        int w = MeasureText(t, fs); Color c = i == g->focus ? (Color){255, 235, 140, 255} : (Color){235, 235, 235, 230};
        DrawRectangle((int)(p.x - w * 0.5f - 4), (int)(p.y - fs - 3), w + 8, fs + 6, (Color){0, 0, 0, 110});
        dev_text(t, p.x - w * 0.5f, p.y - fs, fs, c);
    }
}

static int gallery_init(CretSim** psim, CretView* view, int W, int H) {
    if (!gallery_world()) return 1;
    CretConfig cfg = cret_default_config(); cfg.world_seed = GAL_SEED; cfg.world_n = GAL_N; cfg.world_cell = GAL_CELL;
    cfg.num_agents = CRET_SPECIES; cfg.player_growth = 1; cfg.weather = 0; cfg.cloud_cover = 0.25f; cfg.seed = 1;
    CretSim* sim = calloc(1, sizeof *sim);
    if (cret_sim_init(sim, cfg) != 0) { fprintf(stderr, "gallery: world failed\n"); free(sim); return 1; }
    sim->time_of_day = 10.5f / 24;
    *psim = sim;
    (void)view; (void)W; (void)H;
    return 0;
}

static int run_gallery(int W, int H, bool size_set) {
    CretSim* sim = NULL; CretView view;
    CretSettings st; cret_settings_load(&st); if (!size_set) cret_settings_resolution(st.res, &W, &H);
    if (!IsWindowReady()) { SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT); InitWindow(W, H, "Cretaceous gallery"); }   // the web build keeps the title's window
    SetTraceLogLevel(LOG_WARNING); SetExitKey(KEY_NULL);
    if (gallery_init(&sim, &view, W, H)) return 1;
    if (!cret_view_init(&view, sim->world, GetRenderWidth(), GetRenderHeight())) return 1;
    if (!getenv("CRET_PRESET")) cret_view_set_preset(&view, st.preset);
    Gallery* g = calloc(1, sizeof *g); gallery_build(g, sim, &view);
    GalFx gf = {g, sim}; view.fx_draw = gallery_fx_draw; view.fx_ctx = &gf;
    CretCamera cam = {{gal_centre(), GAL_GROUND + 14, gal_row_z(GI_TREE) - 90}, PI / 2, -0.05f, 60};
    CretCamera from = cam, to = cam; float fly_t = 1;
    CretDebug* dbg = calloc(1, sizeof *dbg); cret_debug_init(dbg, NULL, 1, true);
    const char* shot_dir = getenv("CRET_SHOT_DIR") ? getenv("CRET_SHOT_DIR") : "screenshots"; int shots = 0;
    bool help = true; const char* wname[3] = {"clear", "overcast", "rain"}; int weather = 0; float wcov[3] = {0.25f, 0.8f, 0.97f};
    printf("gallery: %d items (WASD fly, right mouse look, Tab/N/P items, L LOD, T/G time, [ ] sun, R weather, C/V clip, Z slow, X wire, K skeleton, H labels, F10 shot)\n", g->n);
    while (!WindowShouldClose()) {
        float dt = GetFrameTime(); if (dt > 0.1f) dt = 0.1f;
        if (IsWindowResized()) cret_view_resize(&view, GetRenderWidth(), GetRenderHeight());
        bool con = cret_debug_frame(dbg, sim, &view, &cam, 0);
        if (!con) {
            if (IsKeyPressed(KEY_ESCAPE)) break;
            bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            // Fly.
            float sp = (shift ? 60.0f : 14.0f) * dt;
            float fx = cosf(cam.pitch) * cosf(cam.yaw), fy = sinf(cam.pitch), fz = cosf(cam.pitch) * sinf(cam.yaw), rx = -sinf(cam.yaw), rz = cosf(cam.yaw);
            bool moved = false;
            if (IsKeyDown(KEY_W)) { cam.pos[0] += fx * sp; cam.pos[1] += fy * sp; cam.pos[2] += fz * sp; moved = true; }
            if (IsKeyDown(KEY_S)) { cam.pos[0] -= fx * sp; cam.pos[1] -= fy * sp; cam.pos[2] -= fz * sp; moved = true; }
            if (IsKeyDown(KEY_D)) { cam.pos[0] += rx * sp; cam.pos[2] += rz * sp; moved = true; }
            if (IsKeyDown(KEY_A)) { cam.pos[0] -= rx * sp; cam.pos[2] -= rz * sp; moved = true; }
            if (IsKeyDown(KEY_E) || IsKeyDown(KEY_SPACE)) { cam.pos[1] += sp; moved = true; }
            if (IsKeyDown(KEY_Q) || IsKeyDown(KEY_LEFT_CONTROL)) { cam.pos[1] -= sp; moved = true; }
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) { Vector2 md = GetMouseDelta(); cam.yaw += md.x * 0.003f; cam.pitch -= md.y * 0.003f; moved = true; }
            float look = 1.6f * dt;
            if (IsKeyDown(KEY_LEFT)) { cam.yaw -= look; moved = true; }
            if (IsKeyDown(KEY_RIGHT)) { cam.yaw += look; moved = true; }
            if (IsKeyDown(KEY_UP)) { cam.pitch += look; moved = true; }
            if (IsKeyDown(KEY_DOWN)) { cam.pitch -= look; moved = true; }
            cam.pitch = clampf(cam.pitch, -1.5f, 1.5f); cam.pos[1] = fmaxf(cam.pos[1], GAL_GROUND + 0.5f);
            if (moved) fly_t = 1;
            // Items.
            int step = IsKeyPressed(KEY_N) || (IsKeyPressed(KEY_TAB) && !shift) ? 1 : IsKeyPressed(KEY_P) || (IsKeyPressed(KEY_TAB) && shift) ? -1 : 0;
            if (step && g->n) {
                if (g->focus < 0) {   // the nearest item first
                    float bd = 1e9f; for (int i = 0; i < g->n; i++) { float d = hypotf(g->item[i].x - cam.pos[0], g->item[i].z - cam.pos[2]); if (d < bd) { bd = d; g->focus = i; } }
                } else g->focus = (g->focus + step + g->n) % g->n;
                from = cam; gallery_frame(&g->item[g->focus], &to); fly_t = 0;
            }
            if (IsKeyPressed(KEY_L)) { g->lod_mode = (g->lod_mode + 1) % 4; cret_debug_print(dbg, (Color){235, 235, 235, 255}, "LOD: %s", gallery_lod_name(g->lod_mode)); }
            if (IsKeyPressed(KEY_T)) sim->time_of_day = fmodf(sim->time_of_day + 1.0f / 24, 1.0f);
            if (IsKeyPressed(KEY_G)) sim->time_of_day = fmodf(sim->time_of_day + 23.0f / 24, 1.0f);
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) sim->time_of_day = fmodf(sim->time_of_day + 1.0f / 144, 1.0f);
            if (IsKeyPressed(KEY_LEFT_BRACKET)) sim->time_of_day = fmodf(sim->time_of_day + 143.0f / 144, 1.0f);
            if (IsKeyPressed(KEY_R)) { weather = (weather + 1) % 3; cret_debug_print(dbg, (Color){235, 235, 235, 255}, "weather %s", wname[weather]); }
            if (IsKeyPressed(KEY_C) || IsKeyPressed(KEY_V)) {
                int d = IsKeyPressed(KEY_C) ? 1 : GC_COUNT - 1;
                if (g->focus >= 0 && g->item[g->focus].type == GI_DINO) { int k = g->item[g->focus].variant; g->dino_clip[k] = ((g->dino_clip[k] < 0 ? g->clip : g->dino_clip[k]) + d) % GC_COUNT; }
                else { g->clip = (g->clip + d) % GC_COUNT; for (int k = 0; k < CRET_SPECIES; k++) g->dino_clip[k] = -1; }
                g->clip_t = 0;
            }
            if (IsKeyPressed(KEY_Z)) g->slow = g->slow > 0.5f ? 0.25f : g->slow > 0.2f ? 0.1f : 1.0f;
            if (IsKeyPressed(KEY_X)) g->wire = !g->wire;
            if (IsKeyPressed(KEY_K)) g->skel = !g->skel;
            if (IsKeyPressed(KEY_H)) g->labels = !g->labels;
            if (IsKeyPressed(KEY_F1)) help = !help;
            if (IsKeyPressed(KEY_F2)) view.settings.wireframe = !view.settings.wireframe;
            if (IsKeyPressed(KEY_F5)) cret_view_reload_shaders(&view);
        }
        if (fly_t < 1) {   // ease to the item
            fly_t = fminf(fly_t + dt / 0.45f, 1); float e = fly_t * fly_t * (3 - 2 * fly_t);
            for (int k = 0; k < 3; k++) cam.pos[k] = from.pos[k] + (to.pos[k] - from.pos[k]) * e;
            cam.yaw = from.yaw + wrap_pi(to.yaw - from.yaw) * e; cam.pitch = from.pitch + (to.pitch - from.pitch) * e;
        }
        sim->cloud_cover = sim->cfg.cloud_cover = wcov[weather]; view.settings.cloud_coverage = wcov[weather];
        gallery_apply_lod(g, &view, &cam);
        gallery_pose_dinos(g, sim, &view, dt * g->slow);
        BeginDrawing();
        cret_view_render(&view, sim, &cam, dt * g->slow);
        int fs = 15;
        if (g->labels) gallery_labels(g, &view, &cam, fs);
        if (g->skel) for (int k = 0; k < g->ndino; k++) gallery_skeleton(&view, sim, g->dino_slot[k]);
        dev_text(TextFormat("gallery  |  %d fps  |  time %05.2f  |  %s  |  LOD %s  |  clip %s x%.2f%s%s", GetFPS(), sim->time_of_day * 24, wname[weather], gallery_lod_name(g->lod_mode),
                            GC_NAME[g->clip], g->slow, g->wire ? "  |  wireframe" : "", g->skel ? "  |  skeleton" : ""), 12, 10, 16, (Color){235, 235, 235, 230});
        if (g->focus >= 0) { char t[256]; gallery_item_info(g, &view, &g->item[g->focus], &cam, t, sizeof t); dev_text(TextFormat("[%d/%d %s] %s", g->focus + 1, g->n, GAL_ROW[g->item[g->focus].type], t), 12, 32, 16, (Color){255, 235, 140, 255}); }
        if (help) {
            const char* h = "WASD fly  Q/E down/up  Shift fast  RMB or arrows look  Tab/N/P items  L LOD  T/G hour  [ ] sun  R weather  C/V clip  Z slow  X wire  K skeleton  H labels  F10 shot  ` console  F1 help  Esc quit";
            dev_text(h, GetScreenWidth() - MeasureText(h, 14) - 12, GetScreenHeight() - 24, 14, (Color){230, 230, 230, 190});
        }
        cret_debug_draw(dbg, sim, sim, &view, &cam, 0, 1);
        EndDrawing();
        if (!con && IsKeyPressed(KEY_F10)) {
            dev_mkdirs(shot_dir); char p[1024]; snprintf(p, sizeof p, "%s/gallery_%03d.png", shot_dir, shots++);
            Image im = LoadImageFromScreen(); ExportImage(im, p); UnloadImage(im); cret_debug_print(dbg, (Color){150, 230, 150, 255}, "saved %s", p);
        }
    }
    cret_view_close(&view); CloseWindow(); cret_sim_close(sim); free(sim); free(g); free(dbg);
    return 0;
}

// Windowless: each row in pages of a few items, labelled, plus an overview.
// only: a --shot list (shot_selected) of the pages to write, NULL for all.
static int capture_gallery(const char* dir, const char* only, int W, int H) {
    dev_mkdirs(dir);
    SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(W, H, "cretaceous gallery capture"); SetTraceLogLevel(LOG_WARNING);
    CretSim* sim = NULL; CretView view;
    if (gallery_init(&sim, &view, W, H)) return 1;
    if (!cret_view_init(&view, sim->world, W, H)) return 1;
    Gallery* g = calloc(1, sizeof *g); gallery_build(g, sim, &view);
    GalFx gf = {g, sim}; view.fx_draw = gallery_fx_draw; view.fx_ctx = &gf;
    typedef struct { char name[64]; CretCamera cam; int clip; bool wire, skel; int lod, lo, hi; } Page; Page pages[96]; int np = 0;
    // Pages: a few neighbours of one row (props: up to 36 m wide), framed from the front and slightly above.
    for (int row = 0; row < GI_ROWS; row++) {
        int idx[128], n = 0; for (int i = 0; i < g->n && n < 128; i++) if (g->item[i].type == row) idx[n++] = i;
        int cap = row == GI_TREE ? 3 : row == GI_PROP ? 6 : row == GI_NEST ? 3 : 2, page = 1;
        for (int k = 0; k < n && np < 90;) {
            int p0 = k; float xmin = 1e9f, xmax = -1e9f, hmax = 0, z = g->item[idx[k]].z;
            while (k < n && k - p0 < cap && g->item[idx[k]].z == z) {
                const GalItem* it = &g->item[idx[k]];
                if (row == GI_PROP && k > p0 && it->x + it->r - xmin > 36) break;
                xmin = fminf(xmin, it->x - it->r); xmax = fmaxf(xmax, it->x + it->r); hmax = fmaxf(hmax, it->h); k++;
            }
            float w = xmax - xmin, xc = 0.5f * (xmin + xmax), el = row == GI_COVER ? 0.55f : row == GI_NEST ? 0.62f : 0.2f;
            float d = row == GI_NEST ? 0.55f * w + 1.5f : fmaxf(0.62f * w, 1.15f * hmax) + (row == GI_COVER ? 6 : 3), ty = GAL_GROUND + 0.4f * hmax;
            Page* P = &pages[np++]; memset(P, 0, sizeof *P); snprintf(P->name, sizeof P->name, "gallery_%s_%02d", GAL_ROW[row], page++);
            P->cam = (CretCamera){{xc, ty + d * sinf(el), z - d * cosf(el)}, PI / 2, -el, 60};
            P->clip = GC_IDLE; P->lo = idx[p0]; P->hi = idx[k - 1] + 1;
        }
    }
    // A few extra pages: the dinosaurs mid-run with skeletons, wireframe, and props at their lowest LOD.
    for (int k = 0; k < np && np < 92; k++) if (!strcmp(pages[k].name, "gallery_dinos_01")) {
        pages[np] = pages[k]; snprintf(pages[np].name, 64, "gallery_dinos_01_run_skeleton"); pages[np].clip = GC_RUN; pages[np].skel = true; np++;
        pages[np] = pages[k]; snprintf(pages[np].name, 64, "gallery_dinos_01_attack_wire"); pages[np].clip = GC_ATTACK; pages[np].wire = true; np++;
        break;
    }
    for (int k = 0; k < np && np < 93; k++) if (!strcmp(pages[k].name, "gallery_trees_01")) { pages[np] = pages[k]; snprintf(pages[np].name, 64, "gallery_trees_01_impostor"); pages[np].lod = 3; np++; break; }
    pages[np++] = (Page){"gallery_overview", {{gal_centre() - 40, GAL_GROUND + 75, gal_row_z(GI_TREE) - 140}, PI / 2 - 0.15f, -0.38f, 60}, GC_IDLE, false, false, 0, 0, 0};
    // CRET_GALLERY_LODS=1: every tree page also forced to the full, reduced and
    // impostor LOD from the same camera (_full, _reduced, _impostor), to check
    // that the three agree (branch positions, silhouette, lighting).
    bool lods = getenv("CRET_GALLERY_LODS") && atoi(getenv("CRET_GALLERY_LODS")) != 0;
    int rc = 0;
    for (int s = 0; s < np; s++) for (int lm = 0; lm < 4; lm++) {
        Page* P = &pages[s]; g->clip = P->clip; g->wire = P->wire; g->skel = P->skel; g->lod_mode = lm ? lm : P->lod; g->focus = -1; g->clip_t = 0; g->only_lo = P->lo; g->only_hi = P->hi;
        if (lm && (!lods || strncmp(P->name, "gallery_trees_", 14) || P->lod)) continue;
        if (only && !shot_selected(only, P->name)) continue;
        view.adapt_valid = false; view.cam_pos[1] += 1e4f;
        for (int f = 0; f < 30; f++) {
            gallery_apply_lod(g, &view, &P->cam); gallery_pose_dinos(g, sim, &view, 1.0f / 30);
            BeginDrawing(); cret_view_render(&view, sim, &P->cam, 1.0f / 30);
            if (f == 29) {
                gallery_labels(g, &view, &P->cam, 14);
                if (g->skel) for (int k = 0; k < g->ndino; k++) gallery_skeleton(&view, sim, g->dino_slot[k]);
                dev_text(TextFormat("%s  |  LOD %s  |  clip %s", P->name, gallery_lod_name(g->lod_mode), GC_NAME[g->clip]), 12, 10, 16, (Color){235, 235, 235, 230});
            }
            EndDrawing();
        }
        static const char* LM[4] = {"", "_full", "_reduced", "_impostor"};
        Image im = LoadImageFromScreen(); char path[1024]; snprintf(path, sizeof path, "%s/%s%s.png", dir, P->name, LM[lm]);
        if (!ExportImage(im, path)) rc = 1;
        UnloadImage(im); printf("wrote %s\n", path);
    }
    cret_view_close(&view); CloseWindow(); cret_sim_close(sim); free(sim); free(g);
    return rc;
}

// ================================================================ scenarios
typedef struct { float yaw, ang; float pos[3]; bool init; } ScnCam;

// Cameras: follow (behind the player), side (across the action), orbit, high (from above at 50 degrees).
static void scenario_camera(CretScnRun* r, const CretSim* s, int mode, ScnCam* sc, CretCamera* cam, float dt) {
    float cx, cz, rad;
    if (!cret_scenario_centre(r, s, &cx, &cz, &rad)) { cx = r->ox; cz = r->oz; rad = 10; }
    const CretAgent* p = &s->agents[0];
    float want[3], look[3]; float dist = fmaxf(16.0f, rad * 1.7f + 6);
    if (mode == SCN_CAM_FOLLOW && p->alive) {
        float L = CRET_SPECIES_TABLE[p->species].length * cret_growth_scale(p->growth), bd = fmaxf(L * 2.2f, 6);
        sc->yaw += wrap_pi(p->yaw - sc->yaw) * (1 - expf(-dt * 2.0f));
        want[0] = p->x - cosf(sc->yaw) * bd; want[2] = p->z - sinf(sc->yaw) * bd; want[1] = p->y + p->height * 1.4f + 1.0f;
        look[0] = p->x + cosf(sc->yaw) * L; look[1] = p->y + p->height * 0.8f; look[2] = p->z + sinf(sc->yaw) * L;
    } else {
        if (mode == SCN_CAM_ORBIT) sc->ang += dt * 0.12f;
        float a = mode == SCN_CAM_ORBIT ? sc->ang : -PI / 2, up = mode == SCN_CAM_HIGH ? dist * 0.95f : fmaxf(3.0f, dist * 0.22f);
        float back = mode == SCN_CAM_HIGH ? dist * 0.8f : dist;
        want[0] = cx + cosf(a) * back; want[2] = cz + sinf(a) * back; want[1] = cret_height(s->world, cx, cz) + up;
        look[0] = cx; look[2] = cz; look[1] = cret_height(s->world, cx, cz) + 1.2f;
    }
    want[1] = fmaxf(want[1], cret_height(s->world, want[0], want[2]) + 1.5f);
    float k = sc->init ? 1 - expf(-dt * 3.0f) : 1; sc->init = true;
    for (int q = 0; q < 3; q++) sc->pos[q] += (want[q] - sc->pos[q]) * k;
    memcpy(cam->pos, sc->pos, sizeof sc->pos);
    cam->yaw = atan2f(look[2] - cam->pos[2], look[0] - cam->pos[0]);
    cam->pitch = atan2f(look[1] - cam->pos[1], hypotf(look[0] - cam->pos[0], look[2] - cam->pos[2])); cam->fov = 60;
}

static void scenario_overlay(const CretScnRun* r, float ui, bool manual, int cam_mode, bool paused) {
    int fs = (int)(15 * ui);
    dev_text(TextFormat("%s  |  %.1f / %.0f s%s%s  |  camera %s%s", r->sc.title, r->tick / (float)CRET_TICK_HZ, r->sc.seconds, r->ended[0] ? "  |  ended: " : "", r->ended,
                        CRET_SCN_CAM_NAMES[cam_mode], manual ? "  |  YOU have the player" : ""), 12 * ui, 10 * ui, (int)(17 * ui), (Color){240, 240, 240, 240});
    char sum[2048]; cret_scenario_summary(r, sum, sizeof sum);
    float y = 34 * ui; char* line = strchr(sum, '\n'); line = line ? line + 1 : sum;
    for (char* e; *line; line = e + 1) {
        e = strchr(line, '\n'); if (!e) break; *e = 0;
        dev_text(line, 12 * ui, y, fs, (Color){225, 225, 225, 220}); y += fs * 1.25f;
    }
    if (paused) dev_text("paused", GetScreenWidth() * 0.5f - 30, GetScreenHeight() * 0.4f, (int)(24 * ui), (Color){255, 255, 255, 230});
}

static bool scenario_setup(const char* name, CretScenario* sc, CretSim** ps, CretScnRun** pr) {
    char err[256];
    if (name[0] == '[') { if (!cret_scenario_parse(name, "fight", sc, err, sizeof err)) { fprintf(stderr, "your fight: %s\n", err); return false; } }
    else if (!cret_scenario_load(name, sc, err, sizeof err)) {
        fprintf(stderr, "scenario %s: %s\n", name, err);
        char names[64][48]; int n = cret_scenario_list(names, 64);
        fprintf(stderr, "scenarios in %s:", CRET_SCN_DIR); for (int k = 0; k < n; k++) fprintf(stderr, " %s", names[k]); fprintf(stderr, "\n");
        return false;
    }
    CretConfig c = cret_scenario_config(sc, cret_default_config()); c.seed = (uint32_t)time(NULL);
    CretSim* s = *ps ? *ps : calloc(1, sizeof *s);
    if (*ps) cret_sim_close(s);
    if (cret_sim_init(s, c) != 0) { fprintf(stderr, "scenario: world failed\n"); return false; }
    CretScnRun* r = *pr ? *pr : calloc(1, sizeof *r);
    if (!cret_scenario_start(r, sc, s)) { fprintf(stderr, "scenario: start failed\n"); return false; }
    *ps = s; *pr = r;
    return true;
}

// Returns 0, 1 on failure, or 3 when the menu asked for the gallery.
static int run_scenario_play(const char* name0, int W, int H, bool size_set) {
    CretScenario sc; CretSim* sim = NULL; CretScnRun* r = NULL;
    // Every scenario and its title, for the Esc menu (no name: open on it).
    static char names[32][48], titles[32][96]; int nscn = cret_scenario_list(names, 32);
    for (int k = 0; k < nscn; k++) { CretScenario t; char e[200]; snprintf(titles[k], sizeof titles[k], "%s", cret_scenario_load(names[k], &t, e, sizeof e) ? t.title : names[k]); }
    static char name[1024]; snprintf(name, sizeof name, "%s", name0 && name0[0] ? name0 : nscn ? names[0] : "");
    bool menu_open = !(name0 && name0[0]); int ret = 0;
    if (!scenario_setup(name, &sc, &sim, &r)) return 1;
    CretSettings st; cret_settings_load(&st); if (!size_set) cret_settings_resolution(st.res, &W, &H);
    if (!IsWindowReady()) { SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT); InitWindow(W, H, "Cretaceous scenario"); }   // the web build keeps the title's window
    SetTraceLogLevel(LOG_WARNING); SetExitKey(KEY_NULL);
    CretView view; if (!cret_view_init(&view, sim->world, GetRenderWidth(), GetRenderHeight())) return 1;
    if (!getenv("CRET_PRESET")) cret_view_set_preset(&view, st.preset);
    CretHud hud; cret_hud_init(&hud); hud.ui_scale = st.ui_scale;
    CretFx* fx = calloc(1, sizeof(CretFx)); cret_fx_init(fx, sim->world, view.dinos_ok ? &view.dinos : NULL);
    view.fx_draw = cret_fx_draw; view.fx_ctx = fx; hud.fb = &fx->hud;
    Control ctl = {.yaw = sim->agents[0].yaw, .pitch = -0.15f, .zoom = 1}; mouse_init(&ctl.mouse); ctl.sens = st.mouse_sens; ctl.invert_y = st.invert_y;
    CretDebug* dbg = calloc(1, sizeof *dbg); cret_debug_init(dbg, NULL, 1, true);
    CretSim* draw = calloc(1, sizeof *draw); CretAgent prev[CRET_MAX_AGENTS]; memcpy(prev, sim->agents, sizeof prev);
    CretCamera cam = {0}; ScnCam scam = {0}; int cam_mode = sc.camera; bool paused = false, printed = false; double acc = 0; float speed = 1;
    CretInput inputs[CRET_MAX_AGENTS];
    printf("scenario %s: \"%s\"  (M take over the player, C camera, F free cam, Space pause, [ ] speed, R restart, ` console, Esc menu)\n", sc.name, sc.title);
    CretMenu pick; cret_menu_init(&pick, 0); int current = 0; for (int k = 0; k < nscn; k++) if (!strcmp(names[k], name)) current = k;
    #define SCN_YOU() do { if (sc.you) { r->manual = true; ctl.yaw = sim->agents[0].yaw; ctl.target_ok = false; mouse_capture(&ctl.mouse, !menu_open); } } while (0)
    SCN_YOU();
    while (!WindowShouldClose()) {
        float dt = GetFrameTime(); if (dt > 0.1f) dt = 0.1f;
        if (IsWindowResized()) cret_view_resize(&view, GetRenderWidth(), GetRenderHeight());
        cret_touch_frame();
        bool con = !menu_open && cret_debug_frame(dbg, sim, &view, &cam, 0); if (dbg->cut) ctl.target_ok = false;
        bool just_opened = false;   // the Esc that opens the menu must not also close it this frame
        // Touch play of your own fight: the controls, and their menu button for the scenario menu.
        CretTouchPlay tp = r->manual && !con && !menu_open && !ctl.free_cam ? cret_touch_play(&hud, sim, 0) : (CretTouchPlay){0};
        ctl.touch_look = tp.look; ctl.touch_looking = tp.looking;
        if (!con && !menu_open && (IsKeyPressed(KEY_ESCAPE) || tp.menu)) { menu_open = true; just_opened = true; pick.sel = 0; mouse_capture(&ctl.mouse, false); }
        else if (!con && !menu_open) {
            if (IsKeyPressed(KEY_M)) { r->manual = !r->manual; ctl.yaw = sim->agents[0].yaw; ctl.target_ok = false; mouse_capture(&ctl.mouse, r->manual); }
            if (IsKeyPressed(KEY_C)) cam_mode = (cam_mode + 1) % SCN_CAM_KINDS;
            if (IsKeyPressed(KEY_F)) { ctl.free_cam = !ctl.free_cam; if (ctl.free_cam) { memcpy(ctl.pos, cam.pos, sizeof ctl.pos); ctl.yaw = cam.yaw; ctl.pitch = cam.pitch; } }
            if (IsKeyPressed(KEY_SPACE) && !r->manual) paused = !paused;
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) speed = fminf(speed * 2, 8);
            if (IsKeyPressed(KEY_LEFT_BRACKET)) speed = fmaxf(speed * 0.5f, 0.125f);
            if (IsKeyPressed(KEY_R) && !r->manual) { if (!scenario_setup(name, &sc, &sim, &r)) break; printed = false; scam.init = false; memcpy(prev, sim->agents, sizeof prev); cret_fx_clear(fx); SCN_YOU(); }
            if (IsKeyPressed(KEY_F5)) cret_view_reload_shaders(&view);
        }
        CretInput mine = r->manual && !con && !menu_open && !ctl.free_cam ? player_input(&ctl, &sim->agents[0], sim->world, &tp) : (CretInput){0};
        acc += paused || menu_open ? 0 : dt * speed;
        while (acc >= CRET_DT) {
            acc -= CRET_DT;
            if (r->done) break;
            cret_scenario_inputs(r, sim, inputs);
            if (r->manual) inputs[0] = mine;
            cret_debug_inputs(dbg, sim, inputs, 0);
            memcpy(prev, sim->agents, sizeof prev);
            cret_sim_step(sim, inputs); cret_scenario_after_step(r, sim); cret_debug_after_step(dbg, sim, 0);
            mine.action = 0; mine.jump = mine.ability = 0;
        }
        if (r->done && !printed) { char out[2048]; cret_scenario_summary(r, out, sizeof out); printf("%s", out); printed = true; }
        interpolate_agents(draw, sim, prev, (float)(acc / CRET_DT));
        cret_fx_update(fx, sim, draw, r->manual ? 0 : -1, &g_fxs, NULL, paused ? 0 : dt);
        if (r->manual || ctl.free_cam) update_camera(&ctl, draw, view.veg_ok ? &view.veg : NULL, 0, &cam, !con, dt);
        else scenario_camera(r, draw, cam_mode, &scam, &cam, dt);
        BeginDrawing();
        cret_view_render(&view, draw, &cam, paused ? 1e-4f : dt * speed);
        if (r->manual) cret_hud_draw(&hud, draw, 0, view.viewproj, cam.yaw);
        if (r->manual && !menu_open) cret_touch_draw(&hud, draw, 0);
        float ui = fmaxf(1, cret_hud_scale(&hud));
        scenario_overlay(r, ui, r->manual, cam_mode, paused);
        const char* h = "M take over  C camera  F free cam  Space pause  [ ] speed  R restart  ` console  Esc menu (all scenarios)";
        if (!cret_touch_active()) dev_text(h, GetScreenWidth() - MeasureText(h, (int)(14 * ui)) - 12, GetScreenHeight() - 24 * ui, (int)(14 * ui), (Color){230, 230, 230, 180});
        cret_debug_draw(dbg, draw, sim, &view, &cam, 0, ui);
        int choice = menu_open ? cret_menu_scenarios(&pick, &hud, NULL, (const char (*)[96])titles, nscn, current) : CRET_SCNMENU_NONE;
        EndDrawing();
        if (choice == CRET_SCNMENU_NONE || (just_opened && choice == CRET_SCNMENU_RESUME)) continue;
        menu_open = false; if (r->manual) mouse_capture(&ctl.mouse, true);
        if (choice == CRET_SCNMENU_QUIT) break;
        if (choice == CRET_SCNMENU_GALLERY) { ret = 3; break; }
        if (choice >= 0 || choice == CRET_SCNMENU_RESTART || choice == CRET_SCNMENU_FIGHT) {
            if (choice >= 0) { current = choice; snprintf(name, sizeof name, "%s", names[choice]); }
            if (choice == CRET_SCNMENU_FIGHT) { current = -1; cret_menu_fight_ini(&pick, name, sizeof name); }
            if (!scenario_setup(name, &sc, &sim, &r)) { ret = 1; break; }
            printed = false; paused = false; scam.init = false; cam_mode = sc.camera; ctl.free_cam = false;
            memcpy(prev, sim->agents, sizeof prev); cret_fx_clear(fx);
            SetWindowTitle(TextFormat("Cretaceous scenario: %s", sc.title));
            SCN_YOU();
        }
    }
    if (!printed) { char out[2048]; cret_scenario_summary(r, out, sizeof out); printf("%s", out); }
    cret_fx_close(fx); free(fx); cret_hud_close(&hud); cret_view_close(&view); CloseWindow();
    cret_sim_close(sim); free(sim); free(r); free(draw); free(dbg);
    return ret;
}

// Windowless: DIR/NAME.mp4 (30 fps, the scenario's camera), DIR/NAME/tNN.png every 5 s, DIR/NAME_summary.txt.
static int capture_scenario(const char* name, const char* dir, int W, int H) {
    CretScenario sc; CretSim* sim = NULL; CretScnRun* r = NULL;
    if (!scenario_setup(name, &sc, &sim, &r)) return 1;
    dev_mkdirs(dir);
    char sd[1024]; snprintf(sd, sizeof sd, "%s/%s", dir, sc.name); dev_mkdirs(sd);
    SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(W, H, "cretaceous scenario capture"); SetTraceLogLevel(LOG_WARNING);
    CretView view; if (!cret_view_init(&view, sim->world, W, H)) return 1;
    CretFx* fx = calloc(1, sizeof(CretFx)); cret_fx_init(fx, sim->world, view.dinos_ok ? &view.dinos : NULL);
    view.fx_draw = cret_fx_draw; view.fx_ctx = fx;
    const CretFxSettings cap_fxs = {0.4f, 0.0f};   // no hit-stop in a review video
    const char* ff = getenv("CRET_FFMPEG") ? getenv("CRET_FFMPEG") : "ffmpeg";
    char out[1100], cmd[2400]; snprintf(out, sizeof out, "%s/%s.mp4", dir, sc.name);
#ifdef _WIN32
    const char* q = "\"", *pmode = "wb";
#else
    const char* q = "'", *pmode = "w";
#endif
    snprintf(cmd, sizeof cmd, "%s -y -loglevel error -f rawvideo -pix_fmt rgba -s %dx%d -r 30 -i - -c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p -movflags +faststart %s%s%s",
             ff, W, H, q, out, q);
    FILE* pipe = popen(cmd, pmode); if (!pipe) { fprintf(stderr, "scenario: cannot run %s\n", ff); return 1; }
    CretInput inputs[CRET_MAX_AGENTS]; CretCamera cam = {0}; ScnCam scam = {0};
    int frames = 0;
    for (int f = -30; !r->done; f++) {
        if (f >= 0) {   // one 30 Hz tick per frame: real time at 30 fps
            cret_scenario_inputs(r, sim, inputs); cret_sim_step(sim, inputs); cret_scenario_after_step(r, sim);
        }
        cret_fx_update(fx, sim, sim, -1, &cap_fxs, NULL, f >= 0 ? CRET_DT : 0);
        scenario_camera(r, sim, sc.camera, &scam, &cam, CRET_DT);
        BeginDrawing(); cret_view_render(&view, sim, &cam, CRET_DT);
        scenario_overlay(r, (float)H / 1080.0f * 1.3f, false, sc.camera, false);
        EndDrawing();
        if (f < 0) continue;
        Image im = LoadImageFromScreen(); if (im.width != W || im.height != H) ImageResize(&im, W, H);
        fwrite(im.data, 4, (size_t)W * H, pipe);
        if (f % (5 * CRET_TICK_HZ) == 0 || r->done) { char p[1200]; snprintf(p, sizeof p, "%s/t%03d.png", sd, f / CRET_TICK_HZ); ExportImage(im, p); }
        UnloadImage(im); frames++;
    }
    int rc = pclose(pipe) != 0;
    char sum[2048]; cret_scenario_summary(r, sum, sizeof sum); printf("%s", sum);
    char sp[1100]; snprintf(sp, sizeof sp, "%s/%s_summary.txt", dir, sc.name); FILE* f = fopen(sp, "w"); if (f) { fprintf(f, "%s\n%s", sc.title, sum); fclose(f); }
    printf("scenario %s: %d frames -> %s%s\n", sc.name, frames, out, rc ? " (ffmpeg failed)" : "");
    cret_fx_close(fx); free(fx); cret_view_close(&view); CloseWindow(); cret_sim_close(sim); free(sim); free(r);
    return rc;
}

// Nest drawing smoke test (make nest-smoke, capture DIR --nest-smoke): the
// gallery's raptor nest with five eggs, framed as Tab frames it, rendered with
// the nests and again without them. Passes when nest.c drew the nest and its
// five eggs and at least 1% of the frame changed; writes both frames.
static int capture_nest_smoke(const char* dir, int W, int H) {
    dev_mkdirs(dir);
    SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(W, H, "cretaceous nest smoke"); SetTraceLogLevel(LOG_WARNING);
    CretSim* sim = NULL; CretView view;
    if (gallery_init(&sim, &view, W, H) || !cret_view_init(&view, sim->world, W, H)) return 1;
    Gallery* g = calloc(1, sizeof *g); gallery_build(g, sim, &view);
    int focus = -1; for (int i = 0; i < g->n; i++) if (g->item[i].type == GI_NEST && g->item[i].kind == CRET_SP_RAPTOR && g->item[i].variant == 5) focus = i;
    CretCamera cam; if (focus >= 0) gallery_frame(&g->item[focus], &cam);
    Image shot[2] = {0}; int nests = 0, eggs = 0, nshow = view.nests.nshow;
    for (int pass = 0; pass < 2 && focus >= 0; pass++) {
        view.nests.nshow = pass ? 0 : nshow; view.adapt_valid = false; view.cam_pos[1] += 1e4f;
        for (int f = 0; f < 30; f++) { BeginDrawing(); cret_view_render(&view, sim, &cam, 1.0f / 30); EndDrawing(); }
        if (!pass) { nests = view.nests.drawn_nests; eggs = view.nests.drawn_eggs; }
        shot[pass] = LoadImageFromScreen();
        char path[1024]; snprintf(path, sizeof path, "%s/nest_smoke_%s.png", dir, pass ? "without" : "with"); ExportImage(shot[pass], path);
    }
    long changed = 0, total = (long)W * H;
    if (shot[0].data && shot[1].data && shot[0].width == shot[1].width && shot[0].height == shot[1].height) {
        const uint8_t *a = shot[0].data, *b = shot[1].data; total = (long)shot[0].width * shot[0].height;
        for (long p = 0; p < total; p++) changed += abs(a[4 * p] - b[4 * p]) + abs(a[4 * p + 1] - b[4 * p + 1]) + abs(a[4 * p + 2] - b[4 * p + 2]) > 24;
    }
    bool ok = focus >= 0 && nests >= 1 && eggs >= 5 && changed * 100 >= total;
    printf("nest smoke: %s (%d nests and %d eggs drawn, %.1f%% of the frame changed)\n", ok ? "ok" : "FAILED", nests, eggs, 100.0 * changed / (total ? total : 1));
    for (int k = 0; k < 2; k++) if (shot[k].data) UnloadImage(shot[k]);
    cret_view_close(&view); CloseWindow(); cret_sim_close(sim); free(sim); free(g);
    return ok ? 0 : 1;
}

static int cret_devmode_run(const DevArgs* d, bool capture, const char* dir, const char* only, int W, int H, bool size_set) {
    if (d->kind == DEV_NEST_SMOKE) return capture_nest_smoke(capture && dir ? dir : "build/cretaceous/nest_smoke", W, H);
    if (d->kind == DEV_GALLERY) return capture ? capture_gallery(dir, only, W, H) : run_gallery(W, H, size_set);
    if (d->kind == DEV_SCENARIO) {
        if (capture) return capture_scenario(d->name, dir, W, H);
        int rc = run_scenario_play(d->name, W, H, size_set);
        return rc == 3 ? run_gallery(W, H, size_set) : rc;   // Gallery from the scenario menu
    }
    return 2;
}
