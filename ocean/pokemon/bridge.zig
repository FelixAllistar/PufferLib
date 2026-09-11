const std = @import("std");
const pkmn = @import("pkmn");
const catalog = @import("catalog.zig");
const Battle = pkmn.gen1.Battle(pkmn.gen1.PRNG);
const State = struct {
    battle: Battle,
    result: pkmn.Result = .{},
    // Public identities are assigned in order of first appearance, never hidden party order.
    seen: [2][6]u8 = @splat(@splat(0)),
    moves: [2][6][4]u8 = @splat(@splat(@splat(0))),
    last_move: [2]u8 = @splat(0),
    last_actor: [2]u8 = @splat(0),
};
comptime {
    std.debug.assert(@sizeOf(State) <= 512 and @alignOf(State) <= 8);
    std.debug.assert(catalog.sets.len < 65535);
}

// Only public move/switch events are retained. Other observations are projected
// from state below; opaque RNG, sleep/confusion/binding durations never escape.
const Events = struct {
    pub const Error = error{};
    s: *State,
    pub fn move(self: Events, args: anytype) error{}!void {
        const id = args[0];
        const p = @intFromEnum(id.player);
        const i = id.id - 1;
        const m = @intFromEnum(args[1]);
        self.s.last_move[p] = m;
        self.s.last_actor[p] = id.id;
        for (&self.s.moves[p][i]) |*known| {
            if (known.* == m) break;
            if (known.* == 0) { known.* = m; break; }
        }
    }
    pub fn switched(self: Events, args: anytype) error{}!void {
        const seen = &self.s.seen[@intFromEnum(args[0].player)];
        const i = args[0].id - 1;
        if (seen[i] == 0) {
            var maximum: u8 = 0;
            for (seen) |n| maximum = @max(maximum, n);
            seen[i] = maximum + 1;
        }
    }
    pub fn activate(_: Events, _: anytype) error{}!void {}
    pub fn boost(_: Events, _: anytype) error{}!void {}
    pub fn cant(_: Events, _: anytype) error{}!void {}
    pub fn clearallboost(_: Events, _: anytype) error{}!void {}
    pub fn crit(_: Events, _: anytype) error{}!void {}
    pub fn curestatus(_: Events, _: anytype) error{}!void {}
    pub fn damage(_: Events, _: anytype) error{}!void {}
    pub fn end(_: Events, _: anytype) error{}!void {}
    pub fn fail(_: Events, _: anytype) error{}!void {}
    pub fn faint(_: Events, _: anytype) error{}!void {}
    pub fn fieldactivate(_: Events, _: anytype) error{}!void {}
    pub fn heal(_: Events, _: anytype) error{}!void {}
    pub fn hitcount(_: Events, _: anytype) error{}!void {}
    pub fn immune(_: Events, _: anytype) error{}!void {}
    pub fn lastmiss(_: Events, _: anytype) error{}!void {}
    pub fn laststill(_: Events, _: anytype) error{}!void {}
    pub fn miss(_: Events, _: anytype) error{}!void {}
    pub fn mustrecharge(_: Events, _: anytype) error{}!void {}
    pub fn ohko(_: Events, _: anytype) error{}!void {}
    pub fn prepare(_: Events, _: anytype) error{}!void {}
    pub fn resisted(_: Events, _: anytype) error{}!void {}
    pub fn start(_: Events, _: anytype) error{}!void {}
    pub fn status(_: Events, _: anytype) error{}!void {}
    pub fn supereffective(_: Events, _: anytype) error{}!void {}
    pub fn tie(_: Events, _: anytype) error{}!void {}
    pub fn transform(_: Events, _: anytype) error{}!void {}
    pub fn turn(_: Events, _: anytype) error{}!void {}
    pub fn win(_: Events, _: anytype) error{}!void {}
};

