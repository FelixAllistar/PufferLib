// Critter placeholders (render/critters.h). Included by view.c.
#include "critters.h"
#include <math.h>
#include <string.h>

// Smoothed draw positions by pool slot: a step from the sim (or a snapshot)
// is eased over about 70 ms; a jump of more than 3 m, a new critter or a
// change of kind snaps.
typedef struct { float x, y, z, yaw, alt, hop; uint8_t live, kind; } CrDraw;
static CrDraw g_crd[CRET_MAX_CRITTERS];
static float g_cr_time = -1;

// Per kind: colour, and body proportions as fractions of the body length
// (body radius across, head radius, tail length, head and tail offsets).
typedef struct { float col[3], body_w, body_h, head, tail, tail_w; } CrLook;
static const CrLook CR_LOOK[CRET_CR_KINDS] = {
    [CRET_CR_MOUSE]  = {{0.42f, 0.33f, 0.24f}, 0.20f, 0.17f, 0.12f, 0.45f, 0.04f},   // brown, long thin tail
    [CRET_CR_BADGER] = {{0.18f, 0.16f, 0.14f}, 0.22f, 0.19f, 0.13f, 0.25f, 0.07f},   // dark, stocky
    [CRET_CR_LIZARD] = {{0.36f, 0.44f, 0.20f}, 0.12f, 0.08f, 0.08f, 0.60f, 0.05f},   // olive, low and long
    [CRET_CR_BIRD]   = {{0.55f, 0.42f, 0.30f}, 0.26f, 0.24f, 0.15f, 0.30f, 0.10f},   // buff, round
    [CRET_CR_FISH]   = {{0.48f, 0.52f, 0.50f}, 0.13f, 0.18f, 0.00f, 0.25f, 0.12f},   // silver, deep and narrow
};

static void cr_sphere(const GfxProgram* p, GLint l_model, unsigned int vao, int tris, bool indexed,
                      float x, float y, float z, float yaw, float sx, float sy, float sz) {
    Matrix m = MatrixMultiply(MatrixScale(sx, sy, sz), MatrixMultiply(MatrixRotateY(-yaw), MatrixTranslate(x, y, z)));
    float16 f = MatrixToFloatV(m);
    glUniformMatrix4fv(l_model, 1, GL_FALSE, f.v);
    glBindVertexArray(vao);
    if (indexed) glDrawElements(GL_TRIANGLES, tris * 3, GL_UNSIGNED_SHORT, 0); else glDrawArrays(GL_TRIANGLES, 0, tris * 3);
    (void)p;
}

// A caught critter lying on the ground or hanging from a carrier's jaws
// (render/nest.c): the body on its side, head and tail limp, colours a
// little darker. Returns spheres drawn (l_color < 0: a shadow pass).
static int cr_draw_limp(const GfxProgram* p, GLint l_model, GLint l_color, unsigned int vao, int tris, bool indexed,
                        int kind, float kg, float x, float y, float z, float yaw, bool held) {
    const CrLook* K = &CR_LOOK[kind]; float L = cret_critter_length(kind, kg), cy = cosf(yaw), sy = sinf(yaw);
    float bw = K->body_w * L, bh = K->body_h * L, by = held ? y - bh : y + bw * 0.8f;   // lying: its width is now its height
    if (l_color >= 0) glUniform3f(l_color, K->col[0] * 0.85f, K->col[1] * 0.85f, K->col[2] * 0.85f);
    cr_sphere(p, l_model, vao, tris, indexed, x, by, z, yaw, L * 0.32f, bw, bh);
    float droop = held ? bh * 1.5f : 0;   // held across the jaws, head and tail hang down
    cr_sphere(p, l_model, vao, tris, indexed, x + cy * L * 0.36f, by - droop, z + sy * L * 0.36f, yaw, K->head * L * 1.2f, K->head * L, K->head * L);
    float tl = L * 0.3f + K->tail * L * 0.5f;
    cr_sphere(p, l_model, vao, tris, indexed, x - cy * tl, by - droop * 1.3f, z - sy * tl, yaw, K->tail * L * 0.5f, K->tail_w * L, K->tail_w * L);
    return 3;
}

