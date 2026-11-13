// Procedural plant meshes (included once by render/veg.c, after its atlas
// layer enum and helpers).
//
// Every tall plant grows from a skeleton: tapered generalized cylinders with
// parallel-transported frames and continuous bark uv, children flared into
// their parents, down to twigs on the full mesh. Foliage hangs on that
// skeleton as many small alpha-tested cards and leaflets oriented in 3D around
// their twig or rachis, so a crown holds up from any side and from below:
//   broadleaf / podocarp  forked (or excurrent) limbs inside a lumpy crown
//                         envelope, twigs carrying photo leaf clusters
//   araucaria             a leader with whorled tiers; branchlets are crossed
//                         scale-leaf cords (Norfolk-like) or scale-leaf rope
//                         tubes that turn up under a dome (monkey-puzzle-like)
//   metasequoia           buttressed, conical, spiralled branches with
//                         twigs of hanging feathery sprays
//   cycads, Williamsonia  stout trunk (forked for Williamsonia) with a radial
//                         crown of pinnate fronds: a rachis tube and keeled
//                         pairs of leaflets
//   tree ferns            fibrous trunk, arching bipinnate fronds (fern
//                         pinnae as small curled cards), croziers, a dead skirt
//   palms                 Sabal-like costapalmate fans of folded segments, or
//                         feather palms with drooping leaflets
//   ferns, horsetails,    keeled frond ribbons; jointed stalk tubes with
//   shrubs                needle whorls; a little branching skeleton
// The reduced (LOD) mesh drops twigs and merges leaf clusters into fewer,
// larger cards (or fewer, wider leaflets), and keeps the skeleton.
// A coarse leaf-area grid over each mesh gives every vertex its sky
// visibility (baked AO, crown depth) and foliage its bent normal.

typedef struct { float p[3]; float uv[2]; int8_t n[4]; uint8_t a[4]; uint8_t w[4]; } VVert;
// n.w  sky visibility (baked AO / crown depth), 0..127
// a    atlas layer; wind weight along the plant (0 at its base .. 255 at the
//      tips of its limbs or fronds); leaf flag; frond dryness (leaves) or
//      bark tone (bark)
// w    limb phase; sub-branch weight (0 where a sub-branch, twig or leaflet
//      leaves its limb .. 255 at its tip, scaled by its flexibility);
//      sub-branch phase; leaf flutter (0 at a card's base .. 255 at its tip)
// Triangles in i are drawn everywhere; fi (twigs) only in the view, not the shadow maps.
typedef struct { VVert* v; int nv, cv; uint32_t* i; int ni, ci; uint32_t* fi; int nfi, cfi; float rad, height; } MB;
typedef struct { float x, y, z; } V3;
static V3 v3(float x, float y, float z) { return (V3){x, y, z}; }
static V3 vadd(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 vsub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 vmul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float vdot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 vcross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static V3 vnorm(V3 a) { float l = sqrtf(vdot(a, a)); return l > 1e-8f ? vmul(a, 1 / l) : v3(0, 1, 0); }
static float vlen(V3 a) { return sqrtf(vdot(a, a)); }
static V3 vlerp(V3 a, V3 b, float t) { return vadd(a, vmul(vsub(b, a), t)); }
static V3 dir_az(float az) { return v3(cosf(az), 0, sinf(az)); }
static V3 dir_ae(float az, float el) { return v3(cosf(az) * cosf(el), sinf(el), sinf(az) * cosf(el)); }
// Rotates v about the unit axis k by a (Rodrigues).
static V3 vrot(V3 v, V3 k, float a) {
    float c = cosf(a), s = sinf(a);
    return vadd(vadd(vmul(v, c), vmul(vcross(k, v), s)), vmul(k, vdot(k, v) * (1 - c)));
}
static V3 any_perp(V3 d) { return vnorm(vcross(d, fabsf(d.y) < 0.9f ? v3(0, 1, 0) : v3(1, 0, 0))); }

typedef struct { float hw, ph, ws, phs, fl; } Wnd;   // wind attributes (see VVert), 0..1
// A branch or twig end where fruit, cones and flowers can hang (crown_tree,
// the araucarias and the redwood record them; add_fruit reads them).
typedef struct { V3 p, d; Wnd w; float wt; } TipPt;
typedef struct {
    MB* m; VRng r; int lod, kind, variant, species;
    int bark_layer, bark_tone, dry, fine;
    float leaf_full, leaf_drawn;   // leaf_cards' area at full detail and as drawn (m^2): bake_sky evens the reduced crown's density
    TipPt* tip; int ntip, ctip;    // branch and twig ends, in growth order
} Gen;
static void tip_add(Gen* g, V3 p, V3 d, Wnd w, float wt) {
    if (g->ntip == g->ctip) { g->ctip = g->ctip ? g->ctip * 2 : 256; g->tip = realloc(g->tip, sizeof(TipPt) * g->ctip); }
    g->tip[g->ntip++] = (TipPt){p, d, w, wt};
}

static int gv(Gen* g, V3 p, float u, float v, V3 n, float ao, int layer, int leaf, Wnd w) {
    MB* m = g->m;
    if (m->nv == m->cv) { m->cv = m->cv ? m->cv * 2 : 2048; m->v = realloc(m->v, sizeof(VVert) * m->cv); }
    n = vnorm(n);
    #define B8(x) ((uint8_t)(clampf(x, 0, 1) * 255 + 0.5f))
    m->v[m->nv] = (VVert){{p.x, p.y, p.z}, {u, v}, {(int8_t)(n.x * 127), (int8_t)(n.y * 127), (int8_t)(n.z * 127), (int8_t)(clampf(ao, 0, 1) * 127)},
                          {(uint8_t)layer, B8(w.hw), (uint8_t)leaf, (uint8_t)(leaf ? g->dry : g->bark_tone)},
                          {(uint8_t)((int)(w.ph * 255) & 255), B8(w.ws), (uint8_t)((int)(w.phs * 255) & 255), B8(w.fl)}};
    #undef B8
    float r = sqrtf(p.x * p.x + p.z * p.z); if (r > m->rad) m->rad = r; if (p.y > m->height) m->height = p.y;
    return m->nv++;
}
static void gtri(Gen* g, int a, int b, int c) {
    MB* m = g->m;
    if (g->fine) {
        if (m->nfi + 3 > m->cfi) { m->cfi = m->cfi ? m->cfi * 2 : 4096; m->fi = realloc(m->fi, sizeof(uint32_t) * m->cfi); }
        m->fi[m->nfi++] = a; m->fi[m->nfi++] = b; m->fi[m->nfi++] = c;
    } else {
        if (m->ni + 3 > m->ci) { m->ci = m->ci ? m->ci * 2 : 4096; m->i = realloc(m->i, sizeof(uint32_t) * m->ci); }
        m->i[m->ni++] = a; m->i[m->ni++] = b; m->i[m->ni++] = c;
    }
}
static float gr(Gen* g) { return vr(&g->r); }
static float grr(Gen* g, float a, float b) { return vrr(&g->r, a, b); }
// The full and reduced meshes of a variant must be the same plant: the same
// skeleton (every path is grown at full resolution on both and only drawn
// through fewer rings), the same leaf points, and the reduced one a subset of
// the cards, merged larger. So the random stream has to run identically on
// both: anything whose draw count depends on lod runs on a forked stream and
// advances the plant's stream by exactly one draw (fork_in / fork_out), and
// nothing else draws conditionally on lod.
static VRng fork_in(Gen* g) { VRng sv = g->r; g->r.s = hashu(sv.s * 0x9e3779b1u + 0x68e31da4u); return sv; }
static void fork_out(Gen* g, VRng sv) { g->r = sv; vr(&g->r); }

// ---------------------------------------------------------------- primitives
// A branch axis: points, radii and wind weights along it.
#define PMAX 24
typedef struct { V3 p[PMAX]; float r[PMAX], hw[PMAX], ws[PMAX]; int n; float len; } Path;
static void path_at(const Path* P, float t, V3* p, V3* d, float* r, float* hw, float* ws) {
    float f = clampf(t, 0, 1) * (P->n - 1); int k = (int)f; if (k >= P->n - 1) k = P->n - 2; f -= k;
    if (p) *p = vlerp(P->p[k], P->p[k + 1], f);
    if (d) *d = vnorm(vsub(P->p[k + 1], P->p[k]));
    if (r) *r = lerpf(P->r[k], P->r[k + 1], f);
    if (hw) *hw = lerpf(P->hw[k], P->hw[k + 1], f);
    if (ws) *ws = lerpf(P->ws[k], P->ws[k + 1], f);
}
static int sides_for(const Gen* g, float r) {
    int s = r > 0.3f ? 12 : r > 0.16f ? 9 : r > 0.07f ? 7 : r > 0.03f ? 5 : r > 0.015f ? 4 : 3;
    return g->lod ? (s > 5 ? (s * 2 + 2) / 3 : s > 3 ? s - 1 : 3) : s;
}
// Bark (or scale-leaf) tube along P with parallel-transported frames (no
// twisting). u wraps urep times around; v runs with the arc length / vtile
// from v0 (children start at their parent's v). lobes: root buttresses.
// Every third point and the last: the reduced mesh's tubes follow the same
// curve through fewer rings (the skeleton is grown once, at full resolution).
static void path_coarse(const Path* P, Path* Q) {
    int m = 0;
    for (int k = 0; k < P->n; k += 3) { Q->p[m] = P->p[k]; Q->r[m] = P->r[k]; Q->hw[m] = P->hw[k]; Q->ws[m] = P->ws[k]; m++; }
    if ((P->n - 1) % 3) { int k = P->n - 1; Q->p[m] = P->p[k]; Q->r[m] = P->r[k]; Q->hw[m] = P->hw[k]; Q->ws[m] = P->ws[k]; m++; }
    Q->n = m; Q->len = P->len;
}
static float tube(Gen* g, const Path* P, int sides, float urep, float vtile, float v0, float ph, float phs, int layer, int leaf, float lobes) {
    Path C; if (g->lod && P->n > 4) { path_coarse(P, &C); P = &C; }
    int base = g->m->nv; float v = v0;
    V3 t0 = vnorm(vsub(P->p[1], P->p[0])), nrm = any_perp(t0);
    for (int k = 0; k < P->n; k++) {
        V3 t = vnorm(vsub(P->p[k < P->n - 1 ? k + 1 : k], P->p[k > 0 ? k - 1 : k]));
        nrm = vnorm(vsub(nrm, vmul(t, vdot(nrm, t)))); V3 b = vcross(t, nrm);
        if (k > 0) v += vlen(vsub(P->p[k], P->p[k - 1])) / vtile;
        float y = P->p[k].y, fl = lobes > 0 && y < 1.6f ? lobes * powf(1 - fmaxf(y, 0) / 1.6f, 2.0f) : 0;
        for (int s = 0; s <= sides; s++) {
            float a = 2 * VPI * s / sides; V3 d = vadd(vmul(nrm, cosf(a)), vmul(b, sinf(a)));
            float rr = P->r[k] * (1 + fl * (0.55f + 0.45f * cosf(5 * a + 0.8f)));
            gv(g, vadd(P->p[k], vmul(d, rr)), (float)s / sides * urep, v, d, 1, layer, leaf, (Wnd){P->hw[k], ph, P->ws[k], phs, 0});
        }
    }
    for (int k = 0; k < P->n - 1; k++) for (int s = 0; s < sides; s++) {
        int a = base + k * (sides + 1) + s, b = a + 1, c = a + sides + 1, d = c + 1;
        gtri(g, a, c, b); gtri(g, b, c, d);
    }
    return v;
}
// Bark tube with the usual texel density (about square texels, the bark
// tile ~0.8 m across on a trunk).
static float bark(Gen* g, const Path* P, float v0, float ph, float phs, float lobes) {
    float r0 = P->r[0], urep = fmaxf(1, roundf(r0 * 6)), vt = clampf(2 * VPI * r0 / urep, 0.08f, 1.6f);
    return tube(g, P, sides_for(g, r0), urep, vt, v0, ph, phs, g->bark_layer, 0, lobes);
}
// Grows a path from p0 along d0 in n steps of len / n: each step turns toward
// up by trop (rad per m; negative droops), wanders by up to wander (rad per
// m) and, near the envelope, bends back inside it (attract).
typedef struct { V3 c, R; float lump[8][4]; int nl; } Env;
static float env_scale(const Env* e, V3 d) {
    float s = 1; for (int i = 0; i < e->nl; i++) s += e->lump[i][3] * fmaxf(d.x * e->lump[i][0] + d.y * e->lump[i][1] + d.z * e->lump[i][2], 0) * 1.0f;
    return s;
}
static float env_depth(const Env* e, V3 p) {   // 0 at the centre .. 1 on the envelope
    V3 q = vsub(p, e->c); V3 s = v3(q.x / e->R.x, q.y / e->R.y, q.z / e->R.z);
    return vlen(s) / env_scale(e, vnorm(q));
}
static float env_dist(const Env* e, V3 p, V3 d) {   // along unit d from p to the envelope (0 outside)
    float lo = 0, hi = 2 * fmaxf(fmaxf(e->R.x, e->R.y), e->R.z) * 1.6f;
    if (env_depth(e, p) >= 1) return 0;
    for (int i = 0; i < 18; i++) { float m = 0.5f * (lo + hi); if (env_depth(e, vadd(p, vmul(d, m))) < 1) lo = m; else hi = m; }
    return lo;
}
static void env_init(Gen* g, Env* e, V3 c, V3 R, int lumps, float amp) {
    e->c = c; e->R = R; e->nl = lumps;
    for (int i = 0; i < lumps; i++) {
        V3 d = dir_ae(grr(g, 0, 2 * VPI), grr(g, -0.6f, 1.2f));
        e->lump[i][0] = d.x; e->lump[i][1] = d.y; e->lump[i][2] = d.z; e->lump[i][3] = grr(g, -amp, amp);
    }
}
static void grow(Gen* g, Path* P, V3 p0, V3 d0, float len, int n, float r0, float r1, float trop, float wander,
                 const Env* e, float attract, float hw0, float hw1, float ws0, float ws1) {
    if (n > PMAX - 1) n = PMAX - 1;
    if (n < 1) n = 1;
    VRng sv = fork_in(g);
    V3 p = p0, d = vnorm(d0); float step = len / n;
    P->n = n + 1; P->len = len;
    for (int k = 0; k <= n; k++) {
        float t = (float)k / n;
        P->p[k] = p; P->r[k] = lerpf(r0, r1, powf(t, 0.85f)); P->hw[k] = lerpf(hw0, hw1, t); P->ws[k] = lerpf(ws0, ws1, t);
        // Turn: tropism toward up (or down), a random wander, and back into the crown near its edge.
        V3 side = any_perp(d);
        V3 nd = vadd(d, vmul(v3(0, 1, 0), trop * step));
        nd = vadd(nd, vmul(vrot(side, d, grr(g, 0, 2 * VPI)), wander * step * grr(g, 0, 1)));
        if (e && attract > 0) {
            float dep = env_depth(e, p);
            if (dep > 0.85f) nd = vadd(nd, vmul(vnorm(vsub(e->c, p)), attract * step * (dep - 0.85f) * 4));
        }
        d = vnorm(nd); p = vadd(p, vmul(d, step));
    }
    fork_out(g, sv);
}

// Card from base b along unit axis d, across unit s: length L, width W; uv
// rectangle (u0, v0)-(u1, v1), v along d. segs > 1 curls it about s by curl.
static void card(Gen* g, V3 b, V3 d, V3 s, float L, float W, int layer, float u0, float v0, float u1, float v1, int segs, float curl, Wnd w) {
    int first = g->m->nv; V3 p = b, dir = d;
    for (int k = 0; k <= segs; k++) {
        float t = (float)k / segs; Wnd wk = w; wk.fl = t; wk.ws = fminf(w.ws + 0.15f * t, 1);
        V3 n = vcross(s, dir);
        gv(g, vsub(p, vmul(s, 0.5f * W)), u0, lerpf(v0, v1, t), n, 1, layer, 1, wk);
        gv(g, vadd(p, vmul(s, 0.5f * W)), u1, lerpf(v0, v1, t), n, 1, layer, 1, wk);
        if (k < segs) { p = vadd(p, vmul(dir, L / segs)); dir = vrot(dir, s, curl / segs); }
    }
    for (int k = 0; k < segs; k++) { int a = first + 2 * k; gtri(g, a, a + 2, a + 1); gtri(g, a + 1, a + 2, a + 3); }
}
// Leaf strip along centre points c[] (across unit s[], frond-up up[],
// half-widths hw[]); keel > 0 lifts the edges along up (a V opening up), so
// a frond or segment reads from the side; keel 0: two vertices across.
static void strip(Gen* g, int n, const V3* c, const V3* s, const V3* up, const float* hw, float keel, int layer,
                  float u0, float u1, float v0, float v1, Wnd w0, Wnd w1) {
    int first = g->m->nv, across = keel != 0 ? 3 : 2;
    for (int k = 0; k < n; k++) {
        float t = n > 1 ? (float)k / (n - 1) : 0; Wnd w = {lerpf(w0.hw, w1.hw, t), w0.ph, lerpf(w0.ws, w1.ws, t), w0.phs, lerpf(w0.fl, w1.fl, t)};
        float v = lerpf(v0, v1, t); V3 lift = vmul(up[k], keel * hw[k]);
        gv(g, vadd(vsub(c[k], vmul(s[k], hw[k])), lift), u0, v, up[k], 1, layer, 1, w);
        if (across == 3) gv(g, c[k], 0.5f * (u0 + u1), v, up[k], 1, layer, 1, w);
        gv(g, vadd(vadd(c[k], vmul(s[k], hw[k])), lift), u1, v, up[k], 1, layer, 1, w);
    }
    for (int k = 0; k < n - 1; k++) {
        int a = first + across * k, b = a + across;
        for (int q = 0; q < across - 1; q++) { gtri(g, a + q, b + q, a + q + 1); gtri(g, a + q + 1, b + q, b + q + 1); }
    }
}

// ---------------------------------------------------------------- sky visibility and bent normals
// Leaf area is splatted into a coarse grid over the mesh; each cell marches 13
// directions (9 over the sky, 4 below the horizon) through it. Visibility
// (sky directions only) becomes n.w; foliage normals bend toward the
// unoccluded directions (all 13), so a crown shades as one volume.
#define AOG 16
static void bake_sky(MB* m, float k_ext, float bend, float leaf_scale) {
    if (!m->nv) return;
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    for (int i = 0; i < m->nv; i++) for (int c = 0; c < 3; c++) { lo[c] = fminf(lo[c], m->v[i].p[c]); hi[c] = fmaxf(hi[c], m->v[i].p[c]); }
    float cs[3]; for (int c = 0; c < 3; c++) { float pad = (hi[c] - lo[c]) * 0.06f + 0.05f; lo[c] -= pad; hi[c] += pad; cs[c] = (hi[c] - lo[c]) / AOG; }
    float* den = calloc(AOG * AOG * AOG, sizeof(float));
    for (int pass = 0; pass < 2; pass++) {
        const uint32_t* I = pass ? m->fi : m->i; int ni = pass ? m->nfi : m->ni;
        for (int t = 0; t + 2 < ni; t += 3) {
            const VVert *a = &m->v[I[t]], *b = &m->v[I[t + 1]], *c = &m->v[I[t + 2]];
            V3 pa = v3(a->p[0], a->p[1], a->p[2]), pb = v3(b->p[0], b->p[1], b->p[2]), pc = v3(c->p[0], c->p[1], c->p[2]);
            float area = 0.5f * vlen(vcross(vsub(pb, pa), vsub(pc, pa))) * (a->a[2] ? 0.55f * leaf_scale : 0.35f);   // cards are about half opaque
            V3 q = vmul(vadd(vadd(pa, pb), pc), 1.0f / 3);
            int ix = (int)((q.x - lo[0]) / cs[0]), iy = (int)((q.y - lo[1]) / cs[1]), iz = (int)((q.z - lo[2]) / cs[2]);
            if (ix < 0 || iy < 0 || iz < 0 || ix >= AOG || iy >= AOG || iz >= AOG) continue;
            den[(iz * AOG + iy) * AOG + ix] += area;
        }
    }
    float vol = cs[0] * cs[1] * cs[2];
    for (int i = 0; i < AOG * AOG * AOG; i++) den[i] = den[i] / vol * k_ext;   // extinction per metre
    V3 D[13]; float Wsky[13]; int nd = 0;
    D[nd] = v3(0, 1, 0); Wsky[nd++] = 0.22f;
    for (int i = 0; i < 4; i++) { D[nd] = dir_ae(i * VPI / 2 + 0.4f, 0.75f); Wsky[nd++] = 0.12f; }
    for (int i = 0; i < 4; i++) { D[nd] = dir_ae(i * VPI / 2 + 1.2f, 0.18f); Wsky[nd++] = 0.075f; }
    for (int i = 0; i < 4; i++) { D[nd] = dir_ae(i * VPI / 2 + 0.4f, -0.5f); Wsky[nd++] = 0; }
    float* vis = malloc(sizeof(float) * AOG * AOG * AOG); V3* bn = malloc(sizeof(V3) * AOG * AOG * AOG);
    float step = fminf(fminf(cs[0], cs[1]), cs[2]);
    for (int cell = 0; cell < AOG * AOG * AOG; cell++) {
        int ix = cell % AOG, iy = (cell / AOG) % AOG, iz = cell / (AOG * AOG);
        V3 p0 = v3(lo[0] + (ix + 0.5f) * cs[0], lo[1] + (iy + 0.5f) * cs[1], lo[2] + (iz + 0.5f) * cs[2]);
        float sv = 0; V3 b = v3(0, 0, 0);
        for (int k = 0; k < 13; k++) {
            float tau = den[cell] * step * 0.5f; V3 p = p0;
            for (int s = 0; s < 3 * AOG; s++) {
                p = vadd(p, vmul(D[k], step));
                int jx = (int)((p.x - lo[0]) / cs[0]), jy = (int)((p.y - lo[1]) / cs[1]), jz = (int)((p.z - lo[2]) / cs[2]);
                if (jx < 0 || jy < 0 || jz < 0 || jx >= AOG || jy >= AOG || jz >= AOG) break;
                tau += den[(jz * AOG + jy) * AOG + jx] * step;
            }
            float t = expf(-tau); sv += Wsky[k] * t; b = vadd(b, vmul(D[k], t));
        }
        vis[cell] = sv; bn[cell] = b;
    }
    for (int i = 0; i < m->nv; i++) {
        VVert* q = &m->v[i];
        float fx = clampf((q->p[0] - lo[0]) / cs[0] - 0.5f, 0, AOG - 1.001f), fy = clampf((q->p[1] - lo[1]) / cs[1] - 0.5f, 0, AOG - 1.001f), fz = clampf((q->p[2] - lo[2]) / cs[2] - 0.5f, 0, AOG - 1.001f);
        int x0 = (int)fx, y0 = (int)fy, z0 = (int)fz; fx -= x0; fy -= y0; fz -= z0;
        float sv = 0; V3 b = v3(0, 0, 0);
        for (int c = 0; c < 8; c++) {
            int dx = c & 1, dy = (c >> 1) & 1, dz = c >> 2;
            float wgt = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy) * (dz ? fz : 1 - fz);
            int j = ((z0 + dz) * AOG + y0 + dy) * AOG + x0 + dx;
            sv += vis[j] * wgt; b = vadd(b, vmul(bn[j], wgt));
        }
        q->n[3] = (int8_t)(clampf(0.22f + 0.95f * sv, 0, 1) * 127);
        if (q->a[2] && bend > 0) {   // foliage: bend toward the open side of the crown
            V3 n = v3(q->n[0] / 127.0f, q->n[1] / 127.0f, q->n[2] / 127.0f), bb = vnorm(b);
            if (vdot(n, bb) < 0) n = vmul(n, -1);
            n = vnorm(vadd(vmul(n, 1 - bend), vmul(bb, bend)));
            q->n[0] = (int8_t)(n.x * 127); q->n[1] = (int8_t)(n.y * 127); q->n[2] = (int8_t)(n.z * 127);
        }
    }
    free(den); free(vis); free(bn);
}

