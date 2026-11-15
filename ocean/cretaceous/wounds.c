// Wounds that last (sim.h, CRET_WOUND_*). Included by sim.c after eco.c; uses
// sim.c's inline maths and body_near. No random numbers: where a blow lands
// and how bad it is follow from the blow, so the sim's stream is unchanged.

// Which place a blow from attacker a wounds on victim b: the hamstring
// ability and an attacker much smaller than b go for the legs, one much
// taller bites down on the body; otherwise it depends on where the blow
// comes from (the nearest point of the attacker's body, so a tail swing comes
// from the tail): from in front the head, from behind the hind legs, from
// the side the body.
static int wound_place(const CretAgent* a, const CretAgent* b) {
    if (a->strike == CRET_AB_HAMSTRING && a->act == CRET_ACT_ATTACK) return CRET_WOUND_LEG;
    float ha = fmaxf(a->height, 0.1f), hb = fmaxf(b->height, 0.1f);
    if (ha < CRET_WOUND_SMALL * hb) return CRET_WOUND_LEG;
    if (ha > CRET_WOUND_TALL * hb) return CRET_WOUND_BODY;
    float ox, oz; body_near(a, b->x, b->z, &ox, &oz);
    float ang = fabsf(wrapa(atan2f(oz - b->z, ox - b->x) - b->yaw));
    return ang < CRET_WOUND_FRONT ? CRET_WOUND_HEAD : ang > CRET_WOUND_REAR ? CRET_WOUND_LEG : CRET_WOUND_BODY;
}

// A blow of hp (after armour) from attacker on victim: a wound if it is hard
// enough (damage() calls it before the victim can die of it).
static void wound_hit(CretSim* s, int attacker, int victim, float hp) {
    CretAgent* b = &s->agents[victim]; float frac = hp / fmaxf(b->max_health, 1);
    if (frac < CRET_WOUND_MIN || attacker == victim) return;
    int p = wound_place(&s->agents[attacker], b);
    CretWound* w = &b->wound[p];
    w->sev = fminf(w->sev + fminf(frac / CRET_WOUND_FULL, 1), 1); w->tick = s->tick;
    s->stats.wounds[b->species][p]++;
}

// Over dt seconds from survive_body (the full sim: a tick; a far group,
// fargroup.c: a second): wounds heal by how the animal lives (fed and
// resting fastest; hungry slowly) and worsen while it starves or is parched.
// Returns what the body wound bleeds (hp a second), which survive_body
// takes off with the rest of its losses.
static float wound_step(CretSim* s, CretAgent* a, float dt) {
    bool starving = (a->food <= 0.05f && a->fat <= 0.1f) || a->water <= 0;
    bool fed = a->food > 0.3f && a->water > 0.3f;
    float rate = starving ? -CRET_WOUND_WORSEN : !fed ? CRET_WOUND_HEAL_HUNGRY : a->act == CRET_ACT_REST ? CRET_WOUND_HEAL_REST : CRET_WOUND_HEAL_MOVE;
    float step = rate * dt / fmaxf(s->cfg.day_seconds, 1);
    for (int p = 0; p < CRET_WOUND_PLACES; p++) {
        CretWound* w = &a->wound[p]; if (w->sev <= 0) continue;
        w->sev = fminf(w->sev - step, 1);
        if (w->sev <= 0) *w = (CretWound){0};
    }
    return a->max_health * CRET_WOUND_BODY_BLEED * a->wound[CRET_WOUND_BODY].sev;
}
