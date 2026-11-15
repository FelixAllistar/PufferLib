// Interface review (capture DIR --ui; included by devmodes.c after
// huntcap.c, whose spot finder it uses): every menu and HUD screen drawn over
// the real world, written as PNGs, for before and after comparisons.
//
// Three backdrops are rendered once each with the game's camera rig: a young
// raptor in thick ferns at noon (bright, busy foliage and sky behind the
// compass), the same at night, and a stegosaur in a herd on grassland at
// noon. Each screen is then drawn over its backdrop for half a second of
// frames, so fades and transitions settle, and written as DIR/ui_NAME.png.
//
// Screens: title, settings, keys (key bindings), credits, pause, scenarios,
// carn (carnivore HUD: pack, prey noticed, quests, a target hit, chips),
// night (the same at night), herb (herbivore HUD: herd, a marked predator,
// grazing), scent (sniffing, V), death, map (M), help (F1).
// CRET_TOUCH=1 shows the touch controls (run it at --size 844 390 for a
// phone held sideways). --shot NAME[,NAME] keeps those screens.
//
// CRET_UI_REUSE=1 keeps the backdrops between runs (DIR/ui_bg*.png and
// their camera in DIR/ui_bg*.cam), for quick turns on the look alone.
//
// The HUD's CPU cost per draw is measured on each play screen (the best of
// five means of 100 draws, the touch controls and the key help included) and printed as "hud NAME: X ms";
// the play loop's own figure is the gprof mark hud+menu (CRET_GPU_PROFILE=1).

typedef struct { CretSim* sim; CretView* view; CretHud* hud; Texture2D bg[3]; float vp[3][16], yaw[3]; } UiCap;
enum { UB_CARN, UB_NIGHT, UB_HERB };

// The scene round the player at spot P: species sp at growth g, its group of
// two more, another species 16 m ahead (hit, at 55%), a nest 22 m off to the
// left, a lizard 7 m ahead, and quests (a personal one and a group one).
static void uicap_scene(CretSim* sim, const HuntSpot* P, int sp, int other, bool marked_other) {
    for (int i = 0; i < sim->cfg.num_agents; i++) sim->agents[i].alive = false;
    memset(sim->carcasses, 0, sizeof sim->carcasses); memset(sim->nests, 0, sizeof sim->nests);
    memset(sim->critters, 0, sizeof sim->critters); sim->ncrit = 0;
    float dir = P->dir;
    #define UI_PUT(i, s, g, dx, dz, yw) do { CretAgent* b = &sim->agents[i]; memset(b, 0, sizeof *b); b->alive = true; b->species = (s); b->growth = (g); \
        cret_apply_growth(b); b->health = b->max_health; b->food = b->water = b->stamina = 1; b->on_ground = true; \
        b->x = P->x + cosf(dir) * (dx) - sinf(dir) * (dz); b->z = P->z + sinf(dir) * (dx) + cosf(dir) * (dz); \
        b->y = cret_height(sim->world, b->x, b->z); b->yaw = (yw); cret_sim_place(sim, i); } while (0)
    UI_PUT(0, sp, 0.64f, 0, 0, dir);
    UI_PUT(1, sp, 1.0f, 5, -7, dir + 0.3f);
    UI_PUT(2, sp, 0.5f, -4, 6, dir - 0.2f);
    UI_PUT(3, other, 1.0f, 16, 3, dir + 2.4f);
    #undef UI_PUT
    for (int i = 0; i < 3; i++) sim->agents[i].group = 1;
    CretAgent* a = &sim->agents[0];
    a->health = a->max_health * 0.72f; a->bleed = a->max_health * 0.01f; a->stamina = 0.58f; a->food = 0.46f; a->water = 0.81f;
    sim->agents[2].health = sim->agents[2].max_health * 0.6f; sim->agents[2].broken = 40;
    sim->agents[3].health = sim->agents[3].max_health * 0.55f;
    if (marked_other) { sim->agents[3].st[CRET_ST_MARKED] = 12; sim->agents[3].mark_by = 1; }
    a->rally_ticks = 600; a->rally_x = a->x + cosf(dir + 0.5f) * 60; a->rally_z = a->z + sinf(dir + 0.5f) * 60;
    a->st[CRET_ST_BURST] = 3;
    const CretSpeciesAbilities* AB = &CRET_SPECIES_ABILITIES[sp % CRET_SPECIES];
    CretProgress* p = &sim->prog[0];
    p->loadout[0] = (uint8_t)AB->ab[0]; p->loadout[1] = (uint8_t)AB->ab[1]; p->loadout[2] = 0; p->marks = 34; p->chain = CRET_CHAIN_STEPS;
    a->ab_cd[1] = CRET_ABILITIES[AB->ab[1]].cooldown * 0.6f;
    a->attack_cd = (int)(CRET_SPECIES_TABLE[sp].cooldown * CRET_TICK_HZ * 0.4f);
    sim->nests[0] = (CretNest){.active = true, .species = (uint8_t)other, .group = 9, .owner = 0, .eggs = 3};
    sim->nests[0].x = P->x + cosf(dir - 0.9f) * 22; sim->nests[0].z = P->z + sinf(dir - 0.9f) * 22; sim->nests[0].y = cret_height(sim->world, sim->nests[0].x, sim->nests[0].z);
    sim->crit_cfg = cret_critter_defaults();
    CretCritter* c = &sim->critters[0]; sim->ncrit = 1;
    c->active = 1; c->kind = CRET_CR_LIZARD; c->state = CRET_CRS_IDLE; c->threat = -1; c->kg = CRET_CRITTER_KINDS[CRET_CR_LIZARD].kg_max;
    c->x = a->x + cosf(dir + 0.25f) * 7; c->z = a->z + sinf(dir + 0.25f) * 7; c->y = cret_height(sim->world, c->x, c->z); c->yaw = dir + 2;
    // Quests: the progress step offers both once their waits run out.
    p->quest.kind = 0; p->quest.wait = 0; sim->gquest[1].kind = 0; sim->gquest[1].wait = 0;
    sim->tick = (sim->tick / 30 + 1) * 30; cret_progress_step(sim);
}