// ---------------------------------------------------------------- leaf clusters on twigs
typedef struct { V3 p, d; Wnd w; float wt; } LeafPt;
typedef struct { LeafPt* a; int n, cap; } LeafPts;
static void lp_add(LeafPts* L, V3 p, V3 d, Wnd w, float wt) {
    if (L->n == L->cap) { L->cap = L->cap ? L->cap * 2 : 1024; L->a = realloc(L->a, sizeof(LeafPt) * L->cap); }
    L->a[L->n++] = (LeafPt){p, d, w, wt};
}
// Places about `count` cards over the candidate points (weighted): each card
// leaves its twig at 20-55 degrees on a random roll, faces up and out with a
// random tilt, a 2x2 sub-sprite of layer. The reduced mesh keeps every
// lod_div-th of the same cards (same point, same roll), each a whole-layer
// card of lod_layer, lod_size long: the crown keeps its shape and density.
static void leaf_cards(Gen* g, const LeafPts* L, int count, V3 centre, int layer, float size, float aspect, float curl, float upbias, int segs,
                       int lod_layer, float lod_size, int lod_div) {
    double tot = 0; for (int i = 0; i < L->n; i++) tot += L->a[i].wt;
    if (tot <= 0) return;
    float scale = (float)(count / tot), size0 = size; VRng sv = fork_in(g); int quad = g->lod ? 0 : 1, ci = 0;
    if (g->lod) { layer = lod_layer; size = lod_size; curl = 0; segs = 1; }
    for (int i = 0; i < L->n; i++) {
        const LeafPt* q = &L->a[i];
        float pk = q->wt * scale, nk = floorf(pk) + (gr(g) < pk - floorf(pk) ? 1 : 0);
        for (int k = 0; k < (int)nk; k++, ci++) {
            V3 d = q->d, perp = vrot(any_perp(d), d, grr(g, 0, 2 * VPI));
            V3 out = vnorm(vsub(q->p, centre));
            V3 ax = vnorm(vadd(vadd(vmul(d, cosf(grr(g, 0.35f, 0.95f))), vmul(perp, 0.8f)), vmul(vadd(v3(0, 1, 0), out), upbias)));
            V3 nt = vnorm(vadd(vadd(vmul(v3(0, 1, 0), 0.6f), vmul(out, 0.5f)), v3(grr(g, -0.6f, 0.6f), grr(g, -0.3f, 0.3f), grr(g, -0.6f, 0.6f))));
            V3 s = vcross(nt, ax); if (vlen(s) < 0.1f) s = any_perp(ax); s = vnorm(s);
            float L0 = size * grr(g, 0.8f, 1.2f);
            int sub = (int)(gr(g) * 4) & 3; float u0 = quad ? (sub & 1) * 0.5f : 0, v0 = quad ? (sub >> 1) * 0.5f : 0, du = quad ? 0.5f : 1;
            if (gr(g) < 0.5f) { u0 += du; du = -du; }   // mirrored
            float L1 = size0 * L0 / size;   // the same card at full detail
            g->leaf_full += L1 * L1 * aspect;
            if (g->lod && ci % lod_div) continue;
            g->leaf_drawn += L0 * L0 * aspect;
            // A merged card sits further back on its point, so the reduced crown keeps the full crown's outline.
            card(g, vsub(q->p, vmul(ax, L0 * (g->lod ? 0.3f : 0.04f))), ax, s, L0, L0 * aspect, layer, u0, v0, u0 + du, v0 + (quad ? 0.5f : 1), segs, curl, q->w);
        }
    }
    fork_out(g, sv);
}

// ---------------------------------------------------------------- crowns (broadleaf, podocarp, shrub)
typedef struct {
    float H, r0, fork;          // height, trunk base radius, trunk height before the crown (fraction of H)
    int stems, leader;          // stems from the base (katsura 2-4); leader: excurrent, branches spiral off it
    int limbs_lo, limbs_hi;     // decurrent: limbs at the fork
    float sp1;                  // excurrent: spacing of branches on the leader (m)
    float el1;                  // limb elevation (rad above horizontal)
    float sp2, sp3, ang2, ang3; // spacing (m) and angle (rad) of branches on limbs, twigs on branches
    float trop1, trop2, trop3;  // tropism per level (rad per m; + up)
    float twig_len;             // longest twig (m)
    int leaf_layer, lod_layer, nleaf;   // cluster cards (2x2 sub-sprites), merged LOD cards (whole layer)
    float leaf_size, lod_size, curl, upbias, lobes, droop;   // droop: outer branches sag (m per m^2)
    float pad;                  // 0..1: foliage gathers on the outer part of each limb (podocarp pads)
    int lod_div;                // the reduced mesh keeps every lod_div-th card (0: every third)
} Crown;
static void crown_tree(Gen* g, const Crown* C, const Env* E) {
    LeafPts L = {0}; float H = C->H;
    // Stems.
    Path stems[6]; int ns = C->stems > 1 ? (C->stems < 6 ? C->stems : 6) : 1; float stem_v[6];
    for (int s = 0; s < ns; s++) {
        float tilt = ns > 1 ? grr(g, 0.12f, 0.26f) : grr(g, 0.0f, 0.05f), az = (ns > 1 ? s * 2 * VPI / ns : 0) + grr(g, 0, 2 * VPI);
        float top = C->leader || ns > 1 ? H * 0.94f : H * C->fork, r0 = ns > 1 ? C->r0 * 0.68f : C->r0;
        V3 p0 = ns > 1 ? vmul(dir_az(az), r0 * 0.6f) : v3(0, 0, 0); p0.y = -0.3f;
        grow(g, &stems[s], p0, vnorm(vadd(v3(0, 1, 0), vmul(dir_az(az), tilt))), top + 0.3f, 10,
             r0, C->leader || ns > 1 ? 0.03f : r0 * 0.72f, 0.02f, 0.06f, NULL, 0, 0, C->leader ? 0.25f : 0.08f, 0, 0);
        stem_v[s] = bark(g, &stems[s], 0, 0, 0, C->lobes * (ns > 1 ? 0.3f : 1));
    }
    // Limbs: off the leader(s), or at the fork.
    typedef struct { V3 p, d; float r, len, hw, v, ph; int st; } Limb;
    Limb limbs[64]; int nl = 0;
    if (C->leader || ns > 1) {
        for (int s = 0; s < ns; s++) {
            float h0 = H * C->fork, az = grr(g, 0, 2 * VPI);
            for (float h = h0; h < H * 0.9f && nl < 64; h += C->sp1 * grr(g, 0.75f, 1.25f)) {
                float t = (h + 0.3f) / (H * 0.94f + 0.3f), hr = (h - h0) / (H * 0.9f - h0); az += 2.39996f + grr(g, -0.3f, 0.3f);
                V3 p, d0; float r, hw; path_at(&stems[s], t, &p, &d0, &r, &hw, NULL);
                V3 d = dir_ae(az, C->el1 + lerpf(-0.25f, 0.35f, hr) + grr(g, -0.15f, 0.15f));
                if (ns > 1) d = vnorm(vadd(d, vmul(vsub(p, v3(0, p.y, 0)), 0.5f)));   // multi-stem: outward
                float len = env_dist(E, p, d) * grr(g, 0.7f, 0.95f); if (len < 0.4f) continue;
                limbs[nl++] = (Limb){p, d, fminf(r * 0.75f, 0.02f + 0.03f * len), len, hw, stem_v[s] * t, gr(g), s};
            }
        }
    } else {
        V3 p, d0; float r; path_at(&stems[0], 1, &p, &d0, &r, NULL, NULL);
        int n = C->limbs_lo + (int)(gr(g) * (C->limbs_hi - C->limbs_lo + 0.99f)); float az = grr(g, 0, 2 * VPI);
        for (int i = 0; i < n; i++) {
            V3 d = dir_ae(az + i * 2 * VPI / n + grr(g, -0.35f, 0.35f), C->el1 + grr(g, -0.15f, 0.2f));
            limbs[nl++] = (Limb){p, d, r * powf(1.0f / n, 0.42f), env_dist(E, p, d) * grr(g, 0.75f, 0.95f), 0.08f, stem_v[0], gr(g), 0};
        }
        int extra = 1 + (int)grr(g, 0, 2.99f);   // lower limbs off the trunk
        for (int i = 0; i < extra && nl < 64; i++) {
            float t = grr(g, 0.6f, 0.9f); V3 q; path_at(&stems[0], t, &q, NULL, &r, NULL, NULL);
            V3 d = dir_ae(grr(g, 0, 2 * VPI), C->el1 * 0.6f + grr(g, -0.1f, 0.15f));
            limbs[nl++] = (Limb){q, d, r * 0.45f, env_dist(E, q, d) * grr(g, 0.7f, 0.95f), 0.06f, stem_v[0] * t, gr(g), 0};
        }
    }
    // Excurrent crowns: foliage on the leader's last metres too, so the top
    // ends in a leafy spire instead of a bare pole above the crown.
    if (C->leader) for (int s = 0; s < ns; s++) for (float u = 0.84f; u <= 1.001f; u += 0.01f) {
        V3 q, dq; float hq; path_at(&stems[s], u, &q, &dq, NULL, &hq, NULL);
        lp_add(&L, q, dq, (Wnd){hq, 0, 0.3f, 0, 0}, 6.0f);
    }
    for (int l = 0; l < nl; l++) {
        Limb* lb = &limbs[l]; Path P;
        grow(g, &P, lb->p, lb->d, lb->len, 7, lb->r, fmaxf(lb->r * 0.18f, 0.012f), C->trop1, 0.35f, E, 1.2f, lb->hw, 0.55f, 0, 0);
        if (C->droop > 0) for (int k = 1; k < P.n; k++) { float t = (float)k / (P.n - 1); P.p[k].y -= C->droop * lb->len * lb->len * t * t * 0.1f; }
        float v1 = bark(g, &P, lb->v, lb->ph, 0, 0);
        // Branches along the limb (phyllotaxis), the limb tip forking in two.
        float az = grr(g, 0, 2 * VPI);
        for (float s = grr(g, 0.15f, 0.3f) * lb->len; s < lb->len * 1.02f; s += C->sp2 * grr(g, 0.7f, 1.3f)) {
            float t = fminf(s / lb->len, 1); V3 p, d; float r, hw; path_at(&P, t, &p, &d, &r, &hw, NULL);
            az += 2.39996f;
            V3 perp = vrot(any_perp(d), d, az);
            float a = C->ang2 * grr(g, 0.75f, 1.25f) * (t > 0.98f ? 0.6f : 1);
            V3 d2 = vnorm(vadd(vadd(vmul(d, cosf(a)), vmul(perp, sinf(a))), vmul(vnorm(vsub(p, E->c)), 0.25f)));
            float len2 = clampf(env_dist(E, p, d2) * grr(g, 0.75f, 1.05f) + 0.2f, 0.3f, fmaxf(lb->len * 0.75f, 0.6f));
            Path Q; float phs = gr(g);
            grow(g, &Q, p, d2, len2, 4, fminf(r * 0.62f, 0.012f + 0.02f * len2), 0.008f, C->trop2, 0.6f, E, 1.5f, hw, fminf(hw + 0.3f, 1), 0, 0.55f);
            if (C->droop > 0) for (int k = 1; k < Q.n; k++) { float tq = (float)k / (Q.n - 1); Q.p[k].y -= C->droop * len2 * len2 * tq * tq * 0.15f; }
            if (!g->lod || Q.r[0] > 0.025f) bark(g, &Q, v1 * t, lb->ph, phs, 0);   // the reduced mesh drops the thinnest branches
            // Twigs; leaf points on the twigs and the outer part of the branch.
            for (float s3 = 0.15f * len2; s3 < len2; s3 += C->sp3 * grr(g, 0.7f, 1.3f)) {
                float t3 = s3 / len2; V3 p3, d3; float r3, hw3, ws3; path_at(&Q, t3, &p3, &d3, &r3, &hw3, &ws3);
                V3 perp3 = vrot(any_perp(d3), d3, grr(g, 0, 2 * VPI));
                float a3 = C->ang3 * grr(g, 0.7f, 1.3f);
                V3 dt = vnorm(vadd(vadd(vmul(d3, cosf(a3)), vmul(perp3, sinf(a3))), v3(0, C->trop3 * 0.3f, 0)));
                float lt = clampf(env_dist(E, p3, dt) + 0.25f, 0.15f, C->twig_len) * grr(g, 0.6f, 1.0f);
                Path T; grow(g, &T, p3, dt, lt, 2, fminf(r3 * 0.7f, 0.012f), 0.004f, C->trop3, 0.8f, NULL, 0, hw3, fminf(hw3 + 0.15f, 1), ws3, fminf(ws3 + 0.35f, 1));
                tip_add(g, T.p[T.n - 1], vnorm(vsub(T.p[T.n - 1], T.p[T.n - 2])), (Wnd){T.hw[T.n - 1], lb->ph, T.ws[T.n - 1], phs, 0}, env_depth(E, T.p[T.n - 1]));
                if (!g->lod) { int bt = g->bark_tone; g->fine = 1; g->bark_tone = 6; tube(g, &T, 3, 1, 0.12f, 0, lb->ph, phs, g->bark_layer, 0, 0); g->fine = 0; g->bark_tone = bt; }
                for (float u = 0.3f; u <= 1.001f; u += 0.24f) {
                    V3 q, dq; float hq, wq; path_at(&T, u, &q, &dq, NULL, &hq, &wq);
                    float dep = env_depth(E, q);
                    lp_add(&L, q, dq, (Wnd){hq, lb->ph, wq, phs, 0}, (0.12f + sstep(0.45f, 0.9f, dep)) * lerpf(1.0f, sstep(0.3f, 0.75f, t), C->pad));
                }
            }
        }
    }
    leaf_cards(g, &L, C->nleaf, E->c, C->leaf_layer, C->leaf_size, 1.0f, C->curl, C->upbias, 2, C->lod_layer, C->lod_size, C->lod_div ? C->lod_div : 3);
    free(L.a);
}

