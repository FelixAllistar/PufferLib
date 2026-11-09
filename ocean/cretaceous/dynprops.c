// Movable props: boulders, big stones, logs and driftwood as Box3D dynamic
// bodies. Included by sim.c (it shares sim.c's static helpers: kill,
// break_bone, MoverCtx), so the build lists need no new file.
//
// Lifecycle. world.c creates every movable prop asleep at its scattered pose
// when its tile streams in (userData = collider id + 1). An agent walking
// into one pushes it (dyn_push, from the mover), which wakes it and gives it
// a record (CretSim.dyn). From then on the sim owns the body: the tile no
// longer creates or destroys it, and the record keeps its pose. Records of
// props at rest far from every agent drop their body and get it back when an
// agent comes near (cret_dyn_stream). The physics world steps only while a
// record is awake, after the agents move (dyn_step); woken bodies and those
// knocked into motion by others are picked up from Box3D's move events.
//
// Pushing. Walking into a movable prop pushes it with a force budget of
// strength x agent mass x g, scaled by how hard the animal leans in
// (0.4 at a standstill to 1.3 at a sprint), as an impulse at the contact
// point (so boulders roll and logs turn), never faster than the animal
// moves into it. Running into one also hands over the momentum of a
// collision of the two masses (restitution 0.2): a sprinting tyrannosaur
// sends a boulder off, a raptor kicks a stone ahead of it. Friction does the rest: a stegosaur or tyrannosaur shoves
// a 2 t boulder, a raptor only stones and small logs. A prop under 8% of
// the animal's mass, or below its knee and under three quarters of its
// mass, does not block it: the animal steps over it and kicks it aside.
//
// Crushing. A prop moving faster than CRUSH_SPEED into an animal (the
// closing speed along the line between them, net of the animal's own
// motion away) hurts it by the collision energy: fraction of max health =
// 0.5 mu v^2 / (victim mass x 25 J/kg), mu the reduced mass, ignored below
// 2%; a third or more breaks a bone. Resting or pushing contact never
// hurts. The last agent to push the prop within PUSH_CREDIT_TICKS gets the
// hit and the kill; otherwise the death is the environment's.
#include <stdint.h>

#define PUSH_CREDIT_TICKS (5 * CRET_TICK_HZ)
#define CRUSH_SPEED 2.5f        // m/s: slower props never hurt
#define CRUSH_J_PER_KG 25.0f    // collision energy per kg of victim that takes all its health
#define KICK_FRACTION 0.08f     // props lighter than this fraction of the animal do not block it
#define DYN_MAX_AWAKE_TICKS (20 * CRET_TICK_HZ)   // a prop still moving after this is put to sleep

// Push strength by species (x mass x g): the stocky quadrupeds lean in hardest.
static const float PUSH_STRENGTH[CRET_SPECIES] = {
    [CRET_SP_RAPTOR] = 0.8f, [CRET_SP_STEGO] = 1.2f, [CRET_SP_TREX] = 1.1f, [CRET_SP_PACHY] = 1.3f, [CRET_SP_SAUROPELTA] = 1.3f, [CRET_SP_GALLI] = 0.5f};
float cret_push_strength(int species) { return species >= 0 && species < CRET_SPECIES ? PUSH_STRENGTH[species] : 1.0f; }

static int dyn_id_of(b3BodyId body) {
    intptr_t u = (intptr_t)b3Body_GetUserData(body);
    return u > 0 ? (int)(u - 1) : -1;
}

int cret_dyn_find(const CretSim* s, int id) {
    for (int k = 0; k < s->ndyn; k++) if (s->dyn[k].id == id) return k;
    return -1;
}
static bool dyn_owned(void* ctx, int id) { return cret_dyn_find((const CretSim*)ctx, id) >= 0; }

static void dyn_read_body(CretDynProp* r) {
    b3WorldTransform t = b3Body_GetTransform(r->body); b3Vec3 v = b3Body_GetLinearVelocity(r->body);
    r->p[0] = (float)t.p.x; r->p[1] = (float)t.p.y; r->p[2] = (float)t.p.z;
    r->q[0] = t.q.v.x; r->q[1] = t.q.v.y; r->q[2] = t.q.v.z; r->q[3] = t.q.s;
    r->v[0] = v.x; r->v[1] = v.y; r->v[2] = v.z;
}

