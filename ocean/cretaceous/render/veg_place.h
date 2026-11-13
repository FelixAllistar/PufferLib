// Plant placement without GL: the kinds, the deterministic per-tile scatter
// from the baked terra vegetation layers, and the trunk sizes the physics
// colliders use. Header-only (static functions), so the renderer (veg.c)
// and the headless sim (world.c, trunk colliders) place every tree alike
// without linking each other. The meshes stay in veg.c.
#ifndef CRET_VEG_PLACE_H
#define CRET_VEG_PLACE_H
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include "../terra/terra.h"

// Plant kinds, matching TERRA_VEG_* layers. VEG_CARPET (low sward tufts)
// follows the splat, VEG_GRASS (tall grass, sedge and reeds) the splat,
// canopy, moisture and a clump noise; neither is a veg layer. The rest are
// derived from the layers and the habitat (render/flora.h): VEG_SNAG standing
// dead trees in old stands, VEG_LILY water lilies on quiet water, VEG_DROP
// fruit, seeds and cones fallen under the plants that bear them, VEG_FUNGUS
// brackets and mushrooms on logs and stumps.
enum {
    VEG_CONIFER, VEG_BROADLEAF, VEG_CYCAD, VEG_FERN, VEG_TREEFERN, VEG_HORSETAIL, VEG_PALM, VEG_SHRUB, VEG_CARPET, VEG_GRASS,
    VEG_SNAG, VEG_LILY, VEG_DROP, VEG_FUNGUS, VEG_KINDS
};
// Variants per kind. For the bake-layer kinds a variant slot holds a species
// (FLORA_SLOT in render/flora.h); a species in two slots has two seeds. Tall
// grass uses variants 0-3, the sward 0-5 (4 clubmoss, 5 moss), standing dead
// trees and lilies 0-3, fungi 0-3 and fallen food one per DROP_* kind.
#define VEG_VARIANTS 8
#define VEG_IMPOSTOR_VIEWS 8
#define VEG_TILE 64.0f

typedef struct VegInstance { float x, y, z, scale, yaw, hue, phase; uint16_t kind, variant; float cos_yaw, sin_yaw; } VegInstance;