// ---------------------------------------------------------------- pinnate fronds
typedef struct {
    V3 base; float az, e0, droop, twist, L;
    float bare;                 // petiole fraction without leaflets
    int pairs;                  // leaflet pairs
    float lmax, lw;             // longest leaflet (m); card width / length
    float ang, keel, curl;      // leaflet angle from the rachis, lift out of the frond plane (the V), droop toward the tip (rad)
    int profile;                // 0 cycad (even, short at both ends), 1 fern (lanceolate), 2 feather palm
    int layer, cols;            // leaflet atlas layer, its sprite columns
    float rachis_r;             // rachis radius at the base (m); 0: none
    int segs, lsegs;            // rachis segments, segments per leaflet
    float ph, flex;             // limb phase, leaflet flexibility
} Frond;
static float frond_profile(int p, float t) {
    if (p == 0) return powf(fmaxf(sinf(VPI * clampf(t * 0.92f + 0.06f, 0, 1)), 0), 0.35f);
    if (p == 1) return powf(fmaxf(sinf(VPI * clampf(t * 0.9f + 0.08f, 0, 1)), 0), 0.75f);
    return powf(fmaxf(sinf(VPI * clampf(t * 0.85f + 0.12f, 0, 1)), 0), 0.6f);
}
static void frond(Gen* g, const Frond* F) {
    int n = F->segs; V3 pts[PMAX]; V3 side[PMAX], upv[PMAX];
    V3 dir = dir_az(F->az), side0 = vnorm(vcross(v3(0, 1, 0), dir)), p = F->base;
    for (int k = 0; k <= n; k++) {
        float t = (float)k / n, e = F->e0 - F->droop * (t * t * 1.3f + t * 0.2f);
        V3 fwd = vnorm(vadd(vmul(dir, cosf(e)), v3(0, sinf(e), 0)));
        V3 sd = vnorm(vrot(side0, fwd, F->twist * t)); sd = vnorm(vsub(sd, vmul(fwd, vdot(sd, fwd))));
        V3 up = vcross(sd, fwd); if (up.y < 0) { up = vmul(up, -1); }
        pts[k] = p; side[k] = sd; upv[k] = up;
        p = vadd(p, vmul(fwd, F->L / n));
    }
    if (F->rachis_r > 0) {
        Path P; P.n = n + 1;
        for (int k = 0; k <= n; k++) { float t = (float)k / n; P.p[k] = pts[k]; P.r[k] = F->rachis_r * lerpf(1.0f, 0.25f, t); P.hw[k] = 0.1f + 0.9f * t * t; P.ws[k] = 0; }
        int saved = g->bark_tone; g->bark_tone = 7;   // green-brown rachis tone (veg.fs)
        tube(g, &P, g->lod ? 3 : (F->rachis_r > 0.03f ? 4 : 3), 1, 0.25f, 0, F->ph, 0, F->layer == L_PSEG ? L_BARK_PALM : g->bark_layer, 0, 0);
        g->bark_tone = saved;
    }
    // Leaflet pairs.
    VRng sv = fork_in(g);
    for (int i = 0; i < F->pairs; i++) {
        float t = F->bare + (1 - F->bare) * (i + 0.5f) / F->pairs, f = t * n; int k = (int)f; if (k >= n) k = n - 1; f -= k;
        V3 c = vlerp(pts[k], pts[k + 1], f), fwd = vnorm(vsub(pts[k + 1], pts[k])), sd = vnorm(vlerp(side[k], side[k + 1], f)), up = vnorm(vlerp(upv[k], upv[k + 1], f));
        float u = (float)i / F->pairs, Lp = F->lmax * frond_profile(F->profile, u) * grr(g, 0.88f, 1.08f);
        float hw = 0.1f + 0.9f * t * t;
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            float a = F->ang * grr(g, 0.92f, 1.08f) * (F->profile == 1 ? lerpf(1.0f, 0.8f, u) : 1);
            V3 dl = vnorm(vadd(vmul(fwd, cosf(a)), vmul(sd, sgn * sinf(a))));
            dl = vnorm(vadd(vmul(dl, cosf(F->keel)), vmul(up, sinf(F->keel))));
            V3 s = vnorm(vcross(up, dl)); if (sgn > 0) s = vmul(s, -1);
            int col = (int)(gr(g) * F->cols) % F->cols; float u0 = (float)col / F->cols, u1 = (float)(col + 1) / F->cols;
            if (sgn < 0) { float tmp = u0; u0 = u1; u1 = tmp; }
            Wnd w = {hw, F->ph, 0, gr(g), 0};
            card(g, vadd(c, vmul(dl, -0.01f)), dl, s, Lp, Lp * F->lw, F->layer, u0, 0.0f, u1, 1.0f, F->lsegs, F->curl * (F->lsegs > 1 ? 1 : 0), w);
            // Leaflet flexibility: the card writes ws 0..0.15 along it; scale it up.
            for (int q = g->m->nv - 2 * (F->lsegs + 1); q < g->m->nv; q++) g->m->v[q].w[1] = (uint8_t)clampf(g->m->v[q].w[3] / 255.0f * F->flex * 255, 0, 255);
        }
    }
    fork_out(g, sv);
}
// Whole-frond ribbon (ground ferns, LOD and dead fronds): an arching strip of
// a whole-frond atlas layer; keel folds it along the rachis.
static void ribbon(Gen* g, V3 base, V3 dir, float e0, float droop, float L, float W, int layer, float keel, float twist, int segs, float ph, float wind0) {
    V3 c[PMAX], s[PMAX], up[PMAX]; float hw[PMAX]; if (segs > PMAX - 1) segs = PMAX - 1;
    V3 side0 = vnorm(vcross(v3(0, 1, 0), dir)), p = base;
    for (int k = 0; k <= segs; k++) {
        float t = (float)k / segs, e = e0 - droop * t * t * 1.3f - droop * t * 0.2f;
        V3 fwd = vnorm(vadd(vmul(dir, cosf(e)), v3(0, sinf(e), 0)));
        V3 sd = vnorm(vadd(vmul(side0, cosf(twist * t)), vmul(vcross(fwd, side0), sinf(twist * t))));
        V3 u = vnorm(vcross(sd, fwd)); if (u.y < 0) u = vmul(u, -1);
        c[k] = p; s[k] = sd; up[k] = u; hw[k] = 0.5f * W * (k == 0 ? 0.35f : 1.0f);
        p = vadd(p, vmul(fwd, L / segs));
    }
    strip(g, segs + 1, c, s, up, hw, keel, layer, 0, 1, 0, 1, (Wnd){wind0, ph, 0, gr(g), 0}, (Wnd){1, ph, 0.4f, 0, 1});
}

// ---------------------------------------------------------------- fruit, cones, flowers
// Skins (L_FRUIT columns) and petals (L_PETAL columns), painted in veg.c.
enum { SKIN_CONE, SKIN_RED, SKIN_DARK, SKIN_YELLOW, SKIN_ORANGE, SKIN_PALM, SKIN_GREEN, SKIN_WHITE };
enum { PET_LILY_W, PET_LILY_P, PET_MAGN, PET_YELLOW, PET_VIOLET, PET_BRACT, PET_SEPAL, PET_DRY };
// A fruit, seed or cone: an ellipsoid centred at c, radius r across and
// r * elong along ax, in a skin column, lit as a solid (untinted, tone 10).
static void ball(Gen* g, V3 c, V3 ax, float r, float elong, int skin, int seg, int rings, Wnd w) {
    ax = vnorm(ax); V3 e1 = any_perp(ax), e2 = vcross(ax, e1);
    int base = g->m->nv, bt = g->bark_tone; g->bark_tone = 10;
    float u0 = skin / 8.0f + 0.004f, du = 1 / 8.0f - 0.008f;
    for (int i = 0; i <= rings; i++) {
        float v = (float)i / rings, th = VPI * v, y = -cosf(th), rr = sinf(th);
        for (int j = 0; j <= seg; j++) {
            float a = 2 * VPI * j / seg; V3 rad = vadd(vmul(e1, cosf(a)), vmul(e2, sinf(a)));
            V3 p = vadd(c, vadd(vmul(ax, y * r * elong), vmul(rad, rr * r)));
            gv(g, p, u0 + du * j / seg, 0.02f + 0.96f * v, vadd(vmul(ax, y / elong), vmul(rad, rr)), 1, L_FRUIT, 0, w);
        }
    }
    for (int i = 0; i < rings; i++) for (int j = 0; j < seg; j++) {
        int a = base + i * (seg + 1) + j, b = a + 1, cc = a + seg + 1, d = cc + 1;
        gtri(g, a, cc, b); gtri(g, b, cc, d);
    }
    g->bark_tone = bt;
}
// A petal or bract card lit as a solid (untinted), both faces: from base b
// along d, across s, curling about s toward its tip.
static void petal(Gen* g, V3 b, V3 d, V3 s, float L, float W, int col, float curl, Wnd w) {
    int bt = g->bark_tone; g->bark_tone = 10;
    float u0 = col / 8.0f + 0.004f, u1 = (col + 1) / 8.0f - 0.004f;
    for (int face = 0; face < 2; face++) {
        int first = g->m->nv; V3 p = b, dir = d; const int segs = 2;
        for (int k = 0; k <= segs; k++) {
            float t = (float)k / segs; Wnd wk = w; wk.fl = t;
            V3 n = vcross(s, dir); if (face) n = vmul(n, -1);
            V3 off = vmul(n, 0.0015f);
            gv(g, vadd(vsub(p, vmul(s, 0.5f * W)), off), u0, t * 0.98f, n, 1, L_PETAL, 0, wk);
            gv(g, vadd(vadd(p, vmul(s, 0.5f * W)), off), u1, t * 0.98f, n, 1, L_PETAL, 0, wk);
            if (k < segs) { p = vadd(p, vmul(dir, L / segs)); dir = vrot(dir, s, curl / segs); }
        }
        for (int k = 0; k < segs; k++) { int a = first + 2 * k; gtri(g, a, a + 2, a + 1); gtri(g, a + 1, a + 2, a + 3); }
    }
    g->bark_tone = bt;
}
// A flower at c facing `up`: n petals of radius R cupped by `cup` (0 flat ..
// 1.4 a closed tulip), round a centre ball of skin `heart` (-1 none).
static void flower(Gen* g, V3 c, V3 up, float R, int col, int n, float cup, int heart, float heart_r, Wnd w) {
    up = vnorm(up); V3 e = any_perp(up); float a0 = grr(g, 0, 2 * VPI);
    for (int k = 0; k < n; k++) {
        float a = a0 + k * 2 * VPI / n + grr(g, -0.12f, 0.12f), cp = cup * grr(g, 0.85f, 1.15f) + (k & 1) * 0.12f;
        V3 dir = vrot(e, up, a), ax = vnorm(vadd(vmul(dir, cosf(cp)), vmul(up, sinf(cp)))), sd = vnorm(vcross(up, dir));
        petal(g, vadd(c, vmul(dir, R * 0.06f)), ax, sd, R * grr(g, 0.9f, 1.05f), R * 0.55f, col, -0.35f * (1 - cup * 0.5f), w);
    }
    if (heart >= 0) ball(g, vadd(c, vmul(up, heart_r * 0.6f)), up, heart_r, heart == SKIN_GREEN ? 2.0f : 1.0f, heart, 6, 3, w);
}
// Hanging fruit on a thin stalk from tip p: k balls along the stalk's end.
static void hang(Gen* g, V3 p, float stalk, int k, float r, int skin, Wnd w) {
    V3 d = vnorm(v3(grr(g, -0.3f, 0.3f), -1, grr(g, -0.3f, 0.3f))), e = any_perp(d);
    Path S; S.n = 2; S.p[0] = p; S.p[1] = vadd(p, vmul(d, stalk)); S.r[0] = S.r[1] = 0.0025f; S.hw[0] = w.hw; S.hw[1] = fminf(w.hw + 0.1f, 1); S.ws[0] = w.ws; S.ws[1] = fminf(w.ws + 0.3f, 1);
    int bt = g->bark_tone; g->bark_tone = 6; tube(g, &S, 3, 1, 0.1f, 0, w.ph, w.phs, g->bark_layer, 0, 0); g->bark_tone = bt;
    Wnd wf = w; wf.ws = S.ws[1];
    for (int i = 0; i < k; i++) {
        V3 o = vrot(e, d, i * 2.39996f + gr(g)); float f = k > 1 ? 1.4f * r : 0;
        ball(g, vadd(S.p[1], vadd(vmul(d, r * (0.8f + 0.5f * i)), vmul(o, f))), d, r * grr(g, 0.85f, 1.1f), 1.1f, skin, 5, 3, wf);
    }
}
// Fruit, cones and flowers of a crowned species at its recorded branch and
// twig ends (g->tip). Positions and sizes come from the plant's stream on
// both meshes alike; the reduced mesh keeps only what reads at its range
// (cones, flowers), the full one adds the small fruit (view only: g->fine).
static void add_fruit(Gen* g, float H) {
    int sp = g->species, nt = g->ntip; if (!nt) return;
    VRng sv = fork_in(g);
    for (int i = 0; i < nt; i++) {
        const TipPt* t = &g->tip[i]; float u = gr(g), u2 = gr(g), sz = grr(g, 0.85f, 1.15f);
        V3 up = v3(0, 1, 0);
        switch (sp) {
        case FL_ARAUCARIA: case FL_UMBRELLA:   // big upright seed cones at the upper branch ends, pollen cones lower down
            if (t->wt > 0.55f && u < (sp == FL_UMBRELLA ? 0.18f : 0.3f)) {
                float r = 0.075f * sz; ball(g, vadd(t->p, v3(0, r * 0.9f, 0)), vnorm(vadd(up, vmul(t->d, 0.3f))), r, 1.18f, u2 < 0.3f ? SKIN_GREEN : SKIN_CONE, g->lod ? 5 : 9, g->lod ? 3 : 6, t->w);
            } else if (t->wt < 0.5f && u < 0.35f && !g->lod) { g->fine = 1; ball(g, vadd(t->p, vmul(t->d, 0.05f)), t->d, 0.016f * sz, 3.2f, SKIN_CONE, 5, 3, t->w); g->fine = 0; }
            break;
        case FL_PODOCARP:   // a dark seed on a swollen red receptacle, hanging from the outer shoots
            if (t->wt > 0.45f && u < 0.025f && !g->lod) {
                g->fine = 1; V3 d = vnorm(vadd(t->d, v3(0, -1.3f, 0))), c = vadd(t->p, vmul(d, 0.02f));
                ball(g, c, d, 0.011f * sz, 1.35f, SKIN_RED, 6, 3, t->w); ball(g, vadd(c, vmul(d, 0.022f * sz)), d, 0.0085f * sz, 1.15f, SKIN_DARK, 6, 3, t->w); g->fine = 0;
            }
            break;
        case FL_MAGNOLIA:   // big cupped flowers at the crown's surface; cone-like red fruit inside
            if (t->wt > 0.55f && u < 0.08f && !g->lod) flower(g, vadd(t->p, vadd(vmul(t->d, 0.12f), v3(0, 0.06f, 0))), vnorm(vadd(t->d, v3(0, 1.4f, 0))), 0.13f * sz, PET_MAGN, 8, 0.85f, SKIN_GREEN, 0.02f, t->w);
            else if (t->wt > 0.5f && u > 0.985f && !g->lod) { g->fine = 1; ball(g, vadd(t->p, v3(0, 0.03f, 0)), vnorm(vadd(t->d, v3(0, 1, 0))), 0.024f * sz, 2.1f, SKIN_RED, 7, 5, t->w); g->fine = 0; }
            break;
        case FL_LAUREL: if (u < 0.03f && !g->lod) { g->fine = 1; hang(g, t->p, 0.03f, 3 + (int)(u2 * 3), 0.0075f, SKIN_DARK, t->w); g->fine = 0; } break;
        case FL_GINKGO: if (u < 0.06f && !g->lod) { g->fine = 1; hang(g, t->p, 0.05f, 2, 0.014f * sz, SKIN_YELLOW, t->w); g->fine = 0; } break;
        case FL_PLANE: if (u < 0.08f && !g->lod) { g->fine = 1; hang(g, t->p, grr(g, 0.1f, 0.18f), 1 + (int)(u2 * 2.5f), 0.02f * sz, SKIN_CONE, t->w); g->fine = 0; } break;
        case FL_UNDERSTOREY: if (u < 0.04f && !g->lod) { g->fine = 1; hang(g, t->p, 0.02f, 4 + (int)(u2 * 3), 0.0065f, SKIN_WHITE, t->w); g->fine = 0; } break;
        case FL_REDWOOD: case FL_CHEIROLEP:   // small round cones along the branch ends
            if (u < (sp == FL_REDWOOD ? 0.8f : 0.12f) && !g->lod) {
                g->fine = 1; int k = sp == FL_REDWOOD ? 1 + (int)(u2 * 3) : 1;
                for (int q = 0; q < k; q++) ball(g, vadd(t->p, vadd(vmul(t->d, -0.15f * q), v3(0, -0.02f, 0))), t->d, 0.011f * sz, 1.2f, SKIN_CONE, 6, 4, t->w);
                g->fine = 0;
            }
            break;
        case FL_BERRY_SHRUB: if (u < 0.14f && !g->lod) { g->fine = 1; hang(g, vadd(t->p, vmul(t->d, 0.08f)), 0.04f, 3 + (int)(u2 * 4), 0.01f, u2 < 0.25f ? SKIN_ORANGE : SKIN_RED, t->w); g->fine = 0; } break;
        }
    }
    (void)H;
    fork_out(g, sv);
}

// ---------------------------------------------------------------- species dimensions
// Height and trunk base radius per kind and variant: the generator's own
// stream, so colliders (cret_veg_trunk_radius) need no mesh.
static VRng species_rng(int kind, int variant) { VRng r = {hashu((uint32_t)kind * 7919u + (uint32_t)variant * 104729u + 12345u)}; return r; }
static void species_dims(int kind, int variant, float* H, float* r0) { veg_species_dims(kind, variant, H, r0); }   // veg_place.h (shared with the trunk colliders)

