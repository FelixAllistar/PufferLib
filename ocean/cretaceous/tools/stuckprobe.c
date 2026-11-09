// Stuck probe: plays scenarios headless and, whenever an animal is told to
// move but has not moved for half a second, prints what holds it: each
// collider touching a slightly larger capsule (tree trunk, static prop,
// movable prop, another animal) or, with none, the ground slope under it.
//   make -C ocean/cretaceous stuckprobe && ./build/cretaceous/stuckprobe [NAME...] [--seed N]
// Run from the repository root.
#include "../sim.h"
#include "../scenario.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { CretSim* s; int self; int n; char line[1024]; } Probe;

static const char* kind_name(int k) {
    static const char* N[] = {"boulder", "stone", "log", "stump", "bush", "other5", "other6", "other7"};
    return k >= 0 && k < 8 ? N[k] : "?";
}

static bool hit(b3ShapeId shape, const b3PlaneResult* res, int n, void* c) {
    Probe* p = c; b3BodyId body = b3Shape_GetBody(shape); CretSim* s = p->s; (void)res;
    if (B3_ID_EQUALS(body, s->agents[p->self].body)) return true;
    char buf[200]; b3Pos bp = b3Body_GetPosition(body); b3ShapeType t = b3Shape_GetType(shape);
    uint64_t cat = b3Shape_GetFilter(shape).categoryBits;
    const CretAgent* a = &s->agents[p->self];
    if (cat & CRET_CAT_AGENT) {
        int j = -1; for (int k = 0; k < s->cfg.num_agents; k++) if (B3_ID_EQUALS(body, s->agents[k].body)) j = k;
        snprintf(buf, sizeof buf, " [animal %d]", j);
    } else if (t == b3_capsuleShape) {
        b3Capsule cap = b3Shape_GetCapsule(shape);
        float cx = (float)bp.x + cap.center1.x, cz = (float)bp.z + cap.center1.z;
        snprintf(buf, sizeof buf, " [trunk r%.2f at %+.1f,%+.1f base y%.1f (ground %.1f)]", cap.radius, cx - a->x, cz - a->z, (float)bp.y + cap.center1.y - cap.radius, cret_height(s->world, cx, cz));
    } else {
        int id = -1;
        if (cat & CRET_CAT_DYN) id = (int)(intptr_t)b3Body_GetUserData(body) - 1;
        else for (int q = 0; q < s->props.ns && id < 0; q++) for (int k = 0; k < s->props.s[q].n; k++) if (B3_ID_EQUALS(s->props.s[q].bodies[k], body)) { id = s->props.s[q].ids[k]; break; }
        const CretWorld* w = s->world;
        if (id >= 0) snprintf(buf, sizeof buf, " [%s %s %s v%d s%.2f at %+.1f,%+.1f y%.1f (ground %.1f) shape %d]", cat & CRET_CAT_DYN ? "dyn" : "static", kind_name(w->props[id].kind),
                              w->prop_cat.name[w->props[id].variant], w->props[id].variant, w->props[id].scale, (float)bp.x - a->x, (float)bp.z - a->z, (float)bp.y, cret_height(w, (float)bp.x, (float)bp.z), (int)t);
        else snprintf(buf, sizeof buf, " [body ? shape %d at %+.1f,%+.1f]", (int)t, (float)bp.x - a->x, (float)bp.z - a->z);
    }
    if (n > 0 && strlen(p->line) + strlen(buf) < sizeof p->line - 1) strcat(p->line, buf);
    p->n += n > 0;
    return true;
}

int main(int argc, char** argv) {
    char names[64][48]; int n = 0; uint32_t seed = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (uint32_t)atoi(argv[++i]);
        else if (n < 64) snprintf(names[n++], 48, "%s", argv[i]);
    }
    if (!n) n = cret_scenario_list(names, 64);
    CretSim* s = calloc(1, sizeof *s); CretScnRun* r = calloc(1, sizeof *r);
    static int still[CRET_MAX_AGENTS];
    for (int k = 0; k < n; k++) {
        CretScenario sc; char err[256];
        if (!cret_scenario_load(names[k], &sc, err, sizeof err)) { fprintf(stderr, "%s: %s\n", names[k], err); continue; }
        CretConfig c = cret_scenario_config(&sc, cret_default_config()); c.seed = seed;
        if (cret_sim_init(s, c) != 0) return 1;
        if (!cret_scenario_start(r, &sc, s)) { cret_sim_close(s); continue; }
        printf("== %s at %.0f %.0f\n", sc.name, r->ox, r->oz);
        memset(still, 0, sizeof still); int reports = 0;
        CretInput in[CRET_MAX_AGENTS];
        while (!r->done) {
            cret_scenario_inputs(r, s, in);
            float px[CRET_MAX_AGENTS], pz[CRET_MAX_AGENTS];
            for (int i = 0; i < s->cfg.num_agents; i++) { px[i] = s->agents[i].x; pz[i] = s->agents[i].z; }
            cret_sim_step(s, in); cret_scenario_after_step(r, s);
            for (int i = 0; i < s->cfg.num_agents; i++) {
                CretAgent* a = &s->agents[i];
                // Bots pick their own inputs inside the step; judge them by the speed they ask for.
                float want = in[i].forward; if (r->group_of[i] < 0 || !a->alive) continue;
                bool bot = want == 0 && a->bot_state != 0;
                float moved = hypotf(a->x - px[i], a->z - pz[i]) * CRET_TICK_HZ;
                bool trying = want > 0.4f || (bot && a->speed > 0.5f);
                still[i] = trying && moved < 0.3f && !cret_act_is_attack(a->act) ? still[i] + 1 : 0;
                if (still[i] != CRET_TICK_HZ / 2 || reports >= 40) continue;
                reports++;
                Probe p = {s, i, 0, ""};
                b3Capsule cap = {{0, a->radius, 0}, {0, a->height - a->radius, 0}, a->radius + 0.15f};
                b3QueryFilter f = {.categoryBits = CRET_CAT_AGENT, .maskBits = CRET_CAT_AGENT | CRET_CAT_PROP, .id = 9, .name = "probe"};
                b3World_CollideMover(s->phys, (b3Pos){a->x, a->y + 0.05f, a->z}, &cap, f, hit, &p);
                float nn[3]; terra_normal(&s->world->map, a->x, a->z, nn);
                printf("  t=%5.1f agent %d %s (%s) at %.1f %.1f y%.1f ground %.1f air%d leap%d ny %.2f st%d act%d v%.2f:%s\n", r->tick / (float)CRET_TICK_HZ, i, CRET_SPECIES_TABLE[a->species].asset,
                       r->group_of[i] == 0 ? "player" : sc.actor[r->group_of[i] - 1].name, a->x, a->z, a->y, cret_height(s->world, a->x, a->z), !a->on_ground, a->leap, nn[1], a->bot_state, a->act,
                       hypotf(a->vx, a->vz), p.n ? p.line : " nothing touching");
            }
        }
        cret_sim_close(s);
    }
    free(r); free(s);
    return 0;
}