int cret_critters_draw(const CretSim* sim, const GfxProgram* p, unsigned int vao, int tris, bool indexed, const float cam[3], float time) {
    float dt = g_cr_time < 0 ? 0 : fminf(fmaxf(time - g_cr_time, 0), 0.1f); g_cr_time = time;
    float ease = 1 - expf(-dt * 15);
    GLint l_model = gfx_loc(p, "u_model"), l_color = gfx_loc(p, "u_color"); int n = 0;
    for (int k = 0; k < CRET_MAX_CRITTERS; k++) {
        const CretCritter* c = &sim->critters[k]; CrDraw* d = &g_crd[k];
        if (!c->active || c->kind >= CRET_CR_KINDS) { d->live = 0; continue; }
        float jump = hypotf(c->x - d->x, c->z - d->z);
        if (!d->live || d->kind != c->kind || jump > 3) { d->x = c->x; d->y = c->y; d->z = c->z; d->yaw = c->yaw; d->alt = c->alt; d->live = 1; d->kind = c->kind; }
        else {
            float dy = c->yaw - d->yaw; while (dy > 3.14159f) dy -= 6.28318f; while (dy < -3.14159f) dy += 6.28318f;
            d->x += (c->x - d->x) * ease; d->y += (c->y - d->y) * ease; d->z += (c->z - d->z) * ease; d->yaw += dy * ease; d->alt += (c->alt - d->alt) * ease;
        }
        d->hop += c->speed * dt;   // gait phase from the speed, so clients (which get no phase) animate too
        float dx = d->x - cam[0], dz = d->z - cam[2];
        if (dx * dx + dz * dz > CRET_CRITTER_DRAW_RANGE * CRET_CRITTER_DRAW_RANGE) continue;
        float L = cret_critter_length(c->kind, c->kg), cy = cosf(d->yaw), sy = sinf(d->yaw);
        const CrLook* K = &CR_LOOK[c->kind];
        // Bob while walking, bounding hops while running (mammals), a wriggle (lizards, fish), wingbeats in flight.
        float stride = fmaxf(L * 1.2f, 0.12f), ph = d->hop / stride * 6.2831853f;
        float bob = c->speed > 0.05f ? (c->kind == CRET_CR_BIRD || c->state == CRET_CRS_FLEE ? fabsf(sinf(ph * 0.5f)) * L * 0.35f : fabsf(sinf(ph)) * L * 0.06f) : 0;
        if (c->kind == CRET_CR_LIZARD || c->kind == CRET_CR_FISH) bob = 0;
        float wig = c->kind == CRET_CR_LIZARD || c->kind == CRET_CR_FISH ? sinf(ph) * (c->speed > 0.05f ? 0.35f : 0.05f) : 0;
        float bw = K->body_w * L, bh = K->body_h * L, by = d->y + d->alt + bob + (c->kind == CRET_CR_FISH ? 0 : bh * 0.9f);
        if (c->state == CRET_CRS_HIDE) by -= bh * 0.4f;   // crouched
        if (l_color >= 0) glUniform3f(l_color, K->col[0], K->col[1], K->col[2]);
        cr_sphere(p, l_model, vao, tris, indexed, d->x, by, d->z, d->yaw + wig * 0.3f, L * 0.32f, bh, bw); n++;   // body
        if (K->head > 0) {
            float hx = d->x + cy * L * 0.36f, hz = d->z + sy * L * 0.36f;
            cr_sphere(p, l_model, vao, tris, indexed, hx, by + bh * 0.35f, hz, d->yaw, K->head * L * 1.2f, K->head * L, K->head * L); n++;
        }
        float ta = d->yaw + 3.14159f - wig, tx = d->x + cosf(ta) * (L * 0.3f + K->tail * L * 0.5f), tz = d->z + sinf(ta) * (L * 0.3f + K->tail * L * 0.5f);
        if (c->kind == CRET_CR_BIRD) {   // wings: out and beating while flying, folded at rest
            float span = c->alt > 0.1f ? L * 0.9f : L * 0.25f, flap = c->alt > 0.1f ? sinf(time * 40) * 0.6f : 0;
            for (int side = -1; side <= 1; side += 2) {
                float wx = d->x - sy * side * span * 0.5f, wz = d->z + cy * side * span * 0.5f;
                cr_sphere(p, l_model, vao, tris, indexed, wx, by + bh * 0.3f + flap * span * 0.3f, wz, d->yaw, L * 0.22f, L * 0.03f, span * 0.5f); n++;
            }
        }
        if (l_color >= 0) glUniform3f(l_color, K->col[0] * 0.8f, K->col[1] * 0.8f, K->col[2] * 0.8f);
        cr_sphere(p, l_model, vao, tris, indexed, tx, by - bh * 0.2f, tz, ta, K->tail * L * 0.5f, K->tail_w * L, K->tail_w * L * (c->kind == CRET_CR_FISH ? 2.0f : 1.0f)); n++;   // tail (a fish: the fin)
    }
    glBindVertexArray(0);
    return n;
}
