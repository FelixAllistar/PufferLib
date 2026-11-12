// The hunting part of the HUD (included by hud.c): what leads a carnivore to
// small prey it has noticed (scent.h cret_prey_noticed: seen in front, heard
// close by, or smelled downwind), and the scent panel while it sniffs.
//   - the food prompt says what and where, relative to the camera ("Lizard
//     9 m ahead to the left"), with how to get it as a hint under it that
//     fades: creep while it feeds, keep still while it looks round;
//   - a tick on the compass strip and, when the prey is off screen, an arrow
//     at the screen edge; on screen a small chevron over it (none within 2 m);
//   - the catch quest's tracker line names the nearest noticed prey, or
//     says to sniff;
//   - while sniffing, a panel lists what the nose finds (prey and carcasses
//     with their place, other animals only as a trail: its age and where it
//     went) and where the wind comes from.
// The prey stays chosen for HUNT_HOLD s after it was last noticed, so the
// marker does not flicker as it slips in and out of sight.

#define HUNT_RANGE 30.0f
#define HUNT_HOLD 2.0f
static const char* const HUNT_NAME[CRET_CR_KINDS] = {"Small mammal", "Repenomamus", "Lizard", "Bird", "Fish"};

// Where (dx, dz) lies from the camera's view: ahead, to the side, behind.
static const char* hunt_where(float cam_yaw, float dx, float dz) {
    float rel = wrapd((atan2f(dz, dx) - cam_yaw) * 57.29578f), r = fabsf(rel);
    bool right = rel > 0;
    return r < 20 ? "ahead" : r < 60 ? (right ? "ahead to the right" : "ahead to the left") : r < 120 ? (right ? "to your right" : "to your left")
         : r < 160 ? (right ? "behind you to the right" : "behind you to the left") : "behind you";
}

// Once a frame (cret_hud_draw): the prey to lead to, with hysteresis.
static void hunt_update(CretHud* h, const CretSim* sim, int me) {
    const CretAgent* a = &sim->agents[me];
    float now = (float)GetTime(), d; int how = 0;
    bool carn = a->alive && CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_CARNIVORE;
    int k = carn ? cret_prey_noticed(sim, me, h->sniffing, HUNT_RANGE, &d, &how) : -1;
    if (k >= 0) { h->prey = k; h->prey_how = how; h->prey_seen_t = now; }
    else if (h->prey >= 0 && (now - h->prey_seen_t > HUNT_HOLD || !sim->critters[h->prey].active || !carn)) h->prey = -1;
    if (h->prey >= 0) {
        const CretCritter* c = &sim->critters[h->prey];
        h->prey_d = hypotf(c->x - a->x, c->z - a->z);
        if (h->prey_d > HUNT_RANGE + 5) h->prey = -1;
    }
}

// The food prompt for small prey that is noticed but not yet in reach:
// false when there is nothing to say.
static bool hunt_prompt(const CretHud* h, const CretSim* sim, int me, float cam_yaw, Prompt* p) {
    const CretAgent* a = &sim->agents[me];
    if (h->prey < 0) {
        const CretQuest* Q = cret_quest_of(sim, me, 0);
        bool want = a->food < 0.6f || (Q && Q->kind == CRET_Q_CATCH);
        if (h->sniffing) { prompt_set(p, NULL, UI_I_SNIFF, "Sniffing", "no small prey within smell", "Try with the wind in your face"); return true; }
        if (!want) return false;
        prompt_set(p, "V", UI_I_SNIFF, "Sniff", "hold, for small prey", NULL);
        return true;
    }
    if (a->food >= 0.9f && !h->sniffing) return false;
    const CretCritter* c = &sim->critters[h->prey];
    const char* where = hunt_where(cam_yaw, c->x - a->x, c->z - a->z);
    const char* what = HUNT_NAME[c->kind < CRET_CR_KINDS ? c->kind : 0];
    int d = (int)roundf(h->prey_d);
    if (c->state == CRET_CRS_FLEE) { prompt_set(p, NULL, UI_I_PREY, what, TextFormat("running %s, %d m", where, d), NULL); return true; }
    if (c->state == CRET_CRS_HIDE) { prompt_set(p, NULL, UI_I_PREY, what, TextFormat("hiding %s, %d m", where, d), "Creep up and bite"); return true; }
    const char* how = h->prey_how == CRET_SENSE_SMELLED ? "  (smelled)" : h->prey_how == CRET_SENSE_HEARD ? "  (heard)" : "";
    const char* hint = h->touch ? "Stalk it: push the stick gently" : "Stalk it: V creeps and sniffs";
    if (h->prey_d < 12) hint = cret_critter_watch(sim, h->prey) > 0 ? "It is looking round: keep still"
                             : h->touch ? "It is feeding: creep closer (push gently)" : "It is feeding: creep closer";
    prompt_set(p, NULL, UI_I_PREY, what, TextFormat("%d m %s%s", d, where, how), hint);
    return true;
}

// The prey's mark under the compass strip (compass(): centre cx, width W, marker row y, span in degrees).
static void hunt_compass(const CretHud* h, const CretSim* sim, int me, float cx, float W, float y, float span, float heading) {
    if (h->prey < 0) return;
    const CretAgent* a = &sim->agents[me]; const CretCritter* c = &sim->critters[h->prey];
    float d = wrapd((atan2f(c->z - a->z, c->x - a->x) + 1.5707963f) * 57.29578f - heading);
    float x = cx + clampf(d / span, -1, 1) * W * 0.5f;
    icon(UI_I_PREY, (Vector2){x, y + 5}, 11, UI_WARN);
}