// A record for collider id (its body, if it has one, becomes the sim's).
// When all are taken, the oldest record without a body (at rest, nobody
// near) is dropped: that prop is back at its scattered pose next time.
static CretDynProp* dyn_record(CretSim* s, int id, b3BodyId body) {
    int k = cret_dyn_find(s, id);
    if (k >= 0) return &s->dyn[k];
    if (s->ndyn == CRET_MAX_DYN) {
        int old = -1;
        for (int j = 0; j < s->ndyn; j++) if (B3_IS_NULL(s->dyn[j].body) && (old < 0 || s->dyn[j].touched < s->dyn[old].touched)) old = j;
        if (old < 0) return NULL;
        s->dyn[old] = s->dyn[--s->ndyn];
    }
    CretDynProp* r = &s->dyn[s->ndyn++]; memset(r, 0, sizeof *r);
    const CretPropCollider* pc = &s->world->props[id];
    r->id = id; r->pusher = -1; r->push_tick = -100000; r->touched = s->tick; r->body = body;
    r->p[0] = pc->x; r->p[1] = pc->y; r->p[2] = pc->z; memcpy(r->q, pc->q, sizeof r->q);
    if (B3_IS_NON_NULL(body) && b3Body_IsValid(body)) dyn_read_body(r);
    return r;
}

void cret_dyn_set(CretSim* s, int id, const float p[3], const float q[4], bool awake) {
    if (!s->world || id < 0 || id >= s->world->nprops || !s->world->props[id].dyn) return;
    CretDynProp* r = dyn_record(s, id, b3_nullBodyId);
    if (!r) return;
    // A pristine body from the tile: adopt it (the tile skips owned props from now on).
    if (B3_IS_NULL(r->body)) {
        int ps = s->props.ns;
        for (int t = 0; t < ps && B3_IS_NULL(r->body); t++) for (int k = 0; k < s->props.s[t].n; k++)
            if (s->props.s[t].ids[k] == id && b3Body_IsValid(s->props.s[t].bodies[k])) { r->body = s->props.s[t].bodies[k]; break; }
    }
    memcpy(r->p, p, sizeof r->p); memcpy(r->q, q, sizeof r->q); r->awake = awake; r->touched = s->tick;
    if (B3_IS_NON_NULL(r->body) && b3Body_IsValid(r->body)) {
        b3Body_SetTransform(r->body, (b3Pos){p[0], p[1], p[2]}, (b3Quat){{q[0], q[1], q[2]}, q[3]});
        if (awake) { b3Body_SetAwake(r->body, true); s->dyn_kick = true; }
    }
}

void cret_dyn_stream(CretSim* s) {
    for (int k = 0; k < s->ndyn; k++) {
        CretDynProp* r = &s->dyn[k];
        bool has = B3_IS_NON_NULL(r->body) && b3Body_IsValid(r->body);
        if (!has) r->body = b3_nullBodyId;
        bool near = cret_prop_stream_loaded(&s->props, r->p[0], r->p[2]);
        if (has && !r->awake && !near) { b3DestroyBody(r->body); r->body = b3_nullBodyId; }
        else if (!has && near) { r->body = cret_prop_body(s->world, s->phys, r->id, r->p, r->q, r->awake); if (r->awake) s->dyn_kick = true; }
        // Something rolling keeps the colliders around it (trees, rocks) loaded.
        if (r->awake) cret_prop_stream_touch(&s->props, s->world, s->phys, r->p[0], r->p[2], 8.0f, s->tick);
    }
}

