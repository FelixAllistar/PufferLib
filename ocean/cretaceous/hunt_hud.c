// The hunting part of the HUD (included by hud.c): what leads a carnivore to
// small prey it has noticed (scent.h cret_prey_noticed: seen in front, heard
// close by, or smelled downwind), and the scent panel while it sniffs.
//   - the food prompt says what and where, relative to the camera ("Lizard
//     ahead to the left, 9 m"), and how to get it: creep while it feeds,
//     keep still while it looks round;
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
static const Color C_PREY = {236, 196, 132, 230};

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

// The food prompt for small prey that is noticed but not yet in reach.
static const char* hunt_prompt(const CretHud* h, const CretSim* sim, int me, float cam_yaw) {
    const CretAgent* a = &sim->agents[me];
    if (h->prey < 0) {
        const CretQuest* Q = cret_quest_of(sim, me, 0);
        bool want = a->food < 0.6f || (Q && Q->kind == CRET_Q_CATCH);
        if (!want || h->sniffing) return h->sniffing ? "Sniffing: no small prey within smell. Try with the wind in your face" : NULL;
        return h->touch ? "Hold Sniff to smell for small prey" : "Hold V to sniff for small prey";
    }
    if (a->food >= 0.9f && !h->sniffing) return NULL;
    const CretCritter* c = &sim->critters[h->prey];
    const char* where = hunt_where(cam_yaw, c->x - a->x, c->z - a->z);
    const char* what = HUNT_NAME[c->kind < CRET_CR_KINDS ? c->kind : 0];
    if (c->state == CRET_CRS_FLEE) return TextFormat("%s running %s, %d m", what, where, (int)roundf(h->prey_d));
    if (c->state == CRET_CRS_HIDE) return TextFormat("%s hiding %s, %d m: creep up and bite", what, where, (int)roundf(h->prey_d));
    const char* how = h->prey_how == CRET_SENSE_SMELLED ? "  (smelled)" : h->prey_how == CRET_SENSE_HEARD ? "  (heard)" : "";
    if (h->prey_d < 12) {
        bool watching = cret_critter_watch(sim, h->prey) > 0;
        return TextFormat("%s %s, %d m%s  -  %s", what, where, (int)roundf(h->prey_d), how,
                          watching ? "it is looking round: keep still" : h->touch ? "it is feeding: creep closer (push gently)" : "it is feeding: creep closer");
    }
    return TextFormat("%s %s, %d m%s  -  %s", what, where, (int)roundf(h->prey_d), how, h->touch ? "stalk it (push gently)" : "stalk it (V creeps and sniffs)");
}

// The prey's tick on the compass strip (compass(): centre cx, width W, top y, span in degrees).
static void hunt_compass(const CretHud* h, const CretSim* sim, int me, float cx, float W, float y, float span, float heading) {
    if (h->prey < 0) return;
    const CretAgent* a = &sim->agents[me]; const CretCritter* c = &sim->critters[h->prey];
    float d = wrapd((atan2f(c->z - a->z, c->x - a->x) + 1.5707963f) * 57.29578f - heading);
    float x = cx + clampf(d / span, -1, 1) * W * 0.5f;
    DrawTriangle((Vector2){x - 5, y + 30}, (Vector2){x, y + 38}, (Vector2){x + 5, y + 30}, C_PREY);
}

