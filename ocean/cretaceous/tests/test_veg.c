// Plant meshes and card shading math (render/veg_gen.h, render/veg_look.h):
// no GL, no world. Every kind and variant builds, at both levels of detail,
// with valid indices and unit normals; bent normals face out of their plant;
// rounded and creased cards keep their outline; the edge-on fade curve is
// monotonic and ends where it should; the vertex totals stay in budget.
// `test_veg --audit` prints the texel density, edge-on share and normal
// statistics per plant family (docs/plants/README.md).
#include "test_util.h"
#include "../render/veg_place.h"
#include <string.h>
#include "../render/veg_layers.h"
#include "../render/veg_gen.h"

static const char* const KIND[VEG_KINDS] = {"conifer", "broadleaf", "cycad", "fern", "treefern", "horsetail", "palm", "shrub", "carpet", "grass", "snag", "lily", "drop", "fungus"};

static V3 vpos(const VVert* q) { return v3(q->p[0], q->p[1], q->p[2]); }
static V3 vnrm(const VVert* q) { return v3(q->n[0] / 127.0f, q->n[1] / 127.0f, q->n[2] / 127.0f); }
static bool tri_leaf(const MB* m, const uint32_t* I, int t) { return m->v[I[t]].a[2] && m->v[I[t]].a[0] != L_PETAL; }

// ---------------------------------------------------------------- audit
// One plant family: the kinds it draws and what its cards are.
typedef struct { const char* name; int kind; int layer; } Family;   // layer -1: every leaf card of the kind
static const Family FAM[] = {
    {"tall grass", VEG_GRASS, -1}, {"sward tufts", VEG_CARPET, -1}, {"ground ferns", VEG_FERN, -1}, {"cycads", VEG_CYCAD, -1},
    {"shrubs", VEG_SHRUB, -1}, {"broadleaf crowns", VEG_BROADLEAF, -1}, {"conifer crowns", VEG_CONIFER, -1}, {"palms", VEG_PALM, -1},
    {"tree ferns", VEG_TREEFERN, -1}, {"horsetails", VEG_HORSETAIL, -1}, {"flowers (petals)", -1, L_PETAL},
};
#define NFAM ((int)(sizeof FAM / sizeof FAM[0]))
// Screen: 1080 rows over a 60 degree vertical field of view (the game camera).
#define PX_RAD (2.0f * 0.57735027f / 1080.0f)
static int cmp_f2(const void* a, const void* b) { float x = *(const float*)a, y = *(const float*)b; return x < y ? -1 : x > y; }