// ---------------------------------------------------------------- pushing
// From the mover's plane callback: a movable prop touching agent i.
static void dyn_note_contact(MoverCtx* m, b3ShapeId shape, b3BodyId body, const b3PlaneResult* res) {
    for (int k = 0; k < m->npush; k++) if (B3_ID_EQUALS(m->push[k].body, body)) return;
    if (m->npush == (int)(sizeof m->push / sizeof m->push[0])) return;
    // Planes are relative to the query origin: the agent's feet (+5 cm) as the
    // move starts (later iterations are a few cm on; this only sets the lever arm).
    const CretAgent* a = &m->s->agents[m->self];
    m->push[m->npush++] = (MoverPush){body, res->plane.normal, {res->point.x + a->x, res->point.y + a->y + 0.05f, res->point.z + a->z}};
}
// True for a movable prop the animal kicks aside or steps over instead of
// being stopped by: one under 8% of its mass, or one below its knee (35% of
// its height, at most 0.6 m) and under three quarters of its mass.
static bool dyn_kickable(const CretSim* s, int i, b3BodyId body) {
    const CretAgent* a = &s->agents[i]; float m = b3Body_GetMass(body), ma = cret_mass(a);
    if (m < KICK_FRACTION * ma) return true;
    int id = dyn_id_of(body); if (id < 0 || m >= 0.75f * ma) return false;
    const CretPropCollider* pc = &s->world->props[id];
    return s->world->prop_cat.height[pc->variant] * pc->scale < fminf(0.35f * a->height, 0.6f);
}

// After agent i moved (not when predicting): shove what it walked into.
// (vx, vz): its velocity before the planes clipped it. Against a face the
// push runs along the contact normal; a kicked prop (or one under the
// animal) goes the way from the animal to its centre.
static void dyn_push(CretSim* s, int i, const MoverCtx* m, float vx, float vz) {
    CretAgent* a = &s->agents[i];
    for (int k = 0; k < m->npush; k++) {
        b3BodyId body = m->push[k].body; int id = dyn_id_of(body);
        if (id < 0 || !b3Body_IsValid(body)) continue;
        b3Vec3 n = m->push[k].normal; float h = sqrtf(n.x * n.x + n.z * n.z), dx, dz;
        if (h >= 0.2f && !dyn_kickable(s, i, body)) { dx = -n.x / h; dz = -n.z / h; }   // from the animal into the prop
        else {
            b3Pos c = b3Body_GetWorldCenter(body); float cx = c.x - a->x, cz = c.z - a->z, cl = sqrtf(cx * cx + cz * cz);
            if (cl < 1e-3f) continue;
            dx = cx / cl; dz = cz / cl;
        }
        float into = vx * dx + vz * dz;
        if (into < 0.2f) continue;
        // Sustained: a shove up to the force budget, toward the animal's speed.
        float lean = 0.4f + 0.6f * clampf(into / fmaxf(a->trot_speed, 0.5f), 0, 1.5f);
        float force = cret_push_strength(a->species) * cret_mass(a) * 9.8f * lean;
        float mass = b3Body_GetMass(body), ma = cret_mass(a);
        b3Vec3 v = b3Body_GetLinearVelocity(body); float vn = v.x * dx + v.z * dz;
        float J = fminf(mass * (into - vn), force * CRET_DT);
        // Impact: running into it (at last tick's actual speed) hands over the
        // momentum of a collision between the two masses (restitution 0.2).
        float vl = sqrtf(vx * vx + vz * vz), closing = vl > 1e-3f ? a->speed * into / vl : 0;
        J = fmaxf(J, mass * ma / (mass + ma) * fmaxf(closing - vn, 0) * 1.2f);
        J = clampf(J, 0, mass * fmaxf(1.2f * into - vn, 0));
        if (J <= 0) continue;
        CretDynProp* r = dyn_record(s, id, body);
        if (!r) continue;
        r->pusher = (int8_t)i; r->push_tick = s->tick; r->awake = 1; r->touched = s->tick;
        b3Vec3 pt = m->push[k].point;
        b3Body_ApplyLinearImpulse(body, (b3Vec3){dx * J, 0, dz * J}, (b3Pos){pt.x, pt.y, pt.z}, true);
        s->dyn_kick = true;
    }
}

// ---------------------------------------------------------------- crushing
typedef struct { b3BodyId body; bool hit; } CrushQuery;
static bool crush_overlap(b3ShapeId shape, void* c) {
    CrushQuery* q = c; if (B3_ID_EQUALS(b3Shape_GetBody(shape), q->body)) { q->hit = true; return false; }
    return true;
}

