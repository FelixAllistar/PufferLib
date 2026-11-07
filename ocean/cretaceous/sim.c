#include "sim.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

#define CAT_AGENT 0x2ull
#define CAT_PROP  0x4ull

static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline uint32_t xs32(uint32_t* s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x ? x : 0x9e3779b9u; }
static inline float frand(uint32_t* s) { return (xs32(s) >> 8) * (1.0f / 16777216.0f); }

CretConfig cret_default_config(void) {
    return (CretConfig){.world_seed = 1, .world_n = 2048, .world_cell = 4.0f, .num_agents = 16,
                        .max_ticks = 30 * 60 * 10, .day_seconds = 1200.0f, .seed = 1};
}

// ---------------------------------------------------------------- mover
typedef struct MoverCtx { CretSim* s; int self; b3CollisionPlane planes[16]; int count; } MoverCtx;
static bool mover_filter(b3ShapeId shape, void* c) {
    MoverCtx* m = c; return !B3_ID_EQUALS(b3Shape_GetBody(shape), m->s->agents[m->self].body);
}
static bool mover_planes(b3ShapeId shape, const b3PlaneResult* res, int n, void* c) {
    MoverCtx* m = c; if (!mover_filter(shape, c)) return true;
    for (int i = 0; i < n && m->count < 16; i++)
        m->planes[m->count++] = (b3CollisionPlane){.plane = res[i].plane, .pushLimit = FLT_MAX, .push = 0, .clipVelocity = true};
    return true;
}

static void spawn_point(CretSim* s, float* x, float* z) {
    const CretWorld* w = s->world; float S = w->size;
    for (int tries = 0; tries < 400; tries++) {
        float px = S * (0.1f + 0.8f * frand(&s->rng)), pz = S * (0.1f + 0.8f * frand(&s->rng));
        float h = cret_height(w, px, pz), wl = cret_water(w, px, pz);
        float n[3]; terra_normal(&w->map, px, pz, n);
        if (h > 2.0f && wl < h - 0.2f && n[1] > 0.85f) { *x = px; *z = pz; return; }
    }
    *x = S * 0.5f; *z = S * 0.5f;
}

void cret_sim_respawn(CretSim* s, int i) {
    CretAgent* a = &s->agents[i]; b3BodyId body = a->body;
    memset(a, 0, sizeof(*a)); a->body = body;
    a->alive = true; a->radius = 0.6f; a->height = 1.8f;
    a->walk_speed = 2.2f; a->sprint_speed = 8.5f; a->turn_rate = 2.6f; a->jump_speed = 5.0f; a->stamina = 1;
    spawn_point(s, &a->x, &a->z);
    a->y = cret_height(s->world, a->x, a->z); a->yaw = 6.2831853f * frand(&s->rng);
    b3Body_SetTransform(a->body, (b3Pos){a->x, a->y + a->height * 0.5f, a->z}, b3Quat_identity);
}

int cret_sim_init(CretSim* s, CretConfig cfg) {
    memset(s, 0, sizeof(*s)); s->cfg = cfg; s->rng = cfg.seed * 2654435761u + 1;
    if (cfg.num_agents > CRET_MAX_AGENTS) cfg.num_agents = s->cfg.num_agents = CRET_MAX_AGENTS;
    s->world = cret_world_acquire(cfg.world_seed, cfg.world_n, cfg.world_cell, 1);
    if (!s->world) return -1;
    b3WorldDef wd = b3DefaultWorldDef(); wd.gravity = (b3Vec3){0, -9.8f, 0};
    s->phys = b3CreateWorld(&wd);
    for (int i = 0; i < CRET_MAX_AGENTS; i++) {
        b3BodyDef bd = b3DefaultBodyDef(); bd.type = b3_kinematicBody; bd.position = (b3Pos){0, -1000.0f - i * 10, 0};
        s->agents[i].body = b3CreateBody(s->phys, &bd);
        b3ShapeDef sd = b3DefaultShapeDef(); sd.filter.categoryBits = CAT_AGENT; sd.filter.maskBits = CAT_AGENT | CAT_PROP;
        b3Capsule cap = {{0, -0.3f, 0}, {0, 0.3f, 0}, 0.6f};
        b3CreateCapsuleShape(s->agents[i].body, &sd, &cap);
    }
    cret_sim_reset(s);
    return 0;
}

void cret_sim_reset(CretSim* s) {
    s->tick = 0; s->time_of_day = 0.3f + 0.3f * frand(&s->rng);
    for (int i = 0; i < CRET_MAX_AGENTS; i++) {
        if (i < s->cfg.num_agents) cret_sim_respawn(s, i);
        else { s->agents[i].alive = false; }
    }
}

void cret_sim_close(CretSim* s) {
    if (b3World_IsValid(s->phys)) b3DestroyWorld(s->phys);
    cret_world_release(s->world); s->world = NULL;
}

