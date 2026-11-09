// Movable props on the wire (protocol v11; net_priv.h has the layout).
// Included by net.c, so the build lists need no new file.
//
// Server: per peer, a table of the props the client knows or is being told
// about: the state it acknowledged, the last one sent, and an interest
// priority (as entities). Each snapshot sends, best first, the props whose
// current pose differs from the acknowledged one (or from an unacknowledged
// send), up to NET_PROP_MAX and NET_PROP_BUDGET bytes. A prop whose record
// the server dropped is sent once as gone. Props at rest and acknowledged
// cost nothing; a send in flight is repeated only after 8 snapshots.
//
// Client: a few samples per prop by server tick. netp_client_apply puts each
// at its newest pose in the client sim (asleep: collisions for prediction,
// no physics); the draw copy interpolates them at the render tick like
// remote agents.

#define PROP_TAB (2 * NET_PROPS)
static const NetPropQ PROP_GONE = {.gone = 1};

static bool pq_eq(const NetPropQ* a, const NetPropQ* b) {
    return a->gone == b->gone && (a->gone || (a->x == b->x && a->y == b->y && a->z == b->z && a->q == b->q && a->rest == b->rest));
}
static uint32_t prop_hash(int32_t id) { uint32_t k = (uint32_t)id * 2654435761u; return (k ^ (k >> 16)) & (PROP_TAB - 1); }
static void pe_insert(NetPeer* np, int i) {
    uint32_t h = prop_hash(np->pe[i].id);
    while (np->pidx[h]) h = (h + 1) & (PROP_TAB - 1);
    np->pidx[h] = (int16_t)(i + 1);
}
static void pe_rehash(NetPeer* np) { memset(np->pidx, 0, sizeof np->pidx); for (int i = 0; i < np->npe; i++) pe_insert(np, i); }
static int pe_find(const NetPeer* np, int32_t id) {
    for (uint32_t h = prop_hash(id);; h = (h + 1) & (PROP_TAB - 1)) {
        int i = np->pidx[h] - 1;
        if (i < 0) return -1;
        if (i < np->npe && np->pe[i].id == id) return i;
    }
}

void netp_prop_quant(const NetSchema* sc, const float p[3], const float q[4], bool rest, NetPropQ* o) {
    memset(o, 0, sizeof *o);
    o->x = qpos(p[0], sc->pos_bits); o->y = qpos(p[1] + NET_Y_OFS, 21); o->z = qpos(p[2], sc->pos_bits);
    float a[4], l = 0; int m = 0;
    for (int i = 0; i < 4; i++) { a[i] = isfinite(q[i]) ? q[i] : 0; l += a[i] * a[i]; }
    if (l < 1e-8f) { a[0] = a[1] = a[2] = 0; a[3] = 1; l = 1; }
    l = 1 / sqrtf(l);
    for (int i = 0; i < 4; i++) { a[i] *= l; if (fabsf(a[i]) > fabsf(a[m])) m = i; }
    // Smallest three: the largest component (made positive) is implied.
    float sg = a[m] < 0 ? -1.0f : 1.0f; uint32_t v = (uint32_t)m << 30; int sh = 20;
    for (int i = 0; i < 4; i++) {
        if (i == m) continue;
        float u = sg * a[i] / 0.70710678f * 0.5f + 0.5f;
        v |= (uint32_t)clampf(lroundf(u * 1023), 0, 1023) << sh; sh -= 10;
    }
    o->q = v; o->rest = rest;
}
void netp_prop_pose(const NetPropQ* o, float p[3], float q[4]) {
    p[0] = o->x / NET_POS_Q; p[1] = o->y / NET_POS_Q - NET_Y_OFS; p[2] = o->z / NET_POS_Q;
    int m = (int)(o->q >> 30), sh = 20; float s = 0;
    for (int i = 0; i < 4; i++) {
        if (i == m) continue;
        q[i] = (((o->q >> sh) & 1023u) / 1023.0f - 0.5f) * 2 * 0.70710678f; s += q[i] * q[i]; sh -= 10;
    }
    q[m] = sqrtf(fmaxf(0.0f, 1 - s));
    float l = 1 / sqrtf(s + q[m] * q[m]);
    for (int i = 0; i < 4; i++) q[i] *= l;
}

static int prop_bits(const NetSchema* sc, const NetPropQ* q) { return NET_PROP_ID_BITS + 1 + (q->gone ? 0 : 2 * sc->pos_bits + 21 + 32 + 1); }
static void put_prop(Bits* b, const NetSchema* sc, int32_t id, const NetPropQ* q) {
    bits_put(b, (uint32_t)id, NET_PROP_ID_BITS); bits_put(b, q->gone, 1);
    if (q->gone) return;
    bits_put(b, q->x, sc->pos_bits); bits_put(b, q->y, 21); bits_put(b, q->z, sc->pos_bits); bits_put(b, q->q, 32); bits_put(b, q->rest, 1);
}

