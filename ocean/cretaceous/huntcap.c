// Hunting review (capture DIR --hunt; included by devmodes.c, so it shares
// cretaceous.c's camera rig): how visible small prey is from a hatchling
// raptor's third-person camera.
//
// On the full island it finds four kinds of ground (thick ferns, tall grass,
// leaf litter under broadleaf canopy, open sand) and a fresh-water shallow,
// stands a hatchling raptor (growth 0.35) on each, and puts one critter of
// each land kind (and the fish in the shallows) 3, 8 and 15 m in front of
// it, at noon, at dusk and at night. The camera is the game's rig at zoom 1
// with the default pitch, looking along the raptor's heading; the critter
// stands 10 degrees right of it, side-on.
//
// Each still is measured as it is rendered: the critter shader marks its
// pixels in the HDR target's alpha (cret_critters_mark), and the final
// (tonemapped) frame is compared there against a ring of background around
// the critter as wide as the critter itself. Written per still: pixels,
// mean CIE L* of the critter and of the ring, Weber contrast of luminance,
// the a*b* (hue and chroma) distance, and the signal-to-clutter ratio
// |dL*| / sd(L*) of the ring (how far the animal stands out of the texture
// around it). DIR/hunt_table.csv has every row, DIR/hunt_summary.md the
// table by kind, distance and time of day.
//
// CRET_HUNT_ONLY="ferns,night" keeps the stills whose name contains every
// listed word; CRET_HUNT_HUD=1 also draws the game HUD (the prey prompt and
// marker, after the hunting sense was added) and CRET_HUNT_SNIFF=1 the
// scent view, for review stills.

typedef struct { const char* name; float x, z, dir; float score; } HuntSpot;
enum { HS_FERNS, HS_GRASS, HS_LITTER, HS_SAND, HS_SHALLOW, HS_COUNT };
static const char* const HS_NAME[HS_COUNT] = {"ferns", "grass", "litter", "sand", "shallows"};

static const uint8_t* hc_cell(const TerraMap* m, const uint8_t* base, int stride, float x, float z) {
    int gx = (int)(x / m->cell + 0.5f), gz = (int)(z / m->cell + 0.5f);
    if (gx < 0 || gz < 0 || gx >= m->n || gz >= m->n) return NULL;
    return base + ((size_t)gz * m->n + gx) * stride;
}
// How well (x, z) is ground of type t, about 0..1 (negative: unusable).
static float hc_ground(const CretWorld* w, const CretVeg* vg, int t, float x, float z) {
    const TerraMap* m = &w->map;
    const uint8_t* v = hc_cell(m, m->veg, TERRA_VEG_COUNT, x, z), *sp = hc_cell(m, m->splat, TERRA_MAT_COUNT, x, z);
    if (!v || !sp) return -1;
    float h = cret_height(w, x, z), wet = 0, nn[3]; terra_normal(m, x, z, nn);
    float under = cret_water(w, x, z) - h;
    if (t == HS_SHALLOW) return under > 0.3f && under < 1.2f ? 1.0f : -1.0f;
    if (under > -0.02f || nn[1] < 0.97f) return -1;
    float canopy = (v[TERRA_VEG_CONIFER] + v[TERRA_VEG_BROADLEAF] + 0.6f * v[TERRA_VEG_TREEFERN]) / 255.0f;
    float grass = vg ? fminf(cret_veg_grass_density(vg, x, z, &wet), 1.0f) : 0;
    switch (t) {
    case HS_FERNS: return v[TERRA_VEG_FERN] / 255.0f - 0.5f * grass - 0.4f * canopy;
    case HS_GRASS: return grass * (1 - wet) - 0.3f * v[TERRA_VEG_FERN] / 255.0f - 0.6f * canopy;
    case HS_LITTER: return sp[TERRA_MAT_FOREST] / 255.0f + 0.4f * canopy - 0.8f * (v[TERRA_VEG_FERN] + v[TERRA_VEG_SHRUB]) / 255.0f - 0.5f * grass;
    default: return sp[TERRA_MAT_SAND] / 255.0f - 0.5f * canopy - 0.5f * grass;
    }
}
// The best spot of each kind: the raptor 3 m behind a 16 m line of that
// ground with no trunk on it, the camera on a line of sight down it.
static void hunt_find_spots(const CretWorld* w, const CretVeg* vg, HuntSpot out[HS_COUNT]) {
    float S = w->size;
    for (int t = 0; t < HS_COUNT; t++) out[t] = (HuntSpot){HS_NAME[t], S * 0.5f, S * 0.5f, 0, -1e9f};
    for (float z = S * 0.1f; z < S * 0.9f; z += 12) for (float x = S * 0.1f; x < S * 0.9f; x += 12) {
        for (int t = 0; t < HS_COUNT; t++) {
            if (hc_ground(w, vg, t == HS_SHALLOW ? HS_SAND : t, x, z) < (t == HS_SHALLOW ? -0.5f : 0.3f)) continue;
            if (t == HS_SHALLOW && hc_ground(w, vg, HS_SHALLOW, x, z) > 0) continue;   // the raptor stands on the bank
            for (int d = 0; d < 8; d++) {
                float a = d * PI / 4, dx = cosf(a), dz = sinf(a), sum = 0, h0 = cret_height(w, x, z); bool ok = true;
                for (int k = 0; k <= 6 && ok; k++) {   // 0..18 m ahead (the fish: in the water past the first 2 m)
                    float px = x + dx * k * 3, pz = z + dz * k * 3, g;
                    if (t == HS_SHALLOW) { g = k == 0 ? 1.0f : hc_ground(w, vg, HS_SHALLOW, px, pz); if (k > 0 && g < 0) ok = false; }
                    else { g = hc_ground(w, vg, t, px, pz); if (g < 0 || fabsf(cret_height(w, px, pz) - h0) > 1.0f) ok = false; }
                    sum += g;
                }
                if (!ok) continue;
                if (vg) {
                    float p[3] = {x - dx * 2, h0 + 0.5f, z - dz * 2}, dd[3] = {dx, 0, dz};
                    if (cret_veg_cast_trunks(vg, p, dd, 20.0f, 0.5f) < 20.0f) continue;
                }
                float central = 0.1f * (fabsf(x - S * 0.5f) + fabsf(z - S * 0.5f)) / S;
                if (sum - central > out[t].score) out[t] = (HuntSpot){HS_NAME[t], x, z, a, sum - central};
            }
        }
    }
    for (int t = 0; t < HS_COUNT; t++) fprintf(stderr, "hunt: %s at %.0f %.0f dir %.2f (score %.2f)\n", out[t].name, out[t].x, out[t].z, out[t].dir, out[t].score);
}