// ---------------------------------------------------------------- species
static void gen_broadleaf(Gen* g, int s) {
    float H, r0; species_dims(VEG_BROADLEAF, g->variant, &H, &r0);
    Crown C = {.H = H, .r0 = r0, .fork = grr(g, 0.3f, 0.42f), .stems = 1, .leader = 0, .limbs_lo = 3, .limbs_hi = 5, .sp1 = 0.9f,
               .el1 = 0.7f, .sp2 = 0.5f + 0.03f * H, .sp3 = 0.26f, .ang2 = 0.95f, .ang3 = 0.9f, .trop1 = 0.06f, .trop2 = 0.05f, .trop3 = 0.15f,
               .twig_len = 0.7f, .leaf_layer = L_CL_LAUREL, .lod_layer = L_LEAF2, .nleaf = 1100, .leaf_size = 0.45f, .lod_size = 1.5f,
               .curl = 0.35f, .upbias = 0.25f, .lobes = 0.25f, .droop = 0.0f};
    float spread = 1.0f, cy = 0.6f, ry = 0.4f;
    if (s == 0) { C.leaf_layer = L_CL_MAGN; C.lod_layer = L_LEAF; C.leaf_size = 0.62f; C.lod_size = 1.9f; C.nleaf = 900; C.twig_len = 0.8f; }
    else if (s == 2) { C.leaf_layer = L_CL_HEART; C.lod_layer = L_LEAF3; C.stems = 2 + (int)grr(g, 0, 2.99f); C.fork = 0.3f; C.sp1 = 1.0f; C.el1 = 0.55f;
                       C.leaf_size = 0.38f; C.nleaf = 1000; spread = 0.72f; cy = 0.62f; ry = 0.4f; }
    else if (s == 3) { C.fork = 0.38f; C.nleaf = 420; C.sp2 = 0.5f; C.leaf_size = 0.38f; C.lod_size = 1.0f; spread = 0.85f; C.droop = 0.4f; }
    else { C.nleaf = 1100; C.leaf_size = 0.45f; spread = 0.95f; }
    Env E; float rx = H * 0.44f * spread * grr(g, 0.9f, 1.1f), rz = H * 0.44f * spread * grr(g, 0.9f, 1.1f);
    env_init(g, &E, v3(grr(g, -0.06f, 0.06f) * H, H * cy, grr(g, -0.06f, 0.06f) * H), v3(rx, H * ry, rz), 6, 0.16f);
    crown_tree(g, &C, &E);
}
// Podocarp (Podocarpus, Prumnopitys, Dacrycarpus): a straight trunk with
// fibrous bark under a dense, dark, fine-textured evergreen crown. Many short
// limbs spiral up the leader, rising a little and sagging at the tips, and
// their foliage gathers in pads on the outer part of each limb, so the crown
// reads irregular and layered with dark gaps rather than as one smooth egg.
// The clusters are small (0.34 m) and many, shoots of narrow spiralled leaves
// (L_CL_PODO); the reduced mesh keeps every third, as branch cards (L_PODO).
static void gen_podocarp(Gen* g) {
    float H, r0; species_dims(VEG_CONIFER, g->variant, &H, &r0);
    Crown C = {.H = H, .r0 = r0, .fork = grr(g, 0.22f, 0.3f), .stems = 1, .leader = 1, .sp1 = 0.4f, .el1 = 0.12f,
               .sp2 = 0.3f, .sp3 = 0.15f, .ang2 = 1.0f, .ang3 = 0.95f, .trop1 = 0.05f, .trop2 = 0.08f, .trop3 = 0.2f, .twig_len = 0.45f,
               .leaf_layer = L_CL_PODO, .lod_layer = L_PODO, .nleaf = 3400, .leaf_size = 0.34f, .lod_size = 1.1f, .curl = 0.15f, .upbias = 0.45f,
               .lobes = 0.35f, .droop = 0.3f, .pad = 0.85f, .lod_div = 4};
    Env E; env_init(g, &E, v3(0, H * 0.6f, 0), v3(H * 0.26f * grr(g, 0.9f, 1.15f), H * 0.43f, H * 0.26f * grr(g, 0.9f, 1.15f)), 8, 0.3f);
    // Lumps may bulge the crown anywhere but never pull its top in: the
    // crown must taper up to the leader, or the leader stands bare above it.
    for (int i = 0; i < E.nl; i++) if (E.lump[i][1] > 0.4f) E.lump[i][3] = fmaxf(E.lump[i][3], 0);
    crown_tree(g, &C, &E);
}
// Araucaria. umbrella = 0: Norfolk-like tiers of cords; 1: monkey-puzzle-like
// dome of upturned scale-leaf ropes on a bare trunk.
static void rope_card(Gen* g, V3 p, V3 d, float len, Wnd w) {   // crossed pair: a cord from any side
    int col = (int)(gr(g) * 8) & 7; float u0 = col / 8.0f, u1 = (col + 1) / 8.0f, roll = grr(g, 0, VPI);
    for (int k = 0; k < 2; k++) {
        V3 s = vnorm(vrot(any_perp(d), d, roll + k * VPI * 0.5f));
        card(g, p, d, s, len, len * 0.2f, L_ROPE, u0, 0, u1, 1, 1, 0, w);
    }
}
// Crossed pair of rope-cluster cards (a branchlet forking into upcurled ropes), from base p along d.
static void rope_cluster(Gen* g, V3 p, V3 d, float len, Wnd w) {
    int sub = (int)(gr(g) * 4) & 3; float u0 = (sub & 1) * 0.5f, v0 = (sub >> 1) * 0.5f, roll = grr(g, 0, VPI);
    for (int k = 0; k < 2; k++) {
        V3 s = vnorm(vrot(any_perp(d), d, roll + k * VPI * 0.5f));
        card(g, vsub(p, vmul(d, 0.05f)), d, s, len, len * 0.9f, L_CL_ROPE, u0, v0, u0 + 0.5f, v0 + 0.5f, g->lod ? 1 : 2, 0.25f, w);
    }
}
// Crossed pair of rope cards lying along d from p: a scale-leaf sleeve
// segment on an umbrella branch (the sawtooth outline a tube cannot give).
static void rope_sleeve(Gen* g, V3 p, V3 d, float len, float wid, Wnd w) {
    int col = (int)(gr(g) * 8) & 7; float u0 = col / 8.0f, u1 = (col + 1) / 8.0f, roll = grr(g, 0, VPI);
    for (int k = 0; k < 2; k++) {
        V3 s = vnorm(vrot(any_perp(d), d, roll + k * VPI * 0.5f));
        card(g, p, d, s, len, wid, L_ROPE, u0, 0.05f, u1, 0.7f, 1, 0, w);
    }
}
static void gen_araucaria(Gen* g, bool umbrella) {
    float H, r0; species_dims(VEG_CONIFER, g->variant, &H, &r0);
    Path T; grow(g, &T, v3(0, -0.3f, 0), v3(grr(g, -0.02f, 0.02f), 1, grr(g, -0.02f, 0.02f)), H + 0.3f, 14, r0, 0.03f, 0, 0.02f, NULL, 0, 0, 0.2f, 0, 0);
    bark(g, &T, 0, 0, 0, 0.2f);
    float h0 = umbrella ? H * grr(g, 0.58f, 0.66f) : H * grr(g, 0.2f, 0.3f);
    if (umbrella) for (float h = H * 0.15f; h < h0; h += grr(g, 0.8f, 1.6f)) {   // dead branch stubs on the bare trunk
        V3 p; float r; path_at(&T, (h + 0.3f) / (H + 0.3f), &p, NULL, &r, NULL, NULL);
        Path S; grow(g, &S, p, dir_ae(grr(g, 0, 2 * VPI), grr(g, -0.2f, 0.2f)), r + grr(g, 0.1f, 0.35f), 1, 0.05f, 0.035f, 0, 0, NULL, 0, 0, 0, 0, 0);
        bark(g, &S, 0, 0, 0, 0);
    }
    int whorl = 0, nb = umbrella ? 6 : 5; float az0 = grr(g, 0, 2 * VPI);
    for (float h = h0; h < H * 0.975f; whorl++) {
        float hr = (h - h0) / (H - h0);
        int nbw = nb + (gr(g) < 0.3f ? (gr(g) < 0.5f ? -1 : 1) : 0);
        V3 pt; float rt, hwt; path_at(&T, (h + 0.3f) / (H + 0.3f), &pt, NULL, &rt, &hwt, NULL);
        for (int b = 0; b < nbw; b++) {
            float az = az0 + whorl * 0.7f + b * (2 * VPI / nbw) + grr(g, -0.15f, 0.15f), ph = gr(g);
            V3 d = dir_az(az);
            float bl = umbrella ? lerpf(1.4f, 4.6f, sqrtf(fmaxf(1 - hr * hr, 0))) * grr(g, 0.9f, 1.08f)
                                : (0.6f + powf(1 - hr, 0.9f) * 0.27f * H) * grr(g, 0.85f, 1.1f);
            bool dead = !umbrella && hr < 0.05f && gr(g) < 0.5f;
            int n = umbrella ? 8 : 6; Path P; P.n = n + 1;
            float el = umbrella ? lerpf(0.06f, 0.5f, hr) : lerpf(-0.06f, 0.4f, hr);
            for (int k = 0; k <= n; k++) {   // out nearly level, a sag, the tip turning up
                float t = (float)k / n, x = bl * t;
                float y = x * sinf(el) + (umbrella ? bl * 0.22f * t * t * t : bl * (-0.05f * sinf(VPI * t) + 0.08f * t * t * t));
                P.p[k] = vadd(pt, vadd(vmul(d, x * cosf(el)), v3(0, y, 0)));
                // A branch is a slim pole: 4-9 cm at the trunk, pencil-thin at the tip.
                P.r[k] = lerpf(fminf(0.02f + 0.012f * bl, rt * 0.7f), umbrella ? 0.015f : 0.006f, powf(t, 0.7f));
                P.hw[k] = hwt + 0.6f * t; P.ws[k] = 0;
            }
            if (umbrella && g->lod) tube(g, &P, 3, 1, 0.3f, 0, ph, 0, L_SCALES, 1, 0);   // one coarse tube on the reduced mesh
            else if (umbrella) {   // woody near the trunk, then a dark scale-leaf rope under the sleeve of cards
                Path A = P, B = P; int cut = n / 4 + 1; A.n = cut + 1; B.n = P.n - cut;
                for (int k = 0; k < B.n; k++) { B.p[k] = P.p[k + cut]; B.r[k] = P.r[k + cut] + 0.012f; B.hw[k] = P.hw[k + cut]; B.ws[k] = P.ws[k + cut]; }
                bark(g, &A, 0, ph, 0, 0);
                tube(g, &B, g->lod ? 3 : 4, 1, 0.3f, 0, ph, 0, L_SCALES, 1, 0);
            } else tube(g, &P, g->lod ? 3 : 5, 1, 0.25f, 0, ph, 0, g->bark_layer, 0, 0);
            if (dead) continue;
            tip_add(g, P.p[n], vnorm(vsub(P.p[n], P.p[n - 1])), (Wnd){P.hw[n], ph, 0, 0, 0}, hr);
            // Branchlets: the same places on both meshes (one forked draw each);
            // the reduced mesh keeps every second or third, scaled up.
            VRng sv = fork_in(g);
            float step = umbrella ? 0.3f : 0.22f, side = 1; int bi = 0;
            for (float s = (umbrella ? 0.24f : 0.1f + 0.15f * (1 - hr)) * bl; s < bl * 0.97f; s += step * grr(g, 0.85f, 1.15f), side = -side, bi++) {
                float t = s / bl; V3 p, dp; float r, hw; path_at(&P, t, &p, &dp, &r, &hw, NULL);
                V3 sd = vnorm(vcross(dp, v3(0, 1, 0))); float phs = gr(g);
                int div = umbrella ? 3 : 4; bool keep = !g->lod || bi % div == 0; float big = g->lod ? (umbrella ? 1.7f : 2.0f) : 1;
                Wnd w = {hw, ph, 0, phs, 0}; VRng s2 = fork_in(g);
                if (umbrella) {
                    // The branch itself is clothed in scale leaves: a sleeve of crossed rope cards along it, and
                    // a branchlet forking into ropes that curl up and out, alternate sides.
                    if (keep && (!g->lod || bi % 4 == 0)) rope_sleeve(g, vsub(p, vmul(dp, 0.12f * big)), dp, 0.5f * big, 0.16f * big, w);
                    V3 dd = vnorm(vadd(vadd(vmul(dp, 0.5f), vmul(sd, side * 0.7f)), v3(0, grr(g, 0.3f, 0.8f), 0)));
                    float ll = grr(g, 0.7f, 1.15f) * (1 - 0.35f * t);
                    if (keep) rope_cluster(g, p, dd, ll * (g->lod ? 1.5f : 1.0f), w);
                } else {
                    // Norfolk: pinnate sprays in the tier's plane, alternate, swept forward and drooping a
                    // little, as a pair of cards rolled +-40 degrees about the spray, so the tier reads flat
                    // from above and below and still has some depth from the side.
                    float a = grr(g, 0.8f, 1.1f);
                    V3 dd = vnorm(vadd(vadd(vmul(dp, cosf(a)), vmul(sd, side * sinf(a))), v3(0, -0.12f + 0.1f * t, 0)));
                    float ll = lerpf(0.9f, 0.45f, t) * grr(g, 0.85f, 1.1f) * fminf(1, 0.45f + bl / 4);
                    if (keep) {   // the reduced mesh: one card per kept branchlet, rolled either way in turn
                        V3 flat = vnorm(vcross(dd, v3(0, 1, 0))); float roll = grr(g, -0.15f, 0.15f);
                        for (int k = -1; k <= 1; k += 2) {
                            if (g->lod && ((bi / div) & 1) != (k > 0)) continue;
                            card(g, vsub(p, vmul(dd, 0.04f * big)), dd, vrot(flat, dd, roll + k * 0.7f), ll * big, ll * 0.85f * big, L_ARAU, 0, 0, 1, 1, 1, 0, w);
                        }
                    }
                }
                fork_out(g, s2);
            }
            if (!umbrella) { VRng s2 = fork_in(g); rope_card(g, P.p[n], vnorm(vadd(vsub(P.p[n], P.p[n - 1]), v3(0, 0.1f, 0))), 0.45f, (Wnd){P.hw[n], ph, 0, gr(g), 0}); fork_out(g, s2); }
            fork_out(g, sv);
        }
        h += umbrella ? grr(g, 0.85f, 1.15f) : grr(g, 0.9f, 1.3f) * lerpf(1.0f, 0.7f, hr);
    }
    // Leader: a tuft of cords (Norfolk) or a cap of rising rope clusters (umbrella).
    V3 top = T.p[T.n - 1];
    for (int b = 0; b < (umbrella ? 7 : 5); b++) {
        V3 d = vnorm(vadd(vmul(dir_az(b * 2 * VPI / (umbrella ? 7 : 5) + gr(g)), umbrella ? 0.9f : 0.35f), v3(0, 1, 0)));
        float ll = umbrella ? grr(g, 1.0f, 1.5f) : grr(g, 0.5f, 0.8f), ph = gr(g); VRng s2 = fork_in(g);
        if (umbrella) rope_cluster(g, vadd(top, v3(0, -0.4f, 0)), d, ll * (g->lod ? 1.3f : 1.0f), (Wnd){0.3f, ph, 0, gr(g), 0});
        else rope_card(g, vadd(top, v3(0, -0.3f, 0)), d, ll, (Wnd){0.3f, ph, 0, gr(g), 0});
        fork_out(g, s2);
    }
}
// Metasequoia: buttressed, conical, branches in loose spirals with twigs of
// hanging feathery sprays.
static void gen_metasequoia(Gen* g) {
    float H, r0; species_dims(VEG_CONIFER, g->variant, &H, &r0);
    Path T; grow(g, &T, v3(0, -0.3f, 0), v3(0, 1, 0), H + 0.3f, 14, r0, 0.03f, 0, 0.02f, NULL, 0, 0, 0.18f, 0, 0);
    for (int k = 0; k < T.n; k++) { float t = (float)k / (T.n - 1); T.r[k] = lerpf(r0, 0.03f, powf(t, 0.6f)) + 0.35f * expf(-t * 14); }
    bark(g, &T, 0, 0, 0, 0.6f);
    LeafPts L = {0}; float h0 = H * grr(g, 0.15f, 0.22f), az = grr(g, 0, 2 * VPI);
    for (float h = h0; h < H * 0.97f; h += grr(g, 0.45f, 0.75f)) {
        float hr = (h - h0) / (H - h0); az += 2.39996f + grr(g, -0.2f, 0.2f);
        V3 pt; float rt, hwt; path_at(&T, (h + 0.3f) / (H + 0.3f), &pt, NULL, &rt, &hwt, NULL);
        float bl = (0.6f + powf(1 - hr, 1.1f) * 0.22f * H) * grr(g, 0.8f, 1.15f), el = lerpf(-0.25f, 0.5f, hr) + grr(g, -0.1f, 0.1f), ph = gr(g);
        Path P; int n = 6; P.n = n + 1; V3 d = dir_az(az);
        for (int k = 0; k <= n; k++) {
            float t = (float)k / n, x = bl * t;
            P.p[k] = vadd(pt, vadd(vmul(d, x * cosf(el)), v3(0, x * sinf(el) - (1 - hr) * 0.08f * x * x, 0)));
            P.r[k] = lerpf(fminf(0.02f + 0.015f * bl, rt * 0.7f), 0.008f, powf(t, 0.8f)); P.hw[k] = hwt + 0.6f * t; P.ws[k] = 0;
        }
        bark(g, &P, 0, ph, 0, 0);
        tip_add(g, P.p[n], vnorm(vsub(P.p[n], P.p[n - 1])), (Wnd){P.hw[n], ph, 0, 0, 0}, hr);
        float side = 1; VRng sv = fork_in(g);
        for (float s = 0.2f * bl; s < bl; s += grr(g, 0.18f, 0.26f), side = -side) {
            float t = s / bl; V3 p, dp; float r, hw; path_at(&P, t, &p, &dp, &r, &hw, NULL);
            V3 sd = vnorm(vcross(dp, v3(0, 1, 0)));
            V3 dt = vnorm(vadd(vadd(vmul(dp, 0.5f), vmul(sd, side)), v3(0, -0.35f, 0)));
            float lt = lerpf(0.8f, 0.35f, t) * grr(g, 0.8f, 1.1f); float phs = gr(g);
            Path Q; grow(g, &Q, p, dt, lt, 2, 0.01f, 0.004f, -0.3f, 0.5f, NULL, 0, hw, fminf(hw + 0.15f, 1), 0, 0.6f);
            if (!g->lod) { int bt = g->bark_tone; g->fine = 1; g->bark_tone = 6; tube(g, &Q, 3, 1, 0.12f, 0, ph, phs, g->bark_layer, 0, 0); g->fine = 0; g->bark_tone = bt; }
            lp_add(&L, p, vnorm(vadd(dt, v3(0, -0.6f, 0))), (Wnd){hw, ph, 0, phs, 0}, 0.7f);   // sprays along the branch itself too
            for (float u = 0.2f; u <= 1.001f; u += 0.27f) {
                V3 q, dq; float hq, wq; path_at(&Q, u, &q, &dq, NULL, &hq, &wq);
                lp_add(&L, q, vnorm(vadd(dq, v3(0, -0.5f, 0))), (Wnd){hq, ph, wq, phs, 0}, 1);
            }
        }
        fork_out(g, sv);
    }
    // Sprays hang off the twigs in every roll.
    leaf_cards(g, &L, 2600, v3(0, H * 0.5f, 0), L_CL_SPRAY, 0.62f, 0.85f, 0.4f, -0.15f, 1, L_SPRAY, 1.5f, 4);
    free(L.a);
}
// Cycad crown on top of a trunk at `top`.
static void cycad_crown(Gen* g, V3 top, int s, float scale, int nf) {
    VRng sv = fork_in(g);
    for (int f = 0; f < 5; f++) {   // dead fronds hanging against the trunk (one ribbon each on the reduced mesh)
        g->dry = (int)grr(g, 170, 255); float az = f * 2.39996f + grr(g, 0, 1), e0 = grr(g, -1.2f, -0.6f), tw = grr(g, -0.3f, 0.3f), L = grr(g, 0.9f, 1.4f) * scale, ph = gr(g);
        VRng s2 = fork_in(g);
        if (g->lod) ribbon(g, vadd(top, v3(0, -0.1f, 0)), dir_az(az), e0, 0.4f, L, 0.45f * scale, L_CYCAD, 0.3f, tw, 3, ph, 0.05f);
        else {
            Frond F = {.base = vadd(top, v3(0, -0.08f, 0)), .az = az, .e0 = e0, .droop = 0.3f, .twist = tw, .L = L,
                       .bare = 0.12f, .pairs = 14, .lmax = 0.18f * scale, .lw = 0.12f, .ang = 1.1f, .keel = -0.2f, .curl = 0.0f, .profile = 0, .layer = L_LEAFLET, .cols = 8,
                       .rachis_r = 0.012f, .segs = 4, .lsegs = 1, .ph = ph, .flex = 0.5f};
            frond(g, &F);
        }
        fork_out(g, s2);
    }
    g->dry = 0;
    for (int f = 0; f < nf; f++) {
        float az = f * 2.39996f + grr(g, -0.1f, 0.1f), age = (float)f / nf;
        g->dry = age < 0.12f ? (int)grr(g, 30, 110) : 0;
        float L = (s == 1 ? grr(g, 0.9f, 1.4f) : grr(g, 1.4f, 2.1f)) * lerpf(1.0f, 0.75f, age) * scale;
        float e0 = (s == 1 ? lerpf(0.55f, 1.15f, age) : lerpf(0.1f, 0.95f, age)) + grr(g, -0.08f, 0.08f);
        float droop = s == 1 ? grr(g, 0.1f, 0.3f) : grr(g, 0.35f, 0.7f);
        V3 b = vadd(top, vmul(dir_az(az), 0.12f * scale)); float tw = grr(g, -0.15f, 0.15f); int pairs = 34 + (int)grr(g, 0, 10);
        Frond F = {.base = b, .az = az, .e0 = e0, .droop = droop, .twist = tw, .L = L, .bare = 0.1f,
                   .pairs = g->lod ? 11 : pairs, .lmax = L * (s == 1 ? 0.19f : 0.16f), .lw = g->lod ? 0.42f : 0.2f,
                   .ang = s == 1 ? 1.0f : 1.2f, .keel = s == 1 ? 0.55f : 0.3f, .curl = 0.1f, .profile = 0, .layer = L_LEAFLET, .cols = 8,
                   .rachis_r = g->lod ? 0 : 0.016f * scale, .segs = g->lod ? 3 : 6, .lsegs = 1, .ph = gr(g), .flex = 0.35f};
        if (g->lod) F.lmax *= 1.15f;
        frond(g, &F);
    }
    g->dry = 0;
    fork_out(g, sv);
}
static void gen_cycad(Gen* g, int s) {
    float H, r0; species_dims(VEG_CYCAD, g->variant, &H, &r0);
    if (s == 3) {   // Williamsonia: a slim trunk forking into a few branches, each tipped by a crown and a cone
        V3 b0[3] = {v3(0, -0.2f, 0), v3(0.05f, H * 0.5f, -0.03f), v3(0, H, 0)}; Path T; T.n = 3;
        for (int k = 0; k < 3; k++) { T.p[k] = b0[k]; T.r[k] = lerpf(r0, r0 * 0.8f, k / 2.0f); T.hw[k] = 0.02f * k; T.ws[k] = 0; }
        float v = bark(g, &T, 0, 0, 0, 0);
        int ns = 2 + (int)grr(g, 0, 1.99f); float az0 = grr(g, 0, 2 * VPI);
        for (int k = 0; k < ns; k++) {
            float az = az0 + k * (2 * VPI / ns) + grr(g, -0.35f, 0.35f), lean = grr(g, 0.55f, 0.85f), h = grr(g, 1.0f, 1.7f); V3 d = dir_az(az);
            Path B; B.n = 3;
            B.p[0] = b0[2]; B.p[1] = vadd(b0[2], vadd(vmul(d, h * 0.45f * sinf(lean)), v3(0, h * 0.45f * cosf(lean), 0)));
            B.p[2] = vadd(b0[2], vadd(vmul(d, h * 0.65f * sinf(lean)), v3(0, h * cosf(lean * 0.6f), 0)));
            for (int q = 0; q < 3; q++) { B.r[q] = lerpf(0.13f, 0.11f, q / 2.0f); B.hw[q] = 0.04f + 0.08f * q; B.ws[q] = 0; }
            bark(g, &B, v, gr(g), 0, 0);
            V3 tip = B.p[2]; Path Cn; Cn.n = 3;
            for (int q = 0; q < 3; q++) { Cn.p[q] = vadd(tip, v3(0, -0.05f + 0.16f * q, 0)); Cn.r[q] = q == 2 ? 0.02f : q == 1 ? 0.1f : 0.09f; Cn.hw[q] = 0.2f; Cn.ws[q] = 0; }
            int bt = g->bark_tone; g->bark_tone = 8; tube(g, &Cn, 6, 2, 0.12f, 0, 0, 0, g->bark_layer, 0, 0); g->bark_tone = bt;   // the flower cone
            VRng sf = fork_in(g);   // its ring of bracts, open like a flower
            flower(g, vadd(tip, v3(0, 0.02f, 0)), v3(0, 1, 0), grr(g, 0.12f, 0.16f), PET_BRACT, 11, 0.55f, -1, 0, (Wnd){0.3f, 0, 0, 0, 0});
            fork_out(g, sf);
            cycad_crown(g, tip, 0, 0.8f, 15);
        }
        return;
    }
    Path T; T.n = 5;
    for (int k = 0; k < 5; k++) { float t = k / 4.0f; T.p[k] = v3(0, -0.2f + (H + 0.2f) * t, 0); T.r[k] = r0 * (1 - 0.15f * t) + 0.04f * sinf(VPI * t); T.hw[k] = 0.03f * t; T.ws[k] = 0; }
    if (s == 2) for (int k = 0; k < 5; k++) { float t = k / 4.0f; T.p[k].x += 0.25f * t * t; }
    tube(g, &T, sides_for(g, r0) + 2, fmaxf(1, roundf(r0 * 6)), 0.6f, 0, 0, 0, g->bark_layer, 0, 0);
    V3 top = T.p[4];
    int nf = 20 + (int)grr(g, 0, 8);
    cycad_crown(g, top, s, s == 1 ? 0.9f : 1.0f, nf);
    // Seeds: the squat cycad's big central cone; the trunked one's ring of
    // orange seeds on brown felted megasporophylls in the crown's middle.
    VRng sf = fork_in(g); Wnd w = {0.05f, 0, 0, 0, 0};
    if (s == 1) ball(g, vadd(top, v3(0, 0.3f, 0)), v3(grr(g, -0.1f, 0.1f), 1, 0), grr(g, 0.1f, 0.13f), 2.3f, SKIN_CONE, 9, 6, w);
    else {
        int ns = 8 + (int)grr(g, 0, 6);
        for (int k = 0; k < ns; k++) {
            float a = k * 2.39996f + gr(g), d = grr(g, 0.07f, 0.15f); V3 o = dir_az(a);
            ball(g, vadd(top, vadd(vmul(o, d), v3(0, grr(g, 0.04f, 0.12f), 0))), vnorm(vadd(o, v3(0, 1, 0))), 0.022f, 1.3f, SKIN_ORANGE, 7, 4, w);
        }
        ball(g, vadd(top, v3(0, 0.05f, 0)), v3(0, 1, 0), 0.1f, 0.6f, SKIN_PALM, 8, 4, w);
    }
    fork_out(g, sf);
}
// Tree fern: slim fibrous trunk, arching bipinnate fronds, croziers, a dead skirt.
// stout (Dicksonia-like): a short, thick, shaggy trunk and a broader vase of
// longer, more upright fronds.
static void gen_treefern(Gen* g, bool stout) {
    float H, r0; species_dims(VEG_TREEFERN, g->variant, &H, &r0);
    float lx = grr(g, -0.5f, 0.5f), lz = grr(g, -0.5f, 0.5f);
    Path T; T.n = 8;
    for (int k = 0; k < 8; k++) { float t = k / 7.0f; T.p[k] = v3(lx * t * t, -0.2f + (H + 0.2f) * t, lz * t * t); T.r[k] = lerpf(r0, 0.15f, sqrtf(t)) + (k == 7 ? 0.06f : 0) + 0.12f * expf(-t * 10); T.hw[k] = 0.15f * t; T.ws[k] = 0; }
    tube(g, &T, sides_for(g, r0) + 2, fmaxf(1, roundf(r0 * 6)), 1.0f, 0, 0, 0, g->bark_layer, 0, 0.3f);
    V3 top = T.p[7];
    int nd = 5 + (int)grr(g, 0, 4); if (g->lod) nd = 4;
    VRng sv = fork_in(g);
    for (int f = 0; f < nd; f++) {   // dead fronds hang against the trunk
        float az = f * 2.39996f + grr(g, -0.3f, 0.3f); g->dry = (int)grr(g, 190, 255);
        ribbon(g, vadd(top, v3(0, -0.1f, 0)), dir_az(az), grr(g, -1.35f, -1.0f), grr(g, 0.1f, 0.35f), grr(g, 1.3f, 2.0f), grr(g, 0.45f, 0.6f), L_TFERN, 0.25f, grr(g, -0.6f, 0.6f), g->lod ? 3 : 5, gr(g), 0.05f);
    }
    fork_out(g, sv);
    int nf = 18 + (int)grr(g, 0, 6);
    for (int f = 0; f < nf; f++) {
        float az = f * 2.39996f + grr(g, -0.2f, 0.2f), age = (float)f / nf; bool young = f >= nf - 3; int pairs = 18 + (int)grr(g, 0, 5);
        g->dry = age < 0.2f ? (int)grr(g, 30, 110) : 0;
        float L = (young ? grr(g, 0.9f, 1.4f) : grr(g, 2.3f, 3.2f)) * (stout ? 0.9f : 1.0f);
        float e0 = (young ? grr(g, 1.25f, 1.45f) : lerpf(0.55f, 1.1f, age) + grr(g, -0.1f, 0.1f)) + (stout ? 0.25f : 0);
        float droop = (young ? 0.6f : lerpf(2.1f, 1.4f, age) + grr(g, -0.2f, 0.2f)) * (stout ? 0.7f : 1.0f);
        Frond F = {.base = top, .az = az, .e0 = e0, .droop = droop, .twist = grr(g, -0.3f, 0.3f), .L = L, .bare = 0.1f,
                   .pairs = g->lod ? 8 : pairs, .lmax = L * 0.4f, .lw = g->lod ? 0.55f : 0.38f, .ang = 1.25f, .keel = 0.35f, .curl = -0.5f,
                   .profile = 1, .layer = L_FPINNA, .cols = 4, .rachis_r = g->lod ? 0 : 0.022f, .segs = g->lod ? 4 : 8, .lsegs = g->lod ? 1 : 2, .ph = gr(g), .flex = 0.6f};
        if (g->lod) F.lmax *= 1.12f;
        frond(g, &F);
    }
    g->dry = 0;
    if (!g->lod) for (int c = 0; c < 3; c++) {   // croziers: young fronds still coiled
        float az = grr(g, 0, 2 * VPI); V3 d = dir_az(az); Path P; int n = 14; P.n = n + 1;
        float h = grr(g, 0.35f, 0.6f), R = grr(g, 0.06f, 0.09f);
        for (int k = 0; k <= n; k++) {
            float t = (float)k / n, up = h * fminf(t / 0.6f, 1), a = t < 0.6f ? 0 : (t - 0.6f) / 0.4f * 4.5f;
            V3 q = vadd(top, vadd(v3(0, up + R * sinf(a), 0), vmul(d, 0.05f + R * (1 - cosf(a)))));
            P.p[k] = q; P.r[k] = lerpf(0.022f, 0.012f, t); P.hw[k] = 0.15f + 0.2f * t; P.ws[k] = 0;
        }
        int bt = g->bark_tone; g->bark_tone = 9; tube(g, &P, 5, 1, 0.1f, 0, gr(g), 0, g->bark_layer, 0, 0); g->bark_tone = bt;
    }
}
// Palm: costapalmate fan of folded segments (Sabal-like), or a feather palm.
static void fan_leaf(Gen* g, V3 top, float az, float age, float scale, float ph) {
    VRng sv = fork_in(g);
    float pl = grr(g, 1.3f, 1.9f) * scale, e0 = lerpf(-0.1f, 1.1f, age) + grr(g, -0.12f, 0.12f), droop = lerpf(0.9f, 0.3f, age);
    // Petiole.
    Path P; int n = 4; P.n = n + 1; V3 d = dir_az(az), p = top;
    for (int k = 0; k <= n; k++) {
        float t = (float)k / n, e = e0 - droop * t * 0.6f; V3 f = vnorm(vadd(vmul(d, cosf(e)), v3(0, sinf(e), 0)));
        P.p[k] = p; P.r[k] = lerpf(0.045f, 0.025f, t); P.hw[k] = 0.15f + 0.45f * t; P.ws[k] = 0;
        if (k < n) p = vadd(p, vmul(f, pl / n));
    }
    int bt = g->bark_tone; g->bark_tone = 7; tube(g, &P, g->lod ? 3 : 4, 1, 0.3f, 0, ph, 0, L_BARK_PALM, 0, 0); g->bark_tone = bt;
    V3 h = P.p[n], f = vnorm(vsub(P.p[n], P.p[n - 1]));
    V3 u0 = vnorm(vsub(v3(0, 1, 0), vmul(f, vdot(v3(0, 1, 0), f)))), s = vcross(f, u0);
    float R = grr(g, 1.25f, 1.6f) * scale, span = grr(g, 1.3f, 1.6f);
    int nseg = g->lod ? 9 : 22 + (int)grr(g, 0, 5), ls = g->lod ? 2 : 4;
    for (int i = 0; i < nseg; i++) {
        float a = -span + 2 * span * (i + 0.5f) / nseg + grr(g, -0.03f, 0.03f), Ls = R * (0.8f + 0.2f * cosf(a)) * grr(g, 0.9f, 1.05f);
        // Costapalmate: the costa runs on along f and droops; segments leave it at angle a, fold, and their tips hang.
        V3 dd = vnorm(vadd(vadd(vmul(f, cosf(a)), vmul(s, sinf(a))), vmul(u0, 0.18f * (1 - cosf(a)))));
        V3 c[PMAX], sv[PMAX], up[PMAX]; float hw[PMAX]; V3 q = vadd(h, vmul(f, 0.06f * cosf(a)));
        float wmax = fmaxf(2 * span * R / nseg * 0.62f, 0.05f) * (g->lod ? 1.7f : 1.0f);
        for (int k = 0; k <= ls; k++) {
            float t = (float)k / ls; V3 dk = vnorm(vadd(dd, v3(0, -droop * 0.9f * t * t - 0.15f * t, 0)));
            V3 sk = vnorm(vcross(dk, u0)); if (vlen(sk) < 0.1f) sk = s;
            V3 uk = vnorm(vcross(sk, dk)); if (vdot(uk, u0) < 0) uk = vmul(uk, -1);
            c[k] = q; sv[k] = sk; up[k] = uk; hw[k] = wmax * (0.12f + 0.88f * sstep(0.0f, 0.45f, t)) * (1 - 0.35f * t * t);
            if (k < ls) q = vadd(q, vmul(dk, Ls / ls));
        }
        int col = (int)(gr(g) * 8) & 7;
        strip(g, ls + 1, c, sv, up, hw, g->lod ? 0 : 0.35f, L_PSEG, col / 8.0f, (col + 1) / 8.0f, 0.02f, 0.98f,
              (Wnd){P.hw[n], ph, 0, gr(g), 0}, (Wnd){fminf(P.hw[n] + 0.3f, 1), ph, 0.6f, 0, 1});
    }
    fork_out(g, sv);
}
static void gen_palm(Gen* g, int s) {
    float H, r0; species_dims(VEG_PALM, g->variant, &H, &r0);
    bool feather = s == 3; float bend = grr(g, 0.3f, feather ? 1.8f : 1.2f), az0 = grr(g, 0, 2 * VPI);
    Path T; T.n = 9;
    for (int k = 0; k < 9; k++) { float t = k / 8.0f; T.p[k] = vadd(v3(0, -0.2f + (H + 0.2f) * t, 0), vmul(dir_az(az0), bend * t * t)); T.r[k] = lerpf(r0, r0 * 0.75f, t) + 0.08f * expf(-t * 12); T.hw[k] = 0.25f * t * t; T.ws[k] = 0; }
    tube(g, &T, sides_for(g, r0), fmaxf(1, roundf(r0 * 6)), 0.5f, 0, 0, 0, g->bark_layer, 0, 0);
    V3 top = T.p[8];
    // Dead leaves hanging against the trunk.
    for (int f = 0; f < 3; f++) {
        float az = f * 2.1f + grr(g, 0, 1); g->dry = (int)grr(g, 200, 255);
        if (feather) ribbon(g, vadd(top, v3(0, -0.25f, 0)), dir_az(az), grr(g, -1.35f, -1.0f), 0.2f, grr(g, 2.0f, 2.6f), 0.6f, L_CYCAD, 0.2f, grr(g, -0.5f, 0.5f), 4, gr(g), 0.05f);
        else ribbon(g, vadd(top, v3(0, -0.25f, 0)), dir_az(az), grr(g, -1.3f, -0.9f), 0.2f, grr(g, 2.2f, 2.8f), grr(g, 1.4f, 1.8f), L_PALM, 0.15f, grr(g, -0.5f, 0.5f), 4, gr(g), 0.05f);
    }
    g->dry = 0;
    int nf = feather ? 12 + (int)grr(g, 0, 5) : 11 + (int)grr(g, 0, 4);
    for (int f = 0; f < nf; f++) {
        float az = f * 2.39996f + grr(g, -0.2f, 0.2f), age = (float)f / nf, ph = gr(g);
        g->dry = age < 0.12f ? (int)grr(g, 40, 120) : 0;
        if (!feather) { fan_leaf(g, top, az, age, 1.0f, ph); continue; }
        float L = grr(g, 3.0f, 4.2f) * lerpf(1.0f, 0.8f, age); int pairs = 38 + (int)grr(g, 0, 10);
        Frond F = {.base = top, .az = az, .e0 = lerpf(0.0f, 1.25f, age) + grr(g, -0.1f, 0.1f), .droop = lerpf(1.5f, 0.6f, age), .twist = grr(g, -0.4f, 0.4f),
                   .L = L, .bare = 0.18f, .pairs = g->lod ? 13 : pairs, .lmax = 0.75f * grr(g, 0.85f, 1.1f), .lw = g->lod ? 0.2f : 0.11f,
                   .ang = 1.15f, .keel = 0.4f, .curl = -0.7f, .profile = 2, .layer = L_PSEG, .cols = 8, .rachis_r = 0.04f,
                   .segs = g->lod ? 4 : 8, .lsegs = g->lod ? 1 : 3, .ph = ph, .flex = 0.8f};
        frond(g, &F);
    }
    g->dry = 0;
    // Fruit: two to four branched stalks arching out and down from under the
    // crown, hung with drupes (purple-black on the fan palm, orange on the
    // feather palm); the reduced mesh keeps the stalks.
    VRng sf = fork_in(g); int nc = 2 + (int)grr(g, 0, 2.99f);
    for (int c = 0; c < nc; c++) {
        float az = az0 + c * 2.1f + gr(g), L = grr(g, 0.7f, 1.1f), ph = gr(g); V3 d = dir_az(az);
        Path P; P.n = 5;
        for (int k = 0; k < 5; k++) { float t = k / 4.0f; P.p[k] = vadd(top, vadd(vmul(d, L * 0.6f * t), v3(0, -0.25f + L * (0.25f * t - 0.9f * t * t), 0))); P.r[k] = lerpf(0.025f, 0.008f, t); P.hw[k] = 0.3f + 0.4f * t; P.ws[k] = 0; }
        int bt = g->bark_tone; g->bark_tone = 7; tube(g, &P, 4, 1, 0.3f, 0, ph, 0, L_BARK_PALM, 0, 0); g->bark_tone = bt;
        for (int b = 0; b < 5; b++) {   // rachillae hanging off the stalk's outer half, drupes along them
            V3 p0; float hw; path_at(&P, 0.5f + 0.12f * b, &p0, NULL, NULL, &hw, NULL);
            V3 dd = vnorm(v3(grr(g, -0.4f, 0.4f), -1, grr(g, -0.4f, 0.4f))); float l = grr(g, 0.15f, 0.3f);
            Path Q; Q.n = 2; Q.p[0] = p0; Q.p[1] = vadd(p0, vmul(dd, l)); Q.r[0] = 0.005f; Q.r[1] = 0.003f; Q.hw[0] = hw; Q.hw[1] = fminf(hw + 0.1f, 1); Q.ws[0] = 0; Q.ws[1] = 0.4f;
            g->bark_tone = 7; tube(g, &Q, 3, 1, 0.2f, 0, ph, 0, L_BARK_PALM, 0, 0); g->bark_tone = bt;
            if (g->lod) continue;
            g->fine = 1;
            for (int q = 0; q < 4; q++) {
                V3 o = vrot(any_perp(dd), dd, q * 2.39996f);
                ball(g, vadd(vadd(p0, vmul(dd, l * (0.25f + 0.2f * q))), vmul(o, 0.018f)), dd, feather ? 0.014f : 0.011f, 1.25f, feather ? SKIN_ORANGE : SKIN_PALM, 5, 3, (Wnd){hw, ph, 0.4f, 0, 0});
            }
            g->fine = 0;
        }
    }
    fork_out(g, sf);
}
// Horsetail colony: jointed stalk tubes with whorls of needle branches (full),
// crossed stalk cards (reduced). Giant (Equisetites): tall, thick, mostly
// unbranched stalks, many tipped with a spore cone. Field: low, dense
// stalks feathered with long whorled branches, no cones.
static void gen_horsetail(Gen* g, bool field) {
    int ns = (field ? 22 : 14) + (int)grr(g, 0, 8); if (g->lod) ns = ns * 5 / 8;   // the reduced colony: its first, tallest-looking stalks
    for (int k = 0; k < ns; k++) {
        float a = grr(g, 0, 2 * VPI), d = (field ? 1.3f : 1.7f) * sqrtf(gr(g)), h = field ? grr(g, 0.3f, 0.7f) : grr(g, 1.4f, 3.2f), ph = gr(g);
        V3 b = v3(cosf(a) * d, -0.05f, sinf(a) * d), lean = v3(grr(g, -0.08f, 0.08f), 1, grr(g, -0.08f, 0.08f));
        float rs = field ? 0.011f : grr(g, 0.035f, 0.055f);
        VRng sv = fork_in(g);
        if (g->lod) {
            int st = (int)grr(g, 0, 3.99f); float u0 = (120 + st * 90 - 56) / 512.0f, u1 = (120 + st * 90 + 56) / 512.0f, yaw = grr(g, 0, VPI);
            V3 ax = vnorm(lean);
            for (int q = 0; q < 2; q++) {
                V3 s2 = vnorm(vrot(any_perp(ax), ax, yaw + q * VPI * 0.5f));
                card(g, b, ax, s2, h, field ? 0.35f : 0.32f + rs * 2, L_HORSE, u0, 0, u1, 1, 1, 0, (Wnd){0, ph, 0, 0, 0});
            }
            fork_out(g, sv); continue;
        }
        Path P; int n = 6; P.n = n + 1; V3 dd = vnorm(lean);
        for (int q = 0; q <= n; q++) { float t = (float)q / n; P.p[q] = vadd(b, vmul(dd, h * t)); P.r[q] = lerpf(rs, rs * 0.55f, t); P.hw[q] = t; P.ws[q] = 0; }
        float node = field ? grr(g, 0.05f, 0.08f) : grr(g, 0.2f, 0.32f);
        tube(g, &P, field ? 3 : 5, 1, node, 0, ph, 0, L_HSTEM, 0, 0);
        // Spore cone at the tip of some giant stalks, needle whorls at the nodes of the rest.
        if (!field && gr(g) < 0.45f) {
            Path C; C.n = 3; for (int q = 0; q < 3; q++) { C.p[q] = vadd(P.p[n], vmul(dd, 0.07f * q)); C.r[q] = q == 1 ? rs * 0.9f : rs * 0.4f; C.hw[q] = 1; C.ws[q] = 0; }
            int bt = g->bark_tone; g->bark_tone = 8; tube(g, &C, 5, 1, 0.1f, 0, ph, 0, g->bark_layer, 0, 0); g->bark_tone = bt; fork_out(g, sv); continue;
        }
        for (float y = node; y < h * (field ? 0.92f : 0.85f); y += node) {
            V3 c = vadd(b, vmul(dd, y)); float bl = field ? lerpf(0.16f, 0.05f, y / h) : lerpf(0.3f, 0.1f, y / h), phs = gr(g);
            int nn = field ? 6 : 5; float a0 = grr(g, 0, 2 * VPI);
            for (int q = 0; q < nn; q++) {
                V3 o = dir_az(a0 + q * 2 * VPI / nn), dn = vnorm(vadd(o, v3(0, field ? 0.6f : 0.25f, 0)));
                card(g, c, dn, vnorm(vcross(v3(0, 1, 0), o)), bl, field ? 0.012f : 0.02f, L_HSTEM, 0.6f, 0.3f, 0.65f, 0.95f, 1, field ? 0.3f : 0.6f, (Wnd){y / h, ph, 0, phs, 0});
            }
        }
        fork_out(g, sv);
    }
}
// Shrub layer. Berry shrub: a rounded crown of arching twigs with serrate
// leaves and clusters of berries. Flowering herb: a clump of leafy stems
// with flowers at their tips. Gunnera: a few huge cupped leaves on prickly
// stalks around a conical flower spike. Evergreen scrub: a small branching
// crown of sprigs.
static void gen_herb(Gen* g) {
    int ns = 5 + (int)grr(g, 0, 5), col = gr(g) < 0.6f ? PET_YELLOW : PET_VIOLET;
    for (int k = 0; k < ns; k++) {
        float a = grr(g, 0, 2 * VPI), h = grr(g, 0.35f, 0.8f), ph = gr(g); V3 b = vmul(dir_az(a), grr(g, 0, 0.15f));
        V3 top = vadd(b, vadd(vmul(dir_az(a), grr(g, 0.05f, 0.2f)), v3(0, h, 0)));
        Path P; P.n = 4;
        for (int q = 0; q < 4; q++) { float t = q / 3.0f; P.p[q] = vadd(vlerp(b, top, t), v3(0, 0, 0)); P.r[q] = lerpf(0.006f, 0.003f, t); P.hw[q] = t; P.ws[q] = 0; }
        int bt = g->bark_tone; g->bark_tone = 7; tube(g, &P, 3, 1, 0.2f, 0, ph, 0, g->bark_layer, 0, 0); g->bark_tone = bt;
        for (int l = 0; l < 3; l++) {   // leaves up the stem
            float t = 0.15f + 0.25f * l; V3 p = vlerp(b, top, t), d = vnorm(vadd(dir_az(a + l * 2.4f + gr(g)), v3(0, 0.5f, 0)));
            card(g, p, d, vnorm(vcross(v3(0, 1, 0), d)), grr(g, 0.08f, 0.14f), grr(g, 0.08f, 0.13f), L_GUNNERA, 0, 0, 1, 1, 2, -0.4f, (Wnd){t, ph, 0.3f, gr(g), 0});
        }
        Wnd w = {1, ph, 0.6f, gr(g), 0};
        if (gr(g) < 0.8f) flower(g, top, vnorm(v3(grr(g, -0.3f, 0.3f), 1, grr(g, -0.3f, 0.3f))), grr(g, 0.035f, 0.05f), col, 5, 0.3f, SKIN_YELLOW, 0.009f, w);
        else ball(g, top, v3(0, 1, 0), 0.009f, 1.3f, SKIN_GREEN, 5, 3, w);   // a bud
    }
}
static void gen_gunnera(Gen* g) {
    int nl = 4 + (int)grr(g, 0, 4);
    for (int k = 0; k < nl; k++) {
        float a = k * 2.39996f + gr(g), L = grr(g, 0.7f, 1.4f), e = grr(g, 0.85f, 1.25f), ph = gr(g), R = grr(g, 0.55f, 0.85f);
        V3 d = dir_az(a); Path P; P.n = 5;
        for (int q = 0; q < 5; q++) { float t = q / 4.0f, el = e - 0.7f * t * t; P.p[q] = vadd(vmul(d, L * t * cosf(el) * 0.8f), v3(0, L * sinf(e) * t - 0.25f * L * t * t, 0)); P.r[q] = lerpf(0.035f, 0.015f, t); P.hw[q] = 0.6f * t; P.ws[q] = 0; }
        int bt = g->bark_tone; g->bark_tone = 7; tube(g, &P, 5, 1, 0.3f, 0, ph, 0, g->bark_layer, 0, 0); g->bark_tone = bt;
        // The blade: a big lobed leaf cupped up from the stalk's end, two crossed halves bending down at the rim.
        V3 c = P.p[4], ax = vnorm(vadd(d, v3(0, 0.25f, 0))), sd = vnorm(vcross(v3(0, 1, 0), d));
        card(g, vsub(c, vmul(ax, R * 0.15f)), ax, sd, R * 1.5f, R * 1.5f, L_GUNNERA, 0, 0, 1, 1, 3, -0.7f, (Wnd){0.6f, ph, 0.4f, gr(g), 0});
    }
    // The flower spike: a tall knobbly cone of tiny red-brown flowers.
    float h = grr(g, 0.4f, 0.7f); ball(g, v3(0, h * 0.5f, 0), v3(0, 1, 0), h * 0.16f, 3.0f, SKIN_PALM, 8, 6, (Wnd){0.2f, 0, 0, 0, 0});
}
static void gen_shrub(Gen* g, int sp) {
    if (sp == FL_HERB) { gen_herb(g); return; }
    if (sp == FL_GUNNERA) { gen_gunnera(g); return; }
    bool berry = sp == FL_BERRY_SHRUB; float R = berry ? grr(g, 0.7f, 1.1f) : grr(g, 0.8f, 1.3f);
    Crown C = {.H = R * 1.5f, .r0 = 0.05f, .fork = 0.12f, .stems = 3 + (int)grr(g, 0, 2.99f), .leader = 0, .sp1 = 0.35f, .el1 = 0.7f,
               .sp2 = 0.3f, .sp3 = 0.2f, .ang2 = 0.8f, .ang3 = 0.9f, .trop1 = 0.2f, .trop2 = 0.1f, .trop3 = 0.2f, .twig_len = 0.35f,
               .leaf_layer = berry ? L_CL_BERRY : L_CL_SHRUB, .lod_layer = L_SHRUB, .nleaf = berry ? 380 : 420, .leaf_size = berry ? 0.4f : 0.36f, .lod_size = 0.65f,
               .curl = 0.3f, .upbias = 0.2f, .lobes = 0, .droop = berry ? 0.25f : 0};
    Env E; env_init(g, &E, v3(0, R * 0.7f, 0), v3(R, R * 0.75f, R), 5, 0.2f);
    crown_tree(g, &C, &E);
}