static void audit(void) {
    printf("| family | cards (full mesh) | texel (mm, median at mean scale) | texels per pixel at 2 / 5 / 15 m | edge-on share at 2 / 5 / 15 m (|n.v| < 0.2) | under 0.1 at 5 m | bent normal vs card (deg, median) |\n|---|---|---|---|---|---|---|\n");
    for (int f = 0; f < NFAM; f++) {
        const Family* F = &FAM[f];
        float* tex = malloc(sizeof(float) * 2000000 * 2); int ntex = 0; double cards = 0, nvar = 0;
        double edge[3] = {0}, edge01 = 0, area_sum[3] = {0}; float* bend = malloc(sizeof(float) * 2000000); int nb = 0;
        static const float D[3] = {2, 5, 15};
        for (int k = 0; k < VEG_KINDS; k++) {
            if (F->kind >= 0 && k != F->kind) continue;
            float sc = 0.5f * (SCALE_LO[k] + SCALE_HI[k]);
            for (int q = 0; q < veg_nvar(k); q++) {
                MB m; build_species(&m, k, q, 0); nvar += 1;
                for (int pass = 0; pass < 2; pass++) {
                    const uint32_t* I = pass ? m.fi : m.i; int ni = pass ? m.nfi : m.ni;
                    for (int t = 0; t + 2 < ni; t += 3) {
                        const VVert *a = &m.v[I[t]], *b = &m.v[I[t + 1]], *c = &m.v[I[t + 2]];
                        if (F->layer >= 0 ? a->a[0] != F->layer : !tri_leaf(&m, I, t)) continue;
                        V3 pa = vpos(a), pb = vpos(b), pc = vpos(c), cr = vcross(vsub(pb, pa), vsub(pc, pa));
                        float A = 0.5f * vlen(cr) * sc * sc;
                        float du1 = b->uv[0] - a->uv[0], dv1 = b->uv[1] - a->uv[1], du2 = c->uv[0] - a->uv[0], dv2 = c->uv[1] - a->uv[1];
                        float At = 0.5f * fabsf(du1 * dv2 - du2 * dv1) * AT * AT;
                        if (A < 1e-9f || At < 1e-6f) continue;
                        cards += 0.5;
                        if (ntex < 4000000) { tex[2 * ntex] = sqrtf(A / At); tex[2 * ntex + 1] = A; ntex++; }
                        V3 ng = vnorm(cr), nb3 = vnorm(vadd(vadd(vnrm(a), vnrm(b)), vnrm(c)));
                        if (nb < 2000000) bend[nb++] = acosf(clampf(fabsf(vdot(ng, nb3)), 0, 1)) * 57.2958f;
                        // Seen from an adult's eye (1.8 m) on a ring at each distance round the plant (16 bearings).
                        V3 cen = vmul(vadd(vadd(pa, pb), pc), sc / 3.0f);
                        for (int di = 0; di < 3; di++) for (int az = 0; az < 16; az++) {
                            V3 eye = v3(cosf(az * 0.3927f) * D[di], 1.8f, sinf(az * 0.3927f) * D[di]);
                            float nv = fabsf(vdot(ng, vnorm(vsub(eye, cen))));
                            area_sum[di] += A; if (nv < 0.2f) edge[di] += A;
                            if (di == 1 && nv < 0.1f) edge01 += A;
                        }
                    }
                }
                free(m.v); free(m.i); free(m.fi);
            }
        }
        if (!ntex) { free(tex); free(bend); continue; }
        qsort(tex, ntex, sizeof(float) * 2, cmp_f2);
        double tot = 0; for (int i = 0; i < ntex; i++) tot += tex[2 * i + 1];
        double acc = 0; float med = tex[0]; for (int i = 0; i < ntex; i++) { acc += tex[2 * i + 1]; if (acc >= tot * 0.5) { med = tex[2 * i]; break; } }
        qsort(bend, nb, sizeof(float), cmp_f2);
        printf("| %s | %.0f | %.2f | %.2f / %.2f / %.1f | %.0f%% / %.0f%% / %.0f%% | %.0f%% | %.0f |\n", F->name, cards / nvar, med * 1000,
               2 * PX_RAD / med, 5 * PX_RAD / med, 15 * PX_RAD / med, 100 * edge[0] / area_sum[0], 100 * edge[1] / area_sum[1], 100 * edge[2] / area_sum[2],
               100 * edge01 / area_sum[1], bend[nb / 2]);
        free(tex); free(bend);
    }
}

// ---------------------------------------------------------------- checks
// Every mesh: valid indices, unit normals, and foliage normals on the outward side of the plant.
static void test_meshes(int* verts, int* tris) {
    for (int k = 0; k < VEG_KINDS; k++) for (int q = 0; q < veg_nvar(k); q++) for (int lod = 0; lod < 2; lod++) {
        MB m; build_species(&m, k, q, lod);
        int bad_idx = 0, bad_n = 0, inward = 0, leafv = 0;
        for (int i = 0; i < m.ni; i++) bad_idx += m.i[i] >= (uint32_t)m.nv;
        for (int i = 0; i < m.nfi; i++) bad_idx += m.fi[i] >= (uint32_t)m.nv;
        for (int i = 0; i < m.nv; i++) {
            const VVert* v = &m.v[i]; V3 n = vnrm(v); float l = vlen(n);
            if (!isfinite(l) || fabsf(l - 1) > 0.02f) bad_n++;
            // Foliage on the outside of a crowned plant (the outer third by distance from the trunk):
            // its normal should not point back toward the trunk.
            if (v->a[2] && v->a[0] != L_PETAL && (k == VEG_BROADLEAF || k == VEG_CONIFER || k == VEG_SHRUB)) {
                V3 out = v3(v->p[0], 0, v->p[2]);
                if (vlen(out) > 0.67f * m.rad) { leafv++; if (vdot(n, vnorm(out)) < -0.2f) inward++; }
            }
        }
        CHECK(m.nv > 0 && m.ni % 3 == 0 && m.nfi % 3 == 0, "%s %d lod %d: %d vertices, %d + %d indices", KIND[k], q, lod, m.nv, m.ni, m.nfi);
        CHECK(!bad_idx, "%s %d lod %d: %d indices out of range", KIND[k], q, lod, bad_idx);
        CHECK(!bad_n, "%s %d lod %d: %d normals not unit length", KIND[k], q, lod, bad_n);
        // A few cards on a crown's rim face the trunk (bake_sky bends them toward the open sky, not out);
        // more than 1 in 20 would shade the rim as the inside of the crown.
        CHECK(leafv < 100 || inward <= leafv / 20, "%s %d lod %d: %d of %d outer leaf normals face into the crown", KIND[k], q, lod, inward, leafv);
        if (!lod) { verts[k] += m.nv; tris[k] += (m.ni + m.nfi) / 3; }
        free(m.v); free(m.i); free(m.fi);
    }
}