static inline float vp_clamp(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float vp_lerp(float a, float b, float t) { return a + (b - a) * t; }
static inline uint32_t vp_hash(uint32_t h) { h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16; return h; }
typedef struct { uint32_t s; } VpRng;
static inline float vp_r(VpRng* r) { r->s = vp_hash(r->s + 0x9e3779b9u); return (r->s >> 8) * (1.0f / 16777216.0f); }
static inline float vp_rr(VpRng* r, float a, float b) { return a + (b - a) * vp_r(r); }

#include "flora.h"

static inline bool is_tree(int kind) { return kind == VEG_CONIFER || kind == VEG_BROADLEAF || kind == VEG_TREEFERN || kind == VEG_PALM || kind == VEG_CYCAD || kind == VEG_SNAG; }
// Spacing (m) of the scatter lattice and size range per kind (lilies, fallen
// food and fungi are placed by their own rules in render/veg.c).
static const float SPACING[VEG_KINDS] = {7.5f, 8.0f, 4.0f, 1.2f, 4.5f, 2.4f, 7.0f, 2.6f, 0.5f, 0.8f, 12.0f, 1.6f, 1.0f, 1.0f};
static const float SCALE_LO[VEG_KINDS] = {0.7f, 0.75f, 0.7f, 0.9f, 0.75f, 0.9f, 0.8f, 0.7f, 0.7f, 0.8f, 0.8f, 0.7f, 0.8f, 0.7f};
static const float SCALE_HI[VEG_KINDS] = {1.25f, 1.25f, 1.2f, 1.65f, 1.25f, 1.5f, 1.2f, 1.3f, 1.55f, 1.2f, 1.2f, 1.3f, 1.2f, 1.3f};
static const float DENSITY[VEG_KINDS] = {0.9f, 0.85f, 0.6f, 0.85f, 0.75f, 1.0f, 0.7f, 0.65f, 1.0f, 1.0f, 0.35f, 1.0f, 1.0f, 1.0f};
// The largest flora_density_mul of a kind: the scatter rejects on the layer
// alone first and only works out the site for plants that may stand.
static const float DENSITY_MUL_MAX[VEG_KINDS] = {1, 1, 2.6f, 1.25f, 1, 1, 1, 2.6f, 1, 1, 1, 1, 1, 1};

static inline float veg_density(const TerraMap* m, int kind, float x, float z) {
    float gx = vp_clamp(x / m->cell, 0, m->n - 1.001f), gz = vp_clamp(z / m->cell, 0, m->n - 1.001f);
    int ix = (int)gx, iz = (int)gz; float fx = gx - ix, fz = gz - iz; size_t i = (size_t)iz * m->n + ix;
    const uint8_t* v = m->veg;
    float a = v[i * TERRA_VEG_COUNT + kind], b = v[(i + 1) * TERRA_VEG_COUNT + kind];
    float c = v[(i + m->n) * TERRA_VEG_COUNT + kind], d = v[(i + m->n + 1) * TERRA_VEG_COUNT + kind];
    return vp_lerp(vp_lerp(a, b, fx), vp_lerp(c, d, fx), fz) / 255.0f;
}
// Only cells terra marked as water count as wet: the water level is also
// extended a few cells under dry banks (so the surface meets the shore), and
// the renderer hides it there. Emergent plants may stand in shallow water.
static inline bool placeable(const TerraMap* m, float x, float z, float max_slope, float wade, float* y) {
    float n[3]; terra_normal(m, x, z, n);
    if (n[1] < 1 / sqrtf(1 + max_slope * max_slope)) return false;
    float h = terra_height(m, x, z), w = terra_water(m, x, z);
    int gx = (int)fminf(fmaxf(x / m->cell + 0.5f, 0), m->n - 1), gz = (int)fminf(fmaxf(z / m->cell + 0.5f, 0), m->n - 1);
    size_t i = (size_t)gz * m->n + gx;
    bool wet = m->water_kind[i] != TERRA_WATER_NONE && w > h + 0.05f;
    if (h < 0.4f || m->water_kind[i] == TERRA_WATER_SEA) return false;
    if (wet && w - h > wade) return false;
    *y = h - 0.15f; return true;
}
static inline int veg_nvar(int kind) { return kind == VEG_GRASS || kind == VEG_SNAG || kind == VEG_LILY || kind == VEG_FUNGUS ? 4 : kind == VEG_CARPET ? 6 : VEG_VARIANTS; }
// Density of a kind's scatter at (x, z) before the habitat multiplier: its
// bake layer, or for standing dead trees the canopy layers in old stands.
static inline float veg_layer_density(const TerraMap* m, int kind, float x, float z) {
    if (kind == VEG_SNAG) return vp_clamp(veg_density(m, VEG_CONIFER, x, z) + veg_density(m, VEG_BROADLEAF, x, z), 0, 1) * flora_old(m, x, z);
    return veg_density(m, kind, x, z);
}
// The variant of a scattered plant: a species by habitat (render/flora.h),
// then one of its seeds. Standing dead trees follow the canopy they died in:
// dead conifers (0, 1: whole and broken) where conifers lead, dead
// broadleaves (2) and leaning broken trunks (3) elsewhere.
static inline int pick_variant(const TerraMap* m, int kind, float x, float z, const FloraSite* s, VpRng* r) {
    float u = vp_r(r), u2 = vp_r(r);
    if (kind == VEG_SNAG) {
        float c = veg_density(m, VEG_CONIFER, x, z), b = veg_density(m, VEG_BROADLEAF, x, z);
        return u * (c + b + 1e-3f) < c ? (u2 < 0.6f ? 0 : 1) : (u2 < 0.6f ? 2 : 3);
    }
    return flora_pick(m, kind, x, z, s, u, u2);
}
// Scatter one kind over a tile on a jittered lattice; deterministic per tile.
static inline int scatter(const TerraMap* m, int kind, int tx, int tz, VegInstance* out, int cap) {
    float sp = SPACING[kind]; int cnt = 0, steps = (int)(VEG_TILE / sp);
    for (int j = 0; j < steps; j++) for (int i = 0; i < steps; i++) {
        uint32_t h = vp_hash((uint32_t)(tx * 4099 + i) * 2654435761u ^ (uint32_t)(tz * 3571 + j) * 40503u ^ (uint32_t)kind * 977u);
        VpRng r = {h};
        float x = tx * VEG_TILE + (i + vp_r(&r)) * sp, z = tz * VEG_TILE + (j + vp_r(&r)) * sp;
        // The layer alone first (the habitat multiplier is at most
        // DENSITY_MUL_MAX), then the site for the plants that may stand.
        float d0 = veg_layer_density(m, kind, x, z) * DENSITY[kind], u = vp_r(&r), dm = d0 * DENSITY_MUL_MAX[kind];
        if (u > dm * dm * 1.6f) continue;
        FloraSite site = flora_site(m, x, z);
        float d = d0 * (kind < TERRA_VEG_COUNT ? flora_density_mul(kind, &site) : site.gap);
        if (u > d * d * 1.6f) continue;
        float y; if (!placeable(m, x, z, is_tree(kind) ? 0.95f : 1.1f, kind == VEG_HORSETAIL ? 0.5f : -1.0f, &y)) continue;
        if (cnt == cap) return cnt;
        out[cnt++] = (VegInstance){x, y, z, vp_rr(&r, SCALE_LO[kind], SCALE_HI[kind]), vp_rr(&r, 0, 6.2831853f), vp_rr(&r, -1, 1), vp_rr(&r, 0, 6.283f),
                                   (uint16_t)kind, (uint16_t)pick_variant(m, kind, x, z, &site, &r), 0, 0};
        out[cnt - 1].cos_yaw = cosf(out[cnt - 1].yaw); out[cnt - 1].sin_yaw = sinf(out[cnt - 1].yaw);
    }
    return cnt;
}

// The generator's per-species dimensions: overall height H and trunk base
// radius r0 (m, at scale 1). render/veg_gen.h builds the meshes from these
// same draws (species_dims), so the colliders below match the drawn trunks.
// Each draws exactly once from the variant's stream.
static inline void veg_species_dims(int kind, int variant, float* H, float* r0) {
    VpRng r = {vp_hash((uint32_t)kind * 7919u + (uint32_t)variant * 104729u + 12345u)}; float h = 0, rr = 0;
    switch (kind == VEG_SNAG ? -2 : flora_species(kind, variant)) {
    case FL_ARAUCARIA: h = vp_rr(&r, 15, 23); rr = 0.1f + 0.018f * h; break;
    case FL_PODOCARP: h = vp_rr(&r, 12, 18); rr = 0.1f + 0.02f * h; break;
    case FL_UMBRELLA: h = vp_rr(&r, 18, 24); rr = 0.12f + 0.02f * h; break;
    case FL_REDWOOD: h = vp_rr(&r, 22, 30); rr = 0.25f + 0.02f * h; break;
    case FL_CHEIROLEP: h = vp_rr(&r, 6, 10); rr = 0.08f + 0.02f * h; break;
    case FL_UNDERSTOREY: h = vp_rr(&r, 4.5f, 6.5f); rr = 0.05f + 0.02f * h; break;
    case FL_KATSURA: h = vp_rr(&r, 9, 14); rr = (0.05f + 0.016f * h) * 1.5f; break;   // several stems: their spread
    case FL_MAGNOLIA: h = vp_rr(&r, 8, 13); rr = 0.06f + 0.024f * h; break;
    case FL_LAUREL: h = vp_rr(&r, 7, 11); rr = 0.06f + 0.024f * h; break;
    case FL_PLANE: h = vp_rr(&r, 12, 17); rr = 0.08f + 0.026f * h; break;
    case FL_GINKGO: h = vp_rr(&r, 10, 15); rr = 0.07f + 0.022f * h; break;
    case FL_CYCAS: if ((variant & 3) == 2) { h = vp_rr(&r, 1.5f, 3.0f); rr = 0.22f; } else { h = vp_rr(&r, 0.3f, 1.6f); rr = 0.3f; } break;
    case FL_ZAMIA: h = vp_rr(&r, 0.15f, 0.6f); rr = 0.34f; break;
    case FL_WILLIAMSONIA: h = vp_rr(&r, 1.0f, 1.7f); rr = 0.19f; break;
    case FL_CYATHEA: h = vp_rr(&r, 3.0f, 5.8f); rr = 0.26f; break;
    case FL_DICKSONIA: h = vp_rr(&r, 1.3f, 2.4f); rr = 0.32f; break;
    case FL_SABAL: h = vp_rr(&r, 4.0f, 8.5f); rr = 0.27f; break;
    case FL_FEATHER_PALM: h = vp_rr(&r, 3.0f, 6.5f); rr = 0.24f; break;
    case -2:   // standing dead trees: whole and broken conifers, a dead broadleaf, a leaning broken trunk
        if (variant == 0) { h = vp_rr(&r, 11, 18); rr = 0.12f + 0.02f * h; }
        else if (variant == 1) { h = vp_rr(&r, 3.5f, 7); rr = 0.35f; }
        else if (variant == 2) { h = vp_rr(&r, 7, 11); rr = 0.08f + 0.024f * h; }
        else { h = vp_rr(&r, 4, 7.5f); rr = 0.26f; }
        break;
    default: h = 1; rr = 0; break;
    }
    if (H) *H = h;
    if (r0) *r0 = rr;
}

// Trunk collider of a scattered plant: a vertical capsule of radius *r from
// the ground to *h (m, at the instance's scale), the drawn trunk's base
// radius (veg_species_dims). False for plants animals walk through: ferns,
// horsetails, shrubs, grass, and cycads whose stem is hidden in the fronds
// (under 1.2 m), which stood as invisible walls when the radii were a table.
static inline bool veg_trunk(const VegInstance* p, float* r, float* h) {
    if (p->kind >= VEG_KINDS || p->variant >= VEG_VARIANTS || !is_tree(p->kind)) return false;
    float H, r0; veg_species_dims(p->kind, p->variant, &H, &r0);
    float tr = r0 * p->scale, th = H * p->scale;
    switch (p->kind) {
    case VEG_CONIFER: *r = tr; *h = fminf(th * 0.8f, 8.0f); return tr > 0.05f;
    case VEG_SNAG: *r = tr; *h = fminf(th * 0.9f, 8.0f); return tr > 0.05f;
    case VEG_BROADLEAF: *r = flora_species(p->kind, p->variant) == FL_KATSURA ? tr * 0.75f : tr; *h = fminf(th * 0.8f, 8.0f); return tr > 0.05f;   // multi-stem: inside the spread
    case VEG_TREEFERN: case VEG_PALM: *r = tr; *h = th * 0.85f; return tr > 0.05f;
    case VEG_CYCAD: if (th < 1.2f) return false; *r = tr * 0.85f; *h = th * 0.5f; return true;
    default: return false;
    }
}
#endif