// ---------------------------------------------------------------- ground ferns
static void old_frond(Gen* g, V3 base, V3 dir, float e0, float droop, float L, float W, int layer, V3 crown, float ao0, float wind0, float twist, int segs, float keel);
// Bracken: a colony of fronds, each an upright stalk carrying a broad,
// near-level triangular blade. Shield fern: a low vase of lanceolate fronds
// (the old ground fern, smaller). Forking fern: wiry stems that fork again
// and again into level pinnate arms, a tangled mat. Royal fern: a tall vase
// of fronds round brown fertile ones.
static void gen_fern(Gen* g, int sp) {
    if (sp == FL_SHIELD) {
        int nf = g->lod ? 10 + (int)grr(g, 0, 3) : 14 + (int)grr(g, 0, 6);
        VRng sv = fork_in(g);
        for (int f = 0; f < nf; f++) {
            float az = f * 2.39996f + grr(g, -0.3f, 0.3f); bool inner = f >= nf - 5;
            float L = (inner ? grr(g, 0.55f, 0.85f) : grr(g, 0.75f, 1.2f));
            g->dry = (!inner && gr(g) < 0.1f) ? (int)grr(g, 110, 220) : 0;
            old_frond(g, vmul(dir_az(az), 0.04f), dir_az(az), inner ? grr(g, 1.2f, 1.45f) : grr(g, 0.9f, 1.25f), inner ? grr(g, 1.0f, 1.4f) : grr(g, 1.4f, 2.0f),
                      L, L * grr(g, 0.28f, 0.34f) * (g->lod ? 1.25f : 1.0f), (f & 1) ? L_FERN2 : L_FERN, v3(0, 0, 0), g->lod ? 0.22f : 0.3f, 0.2f, grr(g, -0.5f, 0.5f), g->lod ? 3 : 6, 0.3f);
        }
        g->dry = 0; fork_out(g, sv);
        return;
    }
    if (sp == FL_BRACKEN) {
        int nf = 4 + (int)grr(g, 0, 4);
        for (int f = 0; f < nf; f++) {
            float a = grr(g, 0, 2 * VPI), d = 0.9f * sqrtf(gr(g)), h = grr(g, 0.45f, 0.85f), ph = gr(g), az = grr(g, 0, 2 * VPI), L = grr(g, 0.7f, 1.05f);
            V3 b = v3(cosf(a) * d, -0.05f, sinf(a) * d), top = vadd(b, v3(grr(g, -0.08f, 0.08f), h, grr(g, -0.08f, 0.08f)));
            Path P; P.n = 3; for (int q = 0; q < 3; q++) { float t = q * 0.5f; P.p[q] = vlerp(b, top, t); P.r[q] = lerpf(0.008f, 0.005f, t); P.hw[q] = 0.4f * t; P.ws[q] = 0; }
            int bt = g->bark_tone; g->bark_tone = 7; if (!g->lod) tube(g, &P, 3, 1, 0.3f, 0, ph, 0, g->bark_layer, 0, 0); g->bark_tone = bt;   // the stalk reads only near
            g->dry = gr(g) < 0.12f ? (int)grr(g, 120, 200) : 0;
            Frond F = {.base = top, .az = az, .e0 = grr(g, 0.15f, 0.4f), .droop = grr(g, 0.25f, 0.5f), .twist = grr(g, -0.2f, 0.2f), .L = L, .bare = 0.02f,
                       .pairs = g->lod ? 4 : 9, .lmax = L * 0.55f, .lw = g->lod ? 0.6f : 0.42f, .ang = 1.15f, .keel = 0.15f, .curl = -0.3f,
                       .profile = 0, .layer = L_FPINNA, .cols = 4, .rachis_r = g->lod ? 0 : 0.005f, .segs = g->lod ? 3 : 5, .lsegs = g->lod ? 1 : 2, .ph = ph, .flex = 0.6f};
            // A bracken blade is widest at its base and tapers: the leaflet profile runs long to short.
            VRng sv = fork_in(g); frond(g, &F); fork_out(g, sv);
            g->dry = 0;
        }
        return;
    }
    if (sp == FL_GLEICHENIA) {
        int ns = 3 + (int)grr(g, 0, 3);
        for (int k = 0; k < ns; k++) {
            float az = grr(g, 0, 2 * VPI), h = grr(g, 0.3f, 0.6f), ph = gr(g); V3 b = vmul(dir_az(az), grr(g, 0, 0.3f));
            V3 n0 = vadd(b, vadd(vmul(dir_az(az), h * 0.4f), v3(0, h, 0)));
            Path P; P.n = 3; for (int q = 0; q < 3; q++) { float t = q * 0.5f; P.p[q] = vlerp(b, n0, t); P.r[q] = 0.005f; P.hw[q] = 0.3f * t; P.ws[q] = 0; }
            int bt = g->bark_tone; g->bark_tone = 9; if (!g->lod) tube(g, &P, 3, 1, 0.3f, 0, ph, 0, g->bark_layer, 0, 0); g->bark_tone = bt;   // wiry stems: near only
            // Two forks, each ending in a pair of level pinnate arms, a dormant bud between.
            for (int f = -1; f <= 1; f += 2) {
                float a1 = az + f * grr(g, 0.5f, 0.8f), l1 = grr(g, 0.25f, 0.4f);
                V3 n1 = vadd(n0, vadd(vmul(dir_az(a1), l1), v3(0, grr(g, -0.05f, 0.08f), 0)));
                Path Q; Q.n = 2; Q.p[0] = n0; Q.p[1] = n1; Q.r[0] = Q.r[1] = 0.004f; Q.hw[0] = 0.3f; Q.hw[1] = 0.6f; Q.ws[0] = Q.ws[1] = 0;
                g->bark_tone = 9; if (!g->lod) tube(g, &Q, 3, 1, 0.3f, 0, ph, 0, g->bark_layer, 0, 0); g->bark_tone = bt;
                for (int f2 = -1; f2 <= 1; f2 += 2) {
                    float L = grr(g, 0.35f, 0.55f);
                    Frond F = {.base = n1, .az = a1 + f2 * grr(g, 0.4f, 0.7f), .e0 = grr(g, -0.05f, 0.15f), .droop = grr(g, 0.1f, 0.3f), .twist = 0, .L = L, .bare = 0.05f,
                               .pairs = g->lod ? 3 : 12, .lmax = 0.09f, .lw = g->lod ? 0.55f : 0.28f, .ang = 1.35f, .keel = 0.2f, .curl = 0,
                               .profile = 0, .layer = L_LEAFLET, .cols = 8, .rachis_r = 0, .segs = 3, .lsegs = 1, .ph = ph, .flex = 0.4f};
                    VRng sv = fork_in(g); if (!g->lod || f2 > 0) frond(g, &F); fork_out(g, sv);   // the reduced mesh: one arm of each pair
                }
            }
        }
        return;
    }
    // Royal fern.
    int nf = 9 + (int)grr(g, 0, 5), nfert = 2 + (int)grr(g, 0, 3);
    for (int f = 0; f < nf + nfert; f++) {
        bool fert = f >= nf; float az = f * 2.39996f + grr(g, -0.2f, 0.2f), L = fert ? grr(g, 0.7f, 1.0f) : grr(g, 0.9f, 1.4f), ph = gr(g);
        g->dry = fert ? (int)grr(g, 200, 245) : 0;
        Frond F = {.base = vmul(dir_az(az), 0.06f), .az = az, .e0 = fert ? grr(g, 1.35f, 1.5f) : grr(g, 1.05f, 1.3f), .droop = fert ? 0.2f : grr(g, 0.6f, 1.0f), .twist = grr(g, -0.3f, 0.3f), .L = L,
                   .bare = 0.25f, .pairs = g->lod ? 4 : (fert ? 9 : 12), .lmax = fert ? 0.07f : L * 0.22f, .lw = fert ? 0.6f : (g->lod ? 0.42f : 0.3f), .ang = fert ? 0.7f : 1.25f,
                   .keel = 0.25f, .curl = -0.3f, .profile = 1, .layer = fert ? L_LEAFLET : L_FPINNA, .cols = fert ? 8 : 4, .rachis_r = g->lod ? 0 : 0.005f,
                   .segs = g->lod ? 3 : 5, .lsegs = 1, .ph = ph, .flex = 0.5f};
        VRng sv = fork_in(g); if (!g->lod || !(f & 1) || fert) frond(g, &F); fork_out(g, sv);   // the reduced vase: every other sterile frond
    }
    g->dry = 0;
}