void netp_put_props(Bits* b, CretNet* n, NetPeer* np, const CretSim* s, uint32_t seq, size_t limit) {
    int h = (int)(seq % NET_HIST); np->npsent[h] = 0;
    const NetSchema* sc = &n->sc; const CretAgent* me = &s->agents[np->slot];
    // Current state of every prop the peer knows or should: the sim's records, the rest gone.
    for (int i = 0; i < np->npe; i++) { np->pe[i].live = 0; np->pe[i].cur = PROP_GONE; }
    for (int k = 0; k < s->ndyn; k++) {
        const CretDynProp* r = &s->dyn[k];
        if (r->id < 0 || r->id >= (1 << NET_PROP_ID_BITS)) continue;
        int i = pe_find(np, r->id);
        if (i < 0) {
            if (np->npe == NET_PROPS) { n->sst.props_full++; continue; }
            i = np->npe++; memset(&np->pe[i], 0, sizeof np->pe[i]); np->pe[i].id = r->id; pe_insert(np, i);
        }
        NetPropEnt* e = &np->pe[i];
        netp_prop_quant(sc, r->p, r->q, !r->awake, &e->cur); e->live = 1;
    }
    // Forget props that are gone and either never sent or acknowledged gone.
    int w = 0;
    for (int i = 0; i < np->npe; i++) {
        const NetPropEnt* e = &np->pe[i];
        bool drop = !e->live && (e->last_seq == 0 || (e->ack_seq >= e->last_seq && e->ack.gone));
        if (!drop) np->pe[w++] = *e;
    }
    if (w != np->npe) { np->npe = w; pe_rehash(np); }
    // Candidates by accumulated interest.
    float cand[NET_PROPS][2]; int nc = 0;
    for (int i = 0; i < np->npe; i++) {
        NetPropEnt* e = &np->pe[i];
        const NetPropQ* A = e->ack_seq ? &e->ack : &PROP_GONE;
        bool unacked = e->last_seq > e->ack_seq;
        bool in_flight = unacked && pq_eq(&e->cur, &e->last) && seq - e->last_seq <= 8;
        bool needs = !in_flight && (!pq_eq(&e->cur, A) || (unacked && !pq_eq(&e->cur, &e->last)));
        float d = 0;
        if (!e->cur.gone) { float dx = e->cur.x / NET_POS_Q - me->x, dz = e->cur.z / NET_POS_Q - me->z; d = sqrtf(dx * dx + dz * dz); }
        e->prio += netp_interest(d);
        if (e->cur.gone != e->last.gone || e->cur.rest != e->last.rest) e->prio += 1.0f;   // coming to rest, or gone: at once
        if (!needs) { if (e->prio > 2) e->prio = 2; continue; }
        if (e->prio >= 1.0f) { cand[nc][0] = (float)i; cand[nc][1] = e->prio; nc++; }
    }
    qsort(cand, (size_t)nc, sizeof cand[0], cmp_prio);
    size_t count_pos = b->pos, p0 = b->pos; bits_put(b, 0, 5);
    int count = 0;
    for (int c = 0; c < nc; c++) {
        NetPropEnt* e = &np->pe[(int)cand[c][0]];
        if (count == NET_PROP_MAX || b->pos + (size_t)prop_bits(sc, &e->cur) > limit) { n->sst.props_full++; break; }
        put_prop(b, sc, e->id, &e->cur);
        np->psent[h][count] = (NetPropSent){e->id, e->cur};
        e->last = e->cur; e->last_seq = seq; e->prio = 0;
        count++;
    }
    size_t end = b->pos; b->pos = count_pos; bits_put(b, (uint32_t)count, 5); b->pos = end;
    np->npsent[h] = (uint8_t)count;
    n->sst.props_sent += (uint64_t)count; n->sst.prop_bits += end - p0;
}

void netp_props_acked(NetPeer* np, int h, uint32_t seq) {
    for (int j = 0; j < np->npsent[h]; j++) {
        int i = pe_find(np, np->psent[h][j].id); if (i < 0) continue;
        NetPropEnt* e = &np->pe[i];
        if (seq > e->ack_seq) { e->ack = np->psent[h][j].q; e->ack_seq = seq; }
    }
}

int netp_get_props(Bits* b, const NetSchema* sc, NetPropSent* out) {
    int n = (int)bits_get(b, 5);
    for (int k = 0; k < n && !b->bad; k++) {
        NetPropSent* p = &out[k]; memset(p, 0, sizeof *p);
        p->id = (int32_t)bits_get(b, NET_PROP_ID_BITS); p->q.gone = (uint8_t)bits_get(b, 1);
        if (p->q.gone) continue;
        p->q.x = bits_get(b, sc->pos_bits); p->q.y = bits_get(b, 21); p->q.z = bits_get(b, sc->pos_bits);
        p->q.q = bits_get(b, 32); p->q.rest = (uint8_t)bits_get(b, 1);
    }
    return b->bad ? -1 : n;
}