static void move_agent(CretSim* s, int i, const CretInput* in) {
    CretAgent* a = &s->agents[i]; const CretWorld* w = s->world; float dt = CRET_DT;
    a->yaw += clampf(in->turn, -1, 1) * a->turn_rate * dt;
    float fwd = clampf(in->forward, -0.5f, 1), str = clampf(in->strafe, -1, 1) * 0.6f;
    int sprint = in->sprint && fwd > 0.5f && a->stamina > 0.05f && !a->swimming;
    float top = sprint ? a->sprint_speed : a->walk_speed * 2.0f;  // forward=1 -> trot
    if (a->swimming) top = a->walk_speed * 0.8f;
    float cy = cosf(a->yaw), sy = sinf(a->yaw);
    float dvx = (cy * fwd - sy * str) * top, dvz = (sy * fwd + cy * str) * top;
    // Slope: slower uphill, cliffs are impassable.
    float n[3]; terra_normal(&w->map, a->x, a->z, n);
    float up = -(n[0] * dvx + n[2] * dvz) / (fmaxf(1e-3f, sqrtf(dvx * dvx + dvz * dvz)));
    float steep = 1 - n[1];
    float slow = clampf(1 - up * steep * 3.0f, 0.15f, 1.15f);
    if (n[1] < 0.6f && up > 0) slow = 0;
    dvx *= slow; dvz *= slow;
    float accel = (a->on_ground || a->swimming ? 10.0f : 1.5f) * dt;
    a->vx += clampf(dvx - a->vx, -accel * top, accel * top);
    a->vz += clampf(dvz - a->vz, -accel * top, accel * top);
    a->stamina = clampf(a->stamina + (sprint ? -0.05f : 0.03f) * dt, 0, 1);
    // Vertical: gravity, jump, swim buoyancy.
    float ground = cret_height(w, a->x, a->z), water = cret_water(w, a->x, a->z);
    float depth = water - ground;
    a->swimming = depth > a->height * 0.65f;
    if (a->swimming) {
        float target = water - a->height * 0.6f;
        a->vy += (clampf(target - a->y, -2, 2) * 4.0f - a->vy * 2.0f) * dt;
    } else {
        a->vy -= 9.8f * dt;
        if (in->jump && a->on_ground) { a->vy = a->jump_speed; a->on_ground = false; }
    }
    // Horizontal collision with agents and props (Box3D mover); ground is analytic.
    MoverCtx ctx = {.s = s, .self = i};
    b3Capsule cap = {{0, a->radius, 0}, {0, a->height - a->radius, 0}, a->radius};
    b3QueryFilter filt = {.categoryBits = CAT_AGENT, .maskBits = CAT_AGENT | CAT_PROP, .id = 1, .name = "cret_mover"};
    b3Pos pos = {a->x, a->y + 0.05f, a->z};
    b3Vec3 target = {a->vx * dt, 0, a->vz * dt};
    for (int it = 0; it < 3; it++) {
        ctx.count = 0;
        b3World_CollideMover(s->phys, pos, &cap, filt, mover_planes, &ctx);
        b3Vec3 want = {target.x, 0, target.z};
        b3PlaneSolverResult r = b3SolvePlanes(want, ctx.planes, ctx.count);
        r.delta.y = 0;
        float f = b3World_CastMover(s->phys, pos, &cap, r.delta, filt, mover_filter, &ctx);
        pos.x += r.delta.x * f; pos.z += r.delta.z * f;
        target.x -= r.delta.x * f; target.z -= r.delta.z * f;
        if (target.x * target.x + target.z * target.z < 1e-6f) break;
    }
    if (ctx.count) { b3Vec3 v = b3ClipVector((b3Vec3){a->vx, 0, a->vz}, ctx.planes, ctx.count); a->vx = v.x; a->vz = v.z; }
    float S = w->size, ox = a->x, oz = a->z;
    a->x = clampf((float)pos.x, 2, S - 2); a->z = clampf((float)pos.z, 2, S - 2);
    a->y += a->vy * dt;
    float g = cret_height(w, a->x, a->z);
    if (!a->swimming && a->y <= g + 0.02f) { a->y = g; if (a->vy < 0) a->vy = 0; a->on_ground = true; }
    else if (!a->swimming && a->y - g < 0.35f && a->vy <= 0) { a->y = g; a->vy = 0; a->on_ground = true; } // stick to slopes
    else a->on_ground = false;
    if (a->swimming && a->y < g) a->y = g;
    a->distance += sqrtf((a->x - ox) * (a->x - ox) + (a->z - oz) * (a->z - oz));
    b3Body_SetTransform(a->body, (b3Pos){a->x, a->y + a->height * 0.5f, a->z}, b3Quat_identity);
}

void cret_sim_step(CretSim* s, const CretInput* inputs) {
    for (int i = 0; i < s->cfg.num_agents; i++) {
        if (!s->agents[i].alive) continue;
        move_agent(s, i, &inputs[i]); s->agents[i].ticks_alive++;
    }
    s->tick++;
    s->time_of_day += CRET_DT / s->cfg.day_seconds;
    if (s->time_of_day >= 1) s->time_of_day -= 1;
}