// ---------------------------------------------------------------- water, moss, fungi, fallen food
// Water lilies (the instance stands on the water surface): a patch of round
// pads lying flat, some edges lifted, and (variants 1-3) a white or pink
// flower standing just above them or a closed bud.
static void gen_lily(Gen* g, int v) {
    int np = 5 + (int)grr(g, 0, 5);
    for (int k = 0; k < np; k++) {
        float a = grr(g, 0, 2 * VPI), d = 0.9f * sqrtf(gr(g)), R = grr(g, 0.12f, 0.22f), rot = grr(g, 0, 2 * VPI), tilt = gr(g) < 0.2f ? grr(g, 0.1f, 0.35f) : 0;
        V3 c = v3(cosf(a) * d, 0.01f + 0.002f * k, sinf(a) * d), ax = dir_az(rot), sd = vcross(v3(0, 1, 0), ax);
        V3 n = vnorm(vadd(v3(0, 1, 0), vmul(ax, -tilt)));
        int first = g->m->nv;
        for (int q = 0; q < 4; q++) {   // a flat quad over the pad's square, centre at the middle of the layer
            float su = (q & 1) ? 1.0f : -1.0f, sv = (q >> 1) ? 1.0f : -1.0f;
            V3 p = vadd(c, vadd(vmul(ax, sv * R), vmul(sd, su * R))); if (sv > 0) p.y += tilt * R;
            gv(g, p, 0.5f + 0.5f * su, 0.5f + 0.5f * sv, n, 0.9f, L_LILY, 1, (Wnd){0.3f, gr(g), 0, 0, 0});
        }
        gtri(g, first, first + 2, first + 1); gtri(g, first + 1, first + 2, first + 3);
    }
    if (v == 0) return;
    V3 c = v3(grr(g, -0.2f, 0.2f), 0.04f, grr(g, -0.2f, 0.2f)); Wnd w = {0.2f, gr(g), 0, 0, 0};
    if (v == 3) { ball(g, vadd(c, v3(0, 0.05f, 0)), v3(0.1f, 1, 0), 0.025f, 1.8f, SKIN_GREEN, 7, 4, w); return; }
    flower(g, c, v3(0, 1, 0), grr(g, 0.09f, 0.11f), PET_SEPAL, 4, 0.15f, -1, 0, w);
    flower(g, vadd(c, v3(0, 0.01f, 0)), v3(0, 1, 0), grr(g, 0.09f, 0.11f), v == 1 ? PET_LILY_W : PET_LILY_P, 11, 0.45f, -1, 0, w);
    flower(g, vadd(c, v3(0, 0.015f, 0)), v3(0, 1, 0), 0.06f, v == 1 ? PET_LILY_W : PET_LILY_P, 7, 1.0f, SKIN_YELLOW, 0.016f, w);
}
// Clubmoss (variant 4): a mat of creeping, forking Selaginella sprays and a
// few upright cone-tipped shoots. Moss (5): a low cushion of moss shoots.
static void gen_moss(Gen* g, int v) {
    int n = v == 4 ? 10 + (int)grr(g, 0, 5) : 9 + (int)grr(g, 0, 5);
    for (int k = 0; k < n; k++) {
        float a = grr(g, 0, 2 * VPI), d = (v == 4 ? 0.3f : 0.18f) * sqrtf(gr(g)); V3 b = v3(cosf(a) * d, 0.0f, sinf(a) * d);
        V3 dir = vnorm(vadd(dir_az(grr(g, 0, 2 * VPI)), v3(0, v == 4 ? grr(g, 0.1f, 0.35f) : grr(g, 1.0f, 2.5f), 0)));
        float L = v == 4 ? grr(g, 0.12f, 0.22f) : grr(g, 0.05f, 0.09f); int col = v == 4 ? (gr(g) < 0.85f ? (int)(gr(g) * 2) : 2) : 3;
        V3 sd = vnorm(vcross(v3(0, 1, 0), dir)); if (vlen(vcross(v3(0, 1, 0), dir)) < 0.1f) sd = any_perp(dir);
        V3 up = vnorm(vcross(sd, dir));
        if (v == 4 && col < 2) sd = vnorm(vrot(sd, dir, 0.0f));
        Wnd w = {0.5f, gr(g), 0, 0, 0}, w2 = {0.5f, gr(g), 0, 0, 0};
        if (g->lod && k % 3) continue;   // the reduced tuft: every third card, larger, single
        float big = g->lod ? 1.5f : 1.0f;
        card(g, b, dir, col < 2 ? vnorm(vadd(sd, vmul(up, 0.1f))) : sd, L * big, L * big * (col == 3 ? 0.9f : 0.5f), L_CLUB, col / 4.0f, 0, (col + 1) / 4.0f, 1, g->lod ? 1 : 2, v == 4 ? -0.4f : 0.2f, w);
        if (col >= 2 && !g->lod) card(g, b, dir, vnorm(vcross(dir, sd)), L, L * (col == 3 ? 0.9f : 0.5f), L_CLUB, col / 4.0f, 0, (col + 1) / 4.0f, 1, 2, 0.2f, w2);
    }
}
// Fungi on dead wood, placed by render/veg.c against stumps and beside logs:
// 0 a tier of bracket shelves (the instance's +x faces out of the wood),
// 1 one big bracket, 2 a cluster of small brown mushrooms, 3 a few tall
// pale ones.
static void bracket(Gen* g, V3 c, float R, float th) {
    // A half disc reaching out along +x from the wood at c: zoned top, pore face below.
    int seg = 8, first; float a0 = -VPI * 0.5f;
    for (int face = 0; face < 2; face++) {
        first = g->m->nv; float y = face ? -th * 0.5f : th * 0.5f; int bt = g->bark_tone; g->bark_tone = 10;
        gv(g, vadd(c, v3(-0.02f, y, 0)), face ? 0.25f + 0.125f : 0.125f, 0.02f, v3(0, face ? -1 : 1, 0), 0.8f, L_FUNGUS, 0, (Wnd){0});
        for (int k = 0; k <= seg; k++) {
            float a = a0 + VPI * k / seg, rr = R * (0.92f + 0.08f * cosf(a * 3 + c.y * 20));
            V3 p = vadd(c, v3(cosf(a) * rr, y - (face ? 0 : th * 0.4f * cosf(a)), sinf(a) * rr * 1.3f));
            gv(g, p, (face ? 0.25f : 0) + 0.25f * k / seg, 0.98f, vnorm(v3(cosf(a) * 0.4f, face ? -1 : 1, sinf(a) * 0.4f)), 1, L_FUNGUS, 0, (Wnd){0});
        }
        for (int k = 0; k < seg; k++) { if (face) gtri(g, first, first + k + 2, first + k + 1); else gtri(g, first, first + k + 1, first + k + 2); }
        g->bark_tone = bt;
    }
}
static void mushroom(Gen* g, V3 b, float h, float R, int cap_col, float lean) {
    V3 top = vadd(b, v3(lean * h, h, 0));
    Path S; S.n = 3; for (int q = 0; q < 3; q++) { float t = q * 0.5f; S.p[q] = vlerp(b, top, t); S.r[q] = R * lerpf(0.28f, 0.18f, t); S.hw[q] = 0; S.ws[q] = 0; }
    int bt = g->bark_tone; g->bark_tone = 10;
    float u0 = 0.75f + 0.01f, du = 0.23f; int base = g->m->nv, seg = 7;
    for (int q = 0; q < 3; q++) for (int k = 0; k <= seg; k++) {   // stem in the pale column
        float a = 2 * VPI * k / seg; V3 d = v3(cosf(a), 0, sinf(a));
        gv(g, vadd(S.p[q], vmul(d, S.r[q])), u0 + du * k / seg, q * 0.45f, d, 0.7f + 0.15f * q, L_FUNGUS, 0, (Wnd){0});
    }
    for (int q = 0; q < 2; q++) for (int k = 0; k < seg; k++) { int a = base + q * (seg + 1) + k; gtri(g, a, a + seg + 1, a + 1); gtri(g, a + 1, a + seg + 1, a + seg + 2); }
    // Cap: a dome over a gill disc.
    int c0 = g->m->nv; float cu = cap_col / 4.0f + 0.01f;
    gv(g, vadd(top, v3(0, R * 0.45f, 0)), cu + 0.11f, 0.02f, v3(0, 1, 0), 1, L_FUNGUS, 0, (Wnd){0});
    for (int k = 0; k <= seg * 2; k++) {
        float a = VPI * k / seg; V3 d = v3(cosf(a), 0, sinf(a));
        gv(g, vadd(top, vadd(vmul(d, R * 0.75f), v3(0, R * 0.35f, 0))), cu + 0.23f * k / (2 * seg), 0.5f, vnorm(vadd(d, v3(0, 1.2f, 0))), 1, L_FUNGUS, 0, (Wnd){0});
        gv(g, vadd(top, vadd(vmul(d, R), v3(0, -R * 0.05f, 0))), cu + 0.23f * k / (2 * seg), 0.98f, vnorm(vadd(d, v3(0, 0.3f, 0))), 0.9f, L_FUNGUS, 0, (Wnd){0});
    }
    for (int k = 0; k < seg * 2; k++) { int a = c0 + 1 + 2 * k; gtri(g, c0, a + 2, a); gtri(g, a, a + 2, a + 1); gtri(g, a + 1, a + 2, a + 3); }
    int gc = g->m->nv; gv(g, vadd(top, v3(0, -R * 0.02f, 0)), 0.75f + 0.12f, 0.5f, v3(0, -1, 0), 0.5f, L_FUNGUS, 0, (Wnd){0});
    for (int k = 0; k <= seg * 2; k++) { float a = VPI * k / seg; gv(g, vadd(top, vadd(vmul(v3(cosf(a), 0, sinf(a)), R), v3(0, -R * 0.05f, 0))), 0.76f + 0.22f * k / (2 * seg), 0.9f, v3(0, -1, 0), 0.6f, L_FUNGUS, 0, (Wnd){0}); }
    for (int k = 0; k < seg * 2; k++) gtri(g, gc, gc + 1 + k, gc + 2 + k);
    g->bark_tone = bt;
}
static void gen_fungus(Gen* g, int v) {
    if (v == 0) { int n = 3 + (int)grr(g, 0, 3); for (int k = 0; k < n; k++) bracket(g, v3(0, 0.1f * k + grr(g, 0, 0.03f), grr(g, -0.1f, 0.1f)), grr(g, 0.06f, 0.11f) * (1 - 0.1f * k), 0.025f); return; }
    if (v == 1) { bracket(g, v3(0, 0, 0), grr(g, 0.16f, 0.24f), 0.05f); return; }
    int n = v == 2 ? 5 + (int)grr(g, 0, 5) : 2 + (int)grr(g, 0, 3);
    for (int k = 0; k < n; k++) {
        float a = grr(g, 0, 2 * VPI), d = (v == 2 ? 0.12f : 0.2f) * sqrtf(gr(g));
        mushroom(g, v3(cosf(a) * d, -0.01f, sinf(a) * d), v == 2 ? grr(g, 0.03f, 0.07f) : grr(g, 0.08f, 0.15f), v == 2 ? grr(g, 0.015f, 0.03f) : grr(g, 0.03f, 0.05f), v == 2 ? 2 : 3, grr(g, -0.15f, 0.15f));
    }
}
// Fallen food (DROP_* variants): what lies under a fruiting plant, a few
// items scattered over half a metre, half sunk into the litter.
static void gen_drop(Gen* g, int v) {
    static const int SK[DROP_KINDS] = {SKIN_CONE, SKIN_RED, SKIN_RED, SKIN_DARK, SKIN_YELLOW, SKIN_ORANGE, SKIN_PALM, SKIN_RED};
    static const float RAD[DROP_KINDS] = {0.08f, 0.014f, 0.03f, 0.011f, 0.016f, 0.026f, 0.02f, 0.011f}, EL[DROP_KINDS] = {1.18f, 1.3f, 2.0f, 1.2f, 1.1f, 1.3f, 1.3f, 1.0f};
    int n = v == DROP_ARAU_CONE ? 1 + (int)grr(g, 0, 1.6f) : v == DROP_MAGNOLIA ? 2 + (int)grr(g, 0, 2) : v == DROP_CYCAD_SEED ? 4 + (int)grr(g, 0, 4) : 9 + (int)grr(g, 0, 7);
    for (int k = 0; k < n; k++) {
        float a = grr(g, 0, 2 * VPI), d = (v == DROP_ARAU_CONE ? 0.35f : 0.28f) * sqrtf(gr(g)), r = RAD[v] * grr(g, 0.85f, 1.15f), sq = v == DROP_GINKGO && gr(g) < 0.4f ? 0.55f : 1.0f;
        V3 ax = vnorm(v3(cosf(a + 1), grr(g, -0.15f, 0.15f), sinf(a + 1)));
        V3 c = v3(cosf(a) * d, r * 0.55f * sq, sinf(a) * d);
        int first = g->m->nv;
        ball(g, c, ax, r, EL[v], k == 0 && v == DROP_PODO_SEED ? SKIN_DARK : SK[v], v == DROP_ARAU_CONE ? 9 : 6, v == DROP_ARAU_CONE ? 6 : 4, (Wnd){0});
        if (sq < 1) for (int q = first; q < g->m->nv; q++) g->m->v[q].p[1] = c.y + (g->m->v[q].p[1] - c.y) * sq;   // squashed
        if (v == DROP_PODO_SEED) ball(g, vadd(c, vmul(ax, r * 1.6f)), ax, r * 0.75f, 1.1f, SKIN_DARK, 6, 4, (Wnd){0});
    }
    if (v == DROP_ARAU_CONE) for (int k = 0; k < 6; k++) {   // loose cone scales round the cone
        float a = grr(g, 0, 2 * VPI), d = grr(g, 0.15f, 0.45f);
        ball(g, v3(cosf(a) * d, 0.006f, sinf(a) * d), v3(cosf(a + 2), 0.1f, sinf(a + 2)), 0.012f, 2.2f, SKIN_CONE, 5, 3, (Wnd){0});
    }
    if (v == DROP_MAGNOLIA) for (int k = 0; k < 2; k++) {   // a fallen tepal or two
        float a = grr(g, 0, 2 * VPI); V3 d = dir_az(a);
        petal(g, v3(grr(g, -0.2f, 0.2f), 0.004f, grr(g, -0.2f, 0.2f)), vnorm(vadd(d, v3(0, 0.08f, 0))), vcross(v3(0, 1, 0), d), 0.09f, 0.05f, k ? PET_DRY : PET_MAGN, 0.3f, (Wnd){0});
    }
}