// Renders one backdrop (warm-up frames so exposure and clouds settle) into bg[k].
static void uicap_backdrop(UiCap* U, int k, const HuntSpot* P, float tod, int W, int H, const char* dir) {
    CretSim* sim = U->sim; CretAgent* a = &sim->agents[0];
    char png[1024], cam_path[1024]; snprintf(png, sizeof png, "%s/ui_bg%d.png", dir, k); snprintf(cam_path, sizeof cam_path, "%s/ui_bg%d.cam", dir, k);
    if (getenv("CRET_UI_REUSE") && atoi(getenv("CRET_UI_REUSE")) && FileExists(png) && FileExists(cam_path)) {
        Image im = LoadImage(png); FILE* f = fopen(cam_path, "rb");
        bool ok = im.width == W && im.height == H && f && fread(U->vp[k], sizeof(float), 16, f) == 16 && fread(&U->yaw[k], sizeof(float), 1, f) == 1;
        if (f) fclose(f);
        if (ok) { U->bg[k] = LoadTextureFromImage(im); UnloadImage(im); return; }
        UnloadImage(im);
    }
    sim->time_of_day = tod; sim->cloud_cover = 0.3f; U->view->settings.cloud_coverage = 0.3f;
    float pv[3]; cam_pivot(a, pv); float boom = cam_boom(a, 1), pitch = -0.12f, yaw = P->dir - 0.12f;
    float cx = pv[0] - cosf(pitch) * cosf(yaw) * boom, cz = pv[2] - cosf(pitch) * sinf(yaw) * boom;
    CretCamera cam = {{cx, fmaxf(pv[1] - sinf(pitch) * boom, cam_floor(sim->world, cx, cz)), cz}, yaw, pitch, 60};
    U->view->adapt_valid = false; U->view->cam_pos[1] += 1e4f;
    for (int f = 0; f < 24; f++) { BeginDrawing(); cret_view_render(U->view, sim, &cam, 1.0f / 30); EndDrawing(); }
    Image im = LoadImageFromScreen(); U->bg[k] = LoadTextureFromImage(im); ExportImage(im, png); UnloadImage(im);
    memcpy(U->vp[k], U->view->viewproj, sizeof U->vp[k]); U->yaw[k] = yaw;
    FILE* f = fopen(cam_path, "wb"); if (f) { fwrite(U->vp[k], sizeof(float), 16, f); fwrite(&U->yaw[k], sizeof(float), 1, f); fclose(f); }
}