static void crush(CretSim* s, CretDynProp* r, int j, float frac, float nx, float ny, float nz, float closing, float mp) {
    CretAgent* b = &s->agents[j]; const CretSpecies* SB = &CRET_SPECIES_TABLE[b->species];
    float mb = cret_mass(b);
    int credit = r->pusher >= 0 && r->pusher < s->cfg.num_agents && r->pusher != j && s->tick - r->push_tick <= PUSH_CREDIT_TICKS ? r->pusher : -1;
    float hp = fminf(frac * b->max_health * (1 - 0.5f * SB->armor), b->health);
    b->health -= hp; b->hurt_ticks = 12; b->last_attacker = credit;
    float f = hp / fmaxf(b->max_health, 1); b->ev_taken += f;
    if (credit >= 0) {
        s->agents[credit].ev_dealt += f;
        cret_log_hit(s, credit, j, CRET_HIT_CRUSH, f);
        cret_quest_on_damage(s, credit, j, f);
    }
    // Knocked along the hit; the prop loses the momentum it gave.
    float dv = fminf(closing * mp / (mp + mb) * 1.2f, 9.0f), hl = sqrtf(nx * nx + nz * nz);
    if (hl > 1e-3f) { b->vx += nx / hl * dv; b->vz += nz / hl * dv; }
    if (dv > 1.5f && b->on_ground && !b->swimming) { b->vy = dv * 0.4f; b->on_ground = false; }
    float J = mp * mb / (mp + mb) * closing * 0.6f;
    b3Body_ApplyLinearImpulseToCenter(r->body, (b3Vec3){-nx * J, -ny * J, -nz * J}, true);
    if (frac > 0.33f) break_bone(b, 45.0f + 45.0f * frand(&s->rng));
    if (b->health <= 0.01f) kill(s, j);
}

static void dyn_crush(CretSim* s, CretDynProp* r) {
    float sp2 = r->v[0] * r->v[0] + r->v[1] * r->v[1] + r->v[2] * r->v[2];
    if (sp2 < CRUSH_SPEED * CRUSH_SPEED) return;
    const CretWorld* w = s->world; const CretPropCollider* pc = &w->props[r->id];
    float reach = w->prop_cat.radius[pc->variant] * pc->scale + 0.5f, mp = b3Body_GetMass(r->body);
    b3Pos com = b3Body_GetWorldCenter(r->body);
    for (int j = 0; j < s->cfg.num_agents; j++) {
        CretAgent* b = &s->agents[j]; if (!b->alive) continue;
        float dx = b->x - com.x, dz = b->z - com.z, R = reach + b->radius;
        if (dx * dx + dz * dz > R * R || b->y > com.y + reach || b->y + b->height < com.y - reach) continue;
        bool cool = false;
        for (int c = 0; c < 4; c++) if (r->hit_agent[c] == j + 1 && s->tick - r->hit_tick[c] < 20) cool = true;
        if (cool) continue;
        // The animal's capsule, a little fattened, against the prop's own shapes.
        b3Vec3 pts[2] = {{0, b->radius, 0}, {0, fmaxf(b->height - b->radius, b->radius), 0}};
        b3ShapeProxy proxy = {pts, 2, b->radius + 0.1f};
        b3QueryFilter f = {.categoryBits = CRET_CAT_AGENT, .maskBits = CRET_CAT_DYN, .id = 4, .name = "cret_crush"};
        CrushQuery q = {r->body, false};
        b3World_OverlapShape(s->phys, (b3Pos){b->x, b->y, b->z}, &proxy, f, crush_overlap, &q);
        if (!q.hit) continue;
        float cy = b->y + 0.5f * b->height, ny = cy - com.y, l = sqrtf(dx * dx + ny * ny + dz * dz);
        if (l < 1e-3f) continue;
        float nx = dx / l, nyy = ny / l, nz = dz / l;
        float closing = r->v[0] * nx + r->v[1] * nyy + r->v[2] * nz - fmaxf(0, b->vx * nx + b->vy * nyy + b->vz * nz);
        if (closing < CRUSH_SPEED) continue;
        float mb = cret_mass(b), mu = mp * mb / (mp + mb), frac = 0.5f * mu * closing * closing / (mb * CRUSH_J_PER_KG);
        if (frac < 0.02f) continue;
        int c = 0; for (int k = 1; k < 4; k++) if (r->hit_tick[k] < r->hit_tick[c]) c = k;
        r->hit_agent[c] = (uint8_t)(j + 1); r->hit_tick[c] = s->tick;
        crush(s, r, j, frac, nx, nyy, nz, closing, mp);
    }
}