CretInput cret_decode_action(const float* act) {
    static const float throttle[5] = {-0.5f, 0, 0.5f, 1, 1}, turn[5] = {-1, -0.4f, 0, 0.4f, 1}, strafe[3] = {-1, 0, 1};
    int t = (int)act[0], r = (int)act[1], st = (int)act[2], j = (int)act[3];
    t = t < 0 ? 0 : t > 4 ? 4 : t; r = r < 0 ? 0 : r > 4 ? 4 : r; st = st < 0 ? 0 : st > 2 ? 2 : st;
    return (CretInput){.forward = throttle[t], .turn = turn[r], .strafe = strafe[st], .sprint = t == 4, .jump = j == 1};
}

void cret_sim_observe(const CretSim* s, int i, float* o) {
    const CretAgent* a = &s->agents[i]; const CretWorld* w = s->world; float S = w->size;
    memset(o, 0, sizeof(float) * CRET_OBS_SIZE);
    float cy = cosf(a->yaw), sy = sinf(a->yaw);
    float g = cret_height(w, a->x, a->z), wl = cret_water(w, a->x, a->z);
    float n[3]; terra_normal(&w->map, a->x, a->z, n);
    int k = 0;
    o[k++] = cy; o[k++] = sy;
    o[k++] = (cy * a->vx + sy * a->vz) / 10; o[k++] = (-sy * a->vx + cy * a->vz) / 10; o[k++] = a->vy / 10;
    o[k++] = a->x / S * 2 - 1; o[k++] = a->z / S * 2 - 1; o[k++] = g / 500;
    o[k++] = clampf((wl - g) / 5, -1, 2); o[k++] = a->swimming; o[k++] = a->on_ground; o[k++] = a->stamina;
    o[k++] = n[0]; o[k++] = n[2];
    o[k++] = sinf(6.2831853f * s->time_of_day); o[k++] = cosf(6.2831853f * s->time_of_day);
    int half = CRET_PATCH / 2;
    for (int pz = -half; pz <= half; pz++) for (int px = -half; px <= half; px++) {
        float lx = px * CRET_PATCH_SPACING, lz = pz * CRET_PATCH_SPACING;
        float wx = a->x + cy * lz - sy * lx, wz = a->z + sy * lz + cy * lx;
        float h = cret_height(w, wx, wz), wa = cret_water(w, wx, wz);
        o[k++] = clampf((h - g) / 20, -2, 2); o[k++] = clampf((wa - h) / 3, -1, 2);
    }
    // Nearest agents, egocentric.
    int best[CRET_NEIGHBORS]; float bd[CRET_NEIGHBORS];
    for (int q = 0; q < CRET_NEIGHBORS; q++) { best[q] = -1; bd[q] = 1e30f; }
    for (int j = 0; j < s->cfg.num_agents; j++) {
        if (j == i || !s->agents[j].alive) continue;
        float dx = s->agents[j].x - a->x, dz = s->agents[j].z - a->z, d = dx * dx + dz * dz;
        for (int q = 0; q < CRET_NEIGHBORS; q++) if (d < bd[q]) {
            for (int r = CRET_NEIGHBORS - 1; r > q; r--) { bd[r] = bd[r - 1]; best[r] = best[r - 1]; }
            bd[q] = d; best[q] = j; break;
        }
    }
    for (int q = 0; q < CRET_NEIGHBORS; q++) {
        if (best[q] < 0) { k += 5; continue; }
        const CretAgent* b = &s->agents[best[q]]; float dx = b->x - a->x, dz = b->z - a->z;
        o[k++] = 1; o[k++] = clampf((cy * dx + sy * dz) / 100, -2, 2); o[k++] = clampf((-sy * dx + cy * dz) / 100, -2, 2);
        o[k++] = clampf((b->y - a->y) / 20, -2, 2); o[k++] = cosf(b->yaw - a->yaw);
    }
}

CretInput cret_bot_input(CretSim* s, int i) {
    CretAgent* a = &s->agents[i]; CretInput in = {0};
    // Wander: keep heading with slow random turns, avoid deep water and cliffs.
    float cy = cosf(a->yaw), sy = sinf(a->yaw);
    float ax = a->x + cy * 12, az = a->z + sy * 12;
    float h = cret_height(s->world, ax, az), wl = cret_water(s->world, ax, az), g = cret_height(s->world, a->x, a->z);
    float noise = sinf(a->ticks_alive * 0.013f + i * 1.7f) + 0.5f * sinf(a->ticks_alive * 0.041f + i);
    in.turn = 0.35f * noise; in.forward = 0.5f;
    if (wl > h + 0.8f || h - g > 6) in.turn = (i & 1) ? 1 : -1;
    if ((a->ticks_alive / 300 + i) % 7 == 0) in.forward = 1;
    return in;
}