static bool uicap_only(const char* only, const char* name) {
    if (!only) return true;
    char b[512]; snprintf(b, sizeof b, "%s", only);
    for (char* t = strtok(b, ","); t; t = strtok(NULL, ",")) if (!strcmp(t, name)) return true;
    return false;
}

// The key help as the play loop draws it (cretaceous.c): F1 opens it; else its hint.
static void uicap_help(CretHud* hud, bool open) { cret_hud_help(hud, open, false, "` console  F6 dummy  F7 passive  F8 labels  F9 colliders"); }

enum { US_TITLE, US_SETTINGS, US_KEYS, US_CREDITS, US_PAUSE, US_SCENARIOS, US_CARN, US_NIGHT, US_HERB, US_SCENT, US_DEATH, US_MAP, US_HELP, US_COUNT };
static const char* const US_NAME[US_COUNT] = {"title", "settings", "keys", "credits", "pause", "scenarios", "carn", "night", "herb", "scent", "death", "map", "help"};

static int capture_ui(const char* dir, const char* only, int W, int H) {
    dev_mkdirs(dir);
    CretConfig cfg = cret_default_config(); cfg.player_species = CRET_SP_RAPTOR; cfg.num_agents = 8; cfg.weather = 0;
    CretSim* sim = calloc(1, sizeof *sim);
    if (cret_sim_init(sim, cfg) != 0) { fprintf(stderr, "ui: world failed\n"); return 1; }
    SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(W, H, "cretaceous ui capture"); SetTraceLogLevel(LOG_WARNING); SetExitKey(KEY_NULL);
    CretView view; if (!cret_view_init(&view, sim->world, W, H)) return 1;
    view.settings.render_scale = 1; cret_view_resize(&view, W, H);
    CretHud hud; cret_hud_init(&hud); snprintf(hud.character, sizeof hud.character, "Felix");
    CretSettings st; cret_settings_default(&st);
    CretMenu menu; cret_menu_init(&menu, CRET_SP_RAPTOR); cret_menu_characters(&menu, NULL);
    static char names[32][48], titles[32][96]; int nscn = cret_scenario_list(names, 32);
    for (int k = 0; k < nscn; k++) { CretScenario t; char e[200]; snprintf(titles[k], sizeof titles[k], "%.95s", cret_scenario_load(names[k], &t, e, sizeof e) ? t.title : names[k]); }
    HuntSpot spot[HS_COUNT]; hunt_find_spots(sim->world, view.veg_ok ? &view.veg : NULL, spot);
    UiCap U = {.sim = sim, .view = &view, .hud = &hud};
    // The window's own viewport, 2D projection and blending, as after a rendered frame (the renderer's set-up
    // leaves others, and reused backdrops skip its frames).
    #define UI_SCREEN_2D() do { rlDrawRenderBatchActive(); rlViewport(0, 0, GetRenderWidth(), GetRenderHeight()); rlMatrixMode(RL_PROJECTION); rlLoadIdentity(); \
        rlOrtho(0, GetRenderWidth(), GetRenderHeight(), 0, 0, 1); rlMatrixMode(RL_MODELVIEW); rlLoadIdentity(); \
        rlEnableColorBlend(); rlSetBlendMode(RL_BLEND_ALPHA); rlDisableDepthTest(); rlDisableDepthMask(); } while (0)
    // Backdrops: the scene each play screen is drawn over.
    uicap_scene(sim, &spot[HS_FERNS], CRET_SP_RAPTOR, CRET_SP_STEGO, false); uicap_backdrop(&U, UB_CARN, &spot[HS_FERNS], 0.5f, W, H, dir);
    uicap_backdrop(&U, UB_NIGHT, &spot[HS_FERNS], 0.86f, W, H, dir);
    CretSim* herb = calloc(1, sizeof *herb); memcpy(herb, sim, sizeof *herb);
    uicap_scene(herb, &spot[HS_GRASS], CRET_SP_STEGO, CRET_SP_TREX, true);
    { CretSim* keep = U.sim; U.sim = herb; uicap_backdrop(&U, UB_HERB, &spot[HS_GRASS], 0.5f, W, H, dir); U.sim = keep; }
    CretHudFeedback fb; memset(&fb, 0, sizeof fb); fb.marker_t = 9; fb.target = 3; fb.target_t = 2; fb.target_shown = 0.7f;
    for (int i = 0; i < 6; i++) fb.dir[i].t = 9;
    for (int s = 0; s < US_COUNT; s++) {
        if (!uicap_only(only, US_NAME[s])) continue;
        CretSim* S = s == US_HERB ? herb : sim;
        int bg = s == US_NIGHT ? UB_NIGHT : s == US_HERB ? UB_HERB : UB_CARN;
        S->time_of_day = s == US_NIGHT ? 0.86f : 0.5f;
        bool play = s >= US_CARN;
        hud.map_big = s == US_MAP; hud.sniffing = s == US_SCENT; hud.menu = s == US_PAUSE; hud.credits = false; hud.sel = 0;
        hud.fb = s == US_CARN || s == US_NIGHT ? &fb : NULL;
        S->agents[0].alive = s != US_DEATH; S->agents[0].dead_ticks = 2 * CRET_TICK_HZ;
        menu.page = s == US_SETTINGS ? 1 : s == US_KEYS ? 2 : s == US_CREDITS ? 3 : 0; menu.sel = 1; menu.set_sel = 1; menu.t = 0;
        double t0 = GetTime(); int frames = 0;
        // Half a second of frames: fades and the menus' transitions settle.
        while (GetTime() - t0 < 0.5 || frames < 8) {
            cret_touch_frame();
            BeginDrawing(); UI_SCREEN_2D(); ClearBackground(BLACK);
            DrawTexture(U.bg[bg], 0, 0, WHITE);
            if (play || s == US_PAUSE) {
                hud.play_t0 = GetTime() - 10;   // ten seconds into play: the "F1 help" hint shows
                cret_touch_play(&hud, S, 0);
                cret_hud_draw(&hud, S, 0, U.vp[bg], U.yaw[bg]);
                if (!hud.menu) cret_touch_draw(&hud, S, 0);
                if (s == US_PAUSE) cret_hud_menu(&hud, S, 0);
                uicap_help(&hud, s == US_HELP);
            } else if (s == US_SCENARIOS) cret_menu_scenarios(&menu, &hud, NULL, (const char (*)[96])titles, nscn, 0);
            else cret_menu_title(&menu, &hud, NULL, &st, GetFrameTime());
            EndDrawing(); frames++;
        }
        Image im = LoadImageFromScreen(); char path[1024]; snprintf(path, sizeof path, "%s/ui_%s.png", dir, US_NAME[s]);
        ExportImage(im, path); UnloadImage(im); printf("wrote %s\n", path);
        if (play && s != US_HELP) {   // CPU per HUD draw: the best of 5 rounds of 100 (the GL batches are submitted, not waited for)
            double best = 1e9;
            for (int round = 0; round < 5; round++) {
                rlDrawRenderBatchActive(); glFinish(); double c0 = GetTime();
                for (int i = 0; i < 100; i++) { cret_hud_draw(&hud, S, 0, U.vp[bg], U.yaw[bg]); cret_touch_draw(&hud, S, 0); uicap_help(&hud, false); }
                double ms = (GetTime() - c0) * 1000.0 / 100; rlDrawRenderBatchActive(); glFinish();
                if (ms < best) best = ms;
            }
            printf("hud %s: %.3f ms\n", US_NAME[s], best);
        }
    }
    for (int k = 0; k < 3; k++) UnloadTexture(U.bg[k]);
    cret_hud_close(&hud); cret_view_close(&view); CloseWindow(); cret_sim_close(sim); free(sim); free(herb);
    return 0;
}