fn advance(s: *State, c1: pkmn.Choice, c2: pkmn.Choice) c_int {
    s.last_move = @splat(0);
    s.last_actor = @splat(0);
    var options = pkmn.battle.options(Events{ .s = s }, pkmn.gen1.chance.NULL, pkmn.gen1.calc.NULL);
    s.result = s.battle.update(c1, c2, &options) catch return 4;
    return @intFromEnum(s.result.type);
}
export fn pk_start(raw: *align(8) [512]u8, seed: u64, teams: *const [12]u16) c_int {
    var mons: [12]pkmn.gen1.helpers.Pokemon = undefined;
    for (teams, 0..) |set, i| {
        if (set >= catalog.sets.len) return 4;
        mons[i] = catalog.sets[set];
        for (teams[(i / 6) * 6 .. i]) |previous| {
            if (catalog.sets[previous].species == mons[i].species) return 4;
        }
    }
    @memset(raw, 0);
    const s: *State = @ptrCast(raw);
    s.* = .{ .battle = pkmn.gen1.helpers.Battle.init(seed, mons[0..6], mons[6..12]) };
    return advance(s, .{}, .{});
}
fn action(s: *const State, p: usize, c: pkmn.Choice) usize {
    return switch (c.type) {
        .Pass => 10,
        .Move => if (c.data == 0) 11 else c.data - 1,
        .Switch => 4 + @as(usize, s.battle.sides[p].order[c.data - 1]) - 1,
    };
}
fn choices(s: *const State, p: usize, out: *[pkmn.CHOICES_SIZE]pkmn.Choice) u8 {
    if (s.result.type != .None) return 0;
    return s.battle.choices(@enumFromInt(p), if (p == 0) s.result.p1 else s.result.p2, out);
}
export fn pk_mask(raw: *align(8) const [512]u8, player: c_int, out: *[160]u8) void {
    @memset(out, 0);
    if (player < 0 or player > 1) return;
    const s: *const State = @ptrCast(raw);
    const p: usize = @intCast(player);
    var cs: [pkmn.CHOICES_SIZE]pkmn.Choice = undefined;
    for (cs[0..choices(s, p, &cs)]) |c| out[action(s, p, c)] = 1;
}
export fn pk_update(raw: *align(8) [512]u8, p1: c_int, p2: c_int) c_int {
    const s: *State = @ptrCast(raw);
    if (s.result.type != .None) return @intFromEnum(s.result.type);
    var selected: [2]pkmn.Choice = undefined;
    for ([_]c_int{p1, p2}, 0..) |a, p| {
        var found = false;
        var cs: [pkmn.CHOICES_SIZE]pkmn.Choice = undefined;
        for (cs[0..choices(s, p, &cs)]) |c| {
            if (a == action(s, p, c)) { selected[p] = c; found = true; break; }
        }
        // Reject invalid choices BEFORE mutating the engine.
        if (!found) return -1;
    }
    return advance(s, selected[0], selected[1]);
}
fn status(bits: u8) u8 {
    if (bits & 7 != 0) return 1; // Sleep class, never remaining duration.
    if (bits & 8 != 0) return if (bits & 128 != 0) 6 else 2;
    if (bits & 16 != 0) return 3;
    if (bits & 32 != 0) return 4;
    if (bits & 64 != 0) return 5;
    return 0;
}
fn scaled(v: u16) u8 { return @intCast(@min(v / 4, 255)); }
export fn pk_observe(raw: *align(8) const [512]u8, player: c_int, out: *[640]u8) void {
    @memset(out, 0);
    if (player < 0 or player > 1) return;
    const s: *const State = @ptrCast(raw);
    const observer: usize = @intCast(player);
    out[0] = 1; // Battle phase.
    out[2] = @intCast(@min(s.battle.turn, 255));
    out[3] = @intCast(s.battle.turn >> 8);
    for (0..2) |relative| {
        const p = observer ^ relative;
        const side = &s.battle.sides[p];
        out[4 + relative] = if (relative == 0) side.order[0] else s.seen[p][side.order[0] - 1];
        out[6 + relative] = @intFromEnum(if (p == 0) s.result.p1 else s.result.p2);
        out[8 + relative] = s.last_move[p];
        out[10 + relative] = if (relative == 0 or s.last_actor[p] == 0)
            s.last_actor[p] else s.seen[p][s.last_actor[p] - 1];
        for (side.pokemon, 0..) |mon, i| {
            if (relative == 1 and s.seen[p][i] == 0) continue;
            const display: usize = if (relative == 0) i else s.seen[p][i] - 1;
            const base = 16 + relative * 192 + display * 32;
            out[base] = @intFromEnum(mon.species);
            out[base + 1] = @intCast((@as(u32, mon.hp) * 255 + mon.stats.hp - 1) / mon.stats.hp);
            out[base + 2] = status(mon.status);
            out[base + 3] = @intFromBool(mon.hp == 0);
            out[base + 4] = mon.level;
            out[base + 5] = @intFromEnum(mon.types.type1);
            out[base + 6] = @intFromEnum(mon.types.type2);
            if (relative == 0) {
                const moves = if (side.order[0] == i + 1) side.active.moves else mon.moves;
                for (moves, 0..) |m, j| { out[base + 8 + j] = @intFromEnum(m.id); out[base + 12 + j] = m.pp; }
                out[base + 16] = scaled(mon.stats.hp);
                out[base + 17] = scaled(mon.stats.atk);
                out[base + 18] = scaled(mon.stats.def);
                out[base + 19] = scaled(mon.stats.spe);
                out[base + 20] = scaled(mon.stats.spc);
            } else {
                @memcpy(out[base + 8 ..][0..4], &s.moves[p][i]);
            }
        }
        const a = &side.active;
        const base = 400 + relative * 32;
        out[base] = @intFromEnum(a.species);
        out[base + 1] = @intFromEnum(a.types.type1);
        out[base + 2] = @intFromEnum(a.types.type2);
        inline for (.{"atk", "def", "spe", "spc", "accuracy", "evasion"}, 0..) |field, j| {
            out[base + 3 + j] = @intCast(@as(i16, @field(a.boosts, field)) + 6);
        }
        inline for (.{"Bide", "Thrashing", "Charging", "Binding", "Invulnerable", "Confusion", "Mist", "FocusEnergy", "Substitute", "Recharging", "Rage", "LeechSeed", "Toxic", "LightScreen", "Reflect"}, 0..) |field, j| {
            out[base + 9 + j] = @intFromBool(@field(a.volatiles, field));
        }
        if (relative == 0) {
            out[base + 24] = scaled(a.stats.atk);
            out[base + 25] = scaled(a.stats.def);
            out[base + 26] = scaled(a.stats.spe);
            out[base + 27] = scaled(a.stats.spc);
        }
    }
    pk_mask(raw, player, out[480..640]);
}
export fn pk_turn(raw: *align(8) const [512]u8) c_int {
    const s: *const State = @ptrCast(raw);
    return s.battle.turn;
}
export fn pk_species(set: c_int) c_int {
    if (set < 0 or set >= catalog.sets.len) return 0;
    return @intFromEnum(catalog.sets[@intCast(set)].species);
}
export fn pk_set_name(set: c_int) [*:0]const u8 {
    if (set < 0 or set >= catalog.names.len) return "invalid";
    return catalog.names[@intCast(set)].ptr;
}
export fn pk_set_move(set: c_int, slot: c_int) c_int {
    if (set < 0 or set >= catalog.sets.len or slot < 0 or slot >= 4) return 0;
    const moves = catalog.sets[@intCast(set)].moves;
    if (slot >= moves.len) return 0;
    return @intFromEnum(moves[@intCast(slot)]);
}
const species_ranges = blk: {
    @setEvalBranchQuota(100000);
    var ranges: [150][2]u16 = @splat(.{0,0});
    for (catalog.sets, 0..) |set, i| {
        const species = @intFromEnum(set.species);
        if (ranges[species][1] == 0) ranges[species][0] = @intCast(i);
        ranges[species][1] += 1;
    }
    break :blk ranges;
};
export fn pk_species_count(species: c_int) c_int {
    if (species < 1 or species > 149) return 0;
    return species_ranges[@intCast(species)][1];
}
export fn pk_species_set(species: c_int, variant: c_int) c_int {
    if (variant < 0 or variant >= pk_species_count(species)) return -1;
    return species_ranges[@intCast(species)][0] + variant;
}
export fn pk_move_name(move: c_int) [*:0]const u8 {
    if (move < 0 or move > 165) return "unknown";
    const names = comptime blk: {
        var result: [166][:0]const u8 = undefined;
        for (0..166) |i| result[i] = @tagName(@as(pkmn.gen1.Move, @enumFromInt(i))) ++ "";
        break :blk result;
    };
    return names[@intCast(move)].ptr;
}