// Ground cover and grass keep their meshes (and random streams) from before
// the skeleton generator: the sward and tall grass are tuned as they are.
static void old_card(Gen* g, V3 c, V3 right, V3 up, float w, float h, int layer, V3 crown, float ao, float wind) {
    V3 n = vnorm(vcross(right, up)), out = vnorm(vsub(c, crown)); if (vdot(n, out) < 0) n = vmul(n, -1);
    V3 bent = vnorm(vadd(vmul(n, 0.4f), vmul(out, 0.6f)));
    int a = g->m->nv; V3 r = vmul(right, w * 0.5f), u = vmul(up, h * 0.5f); float aol = ao * 0.85f;
    gv(g, vsub(vsub(c, r), u), 0, 0, bent, aol, layer, 1, (Wnd){wind * 0.7f, 0, 0, 0, 0});
    gv(g, vsub(vadd(c, r), u), 1, 0, bent, aol, layer, 1, (Wnd){wind * 0.7f, 0, 0, 0, 0});
    gv(g, vadd(vsub(c, r), u), 0, 1, bent, ao, layer, 1, (Wnd){wind, 0, 0, 0, 0});
    gv(g, vadd(vadd(c, r), u), 1, 1, bent, ao, layer, 1, (Wnd){wind, 0, 0, 0, 0});
    gtri(g, a, a + 1, a + 2); gtri(g, a + 1, a + 3, a + 2);
}
static void old_frond(Gen* g, V3 base, V3 dir, float e0, float droop, float L, float W, int layer, V3 crown, float ao0, float wind0, float twist, int segs, float keel) {
    V3 side0 = vnorm(vcross(v3(0, 1, 0), dir)); V3 p = base; int first = g->m->nv, across = keel > 0 ? 3 : 2;
    for (int k = 0; k <= segs; k++) {
        float t = (float)k / segs, e = e0 - droop * t * t * 1.3f - droop * t * 0.2f;
        V3 fwd = vnorm(vadd(vmul(dir, cosf(e)), v3(0, sinf(e), 0)));
        V3 side = vnorm(vadd(vmul(side0, cosf(twist * t)), vmul(vcross(fwd, side0), sinf(twist * t))));
        V3 up = vnorm(vcross(side, fwd));
        if (up.y < 0) up = vmul(up, -1);
        V3 bent = vnorm(vadd(vmul(up, 0.55f), vmul(vnorm(vsub(p, crown)), 0.45f)));
        float w = W * (k == 0 ? 0.35f : 1.0f);
        float ao = lerpf(ao0, 1.0f, sstep(0.0f, 0.7f, t)), wind = lerpf(wind0, 1.0f, t * t);
        Wnd wd = {wind, 0, 0, 0, keel > 0 ? t : 0}; V3 lift = vmul(up, keel * 0.5f * w);
        gv(g, vadd(vadd(p, vmul(side, -0.5f * w)), lift), 0, t, bent, ao, layer, 1, wd);
        if (across == 3) gv(g, p, 0.5f, t, bent, ao, layer, 1, wd);
        gv(g, vadd(vadd(p, vmul(side, 0.5f * w)), lift), 1, t, bent, ao, layer, 1, wd);
        p = vadd(p, vmul(fwd, L / segs));
    }
    for (int k = 0; k < segs; k++) {
        int a = first + across * k, b = a + across;
        for (int q = 0; q < across - 1; q++) { gtri(g, a + q, b + q, a + q + 1); gtri(g, a + q + 1, b + q, b + q + 1); }
    }
}
static void gen_cover(Gen* g, int kind, int variant) {
    VRng* r = &g->r; int lod = g->lod;
    switch (kind) {
    case VEG_CARPET: {     // low sward tuft: 0/1 young ferns, 2 broad-leaved forb, 3 fern and forb mix
        int nf = lod ? 4 : 7 + (int)vrr(r, 0, 3); V3 c = v3(0, 0.02f, 0);
        for (int f = 0; f < nf; f++) {
            float az = f * 2.39996f + vrr(r, -0.4f, 0.4f);
            V3 base = vadd(c, vmul(dir_az(az + 1.3f), vrr(r, 0.0f, 0.18f)));
            bool forb = variant == 2 || (variant == 3 && (f & 1));
            if (forb) {
                float L = vrr(r, 0.16f, 0.26f); V3 d = dir_az(az); V3 up = vnorm(vadd(vmul(d, 0.6f), v3(0, vrr(r, 0.3f, 0.8f), 0)));
                V3 cc = vadd(base, vadd(vmul(d, L * 0.6f), v3(0, vrr(r, 0.06f, 0.16f), 0)));
                old_card(g, cc, vnorm(vcross(up, v3(0, 1, 0.01f))), up, L, L * 1.2f, L_SHRUB, c, 0.55f, 0.4f);
            } else {
                float L = vrr(r, 0.28f, 0.5f);
                g->dry = vr(r) < 0.08f ? (int)vrr(r, 90, 200) : 0;
                old_frond(g, base, dir_az(az), vrr(r, 0.6f, 1.15f), vrr(r, 1.0f, 1.6f), L, L * 0.34f, (f & 1) ? L_FERN2 : L_FERN, v3(0, -0.1f, 0), 0.35f, 0.25f, vrr(r, -0.4f, 0.4f), lod ? 2 : 3, 0);
            }
        }
        g->dry = 0;
        break; }
    case VEG_GRASS: {      // 0 meadow grass, 1 tall grass, 2 sedge fountain, 3 reeds with a sedge skirt
        int segs = lod ? 2 : 4;
        int nr = variant == 0 ? (lod ? 4 : 8) : variant == 1 ? (lod ? 5 : 10) : variant == 2 ? (lod ? 4 : 9) : (lod ? 4 : 8);
        for (int f = 0; f < nr; f++) {
            float az = f * 2.39996f + vrr(r, -0.35f, 0.35f), L, W, e0, droop, tw = vrr(r, -0.5f, 0.5f); int layer = L_GRASS;
            V3 base = vmul(dir_az(az + 1.7f), vrr(r, 0.0f, variant == 3 ? 0.25f : 0.12f));
            switch (variant) {
            case 0: L = vrr(r, 0.6f, 0.9f); W = vrr(r, 0.45f, 0.62f); e0 = vrr(r, 1.18f, 1.45f); droop = vrr(r, 0.35f, 0.8f); break;
            case 1: L = vrr(r, 0.9f, 1.2f); W = vrr(r, 0.5f, 0.7f); e0 = vrr(r, 1.25f, 1.48f); droop = vrr(r, 0.4f, 0.9f); break;
            case 2: layer = L_SEDGE; L = vrr(r, 0.7f, 1.0f); W = vrr(r, 0.5f, 0.62f); e0 = vrr(r, 0.95f, 1.3f); droop = vrr(r, 1.3f, 2.0f); break;
            default:
                if (f < 3) { layer = L_SEDGE; L = vrr(r, 0.6f, 0.85f); W = vrr(r, 0.5f, 0.6f); e0 = vrr(r, 1.0f, 1.25f); droop = vrr(r, 1.2f, 1.8f); }
                else { layer = L_REED; L = vrr(r, 1.2f, 1.6f); W = vrr(r, 0.55f, 0.7f); e0 = vrr(r, 1.38f, 1.52f); droop = vrr(r, 0.1f, 0.35f); tw *= 0.4f; }
                break;
            }
            g->dry = vr(r) < 0.1f ? (int)vrr(r, 50, 140) : 0;
            old_frond(g, base, dir_az(az), e0, droop, L, W * (lod ? 1.3f : 1.0f), layer, v3(0, -0.5f, 0), 0.35f, 0.04f, tw, segs, 0);
        }
        g->dry = 0;
        break; }
    }
}