// A card's outline seen face on: the area its triangles cover projected on the card's plane.
static float projected_area(const MB* m, V3 n) {
    float A = 0;
    for (int t = 0; t + 2 < m->ni; t += 3) {
        V3 a = vpos(&m->v[m->i[t]]), b = vpos(&m->v[m->i[t + 1]]), c = vpos(&m->v[m->i[t + 2]]);
        A += 0.5f * fabsf(vdot(vcross(vsub(b, a), vsub(c, a)), n));
    }
    return A;
}
static void test_card_shapes(void) {
    // A flat card, then the same card rounded and creased: the outline seen face on keeps its area.
    VVert vb[64]; uint32_t ib[96];
    for (int crease = 0; crease < 2; crease++) {
        MB m = {0}; m.v = vb; m.cv = 64; m.i = ib; m.ci = 96;
        Gen g = {.m = &m, .leaflet = 1}; g.look = crease ? VEG_LOOK_ROUND | VEG_LOOK_CREASE : 0;
        V3 d = v3(0, 1, 0), s = v3(1, 0, 0);
        card(&g, v3(0, 0, 0), d, s, 0.4f, 0.2f, L_LEAFLET, 0, 0, 1, 1, 1, 0, (Wnd){0});
        float A = projected_area(&m, vcross(s, d));
        CHECK(fabsf(A - 0.08f) < 1e-4f, "card (crease %d): face-on area %.5f, want 0.08", crease, A);
        // Seen edge on (along s) the creased card shows a wedge, the flat one nothing.
        float E = projected_area(&m, s);
        if (crease) CHECK(E > 0.4f * 0.2f * 0.5f * VEG_CREASE_LIFT * 0.9f, "creased card: edge-on area %.5f", E);
        else CHECK(E < 1e-6f, "flat card: edge-on area %.5f", E);
        // Rounded normals: unit, on the card's front side, tilted toward their edge.
        for (int i = 0; i < m.nv; i++) {
            V3 n = vnrm(&m.v[i]), f = vcross(s, d);
            CHECK(fabsf(vlen(n) - 1) < 0.02f && vdot(n, f) > 0.5f, "card vertex %d: normal (%.2f %.2f %.2f)", i, n.x, n.y, n.z);
            if (crease && m.v[i].p[0] != 0) CHECK(vdot(n, s) * m.v[i].p[0] > 0, "rounded card vertex %d leans the wrong way", i);
        }
    }
    // A creased strip (grass blade): the same face-on area as the flat one.
    for (int crease = 0; crease < 2; crease++) {
        VVert v2[64]; uint32_t i2[192]; MB m = {0}; m.v = v2; m.cv = 64; m.i = i2; m.ci = 192;
        Gen g = {.m = &m}; g.look = crease ? VEG_LOOK_ROUND | VEG_LOOK_CREASE : 0;
        V3 c[4], sv[4], up[4]; float hw[4];
        for (int k = 0; k < 4; k++) { c[k] = v3(0, 0.1f * k, 0); sv[k] = v3(1, 0, 0); up[k] = v3(0, 0, 1); hw[k] = 0.02f; }
        strip(&g, 4, c, sv, up, hw, 0, L_GRASS, 0, 1, 0, 1, (Wnd){0}, (Wnd){0});
        float A = projected_area(&m, v3(0, 0, 1));
        CHECK(fabsf(A - 0.3f * 0.04f) < 1e-5f, "strip (crease %d): face-on area %.6f, want 0.012", crease, A);
    }
}

