#!/usr/bin/env python3
"""Reads the Cretaceous server's telemetry log (nettelem.c, docs/anticheat.md).

The log is what the server saw of each logged slot: a client's player, or a
bot or policy a tool asked to log, in the same records. Only a slot and a
player id per connection identify anyone.

    python3 tools/cret_telemetry.py FILE            # a summary per player
    python3 tools/cret_telemetry.py FILE --aim      # the aim-snap example below
    python3 tools/cret_telemetry.py FILE --check    # exit 1 unless every record is well formed

As a module: `header, records = read(path)`; each record is a dict with
`type` (one of TYPES), `slot`, `tick` and the type's fields.
"""
import struct
import sys

MAGIC = b"CRETTEL1"
FORMAT = 1
TYPES = {1: "join", 2: "leave", 3: "input", 4: "vis", 5: "world", 6: "swing", 7: "appear", 8: "ack", 9: "flag"}
CHECKS = ["ack of a snapshot never sent", "movement byte -128", "inputs faster than real time", "view in the future",
          "view far in the past", "input packet flood", "second character name"]
HOW = {0: "applied", 1: "caught up", 2: "held"}
ACT_ATTACK, ACT_ATTACK2 = 4, 7
TICK_HZ = 30


def _mask(b, n):
    return [i for i in range(n) if b[i // 8] >> (i % 8) & 1]


def _decode(t, p, ents):
    if t == "join":
        player, human, species = struct.unpack_from("<IBB", p)
        return {"player": player, "human": bool(human), "species": species}
    if t == "leave":
        return {"player": struct.unpack_from("<I", p)[0]}
    if t == "input":
        seq, how, fwd, strafe, turn, flags, aim, alive, view, x, y, z, yaw = struct.unpack_from("<IBbbbBbBfffff", p)
        return {"seq": seq, "how": HOW.get(how, how), "forward": fwd / 127, "strafe": strafe / 127, "turn": turn / 127, "aim": aim / 127,
                "sprint": bool(flags & 1), "jump": bool(flags & 2), "action": flags >> 2 & 7, "ability": flags >> 5 & 3, "carry": bool(flags & 128),
                "alive": bool(alive), "view": view, "x": x, "y": y, "z": z, "yaw": yaw, "raw": (fwd, strafe, turn, flags, aim)}
    if t == "vis":
        m = len(p) // 2
        return {"relevant": _mask(p[:m], ents), "seen": _mask(p[m:], ents)}
    if t == "world":
        n = struct.unpack_from("<H", p)[0]
        agents = []
        for i in range(n):
            x, z, yaw, fl, hp, sp = struct.unpack_from("<HHBBBB", p, 2 + 8 * i)
            agents.append({"x": x / 8, "z": z / 8, "yaw": yaw / 256 * 6.283185307, "alive": bool(fl & 1), "species": fl >> 1 & 7,
                           "action": fl >> 4 & 7, "health": hp / 255, "speed": sp / 4})
        return {"agents": agents}
    if t == "swing":
        act, landed, target, rel, seen, dist, back = struct.unpack_from("<BBHBBff", p)
        return {"action": act, "landed": bool(landed), "target": None if target == 0xFFFF else target, "target_relevant": bool(rel),
                "target_seen": bool(seen), "distance": dist, "rewind": back}
    if t == "appear":
        e, dist, bearing = struct.unpack_from("<Hff", p)
        return {"entity": e, "distance": dist, "bearing": bearing}
    if t == "ack":
        ack, ack_tick, newest, count, queue, rtt = struct.unpack_from("<IIIBBH", p)
        return {"ack": ack, "ack_tick": ack_tick, "newest": newest, "inputs": count, "queue": queue, "rtt_ms": rtt}
    if t == "flag":
        kind, count, value = struct.unpack_from("<BIf", p)
        return {"kind": CHECKS[kind] if kind < len(CHECKS) else kind, "count": count, "value": value}
    raise ValueError(t)


SIZES = {"join": 6, "leave": 4, "input": 31, "appear": 10, "swing": 14, "ack": 16, "flag": 9}


def read(path, strict=False):
    """The header (a dict) and the list of records. strict: raise on anything malformed."""
    with open(path, "rb") as f:
        d = f.read()
    if len(d) < 32 or d[:8] != MAGIC:
        raise ValueError(f"{path}: not a Cretaceous telemetry log")
    fmt, proto, tick_hz, snap_hz, agents, ents, seed, size, tick0 = struct.unpack_from("<HHHHHHIfI", d, 8)
    if fmt != FORMAT:
        raise ValueError(f"{path}: format {fmt}, this reader knows {FORMAT}")
    head = {"format": fmt, "protocol": proto, "tick_hz": tick_hz, "snapshot_hz": snap_hz, "agents": agents, "entities": ents,
            "world_seed": seed, "world_size": size, "start_tick": tick0}
    out, at = [], 32
    while at < len(d):
        if at + 8 > len(d):
            if strict:
                raise ValueError(f"record header cut at byte {at}")
            break
        ty, slot, n, tick = struct.unpack_from("<BBHI", d, at)
        p = d[at + 8:at + 8 + n]
        t = TYPES.get(ty)
        want = SIZES.get(t, (2 * ((ents + 7) // 8)) if t == "vis" else None)
        if t == "world" and n >= 2:
            want = 2 + 8 * struct.unpack_from("<H", p)[0]
        if t is None or len(p) != n or (want is not None and n != want):
            if strict:
                raise ValueError(f"bad record at byte {at}: type {ty}, {n} bytes")
            break
        r = {"type": t, "slot": slot, "tick": tick}
        r.update(_decode(t, p, ents))
        out.append(r)
        at += 8 + n
    return head, out


def players(records):
    """Records per player: the slot's records from its JOIN to its LEAVE, keyed by player id."""
    cur, per = {}, {}
    for r in records:
        if r["type"] == "join":
            cur[r["slot"]] = r["player"]
            per[r["player"]] = {"slot": r["slot"], "human": r["human"], "records": []}
        p = cur.get(r["slot"])
        if p is not None:
            per[p]["records"].append(r)
        if r["type"] == "leave":
            cur.pop(r["slot"], None)
    return per


def summary(head, records):
    print(f"protocol {head['protocol']}, {head['agents']} agents, {head['tick_hz']} Hz ticks, {head['snapshot_hz']} Hz snapshots, "
          f"{len(records)} records")
    for pid, p in sorted(players(records).items()):
        rs = p["records"]
        n = {t: sum(1 for r in rs if r["type"] == t) for t in TYPES.values()}
        held = sum(1 for r in rs if r["type"] == "input" and r["how"] == "held")
        rtt = [r["rtt_ms"] for r in rs if r["type"] == "ack"]
        flags = {}
        for r in rs:
            if r["type"] == "flag":
                flags[r["kind"]] = r["count"]
        landed = sum(1 for r in rs if r["type"] == "swing" and r["landed"])
        rtt_s = f"rtt {sum(rtt) / len(rtt):.0f} ms" if rtt else "no acks"
        print(f"player {pid} (slot {p['slot']}, {'person' if p['human'] else 'bot'}): {n['input']} inputs ({held} held), {n['vis']} vis, "
              f"{n['appear']} came into sight, {n['swing']} swings ({landed} landed), {rtt_s}" + "".join(f", {k}: {v}" for k, v in flags.items()))


def aim_snaps(head, records, snap=1.0, window_s=1.5, reach=3.0):
    """The example for classifiers: per player, how fast the aim moves.

    The aim is the head's turn from the body (-1..1 over the species' neck
    arc), set by the camera, so a flick of the mouse moves it in one tick and
    an aim-bot's snap does too: what tells them apart is how often it
    happens, how it lands and how soon after a target appears. Printed per
    player: the per-tick change of the aim (median, 95th and 99th
    percentiles, largest), snaps (a change of at least `snap` in one tick) a
    minute, the share of snaps followed by a swing within 10 ticks, the
    swings at a target within `reach` m of the strike point that was out of
    plain sight, and the reaction time from a
    target coming into plain sight to the first tick the player turns or
    aims toward it (within window_s).
    """
    for pid, p in sorted(players(records).items()):
        inp = [r for r in p["records"] if r["type"] == "input" and r["how"] != "held" and r["alive"]]
        if len(inp) < 2:
            continue
        d = [abs(b["aim"] - a["aim"]) for a, b in zip(inp, inp[1:]) if b["tick"] - a["tick"] <= 1]
        ds = sorted(d)
        q = (lambda f: ds[min(int(f * len(ds)), len(ds) - 1)]) if ds else (lambda f: 0)
        snaps = [b["tick"] for a, b in zip(inp, inp[1:]) if b["tick"] - a["tick"] <= 1 and abs(b["aim"] - a["aim"]) >= snap]
        swings = [r for r in p["records"] if r["type"] == "swing"]
        press = sorted(r["tick"] for r in inp if r["action"] in (ACT_ATTACK, ACT_ATTACK2))
        snap_swing = sum(1 for t in snaps if any(t <= u <= t + 10 for u in press))
        unseen = sum(1 for r in swings if r["target"] is not None and r["distance"] <= reach and not r["target_seen"])
        by_tick = {r["tick"]: r for r in inp}
        react = []
        for a in (r for r in p["records"] if r["type"] == "appear"):
            side = 1 if a["bearing"] > 0 else -1
            for k in range(1, int(window_s * TICK_HZ) + 1):
                r, prev = by_tick.get(a["tick"] + k), by_tick.get(a["tick"] + k - 1)
                if r and prev and (r["turn"] * side > 0.5 or (r["aim"] - prev["aim"]) * side > 0.2):
                    react.append(k * 1000 / TICK_HZ)
                    break
        minutes = max(len(inp) / TICK_HZ / 60, 1e-9)
        rt = sorted(react)
        print(f"player {pid} (slot {p['slot']}, {'person' if p['human'] else 'bot'}): {len(inp)} ticks; aim change per tick "
              f"median {q(0.5):.3f}, p95 {q(0.95):.3f}, p99 {q(0.99):.3f}, max {ds[-1] if ds else 0:.3f}; "
              f"{len(snaps) / minutes:.1f} snaps/min ({snap_swing} followed by a swing); "
              f"{len(swings)} swings, {unseen} at a target in reach but out of plain sight; "
              f"reaction to a target appearing: {len(rt)} of {sum(1 for r in p['records'] if r['type'] == 'appear')}"
              + (f", median {rt[len(rt) // 2]:.0f} ms" if rt else ""))


def main(argv):
    if len(argv) < 2 or argv[1].startswith("-"):
        print(__doc__)
        return 2
    try:
        head, records = read(argv[1], strict="--check" in argv)
    except (OSError, ValueError, struct.error) as e:
        print(f"cret_telemetry: {e}", file=sys.stderr)
        return 1
    if "--check" in argv:
        print(f"cret_telemetry: {argv[1]}: {len(records)} records, all well formed")
        return 0
    if "--aim" in argv:
        aim_snaps(head, records)
    else:
        summary(head, records)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