// On screen: a small chevron over the prey; off screen: an arrow at the edge.
static void hunt_marker(const CretHud* h, const CretSim* sim, int me, const float* vp, float cam_yaw) {
    if (h->prey < 0 || h->prey_d < 2.0f) return;
    const CretAgent* a = &sim->agents[me]; const CretCritter* c = &sim->critters[h->prey];
    float sw = SW(), sh = SH(), px, py, L = cret_critter_length(c->kind, c->kg);
    float fade = clampf((h->prey_d - 2.0f) / 2.0f, 0, 1);
    Color col = with(UI_WARN, fade);
    bool on = project(vp, c->x, c->y + c->alt + L * 0.6f + 0.25f, c->z, &px, &py) && px > 30 && px < sw - 30 && py > 60 && py < sh - 60;
    if (on) {
        icon(UI_I_PREY, (Vector2){px + 1, py - 6}, 20, (Color){0, 0, 0, (unsigned char)(90 * fade)});
        icon(UI_I_PREY, (Vector2){px, py - 7}, 16, col);
        return;
    }
    // Edge arrow: the direction from the screen centre, by the bearing relative to the camera.
    float rel = atan2f(c->z - a->z, c->x - a->x) - cam_yaw;   // 0 ahead, +pi/2 to the right
    float ux = sinf(rel), uy = -cosf(rel);                     // screen space: right, up
    float m = fminf((sw * 0.5f - 40) / fmaxf(fabsf(ux), 1e-3f), (sh * 0.5f - 70) / fmaxf(fabsf(uy), 1e-3f));
    float ex = sw * 0.5f + ux * m, ey = sh * 0.5f + uy * m;
    Vector2 tip = {ex + ux * 12, ey + uy * 12}, l = {ex - uy * 8, ey + ux * 8}, r = {ex + uy * 8, ey - ux * 8};
    DrawTriangle(tip, r, l, fa(col)); DrawTriangle(tip, l, r, fa(col));
    const char* t = TextFormat("%d m", (int)roundf(h->prey_d));
    text_sh(h, t, ex - ux * 24 - text_w(h, false, t, UI_T_SMALL + 1) * 0.5f, ey - uy * 24 - 7, UI_T_SMALL + 1, col);
}

// While sniffing: what the nose finds, 300 units wide from x, below top
// (the bottom of what is drawn above it).
static void hunt_scent_panel(const CretHud* h, const CretSim* sim, int me, float cam_yaw, float x, float top) {
    if (!h->sniffing || !sim->agents[me].alive) return;
    const CretAgent* a = &sim->agents[me];
    CretScent sc; cret_scent_query(sim, me, true, &sc);
    float w = 300, y = top + 18;
    int lines = sc.range[CRET_SCENT_CARCASS] > 0 ? (sc.n ? sc.n : 1) : 1;
    panel((Rectangle){x - UI_PAD, y - UI_PAD + 2, w + 2 * UI_PAD, 46 + lines * 20});
    icon(UI_I_SNIFF, (Vector2){x + 7, y + 6}, 13, UI_DIM);
    label(h, "Scent", x + 20, y, UI_T_SMALL, UI_DIM);
    const char* wind = TextFormat("from the %s%s", bearing(-sc.wind_x, -sc.wind_z), sc.rain > 0.3f ? ", rain" : "");
    float ww = text_w(h, false, wind, UI_T_SMALL + 1);
    icon(UI_I_WIND, (Vector2){x + w - ww - 12, y + 7}, 13, UI_DIM);
    text(h, false, wind, x + w - ww, y - 1, UI_T_SMALL + 1, UI_DIM);
    y += 24;
    if (sc.range[CRET_SCENT_CARCASS] <= 0) { text(h, false, "Plant eaters do not hunt by smell.", x, y, UI_T_BODY - 1, UI_DIM); return; }
    if (!sc.n) { text(h, false, "Nothing on the wind.", x, y, UI_T_BODY - 1, UI_DIM); return; }
    for (int q = 0; q < sc.n; q++) {
        const CretScentHit* s = &sc.hit[q]; const char* t;
        const char* where = hunt_where(cam_yaw, s->x - a->x, s->z - a->z);
        if (s->what == CRET_SCENT_CRITTER) t = TextFormat("%s  %d m %s", HUNT_NAME[sim->critters[s->index].kind % CRET_CR_KINDS], (int)roundf(s->dist), where);
        else if (s->what == CRET_SCENT_CARCASS) t = TextFormat("Carcass  %d m %s", (int)roundf(s->dist), where);
        else t = TextFormat("%s trail  %d s old, going %s", CRET_SPECIES_TABLE[sim->agents[s->index].species].name, (int)roundf(s->age), bearing(cosf(s->dir), sinf(s->dir)));
        Color c = with(s->what == CRET_SCENT_ANIMAL ? UI_DIM : UI_INK, 0.55f + 0.45f * clampf(s->strength * 2, 0, 1));
        int ic = s->what == CRET_SCENT_CRITTER ? UI_I_PREY : s->what == CRET_SCENT_CARCASS ? UI_I_MEAT : UI_I_SNIFF;
        icon(ic, (Vector2){x + 6, y + 8}, 11, c);
        text(h, false, fit(h, t, UI_T_BODY - 1, w - 20), x + 18, y, UI_T_BODY - 1, c); y += 20;
    }
}