// ---------------------------------------------------------------- step
static void dyn_init(CretSim* s) {
    s->ground = cret_world_add_ground(s->world, s->phys);
    s->props.owned = dyn_owned; s->props.owned_ctx = s;
    b3World_SetMaximumLinearSpeed(s->phys, 40.0f);
}

// Back to the scatter: every record's body goes, tiles reload with pristine props.
static void dyn_reset(CretSim* s) {
    cret_prop_stream_trim(&s->props, s->tick, -1);
    for (int k = 0; k < s->ndyn; k++) if (B3_IS_NON_NULL(s->dyn[k].body) && b3Body_IsValid(s->dyn[k].body)) b3DestroyBody(s->dyn[k].body);
    memset(s->dyn, 0, sizeof s->dyn); s->ndyn = 0; s->dyn_awake = 0; s->dyn_kick = false;
}

static int dyn_speed_cmp(const void* a, const void* b) {
    const float* x = a; const float* y = b; return x[0] < y[0] ? -1 : x[0] > y[0] ? 1 : x[1] < y[1] ? -1 : x[1] > y[1];
}

static void dyn_step(CretSim* s) {
    cret_dyn_stream(s);
    if (!s->dyn_awake && !s->dyn_kick) return;
    s->dyn_kick = false;
    b3World_Step(s->phys, CRET_DT, 4);
    b3BodyEvents ev = b3World_GetBodyEvents(s->phys);
    for (int e = 0; e < ev.moveCount; e++) {
        const b3BodyMoveEvent* m = &ev.moveEvents[e];
        intptr_t u = (intptr_t)m->userData; if (u <= 0) continue;   // agents
        int id = (int)(u - 1);
        CretDynProp* r = dyn_record(s, id, m->bodyId);
        if (!r) {   // no room to track it: back where it lay, asleep
            const CretPropCollider* pc = &s->world->props[id];
            b3Body_SetTransform(m->bodyId, (b3Pos){pc->x, pc->y, pc->z}, (b3Quat){{pc->q[0], pc->q[1], pc->q[2]}, pc->q[3]});
            b3Body_SetLinearVelocity(m->bodyId, b3Vec3_zero); b3Body_SetAngularVelocity(m->bodyId, b3Vec3_zero);
            b3Body_SetAwake(m->bodyId, false);
            continue;
        }
        r->body = m->bodyId; dyn_read_body(r); r->touched = s->tick;
        r->awake = !m->fellAsleep;
    }
    // Activity limits: long-rolling props and the slowest beyond the cap sleep.
    float order[CRET_MAX_DYN][2]; int n = 0;
    for (int k = 0; k < s->ndyn; k++) {
        CretDynProp* r = &s->dyn[k];
        if (r->awake && B3_IS_NON_NULL(r->body) && b3Body_IsValid(r->body) && !b3Body_IsAwake(r->body)) r->awake = 0;
        if (!r->awake) { r->awake_ticks = 0; continue; }
        if (++r->awake_ticks > DYN_MAX_AWAKE_TICKS) { b3Body_SetAwake(r->body, false); r->awake = 0; r->v[0] = r->v[1] = r->v[2] = 0; continue; }
        order[n][0] = r->v[0] * r->v[0] + r->v[1] * r->v[1] + r->v[2] * r->v[2]; order[n][1] = (float)k; n++;
    }
    if (n > CRET_DYN_AWAKE_MAX) {
        qsort(order, n, sizeof order[0], dyn_speed_cmp);
        for (int q = 0; q < n - CRET_DYN_AWAKE_MAX; q++) {
            CretDynProp* r = &s->dyn[(int)order[q][1]];
            b3Body_SetAwake(r->body, false); r->awake = 0; r->v[0] = r->v[1] = r->v[2] = 0;
        }
        n = CRET_DYN_AWAKE_MAX;
    }
    s->dyn_awake = n;
    for (int k = 0; k < s->ndyn; k++) if (s->dyn[k].awake && B3_IS_NON_NULL(s->dyn[k].body)) dyn_crush(s, &s->dyn[k]);
}