test "private state and random counters do not leak" {
    var raw: [512]u8 align(8) = undefined;
    const teams = [12]u16{445,392,511,365,421,231, 445,392,511,365,421,231};
    try std.testing.expectEqual(0, pk_start(&raw, 1, &teams));
    var before: [640]u8 = undefined;
    var after: [640]u8 = undefined;
    const s: *State = @ptrCast(&raw);
    pk_observe(&raw, 0, &before);
    s.battle.sides[1].pokemon[1].moves[0].id = .Splash;
    s.battle.sides[1].pokemon[1].hp = 1;
    s.battle.rng = pkmn.gen1.helpers.Battle.init(987, &.{catalog.sets[0]}, &.{catalog.sets[0]}).rng;
    pk_observe(&raw, 0, &after);
    try std.testing.expectEqualSlices(u8, &before, &after);
    s.battle.sides[0].pokemon[0].status = 1;
    pk_observe(&raw, 0, &before);
    s.battle.sides[0].pokemon[0].status = 7;
    pk_observe(&raw, 0, &after);
    try std.testing.expectEqualSlices(u8, &before, &after);
}

test "event recorder preserves upstream battle transitions" {
    const teams = [12]u16{445,392,511,365,421,231, 389,526,470,328,336,429};
    var mons: [12]pkmn.gen1.helpers.Pokemon = undefined;
    for (teams, 0..) |set, i| mons[i] = catalog.sets[set];
    for (1..9) |seed| {
        var raw: [512]u8 align(8) = undefined;
        try std.testing.expectEqual(0, pk_start(&raw, seed, &teams));
        const wrapped: *State = @ptrCast(&raw);
        var reference = pkmn.gen1.helpers.Battle.init(seed, mons[0..6], mons[6..12]);
        var options = pkmn.gen1.NULL;
        var result = try reference.update(.{}, .{}, &options);
        var rng = pkmn.PSRNG.init(seed);
        for (0..512) |_| {
            try std.testing.expectEqualDeep(reference, wrapped.battle);
            try std.testing.expectEqual(result, wrapped.result);
            if (result.type != .None) break;
            var c1: [pkmn.CHOICES_SIZE]pkmn.Choice = undefined;
            var c2: [pkmn.CHOICES_SIZE]pkmn.Choice = undefined;
            const n1 = reference.choices(.P1, result.p1, &c1);
            const n2 = reference.choices(.P2, result.p2, &c2);
            const a = c1[rng.range(u8, 0, n1)];
            const b = c2[rng.range(u8, 0, n2)];
            const actual = pk_update(&raw, @intCast(action(wrapped, 0, a)), @intCast(action(wrapped, 1, b)));
            result = try reference.update(a, b, &options);
            try std.testing.expectEqual(@as(c_int, @intFromEnum(result.type)), actual);
        }
    }
}