// sRGB byte to CIE L*, a*, b* (D65).
static void hc_lab(const unsigned char* p, float lab[3]) {
    float c[3];
    for (int i = 0; i < 3; i++) { float v = p[i] / 255.0f; c[i] = v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f); }
    float X = (0.4124f * c[0] + 0.3576f * c[1] + 0.1805f * c[2]) / 0.95047f, Y = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
    float Z = (0.0193f * c[0] + 0.1192f * c[1] + 0.9505f * c[2]) / 1.08883f;
    #define HC_F(t) ((t) > 0.008856f ? cbrtf(t) : 7.787f * (t) + 16.0f / 116.0f)
    float fx = HC_F(X), fy = HC_F(Y), fz = HC_F(Z);
    lab[0] = 116 * fy - 16; lab[1] = 500 * (fx - fy); lab[2] = 200 * (fy - fz);
}

typedef struct { int px, w, h; float Lc, Lr, weber, dab, scr; } HuntStat;

// Critter pixels from the HDR alpha mark (rw x rh, bottom-up) against a
// ring as wide as the critter's equivalent radius (at least 3 px) in the
// final frame (W x H, top-down).
static HuntStat hunt_measure(const CretView* v, const Image* im) {
    HuntStat st = {0};
    int rw = v->rw, rh = v->rh, W = im->width, H = im->height;
    float* hdr = malloc(sizeof(float) * 4 * rw * rh); uint8_t* mask = calloc((size_t)W * H, 1);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, v->hdr.fbo); glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, rw, rh, GL_RGBA, GL_FLOAT, hdr); glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    int x0 = W, x1 = -1, y0 = H, y1 = -1;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int sx = x * rw / W, sy = (H - 1 - y) * rh / H;
        if (fabsf(hdr[4 * ((size_t)sy * rw + sx) + 3] - 0.5f) < 0.1f) {
            mask[(size_t)y * W + x] = 1; st.px++;
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    }
    free(hdr);
    if (!st.px) { free(mask); return st; }
    st.w = x1 - x0 + 1; st.h = y1 - y0 + 1;
    int r = (int)fmaxf(3, ceilf(sqrtf(st.px / 3.14159f)));
    const unsigned char* pix = im->data;   // RGBA8 from LoadImageFromScreen
    double sc[3] = {0}, sr[3] = {0}, srr = 0, Yc = 0, Yr = 0; int nr = 0;
    for (int y = fmaxf(y0 - r, 0); y <= fminf(y1 + r, H - 1); y++) for (int x = fmaxf(x0 - r, 0); x <= fminf(x1 + r, W - 1); x++) {
        float lab[3]; hc_lab(pix + 4 * ((size_t)y * W + x), lab);
        float Y = lab[0] > 8 ? powf((lab[0] + 16) / 116, 3) : lab[0] / 903.3f;
        if (mask[(size_t)y * W + x]) { for (int i = 0; i < 3; i++) sc[i] += lab[i]; Yc += Y; continue; }
        bool near = false;
        for (int dy = -r; dy <= r && !near; dy++) for (int dx = -r; dx <= r && !near; dx++) {
            int qx = x + dx, qy = y + dy;
            if (qx >= 0 && qy >= 0 && qx < W && qy < H && dx * dx + dy * dy <= r * r) near = mask[(size_t)qy * W + qx];
        }
        if (!near) continue;
        for (int i = 0; i < 3; i++) sr[i] += lab[i];
        srr += lab[0] * lab[0]; Yr += Y; nr++;
    }
    free(mask);
    if (!nr) return st;
    for (int i = 0; i < 3; i++) { sc[i] /= st.px; sr[i] /= nr; }
    Yc /= st.px; Yr /= nr;
    float sd = sqrtf(fmaxf(srr / nr - sr[0] * sr[0], 0));
    st.Lc = sc[0]; st.Lr = sr[0]; st.weber = (Yc - Yr) / fmax(Yr, 1e-4);
    st.dab = hypotf(sc[1] - sr[1], sc[2] - sr[2]); st.scr = (float)fabs(sc[0] - sr[0]) / fmaxf(sd, 0.5f);
    return st;
}

