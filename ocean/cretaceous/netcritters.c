// Critters on the wire (protocol v12; net_priv.h has the layout). Included by
// net.c, so the build lists need no new file.
//
// The server is authoritative: critters live only in its sim. Each snapshot
// carries the peer's NET_CRIT_MAX nearest critters within NET_CRIT_RANGE of
// its animal, whole (no baselines or acks): position as 1/16 m offsets from
// a whole-metre reference near the animal, heading, height above the ground
// (a bird flying off), size within the kind's mass range, speed and state.
// The client replaces its critters with the newest set (netp_client_apply),
// putting each on its own terrain; the renderer smooths the steps. A critter
// left out of a snapshot (eaten, gone, or now further than others) vanishes
// from the client until it is sent again.

static int crit_kg_q(const CretCritter* c) {
    const CretCritterKind* K = &CRET_CRITTER_KINDS[c->kind];
    return (int)clampf(lroundf((c->kg - K->kg_min) / fmaxf(K->kg_max - K->kg_min, 1e-3f) * 63), 0, 63);
}

void netp_put_critters(Bits* b, const CretSim* s, int slot) {
    const CretAgent* me = &s->agents[slot];
    int pick[NET_CRIT_MAX]; float pd[NET_CRIT_MAX]; int n = 0;
    float r2 = NET_CRIT_RANGE * NET_CRIT_RANGE;
    for (int k = 0; k < CRET_MAX_CRITTERS && s->ncrit; k++) {   // the nearest, kept sorted
        const CretCritter* c = &s->critters[k]; if (!c->active || c->kind >= CRET_CR_KINDS) continue;
        float dx = c->x - me->x, dz = c->z - me->z, d = dx * dx + dz * dz;
        if (d > r2 || !isfinite(d) || (n == NET_CRIT_MAX && d >= pd[n - 1])) continue;
        int at = n < NET_CRIT_MAX ? n++ : n - 1;
        while (at > 0 && pd[at - 1] > d) { pd[at] = pd[at - 1]; pick[at] = pick[at - 1]; at--; }
        pd[at] = d; pick[at] = k;
    }
    // Whole metres near the animal: offsets stay within 12 bits (+-128 m).
    uint32_t rx = (uint32_t)clampf(floorf(isfinite(me->x) ? me->x : 0), 0, 65535), rz = (uint32_t)clampf(floorf(isfinite(me->z) ? me->z : 0), 0, 65535);
    bits_put(b, (uint32_t)n, NET_CRIT_COUNT_BITS); bits_put(b, rx, 16); bits_put(b, rz, 16);
    for (int q = 0; q < n; q++) {
        const CretCritter* c = &s->critters[pick[q]];
        int32_t dx = (int32_t)lroundf((c->x - (float)rx) * NET_CRIT_Q), dz = (int32_t)lroundf((c->z - (float)rz) * NET_CRIT_Q);
        dx = dx < -2048 ? -2048 : dx > 2047 ? 2047 : dx; dz = dz < -2048 ? -2048 : dz > 2047 ? 2047 : dz;
        float yaw = isfinite(c->yaw) ? c->yaw : 0; yaw -= 6.2831853f * floorf(yaw / 6.2831853f);
        bits_put(b, (uint32_t)pick[q], NET_CRIT_SLOT_BITS); bits_put(b, c->kind, 3); bits_put(b, c->state < CRET_CRS_COUNT ? c->state : 0, 3);
        bits_put(b, (uint32_t)dx & 0xfff, 12); bits_put(b, (uint32_t)dz & 0xfff, 12);
        bits_put(b, (uint32_t)lroundf(yaw / 6.2831853f * 64) & 63, 6);
        bits_put(b, (uint32_t)clampf(lroundf(c->alt * 2), 0, 15), 4); bits_put(b, (uint32_t)crit_kg_q(c), 6);
        bits_put(b, (uint32_t)clampf(lroundf(c->speed * 2), 0, 15), 4);
    }
}

int netp_get_critters(Bits* b, CretCritter* out, uint16_t* slots) {
    int n = (int)bits_get(b, NET_CRIT_COUNT_BITS); float rx = (float)bits_get(b, 16), rz = (float)bits_get(b, 16);
    if (n > NET_CRIT_MAX) { b->bad = true; return 0; }
    for (int q = 0; q < n && !b->bad; q++) {
        CretCritter c = {.active = 1, .threat = -1};
        uint32_t slot = bits_get(b, NET_CRIT_SLOT_BITS); c.kind = (uint8_t)bits_get(b, 3); c.state = (uint8_t)bits_get(b, 3);
        int32_t dx = sext(bits_get(b, 12), 12), dz = sext(bits_get(b, 12), 12);
        c.yaw = bits_get(b, 6) * (6.2831853f / 64); c.alt = bits_get(b, 4) * 0.5f;
        int kq = (int)bits_get(b, 6); c.speed = bits_get(b, 4) * 0.5f;
        if (slot >= CRET_MAX_CRITTERS || c.kind >= CRET_CR_KINDS || c.state >= CRET_CRS_COUNT) { b->bad = true; break; }
        const CretCritterKind* K = &CRET_CRITTER_KINDS[c.kind];
        c.x = rx + dx / NET_CRIT_Q; c.z = rz + dz / NET_CRIT_Q; c.kg = K->kg_min + (K->kg_max - K->kg_min) * kq / 63.0f; c.hp = K->hp;
        out[q] = c; slots[q] = (uint16_t)slot;
    }
    return b->bad ? 0 : n;
}

// The client's critters become the newest set the server sent.
void netp_client_apply_critters(NetClient* c, CretSim* s) {
    memset(s->critters, 0, sizeof s->critters); s->ncrit = 0;
    for (int q = 0; q < c->ncrit; q++) {
        int k = c->crit_slot[q] % CRET_MAX_CRITTERS; CretCritter t = c->crit[q];
        if (s->world) {
            float h = cret_height(s->world, t.x, t.z), w = cret_water(s->world, t.x, t.z);
            t.y = t.kind == CRET_CR_FISH && w > h ? w - fminf(0.25f, 0.5f * (w - h)) : h;
        }
        if (!s->critters[k].active) s->ncrit++;
        s->critters[k] = t;
    }
}
