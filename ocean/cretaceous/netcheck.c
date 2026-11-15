// Server checks (net.h, CRET_AC_*; docs/anticheat.md). Included by net.c.
// Each check looks for something an unmodified client never sends, however
// bad its link: loss, jitter, reordering and duplicates change when inputs
// arrive and how many come at once, never what they claim. LOG counts and
// logs each finding (one line per peer and kind every NET_AC_REPORT_S at
// most, and a telemetry record); ENFORCE also refuses what the finding would
// gain. Nothing here kicks anyone: that is for a person or a classifier
// with more evidence to decide.

const char* const CRET_AC_NAMES[CRET_AC_KINDS] = {
    "ack of a snapshot never sent", "movement byte -128", "inputs faster than real time", "view in the future",
    "view far in the past", "input packet flood", "second character name",
};

void netp_check(CretNet* n, NetPeer* np, int kind, int tick, float value) {
    if (!n->checks || kind < 0 || kind >= CRET_AC_KINDS) return;
    uint32_t c = ++np->ac_count[kind]; n->sst.flags++;
    if (c == 1 || tick - np->ac_said[kind] >= NET_AC_REPORT_S * CRET_TICK_HZ) {
        np->ac_said[kind] = tick;
        fprintf(stderr, "cretaceous: check: slot %d (player %u): %s (%u so far, %.1f)\n", np->slot, np->player, CRET_AC_NAMES[kind], c, value);
    }
    netp_tl_flag(n, tick, np->slot, kind, c, value);
}

void netp_check_packet(CretNet* n, NetPeer* np, const CretSim* s) {
    if (!n->checks) return;
    if (np->rx_bad_byte) netp_check(n, np, CRET_AC_BAD_BYTE, s->tick, 0);
    if (np->rx_ack_future) netp_check(n, np, CRET_AC_ACK_FUTURE, s->tick, (float)(np->rx_ack - np->seq));
    // A client numbers its inputs by its own ticks from the welcome, at the
    // server's tick rate, so the newest it claims can lead the ticks since the
    // join only by its clock's drift (well under 0.1% for a real clock) and
    // what it queued while its link was slow. A sped-up client (a speed hack)
    // numbers them faster.
    double allow = (double)(s->tick - np->join_tick) * (1 + NET_AC_RATE_SLACK) + NET_AC_RATE_TICKS;
    if ((double)np->rx_newest > allow) netp_check(n, np, CRET_AC_INPUT_CLOCK, s->tick, (float)((double)np->rx_newest - allow));
    // The packet rate over windows of NET_AC_FLOOD_S: a client sends one a
    // tick; duplicates on the link add a few percent, a stall and its
    // catch-up move packets around within the window.
    np->win_packets++;
    int span = s->tick - np->win_tick;
    if (span >= NET_AC_FLOOD_S * CRET_TICK_HZ) {
        float rate = (float)np->win_packets / (float)span;
        if (rate > NET_AC_FLOOD_RATE) netp_check(n, np, CRET_AC_FLOOD, s->tick, rate);
        np->win_tick = s->tick; np->win_packets = 0;
    }
}

// ENFORCE: the input the server would apply next is ahead of real time by
// the same allowance. A sped-up client's queue is what the server's catch-up
// (two inputs a tick past NET_BUFFER_MAX) would otherwise play out at double
// speed; refused, those inputs wait their turn at one a tick.
bool netp_check_clock(CretNet* n, NetPeer* np, const CretSim* s, uint32_t next) {
    if (n->checks != CRET_AC_ENFORCE) return false;
    return (double)next > (double)(s->tick - np->join_tick) * (1 + NET_AC_RATE_SLACK) + NET_AC_RATE_TICKS;
}

// The client names its character once, as it joins (cretaceous.c). A second
// name loads another character mid-session: another player's growth and
// marks, or this one's saved state again after a bad fight.
void netp_check_profile(CretNet* n, NetPeer* np, CretSim* s, const uint8_t* d, size_t len) {
    if (np->named && n->checks) {
        netp_check(n, np, CRET_AC_RENAME, s->tick, 0);
        if (n->checks == CRET_AC_ENFORCE) return;
    }
    if (netp_server_profile(np, s, d, len)) np->named = true;
}

void cret_net_set_checks(CretNet* n, int mode) { if (n) n->checks = mode < CRET_AC_OFF ? CRET_AC_OFF : mode > CRET_AC_ENFORCE ? CRET_AC_ENFORCE : mode; }

bool cret_net_check_counts(const CretNet* n, int slot, uint32_t out[CRET_AC_KINDS]) {
    for (int k = 0; n && k < CRET_MAX_PEERS; k++)
        if (n->peers[k] && n->peers[k]->slot == slot) { memcpy(out, n->peers[k]->ac_count, sizeof n->peers[k]->ac_count); return true; }
    return false;
}