// ---------------------------------------------------------------- client
static NetPropCl* cl_prop(NetClient* c, int32_t id) {
    for (int k = 0; k < c->nprops; k++) if (c->props[k].id == id) return &c->props[k];
    return NULL;
}
static void cl_gone(NetClient* c, int32_t id, uint32_t seq) {
    if (c->npgone < NET_PROP_TOMBS) c->pgone[c->npgone++] = id;
    c->tomb_id[c->ntomb % NET_PROP_TOMBS] = id; c->tomb_seq[c->ntomb % NET_PROP_TOMBS] = seq; c->ntomb++;
}
// A gone newer than seq was seen for id (a late snapshot must not revive it).
static bool cl_tomb(const NetClient* c, int32_t id, uint32_t seq) {
    uint32_t n = c->ntomb < NET_PROP_TOMBS ? c->ntomb : NET_PROP_TOMBS;
    for (uint32_t k = 0; k < n; k++) if (c->tomb_id[k] == id && c->tomb_seq[k] > seq) return true;
    return false;
}

void netp_client_props(NetClient* c, const NetPropSent* p, int n, uint32_t tick, uint32_t seq) {
    for (int k = 0; k < n; k++) {
        NetPropCl* e = cl_prop(c, p[k].id);
        if (e && seq < e->seq) {   // older than what we have: only a sample for interpolation
            if (p[k].q.gone) continue;
        } else if (p[k].q.gone) {
            if (e) *e = c->props[--c->nprops];
            cl_gone(c, p[k].id, seq);
            continue;
        }
        if (!e) {
            if (c->nprops == NET_PROPS || cl_tomb(c, p[k].id, seq)) continue;
            e = &c->props[c->nprops++]; memset(e, 0, sizeof *e); e->id = p[k].id;
        }
        // Samples sorted by tick, the oldest dropped when full.
        NetPropSample add = {tick, seq, p[k].q}; NetPropSample* S = e->s; int ns = e->ns;
        int at = ns; while (at > 0 && S[at - 1].tick > tick) at--;
        if (at > 0 && S[at - 1].tick == tick) S[at - 1] = add;
        else if (!(ns == NET_PROP_SAMP && at == 0)) {
            if (ns == NET_PROP_SAMP) { memmove(S, S + 1, sizeof *S * (size_t)(ns - 1)); ns--; at--; }
            memmove(S + at + 1, S + at, sizeof *S * (size_t)(ns - at));
            S[at] = add; ns++;
        }
        e->ns = ns;
        if (seq > e->seq) { e->seq = seq; e->dirty = true; }
    }
}

void netp_client_apply_props(NetClient* c, CretSim* s) {
    for (int k = 0; k < c->npgone; k++) cret_dyn_drop(s, c->pgone[k]);
    c->npgone = 0;
    for (int k = 0; k < c->nprops; k++) {
        NetPropCl* e = &c->props[k];
        if (!e->dirty || !e->ns) continue;
        e->dirty = false;
        float p[3], q[4]; netp_prop_pose(&e->s[e->ns - 1].q, p, q);
        cret_dyn_set(s, e->id, p, q, false);
    }
    if (s->ndyn) cret_dyn_stream(s);
}

void netp_client_draw_props(NetClient* c, CretSim* out, double R) {
    for (int k = 0; k < out->ndyn; k++) {
        CretDynProp* r = &out->dyn[k];
        NetPropCl* e = cl_prop(c, r->id);
        if (e && e->ns) {
            const NetPropSample* S = e->s; int ns = e->ns, j = ns - 1;
            while (j > 0 && (double)S[j].tick > R) j--;
            float p0[3], q0[4]; netp_prop_pose(&S[j].q, p0, q0);
            if (j < ns - 1 && (double)S[j].tick <= R) {   // between two samples
                float p1[3], q1[4]; netp_prop_pose(&S[j + 1].q, p1, q1);
                float t = (float)((R - (double)S[j].tick) / (double)(S[j + 1].tick - S[j].tick));
                float d = fabsf(p1[0] - p0[0]) + fabsf(p1[1] - p0[1]) + fabsf(p1[2] - p0[2]);
                if (d < 10) {
                    float dot = q0[0] * q1[0] + q0[1] * q1[1] + q0[2] * q1[2] + q0[3] * q1[3], sg = dot < 0 ? -1.0f : 1.0f, l = 0;
                    for (int i = 0; i < 3; i++) p0[i] += (p1[i] - p0[i]) * t;
                    for (int i = 0; i < 4; i++) { q0[i] += (sg * q1[i] - q0[i]) * t; l += q0[i] * q0[i]; }
                    if (l > 1e-12f) { l = 1 / sqrtf(l); for (int i = 0; i < 4; i++) q0[i] *= l; }
                }
            }
            memcpy(r->p, p0, sizeof r->p); memcpy(r->q, q0, sizeof r->q);
        }
        memcpy(r->pp, r->p, sizeof r->pp); memcpy(r->pq, r->q, sizeof r->pq);
    }
}