// The edge-on fade (veg_look.h, the same curve as veg.fs and veg_depth.fs).
static void test_edge_fade(void) {
    float prev = -1; int mono = 1;
    for (int i = 0; i <= 1000; i++) {
        float nv = i / 1000.0f, f = veg_edge_keep(nv, 0.5f);
        if (f < prev - 1e-6f) mono = 0;
        prev = f;
    }
    CHECK(mono, "edge fade is not monotonic in |n.v|");
    CHECK(veg_edge_keep(0.0f, 0.0f) == 0.0f && veg_edge_keep(0.0f, 1.0f) == 0.0f, "an exactly edge-on card must vanish");
    CHECK(veg_edge_keep(VEG_EDGE_HI, 0.0f) == 1.0f && veg_edge_keep(1.0f, 1.0f) == 1.0f, "a card facing the view must be kept whole");
    // Each card leaves at its own angle: the cut moves with the per-card key, never above VEG_EDGE_HI.
    CHECK(veg_edge_cut(0.0f) < veg_edge_cut(1.0f) && veg_edge_cut(1.0f) <= VEG_EDGE_HI, "edge cut %.3f .. %.3f", veg_edge_cut(0.0f), veg_edge_cut(1.0f));
    // At 0.25 (75 degrees off face-on) every card still shows; a card thinner than a tenth of its width (|n.v| < 0.1) shows for few keys.
    int shown = 0; for (int k = 0; k < 100; k++) shown += veg_edge_keep(0.25f, k / 99.0f) > 0.5f;
    CHECK(shown == 100, "at |n.v| 0.25, %d of 100 cards kept", shown);
    shown = 0; for (int k = 0; k < 100; k++) shown += veg_edge_keep(0.08f, k / 99.0f) > 0.5f;
    CHECK(shown < 40, "at |n.v| 0.08, %d of 100 cards kept", shown);
    // The shaders run the same curve (common.glsl veg_edge_keep): its constants must match these.
    FILE* f = fopen("ocean/cretaceous/shaders/common.glsl", "rb"); char* src = calloc(1, 1 << 20);
    if (f) { fread(src, 1, (1 << 20) - 1, f); fclose(f); }
    char want[160];
    snprintf(want, sizeof want, "float c = %.2f + %.2f * key; return smoothstep(c, c + %.2f, nv);", VEG_EDGE_LO, VEG_EDGE_HI - VEG_EDGE_BAND - VEG_EDGE_LO, VEG_EDGE_BAND);
    CHECK(f && strstr(src, want), "shaders/common.glsl veg_edge_keep does not read \"%s\"", want);
    free(src);
}

int main(int argc, char** argv) {
    double t0 = t_now();
    if (argc > 1 && !strcmp(argv[1], "--audit")) {
        static const int look[3] = {0, VEG_LOOK_ROUND, VEG_LOOK_ROUND | VEG_LOOK_CREASE};
        for (int l = 0; l < 3; l++) { veg_look_mesh = look[l]; printf("\nmesh look %d (round %d, crease %d)\n\n", l, !!(look[l] & VEG_LOOK_ROUND), !!(look[l] & VEG_LOOK_CREASE)); audit(); }
        return 0;
    }
    test_edge_fade();
    test_card_shapes();
    int v0[VEG_KINDS] = {0}, t0s[VEG_KINDS] = {0}, v1[VEG_KINDS] = {0}, t1[VEG_KINDS] = {0};
    veg_look_mesh = 0; test_meshes(v0, t0s);
    veg_look_mesh = VEG_LOOK_ROUND | VEG_LOOK_CREASE; test_meshes(v1, t1);
    // Budget: the crease adds a centre vertex to each row of a blade or leaflet, so no kind more than
    // doubles its triangles, and the whole set of meshes grows by at most 8% in vertices and 12% in
    // triangles (docs/plants/README.md weighs it by what is on screen).
    long sv0 = 0, sv1 = 0, st0 = 0, st1 = 0;
    for (int k = 0; k < VEG_KINDS; k++) {
        sv0 += v0[k]; sv1 += v1[k]; st0 += t0s[k]; st1 += t1[k];
        CHECK(v1[k] <= v0[k] * 1.5f + 64 && t1[k] <= t0s[k] * 2 + 64, "%s: vertices %d -> %d, triangles %d -> %d", KIND[k], v0[k], v1[k], t0s[k], t1[k]);
        if (argc > 1 && !strcmp(argv[1], "-v")) printf("%-9s vertices %7d -> %7d (%+5.1f%%), triangles %7d -> %7d (%+5.1f%%)\n", KIND[k], v0[k], v1[k], 100.0 * (v1[k] - v0[k]) / fmax(v0[k], 1), t0s[k], t1[k], 100.0 * (t1[k] - t0s[k]) / fmax(t0s[k], 1));
    }
    printf("veg: all meshes, vertices %ld -> %ld (%+.1f%%), triangles %ld -> %ld (%+.1f%%) with the crease\n", sv0, sv1, 100.0 * (sv1 - sv0) / sv0, st0, st1, 100.0 * (st1 - st0) / st0);
    CHECK(sv1 <= sv0 * 1.08 && st1 <= st0 * 1.12, "all meshes: vertices %ld -> %ld, triangles %ld -> %ld", sv0, sv1, st0, st1);
    return t_finish("test_veg", t0);
}