static bool hunt_only(const char* list, const char* name) {
    if (!list || !*list) return true;
    char b[256]; snprintf(b, sizeof b, "%s", list);
    for (char* t = strtok(b, ","); t; t = strtok(NULL, ",")) if (!strstr(name, t)) return false;
    return true;
}

static int capture_hunt(const char* dir, int W, int H) {
    dev_mkdirs(dir);
    CretConfig cfg = cret_default_config(); cfg.player_species = CRET_SP_RAPTOR; cfg.num_agents = 2; cfg.weather = 0;
    CretSim* sim = calloc(1, sizeof *sim);
    if (cret_sim_init(sim, cfg) != 0) { fprintf(stderr, "hunt: world failed\n"); return 1; }
    sim->crit_cfg.enabled = 0;
    SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(W, H, "cretaceous hunt capture"); SetTraceLogLevel(LOG_WARNING);
    CretView view; if (!cret_view_init(&view, sim->world, W, H)) return 1;
    view.settings.render_scale = 1; cret_view_resize(&view, W, H);
    CretHud hud; cret_hud_init(&hud);
    HuntSpot spot[HS_COUNT]; hunt_find_spots(sim->world, view.veg_ok ? &view.veg : NULL, spot);
    static const float DIST[3] = {3, 8, 15};
    static const struct { const char* name; float tod; } TOD[3] = {{"noon", 0.5f}, {"dusk", 0.745f}, {"night", 0.86f}};
    static const char* const KN[CRET_CR_KINDS] = {"mouse", "badger", "lizard", "bird", "fish"};
    const char* only = getenv("CRET_HUNT_ONLY"); bool with_hud = getenv("CRET_HUNT_HUD") && atoi(getenv("CRET_HUNT_HUD"));
    bool with_sniff = getenv("CRET_HUNT_SNIFF") && atoi(getenv("CRET_HUNT_SNIFF"));
    char path[1024]; snprintf(path, sizeof path, "%s/hunt_table.csv", dir);
    FILE* csv = fopen(path, "w"); if (!csv) return 1;
    fprintf(csv, "shot,ground,kind,dist,tod,px,w,h,L_critter,L_ring,weber,dab,scr\n");
    // Summary accumulators by kind x distance x time of day.
    float acc[CRET_CR_KINDS][3][3][5] = {{{{0}}}}; int cnt[CRET_CR_KINDS][3][3] = {{{0}}};
    for (int t = 0; t < HS_COUNT; t++) for (int td = 0; td < 3; td++) {
        bool first = true;
        for (int k = 0; k < CRET_CR_KINDS; k++) {
            if ((k == CRET_CR_FISH) != (t == HS_SHALLOW)) continue;
            for (int di = 0; di < 3; di++) {
                char name[128]; snprintf(name, sizeof name, "hunt_%s_%s_%02.0fm_%s", HS_NAME[t], KN[k], DIST[di], TOD[td].name);
                if (!hunt_only(only, name)) continue;
                const HuntSpot* P = &spot[t];
                sim->time_of_day = TOD[td].tod; sim->cloud_cover = 0.3f; view.settings.cloud_coverage = 0.3f;
                for (int i = 0; i < sim->cfg.num_agents; i++) sim->agents[i].alive = false;
                memset(sim->carcasses, 0, sizeof sim->carcasses); memset(sim->nests, 0, sizeof sim->nests);
                CretAgent* a = &sim->agents[0];
                a->alive = true; a->species = CRET_SP_RAPTOR; a->growth = 0.35f; cret_apply_growth(a); a->health = a->max_health;
                a->food = 0.4f; a->water = 0.9f; a->stamina = 1;
                a->x = P->x; a->z = P->z; a->y = cret_height(sim->world, a->x, a->z); a->yaw = P->dir; a->speed = 0; a->on_ground = true;
                cret_sim_place(sim, 0);
                memset(sim->critters, 0, sizeof sim->critters); sim->ncrit = 1;
                const CretCritterKind* K = &CRET_CRITTER_KINDS[k]; CretCritter* c = &sim->critters[0];
                c->active = 1; c->kind = (uint8_t)k; c->state = CRET_CRS_IDLE; c->threat = -1; c->kg = K->kg_min + (K->kg_max - K->kg_min) / 3;
                // 10 degrees right of the heading, so the raptor's own body is not in the way; broadside-ish.
                float brg = P->dir + 0.17f;
                c->x = a->x + cosf(brg) * DIST[di]; c->z = a->z + sinf(brg) * DIST[di]; c->yaw = P->dir + 2.0f;
                c->y = k == CRET_CR_FISH ? cret_water(sim->world, c->x, c->z) - 0.25f : cret_height(sim->world, c->x, c->z);
                // The game's rig at zoom 1, default pitch, looking along the heading.
                float pv[3]; cam_pivot(a, pv); float boom = cam_boom(a, 1), pitch = -0.15f;
                float cx = pv[0] - cosf(pitch) * cosf(P->dir) * boom, cz = pv[2] - cosf(pitch) * sinf(P->dir) * boom;
                float cy = fmaxf(pv[1] - sinf(pitch) * boom, cam_ground(sim->world, cx, cz));
                CretCamera cam = {{cx, cy, cz}, P->dir, pitch, 60};
                // Without the dinosaur models (the sandbox) the player is a white capsule that would
                // fill the middle of the frame and skew the exposure: it is drawn only with the HUD.
                a->alive = with_hud;
                if (first) { view.adapt_valid = false; view.cam_pos[1] += 1e4f; }
                int warm = first ? 24 : 6; first = false;
                for (int f = 0; f < warm; f++) {
                    cret_critters_mark(f == warm - 1);
                    BeginDrawing(); cret_view_render(&view, sim, &cam, 1.0f / 30);
                    if (with_sniff) { a->alive = true; cret_scent_draw(&view, sim, 0, 1, &cam); a->alive = with_hud; }
                    if (with_hud && f == warm - 1) { hud.sniffing = with_sniff; cret_hud_draw(&hud, sim, 0, view.viewproj, cam.yaw); }
                    EndDrawing();
                }
                cret_critters_mark(false);
                Image im = LoadImageFromScreen();
                HuntStat st = hunt_measure(&view, &im);
                snprintf(path, sizeof path, "%s/%s.png", dir, name); ExportImage(im, path); UnloadImage(im);
                fprintf(csv, "%s,%s,%s,%.0f,%s,%d,%d,%d,%.1f,%.1f,%.3f,%.1f,%.2f\n", name, HS_NAME[t], KN[k], DIST[di], TOD[td].name,
                        st.px, st.w, st.h, st.Lc, st.Lr, st.weber, st.dab, st.scr);
                printf("%-40s px %5d (%3dx%3d)  L* %5.1f vs %5.1f  weber %+6.2f  dab %5.1f  scr %5.2f\n", name, st.px, st.w, st.h, st.Lc, st.Lr, st.weber, st.dab, st.scr);
                if (st.px) {
                    float* A = acc[k][di][td]; A[0] += st.px; A[1] += fabsf(st.Lc - st.Lr); A[2] += st.weber; A[3] += st.dab; A[4] += st.scr; cnt[k][di][td]++;
                }
            }
        }
    }
    fclose(csv);
    snprintf(path, sizeof path, "%s/hunt_summary.md", dir); FILE* md = fopen(path, "w");
    if (md) {
        fprintf(md, "| Kind | Distance | Time | Grounds | Pixels | abs dL* | Weber | a*b* distance | Signal/clutter |\n|---|---|---|---|---|---|---|---|---|\n");
        for (int k = 0; k < CRET_CR_KINDS; k++) for (int di = 0; di < 3; di++) for (int td = 0; td < 3; td++) {
            int n = cnt[k][di][td]; if (!n) continue; const float* A = acc[k][di][td];
            fprintf(md, "| %s | %.0f m | %s | %d | %.0f | %.1f | %+.2f | %.1f | %.2f |\n", KN[k], DIST[di], TOD[td].name, n, A[0] / n, A[1] / n, A[2] / n, A[3] / n, A[4] / n);
        }
        fclose(md);
    }
    cret_hud_close(&hud); cret_view_close(&view); CloseWindow(); cret_sim_close(sim); free(sim);
    return 0;
}
