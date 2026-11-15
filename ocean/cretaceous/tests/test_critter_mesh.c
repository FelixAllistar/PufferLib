// Procedural small-animal meshes (render/critter_mesh.h): no GL, no world.
// Every kind at every LOD builds within its triangle budget, with valid
// indices, finite unit normals, valid part data and a left-right symmetric
// shape; the trunk's normals face outward; feet reach the ground; the joints
// the vertex shader turns parts about sit where the animation expects them.
#include "test_util.h"
#include "../render/critter_mesh.h"

static const char* const KIND[CRM_KINDS] = {"mouse", "Repenomamus", "lizard", "bird", "fish"};

static void test_kind(int k) {
    int tris[CRM_LODS];
    for (int l = 0; l < CRM_LODS; l++) {
        CrmMesh m; crm_build(&m, k, l);
        tris[l] = m.ni / 3;
        CHECK(m.ni % 3 == 0 && m.nv > 0 && m.nv < 65536, "%s LOD%d: %d indices, %d vertices", KIND[k], l, m.ni, m.nv);
        int bad_idx = 0; for (int i = 0; i < m.ni; i++) bad_idx += m.idx[i] >= m.nv;
        CHECK(!bad_idx, "%s LOD%d: %d indices out of range", KIND[k], l, bad_idx);
        float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f}; double zsum = 0;
        int bad_n = 0, bad_part = 0, inward = 0, trunk_side = 0;
        for (int i = 0; i < m.nv; i++) {
            const CrmVertex* v = &m.v[i];
            float nl = sqrtf(v->nrm[0] * v->nrm[0] + v->nrm[1] * v->nrm[1] + v->nrm[2] * v->nrm[2]);
            bool finite = true; for (int q = 0; q < 3; q++) finite = finite && isfinite(v->pos[q]) && isfinite(v->nrm[q]) && isfinite(v->piv[q]);
            if (!finite || fabsf(nl - 1) > 1e-3f) bad_n++;
            int part = (int)v->seg[1];
            if (part < 0 || part >= CRM_PARTS || v->seg[1] != (float)part || v->seg[2] < 0 || v->seg[2] > 1 || v->seg[3] < 0 || v->seg[3] > 1.05f) bad_part++;
            // Loft rings: the normal points away from the spine (the bird's thighs sit off the midline: skipped).
            if (part == CRM_TRUNK && fabsf(v->pos[2]) > 0.01f && !(k == 3 && fabsf(v->pos[0] + 0.01f) < 0.07f && v->pos[1] < 0.28f)) {
                trunk_side++; if (v->nrm[2] * v->pos[2] < 0) inward++;
            }
            for (int q = 0; q < 3; q++) { lo[q] = fminf(lo[q], v->pos[q]); hi[q] = fmaxf(hi[q], v->pos[q]); }
            zsum += v->pos[2];
        }
        CHECK(!bad_n, "%s LOD%d: %d vertices with a bad position or normal", KIND[k], l, bad_n);
        CHECK(!bad_part, "%s LOD%d: %d vertices with bad part data", KIND[k], l, bad_part);
        CHECK(trunk_side > 0 && !inward, "%s LOD%d: %d of %d trunk normals face in", KIND[k], l, inward, trunk_side);
        CHECK(fabs(zsum) / m.nv < 1e-4, "%s LOD%d: not symmetric (mean z %.5f)", KIND[k], l, zsum / m.nv);
        // Body length 1 from snout to rump (a fish's and bird's overall): at least that long, and sized like the animal.
        CHECK(hi[0] - lo[0] > 0.9f && hi[0] - lo[0] < 2.5f, "%s LOD%d: %.2f long", KIND[k], l, hi[0] - lo[0]);
        if (k == 4) CHECK(lo[1] < -0.1f && hi[1] > 0.1f, "fish LOD%d: not centred on y = 0 (%.2f..%.2f)", l, lo[1], hi[1]);
        else {
            CHECK(lo[1] > -0.03f && lo[1] < 0.03f, "%s LOD%d: lowest point %.3f, not on the ground", KIND[k], l, lo[1]);
            CHECK(hi[1] < 0.6f, "%s LOD%d: %.2f tall", KIND[k], l, hi[1]);
        }
        // Joints: legs, wings and fins turn about joints behind the neck (so
        // they never turn with the head); eyes, ears and the bill turn with it.
        const CrmRig* R = &CRM_RIG[k]; int wrong = 0;
        for (int i = 0; i < m.nv; i++) {
            const CrmVertex* v = &m.v[i]; int part = (int)v->seg[1];
            if ((part >= CRM_LEG_FL && part <= CRM_WING) && v->seg[0] >= R->neck_x - 0.03f) wrong++;
            if ((part == CRM_EYE || part == CRM_EAR || part == CRM_BEAK) && v->seg[0] <= R->neck_x + 0.03f) wrong++;
            if (part >= CRM_LEG_FL && part <= CRM_LEG_HR && v->seg[0] != v->piv[0]) wrong++;
        }
        CHECK(!wrong, "%s LOD%d: %d vertices turn about the wrong joint", KIND[k], l, wrong);
        crm_free(&m);
    }
    // Budgets: a few hundred triangles up close, a handful far away.
    CHECK(tris[0] >= 250 && tris[0] <= 700, "%s: LOD0 %d tris", KIND[k], tris[0]);
    CHECK(tris[1] < tris[0] * 0.6f && tris[1] >= 100, "%s: LOD1 %d tris (LOD0 %d)", KIND[k], tris[1], tris[0]);
    CHECK(tris[2] <= 80 && tris[2] < tris[1] / 2, "%s: LOD2 %d tris", KIND[k], tris[2]);
    printf("  %-12s %3d / %3d / %2d tris\n", KIND[k], tris[0], tris[1], tris[2]);
}

int main(void) {
    double t0 = t_now();
    CHECK(CRM_KINDS == CRET_CR_KINDS, "critter_mesh.h has %d kinds, critters.h %d", CRM_KINDS, CRET_CR_KINDS);
    for (int k = 0; k < CRM_KINDS; k++) test_kind(k);
    // Legs reach the ground at the LODs that have them.
    for (int k = 0; k < 4; k++) for (int l = 0; l < 2; l++) {
        CrmMesh m; crm_build(&m, k, l); float foot = 1e9f;
        for (int i = 0; i < m.nv; i++) if (m.v[i].seg[1] >= CRM_LEG_FL && m.v[i].seg[1] <= CRM_LEG_HR) foot = fminf(foot, m.v[i].pos[1]);
        CHECK(foot < 0.02f, "%s LOD%d: feet %.3f above the ground", KIND[k], l, foot);
        crm_free(&m);
    }
    return t_finish("test_critter_mesh", t0);
}