// On screen: a small chevron over the prey; off screen: an arrow at the edge.
static void hunt_marker(const CretHud* h, const CretSim* sim, int me, const float* vp, float cam_yaw) {
    if (h->prey < 0 || h->prey_d < 2.0f) return;
    const CretAgent* a = &sim->agents[me]; const CretCritter* c = &sim->critters[h->prey];
    float sw = SW(), sh = SH(), px, py, L = cret_critter_length(c->kind, c->kg);
    float fade = clampf((h->prey_d - 2.0f) / 2.0f, 0, 1);
    Color col = C_PREY; col.a = (unsigned char)(col.a * fade);
    bool on = project(vp, c->x, c->y + c->alt + L * 0.6f + 0.25f, c->z, &px, &py) && px > 30 && px < sw - 30 && py > 60 && py < sh - 60;
    if (on) {
        DrawTriangle((Vector2){px - 7, py - 10}, (Vector2){px, py}, (Vector2){px + 7, py - 10}, (Color){0, 0, 0, (unsigned char)(70 * fade)});
        DrawTriangleLines((Vector2){px - 7, py - 10}, (Vector2){px, py}, (Vector2){px + 7, py - 10}, col);
        return;
    }
    // Edge arrow: the direction from the screen centre, by the bearing relative to the camera.
    float rel = atan2f(c->z - a->z, c->x - a->x) - cam_yaw;   // 0 ahead, +pi/2 to the right
    float ux = sinf(rel), uy = -cosf(rel);                     // screen space: right, up
    float m = fminf((sw * 0.5f - 40) / fmaxf(fabsf(ux), 1e-3f), (sh * 0.5f - 70) / fmaxf(fabsf(uy), 1e-3f));
    float ex = sw * 0.5f + ux * m, ey = sh * 0.5f + uy * m;
    Vector2 tip = {ex + ux * 12, ey + uy * 12}, l = {ex - uy * 8, ey + ux * 8}, r = {ex + uy * 8, ey - ux * 8};
    DrawTriangle(tip, r, l, col); DrawTriangle(tip, l, r, col);
    const char* t = TextFormat("%d m", (int)roundf(h->prey_d));
    text(h, false, t, ex - ux * 24 - text_w(h, false, t, 13) * 0.5f, ey - uy * 24 - 7, 13, col);
}

// While sniffing: what the nose finds, under the minimap.
static void hunt_scent_panel(const CretHud* h, const CretSim* sim, int me, float cam_yaw) {
    if (!h->sniffing || !sim->agents[me].alive) return;
    const CretAgent* a = &sim->agents[me];
    CretScent sc; cret_scent_query(sim, me, true, &sc);
    float w = 300, x = SW() - w - 28, y = (h->touch ? 180 : 220) + 44;
    int lines = sc.range[CRET_SCENT_CARCASS] > 0 ? (sc.n ? sc.n : 1) : 1;
    DrawRectangleRounded((Rectangle){x - 12, y - 10, w + 24, 50 + lines * 20}, 0.08f, 6, PANEL);
    label(h, "SCENT", x, y, 12, DIM);
    const char* wind = TextFormat("wind from the %s%s", bearing(-sc.wind_x, -sc.wind_z), sc.rain > 0.3f ? ", rain" : "");
    text(h, false, wind, x + w - text_w(h, false, wind, 13), y - 1, 13, DIM);
    y += 22;
    if (sc.range[CRET_SCENT_CARCASS] <= 0) { text(h, false, "Plant eaters do not hunt by smell.", x, y, 14, DIM); return; }
    if (!sc.n) { text(h, false, "Nothing on the wind.", x, y, 14, DIM); return; }
    for (int q = 0; q < sc.n; q++) {
        const CretScentHit* s = &sc.hit[q]; const char* t;
        const char* where = hunt_where(cam_yaw, s->x - a->x, s->z - a->z);
        if (s->what == CRET_SCENT_CRITTER) t = TextFormat("%s  %d m %s", HUNT_NAME[sim->critters[s->index].kind % CRET_CR_KINDS], (int)roundf(s->dist), where);
        else if (s->what == CRET_SCENT_CARCASS) t = TextFormat("Carcass  %d m %s", (int)roundf(s->dist), where);
        else t = TextFormat("%s trail  %d s old, going %s", CRET_SPECIES_TABLE[sim->agents[s->index].species].name, (int)roundf(s->age), bearing(cosf(s->dir), sinf(s->dir)));
        Color c = s->what == CRET_SCENT_ANIMAL ? DIM : INK; c.a = (unsigned char)(c.a * (0.55f + 0.45f * clampf(s->strength * 2, 0, 1)));
        text(h, false, t, x, y, 14, c); y += 20;
    }
}