// ---------------------------------------------------------------- new species
// Cheirolep cypress (Frenelopsis): a small, open, often leaning tree whose
// branches end in sprays of jointed scale-leaf shoots (the araucaria rope
// branchlets serve), sparse enough to see through.
static void gen_cheirolep(Gen* g) {
    float H, r0; species_dims(VEG_CONIFER, g->variant, &H, &r0);
    Crown C = {.H = H, .r0 = r0, .fork = grr(g, 0.25f, 0.4f), .stems = 1, .leader = 0, .limbs_lo = 3, .limbs_hi = 5, .sp1 = 0.5f, .el1 = 0.6f,
               .sp2 = 0.35f, .sp3 = 0.22f, .ang2 = 0.8f, .ang3 = 0.8f, .trop1 = 0.12f, .trop2 = 0.1f, .trop3 = 0.3f, .twig_len = 0.5f,
               .leaf_layer = L_CL_ROPE, .lod_layer = L_ROPE, .nleaf = 700, .leaf_size = 0.42f, .lod_size = 0.9f, .curl = 0.2f, .upbias = 0.5f, .lobes = 0.1f, .droop = 0.1f};
    Env E; env_init(g, &E, v3(grr(g, -0.1f, 0.1f) * H, H * 0.62f, 0), v3(H * 0.3f, H * 0.36f, H * 0.28f), 6, 0.3f);
    crown_tree(g, &C, &E);
}
// Ginkgo: a straight leader with few, long ascending limbs (an open,
// irregular crown), fan leaves in tufts on spur shoots.
static void gen_ginkgo(Gen* g) {
    float H, r0; species_dims(VEG_BROADLEAF, g->variant, &H, &r0);
    Crown C = {.H = H, .r0 = r0, .fork = grr(g, 0.22f, 0.3f), .stems = 1, .leader = 1, .sp1 = 0.9f, .el1 = 0.75f,
               .sp2 = 0.85f, .sp3 = 0.5f, .ang2 = 0.7f, .ang3 = 0.9f, .trop1 = 0.1f, .trop2 = 0.08f, .trop3 = 0.2f, .twig_len = 0.5f,
               .leaf_layer = L_CL_GINKGO, .lod_layer = L_LEAF2, .nleaf = 400, .leaf_size = 0.42f, .lod_size = 1.3f, .curl = 0.25f, .upbias = 0.3f, .lobes = 0.15f, .droop = 0.0f};
    Env E; env_init(g, &E, v3(0, H * 0.6f, 0), v3(H * 0.27f, H * 0.42f, H * 0.25f), 7, 0.3f);
    crown_tree(g, &C, &E);
}
// River plane: a tall, broad, spreading crown on a stout trunk, big
// palmate leaves; seed balls hang on long stalks.
static void gen_plane(Gen* g) {
    float H, r0; species_dims(VEG_BROADLEAF, g->variant, &H, &r0);
    Crown C = {.H = H, .r0 = r0, .fork = grr(g, 0.3f, 0.38f), .stems = 1, .leader = 0, .limbs_lo = 3, .limbs_hi = 5, .sp1 = 0.9f, .el1 = 0.55f,
               .sp2 = 0.75f, .sp3 = 0.4f, .ang2 = 0.95f, .ang3 = 0.9f, .trop1 = 0.04f, .trop2 = 0.04f, .trop3 = 0.12f, .twig_len = 0.8f,
               .leaf_layer = L_CL_PLANE, .lod_layer = L_LEAF3, .nleaf = 700, .leaf_size = 0.6f, .lod_size = 1.8f, .curl = 0.3f, .upbias = 0.2f, .lobes = 0.3f, .droop = 0.2f};
    Env E; env_init(g, &E, v3(0, H * 0.62f, 0), v3(H * 0.46f, H * 0.36f, H * 0.46f), 7, 0.18f);
    crown_tree(g, &C, &E);
}
// Standing dead tree: a weathered trunk without leaves. Whole dead conifers
// keep a broken top and short stubs of their whorls; broken conifers are a
// tall stump with a splintered crown; dead broadleaves fork into a few bare,
// broken limbs; the last leans, broken off halfway. Bark is greyed (tone 11).
static void gen_snag(Gen* g, int v) {
    float H, r0; species_dims(VEG_SNAG, v, &H, &r0);
    g->bark_tone = 11;
    float lean = v == 3 ? grr(g, 0.12f, 0.3f) : grr(g, 0, 0.05f), az = grr(g, 0, 2 * VPI);
    Path T; grow(g, &T, v3(0, -0.3f, 0), vnorm(vadd(v3(0, 1, 0), vmul(dir_az(az), lean))), H + 0.3f, 10, r0, v == 0 ? 0.05f : r0 * 0.7f, 0, 0.03f, NULL, 0, 0, 0.05f, 0, 0);
    for (int k = 0; k < T.n; k++) { float t = (float)k / (T.n - 1); T.r[k] = lerpf(r0, v == 0 ? 0.05f : r0 * 0.72f, powf(t, 0.7f)) + 0.25f * r0 * expf(-t * 12); T.hw[k] = 0.04f * t; }
    float vv = bark(g, &T, 0, 0, 0, v == 1 ? 0.3f : 0.15f);
    // Splintered top: a ring of thin spikes round the break.
    if (v != 0) {
        V3 top = T.p[T.n - 1]; float rt = T.r[T.n - 1];
        for (int k = 0; k < 5; k++) {
            float a = az + k * 1.2566f + grr(g, -0.3f, 0.3f); V3 b = vadd(top, vmul(dir_az(a), rt * 0.6f));
            Path S; S.n = 3; float L = grr(g, 0.15f, 0.6f) * (0.5f + rt);
            for (int q = 0; q < 3; q++) { S.p[q] = vadd(b, vadd(v3(0, L * q * 0.5f, 0), vmul(dir_az(a), -rt * 0.25f * q))); S.r[q] = lerpf(rt * 0.35f, 0.01f, q * 0.5f); S.hw[q] = 0.05f; S.ws[q] = 0; }
            tube(g, &S, 4, 1, 0.4f, vv, 0, 0, g->bark_layer, 0, 0);
        }
    }
    // Limbs: stubs of whorls on dead conifers, a few broken forks on the rest.
    int nl = v == 0 ? 10 + (int)grr(g, 0, 6) : v == 2 ? 4 + (int)grr(g, 0, 3) : 2 + (int)grr(g, 0, 2);
    for (int i = 0; i < nl; i++) {
        float t = v == 0 ? grr(g, 0.3f, 0.95f) : v == 2 ? grr(g, 0.55f, 0.95f) : grr(g, 0.4f, 0.9f), a = grr(g, 0, 2 * VPI);
        V3 p; float rt; path_at(&T, t, &p, NULL, &rt, NULL, NULL);
        float len = v == 0 ? grr(g, 0.3f, 1.4f) * (1.2f - t) : v == 2 ? grr(g, 1.2f, 3.2f) : grr(g, 0.4f, 1.2f);
        float el = v == 0 ? grr(g, -0.4f, 0.1f) : grr(g, 0.3f, 0.9f);
        Path B; grow(g, &B, p, dir_ae(a, el), len, 4, fminf(rt * 0.45f, 0.03f + 0.04f * len), 0.015f, v == 0 ? -0.15f : 0.15f, 0.3f, NULL, 0, 0.05f, 0.1f, 0, 0);
        bark(g, &B, 0, 0, 0, 0);
        if (v == 2 && len > 2) {   // one broken fork further out
            V3 q, dq; float rq; path_at(&B, 0.6f, &q, &dq, &rq, NULL, NULL);
            Path C; grow(g, &C, q, vnorm(vadd(dq, vmul(dir_az(a + 1.2f), 0.6f))), len * 0.4f, 3, rq * 0.7f, 0.012f, 0.2f, 0.4f, NULL, 0, 0.1f, 0.12f, 0, 0);
            bark(g, &C, 0, 0, 0, 0);
        }
    }
}

// Builds one kind x variant (lod: the reduced mesh). The species comes from
// the variant's slot (render/flora.h); slots of the same species are the
// same plant from different seeds.
static void build_species(MB* m, int kind, int variant, int lod) {
    memset(m, 0, sizeof(*m));
    int sp = flora_species(kind, variant);
    Gen G = {.m = m, .lod = lod, .kind = kind, .variant = variant, .species = sp};
    G.r = species_rng(kind, variant); vr(&G.r); vr(&G.r);   // the first draws are species_dims'
    G.bark_tone = kind == VEG_TREEFERN ? 1 : kind == VEG_PALM ? 2 : kind == VEG_CONIFER ? 3 : kind == VEG_CYCAD ? 4 : kind == VEG_BROADLEAF ? 5 : kind == VEG_HORSETAIL ? 10 : 0;
    G.bark_layer = kind == VEG_TREEFERN ? L_BARK_FERN : (kind == VEG_PALM || kind == VEG_CYCAD) ? L_BARK_PALM : (kind == VEG_CONIFER || kind == VEG_SNAG) ? L_BARK_CON : L_BARK;
    float k_ext = 0.5f, bend = 0.6f;
    switch (kind) {
    case VEG_CONIFER:
        if (sp == FL_PODOCARP) gen_podocarp(&G); else if (sp == FL_CHEIROLEP) gen_cheirolep(&G); else if (sp == FL_REDWOOD) gen_metasequoia(&G); else gen_araucaria(&G, sp == FL_UMBRELLA);
        break;
    case VEG_BROADLEAF:
        if (sp == FL_GINKGO) gen_ginkgo(&G); else if (sp == FL_PLANE) gen_plane(&G);
        else gen_broadleaf(&G, sp == FL_MAGNOLIA ? 0 : sp == FL_KATSURA ? 2 : sp == FL_UNDERSTOREY ? 3 : 1);
        break;
    case VEG_CYCAD: gen_cycad(&G, variant & 3); k_ext = 0.35f; bend = 0.5f; break;
    case VEG_TREEFERN: gen_treefern(&G, sp == FL_DICKSONIA); k_ext = 0.35f; bend = 0.5f; break;
    case VEG_PALM: gen_palm(&G, sp == FL_FEATHER_PALM ? 3 : 0); k_ext = 0.35f; bend = 0.5f; break;
    case VEG_HORSETAIL: gen_horsetail(&G, sp == FL_HORSETAIL_FIELD); k_ext = 0.25f; bend = 0.3f; break;
    case VEG_FERN: gen_fern(&G, sp); k_ext = 0.4f; bend = 0.4f; break;
    case VEG_LILY: gen_lily(&G, variant); k_ext = 0.3f; bend = 0.2f; break;
    case VEG_DROP: gen_drop(&G, variant); k_ext = 0.2f; bend = 0; break;
    case VEG_FUNGUS: gen_fungus(&G, variant); k_ext = 0.2f; bend = 0; break;
    case VEG_SHRUB: G.bark_tone = 6; gen_shrub(&G, sp); k_ext = 0.6f; break;
    case VEG_SNAG: gen_snag(&G, variant); k_ext = 0.3f; break;
    case VEG_CARPET:
        if (variant >= 4) { gen_moss(&G, variant); k_ext = 0.3f; bend = 0.3f; break; }
        G.r = (VRng){(uint32_t)(kind * 131 + variant * 17 + 5)};   // the sward tufts keep their old streams
        gen_cover(&G, kind, variant);
        return;
    default:   // tall grass: the old streams and baked ramps
        G.r = (VRng){(uint32_t)(kind * 131 + variant * 17 + 5)};
        gen_cover(&G, kind, variant);
        return;
    }
    add_fruit(&G, m->height);
    free(G.tip);
    // The reduced mesh merges every third card into one about three times as
    // long, three to four times the leaf area: its crown would bake much
    // darker than the full one (12% darker crowns in daylight, 40% on the
    // podocarp). Its leaves count at the full mesh's total area instead.
    bake_sky(m, k_ext, bend, G.leaf_drawn > 0 ? G.leaf_full / G.leaf_drawn : 1.0f);
}
