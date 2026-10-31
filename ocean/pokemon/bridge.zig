const std = @import("std");
const pkmn = @import("pkmn");
const catalog = @import("catalog.zig");
const freepick = @import("freepick_rules.zig");
const ACTIONS = 168;
const OBS = 648;
const Battle = pkmn.gen1.Battle(pkmn.gen1.PRNG);
// Single-player rules (our engine fork's cartridge build only): P2 is an in-game trainer under the cartridge's trainer
// AI, P1 the player with badges (pk_start_vs_ai, pk_ai_action).
const has_single = @hasDecl(pkmn.gen1, "single_player");
const Single = if (has_single) pkmn.gen1.single_player.SinglePlayer else struct {};
const State = struct {
    battle: Battle,
    result: pkmn.Result = .{},
    single: Single = if (has_single) .{ .p2 = .{} } else .{},
    single_on: bool = false,
    // Public identities are assigned in order of first appearance, never hidden party order.
    seen: [2][6]u8 = @splat(@splat(0)),
    moves: [2][6][4]u8 = @splat(@splat(@splat(0))),
    last_move: [2]u8 = @splat(0),
    last_actor: [2]u8 = @splat(0),
    // Public successful events, keyed internally by persistent party identity.
    // These are reward/report counters only; never policy observations.
    acting: u8 = 2,
    early_sleep: [2]u8 = @splat(0),
    paralysis_mask: [2]u8 = @splat(0),
    // Showdown-protocol text log of this battle (pk_text_next before a start), for replays; null when off.
    text: ?[*]u8 = null,
    text_cap: u32 = 0,
    text_len: u32 = 0,
    // Sides that moved this turn: a trainer-AI boost or heal without a move is an item (X Defend, Super Potion...).
    text_moved: u8 = 0,
};
// A replay buffer handed over by pk_text_next, taken by the next battle started on this thread.
threadlocal var text_pending: ?[*]u8 = null;
threadlocal var text_pending_cap: u32 = 0;
threadlocal var text_pending_len: u32 = 0;
fn takeText(s: *State) void {
    s.text = text_pending;
    s.text_cap = text_pending_cap;
    s.text_len = text_pending_len;
    text_pending = null;
}
/// Log the next battle started on this thread as Showdown protocol text into buf (len bytes already used, e.g. the
/// header); pk_text_len reads its length after each update.
export fn pk_text_next(buf: [*]u8, cap: u32, len: u32) void {
    text_pending = buf;
    text_pending_cap = cap;
    text_pending_len = len;
}
export fn pk_text_len(raw: *align(8) const [512]u8) u32 {
    const s: *const State = @ptrCast(raw);
    return if (s.text != null) s.text_len else 0;
}
fn put(s: *State, comptime fmt: []const u8, args: anytype) void {
    const buf = s.text orelse return;
    const out = std.fmt.bufPrint(buf[s.text_len..s.text_cap], fmt, args) catch return;
    s.text_len += @intCast(out.len);
}
fn speciesName(sp: pkmn.gen1.Species) []const u8 {
    return switch (sp) {
        .MrMime => "Mr. Mime",
        .NidoranF => "Nidoran-F",
        .NidoranM => "Nidoran-M",
        .Farfetchd => "Farfetch'd",
        else => @tagName(sp),
    };
}
// "p1a: Species" for an engine ID (party slot of the side).
fn ident(s: *const State, id: anytype) struct { p: u8, name: []const u8 } {
    const p = @intFromEnum(id.player);
    return .{ .p = @as(u8, p) + 1, .name = speciesName(s.battle.sides[p].pokemon[id.id - 1].species) };
}
fn eq(a: []const u8, comptime b: []const u8) bool {
    return std.mem.eql(u8, a, b);
}
fn statusText(bits: u8) []const u8 {
    if (bits & 7 != 0) return "slp";
    if (bits & 8 != 0) return if (bits & 128 != 0) "tox" else "psn";
    if (bits & 16 != 0) return "brn";
    if (bits & 32 != 0) return "frz";
    if (bits & 64 != 0) return "par";
    return "";
}
fn itemText(s: *State, p: u8) void {
    if (s.single_on and p == 2 and s.text_moved & 2 == 0) {
        put(s, "|-message|The trainer used an item!\n", .{});
        s.text_moved |= 2;
    }
}
fn hpText(s: *State, mon: anytype) void {
    put(s, "|{d}/{d}", .{ mon.hp, mon.stats.hp });
    if (mon.hp == 0) put(s, " fnt", .{}) else if (statusText(mon.status).len > 0) put(s, " {s}", .{statusText(mon.status)});
}
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
        if (self.s.text != null) {
            const a = ident(self.s, args[0]);
            put(self.s, "|move|p{d}a: {s}|{s}", .{ a.p, a.name, @tagName(args[1]) });
            self.s.text_moved |= @as(u8, 1) << @intCast(a.p - 1);
            if (@as(u8, @bitCast(args[2])) != 0) {
                const t = ident(self.s, args[2]);
                put(self.s, "|p{d}a: {s}", .{ t.p, t.name });
            }
            if (args.len >= 4 and !eq(@tagName(args[3]), "None")) put(self.s, "|[from]{s}", .{@tagName(args[3])});
            put(self.s, "\n", .{});
        }
        const id = args[0];
        const p = @intFromEnum(id.player);
        const i = id.id - 1;
        const m = @intFromEnum(args[1]);
        self.s.acting = @intCast(p);
        self.s.last_move[p] = m;
        self.s.last_actor[p] = id.id;
        for (&self.s.moves[p][i]) |*known| {
            if (known.* == m) break;
            if (known.* == 0) { known.* = m; break; }
        }
    }
    pub fn switched(self: Events, args: anytype) error{}!void {
        if (self.s.text != null) {
            const a = ident(self.s, args[0]);
            put(self.s, "|switch|p{d}a: {s}|{s}, L{d}", .{ a.p, a.name, a.name, args[1].level });
            hpText(self.s, args[1]);
            put(self.s, "\n", .{});
        }
        const seen = &self.s.seen[@intFromEnum(args[0].player)];
        const i = args[0].id - 1;
        if (seen[i] == 0) {
            var maximum: u8 = 0;
            for (seen) |n| maximum = @max(maximum, n);
            seen[i] = maximum + 1;
        }
    }
    pub fn activate(self: Events, args: anytype) error{}!void {
        if (self.s.text == null) return;
        const a = ident(self.s, args[0]);
        const r = @tagName(args[1]);
        if (eq(r, "Confusion")) put(self.s, "|-activate|p{d}a: {s}|confusion\n", .{ a.p, a.name })
        else if (eq(r, "Substitute")) put(self.s, "|-activate|p{d}a: {s}|Substitute|[damage]\n", .{ a.p, a.name })
        else put(self.s, "|-activate|p{d}a: {s}|move: {s}\n", .{ a.p, a.name, r });
    }
    pub fn boost(self: Events, args: anytype) error{}!void {
        if (self.s.text == null) return;
        const a = ident(self.s, args[0]);
        itemText(self.s, a.p);
        const r = @tagName(args[1]);
        const stat = if (eq(r, "Rage") or eq(r, "Attack")) "atk" else if (eq(r, "Defense")) "def" else if (eq(r, "Speed"))
            "spe" else if (eq(r, "SpecialAttack")) "spa" else if (eq(r, "SpecialDefense")) "spd" else if (eq(r, "Accuracy"))
            "accuracy" else "evasion";
        const n: i8 = args[2];
        if (n > 0) put(self.s, "|-boost|p{d}a: {s}|{s}|{d}\n", .{ a.p, a.name, stat, n })
        else put(self.s, "|-unboost|p{d}a: {s}|{s}|{d}\n", .{ a.p, a.name, stat, -n });
    }
    pub fn cant(self: Events, args: anytype) error{}!void {
        if (self.s.text == null) return;
        const a = ident(self.s, args[0]);
        const r = @tagName(args[1]);
        const why = if (eq(r, "Sleep")) "slp" else if (eq(r, "Freeze")) "frz" else if (eq(r, "Paralysis")) "par" else if (eq(r, "Bound"))
            "partiallytrapped" else if (eq(r, "Flinch")) "flinch" else if (eq(r, "Recharge")) "recharge" else if (eq(r, "PP")) "nopp" else r;
        put(self.s, "|cant|p{d}a: {s}|{s}", .{ a.p, a.name, why });
        if (args.len == 3) put(self.s, "|{s}", .{@tagName(args[2])});
        put(self.s, "\n", .{});
    }
    pub fn clearallboost(self: Events, args: anytype) error{}!void {
        _ = args;
        if (self.s.text != null) put(self.s, "|-clearallboost\n", .{});
    }
    pub fn crit(self: Events, args: anytype) error{}!void {
        if (self.s.text == null) return;
        const a = ident(self.s, args[0]);
        put(self.s, "|-crit|p{d}a: {s}\n", .{ a.p, a.name });
    }
    pub fn curestatus(self: Events, args: anytype) error{}!void {
        if (self.s.text == null) return;
        const a = ident(self.s, args[0]);
        put(self.s, "|-curestatus|p{d}a: {s}|{s}{s}\n", .{ a.p, a.name, statusText(args[1]), if (eq(@tagName(args[2]), "Message")) "|[msg]" else "|[silent]" });
    }
    pub fn damage(self: Events, args: anytype) error{}!void {
        if (self.s.text == null) return;
        const a = ident(self.s, args[0]);
        put(self.s, "|-damage|p{d}a: {s}", .{ a.p, a.name });
        hpText(self.s, args[1]);
        const r = @tagName(args[2]);
        if (eq(r, "Poison")) put(self.s, "|[from] psn", .{})
        else if (eq(r, "Burn")) put(self.s, "|[from] brn", .{})
        else if (eq(r, "Confusion")) put(self.s, "|[from] confusion", .{})
        else if (eq(r, "LeechSeed")) put(self.s, "|[from] Leech Seed", .{})
        else if (eq(r, "RecoilOf")) {
            if (args.len == 4) {
                const o = ident(self.s, args[3]);
                put(self.s, "|[from] Recoil|[of] p{d}a: {s}", .{ o.p, o.name });
            }
        } else if (!eq(r, "None")) put(self.s, "|[from] {s}", .{r});
        put(self.s, "\n", .{});
    }
    pub fn end(self: Events, args: anytype) error{}!void {
        if (self.s.text == null) return;
        const a = ident(self.s, args[0]);
        const r = @tagName(args[1]);
        put(self.s, "|-end|p{d}a: {s}|{s}\n", .{ a.p, a.name, if (eq(r, "Confusion")) "confusion" else r });
    }
    pub fn fail(self: Events, args: anytype) error{}!void {
        if (self.s.text == null) return;
        const a = ident(self.s, args[0]);
        put(self.s, "|-fail|p{d}a: {s}\n", .{ a.p, a.name });
    }
    pub fn faint(self: Events, args: anytype) error{}!void {
        if (self.s.text == null) return;
        const a = ident(self.s, args[0]);
        put(self.s, "|faint|p{d}a: {s}\n", .{ a.p, a.name });
    }
    pub fn fieldactivate(_: Events, _: anytype) error{}!void {}
    pub fn heal(self: Events, args: anytype) error{}!void {
        if (self.s.text == null) return;
        const a = ident(self.s, args[0]);
        itemText(self.s, a.p);
        put(self.s, "|-heal|p{d}a: {s}", .{ a.p, a.name });
        hpText(self.s, args[1]);
        const r = @tagName(args[2]);
        if (eq(r, "Silent")) put(self.s, "|[silent]", .{})
        else if (eq(r, "Drain")) {
            if (args.len == 4) {
                const o = ident(self.s, args[3]);
                put(self.s, "|[from] drain|[of] p{d}a: {s}", .{ o.p, o.name });
            }
        }
        put(self.s, "\n", .{});
    }
    pub fn hitcount(self: Events, args: anytype) error{}!void {
        if (self.s.text == null) return;
        const a = ident(self.s, args[0]);
        put(self.s, "|-hitcount|p{d}a: {s}|{d}\n", .{ a.p, a.name, args[1] });
    }
    pub fn immune(self: Events, args: anytype) error{}!void {
        if (self.s.text == null) return;
        const a = ident(self.s, args[0]);
        put(self.s, "|-immune|p{d}a: {s}\n", .{ a.p, a.name });
    }
    pub fn lastmiss(_: Events, _: anytype) error{}!void {}
    pub fn laststill(_: Events, _: anytype) error{}!void {}
    pub fn miss(self: Events, args: anytype) error{}!void {
        if (self.s.text == null) return;
        const a = ident(self.s, args[0]);
        put(self.s, "|-miss|p{d}a: {s}\n", .{ a.p, a.name });
    }
    pub fn mustrecharge(self: Events, args: anytype) error{}!void {
        if (self.s.text == null) return;
        const a = ident(self.s, args[0]);
        put(self.s, "|-mustrecharge|p{d}a: {s}\n", .{ a.p, a.name });
    }
    pub fn ohko(self: Events, args: anytype) error{}!void {
        _ = args;
        if (self.s.text != null) put(self.s, "|-ohko\n", .{});
    }
    pub fn prepare(self: Events, args: anytype) error{}!void {
        if (self.s.text == null) return;
        const a = ident(self.s, args[0]);
        put(self.s, "|-prepare|p{d}a: {s}|{s}\n", .{ a.p, a.name, @tagName(args[1]) });
    }
    pub fn resisted(self: Events, args: anytype) error{}!void {
        if (self.s.text == null) return;
        const a = ident(self.s, args[0]);
        put(self.s, "|-resisted|p{d}a: {s}\n", .{ a.p, a.name });
    }
    pub fn start(self: Events, args: anytype) error{}!void {
        if (self.s.text == null) return;
        const a = ident(self.s, args[0]);
        const r = @tagName(args[1]);
        if (eq(r, "Confusion")) put(self.s, "|-start|p{d}a: {s}|confusion", .{ a.p, a.name })
        else if (eq(r, "ConfusionSilent")) put(self.s, "|-start|p{d}a: {s}|confusion|[silent]", .{ a.p, a.name })
        else if (eq(r, "TypeChange")) put(self.s, "|-start|p{d}a: {s}|typechange", .{ a.p, a.name })
        else {
            put(self.s, "|-start|p{d}a: {s}|{s}", .{ a.p, a.name, r });
            if (args.len == 3) put(self.s, "|{s}", .{@tagName(args[2])});
        }
        put(self.s, "\n", .{});
    }
    pub fn status(self: Events, args: anytype) error{}!void {
        if (self.s.text != null) {
            const a = ident(self.s, args[0]);
            put(self.s, "|-status|p{d}a: {s}|{s}", .{ a.p, a.name, statusText(args[1]) });
            if (args[2] == .Silent) put(self.s, "|[silent]", .{});
            if (args.len >= 4) put(self.s, "|[from] move: {s}", .{@tagName(args[3])});
            put(self.s, "\n", .{});
        }
        // Switching an already-statused Pokemon logs Silent, not a new event.
        if (args[2] == .Silent) return;
        if (args.len >= 4) {
            if (args[3] == pkmn.gen1.Move.Rest) return;
        }
        const target = @intFromEnum(args[0].player);
        const actor = self.s.acting;
        if (actor > 1 or actor == target or args[0].id < 1 or args[0].id > 6) return;
        const bits: u8 = args[1];
        if (bits & 7 != 0 and self.s.battle.turn <= 5) self.s.early_sleep[actor] = 1;
        if (bits & 64 != 0) {
            self.s.paralysis_mask[actor] |= @as(u8, 1) << @as(u3, @intCast(args[0].id - 1));
        }
    }
    pub fn supereffective(self: Events, args: anytype) error{}!void {
        if (self.s.text == null) return;
        const a = ident(self.s, args[0]);
        put(self.s, "|-supereffective|p{d}a: {s}\n", .{ a.p, a.name });
    }
    pub fn tie(_: Events, _: anytype) error{}!void {}
    pub fn transform(self: Events, args: anytype) error{}!void {
        if (self.s.text == null) return;
        const a = ident(self.s, args[0]);
        const b = ident(self.s, args[1]);
        put(self.s, "|-transform|p{d}a: {s}|p{d}a: {s}\n", .{ a.p, a.name, b.p, b.name });
    }
    pub fn turn(self: Events, args: anytype) error{}!void {
        if (self.s.text != null) put(self.s, "|\n|turn|{d}\n", .{args[0]});
        self.s.text_moved = 0;
    }
    pub fn win(_: Events, _: anytype) error{}!void {}
};

fn advance(s: *State, c1: pkmn.Choice, c2: pkmn.Choice) c_int {
    s.acting = 2;
    s.last_move = @splat(0);
    s.last_actor = @splat(0);
    if (has_single and s.single_on) {
        const base = pkmn.battle.options(Events{ .s = s }, pkmn.gen1.chance.NULL, pkmn.gen1.calc.NULL);
        var options = .{ .log = base.log, .chance = base.chance, .calc = base.calc, .trainer = &s.single };
        s.result = s.battle.update(c1, c2, &options) catch return 4;
        return @intFromEnum(s.result.type);
    }
    var options = pkmn.battle.options(Events{ .s = s }, pkmn.gen1.chance.NULL, pkmn.gen1.calc.NULL);
    s.result = s.battle.update(c1, c2, &options) catch return 4;
    return @intFromEnum(s.result.type);
}

/// The player (P1, `player_perfect`: Showdown-standard DVs and stat experience) against an in-game trainer (P2,
/// cartridge enemy DVs, no stat experience) of class `class` under single-player rules: the cartridge trainer AI's
/// items and switches, the AI's 25% stat-down miss, no enemy PP use, and the player's `badges` (bit per badge) with
/// their stat boosts. P2's choices come from pk_ai_action. Needs the cartridge build of our engine fork.
export fn pk_start_vs_ai(raw: *align(8) [512]u8, seed: u64, teams: *const [12]TrainerMon, sizes: *const [2]u8,
    player_perfect: c_int, class: u8, badges: u8) c_int {
    if (!has_single) return 4;
    var mons: [12]pkmn.gen1.helpers.Pokemon = undefined;
    var moves: [12][4]pkmn.gen1.Move = undefined;
    for (0..2) |p| {
        if (sizes[p] < 1 or sizes[p] > 6) return 4;
        for (0..sizes[p]) |k| {
            const mon = teams[p * 6 + k];
            if (mon.species < 1 or mon.species > 151 or mon.level < 1 or mon.level > 100) return 4;
            var count: usize = 0;
            for (mon.moves) |m| {
                if (m == 0) break;
                if (m > 165) return 4;
                moves[p * 6 + k][count] = @enumFromInt(m);
                count += 1;
            }
            if (count == 0) return 4;
            mons[p * 6 + k] = .{ .species = @enumFromInt(mon.species), .level = mon.level, .moves = moves[p * 6 + k][0..count] };
            if (p == 1 or player_perfect == 0) {
                mons[p * 6 + k].dvs = .{ .atk = 9, .def = 8, .spe = 8, .spc = 8 };
                mons[p * 6 + k].stats = .{ .hp = 0, .atk = 0, .def = 0, .spe = 0, .spc = 0 };
            }
        }
    }
    @memset(raw, 0);
    const s: *State = @ptrCast(raw);
    s.* = .{ .battle = pkmn.gen1.helpers.Battle.init(seed, mons[0..sizes[0]], mons[6 .. 6 + sizes[1]]) };
    takeText(s);
    if (has_single) {
        s.single = .{ .p2 = .{ .class = class }, .badges = badges };
        s.single_on = true;
    }
    // Trainer Pokemon have no PP Ups; the perfect player keeps the PP Up maximum helpers gives (as in Gym Leader
    // Matt's runs and the fork's Weedle test)
    for (&s.battle.sides, 0..) |*side, p| for (&side.pokemon) |*mon| for (&mon.moves) |*slot| {
        if (slot.id != .None and (p == 1 or player_perfect == 0)) slot.pp = @intCast(pkmn.gen1.Move.pp(slot.id));
    };
    return advance(s, .{}, .{});
}

/// The cartridge trainer AI's choice for P2 as an action index (as pk_mask/pk_update number them), -1 without one.
export fn pk_ai_action(raw: *align(8) [512]u8) c_int {
    if (!has_single) return -1;
    const s: *State = @ptrCast(raw);
    if (!s.single_on or s.result.type != .None) return -1;
    const single = pkmn.gen1.single_player;
    var buf: [pkmn.CHOICES_SIZE]pkmn.Choice = undefined;
    const c: pkmn.Choice = switch (s.result.p2) {
        .Pass => .{},
        .Switch => .{ .type = .Switch, .data = @intCast(single.trainerReplacement(s.battle.side(.P2))) },
        .Move => move: {
            // A forced move (data 0) is the AI's only option, as in SelectEnemyMove
            for (buf[0..s.battle.choices(.P2, .Move, &buf)]) |c| if (c.type == .Move and c.data == 0) break :move c;
            break :move .{ .type = .Move, .data = single.trainerMove(&s.battle, .P2, &s.single.p2) };
        },
    };
    return @intCast(action(s, 1, c));
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
const FreeMon = extern struct { species: u8, moves: [4]u8 };
fn legalMon(mon: FreeMon) bool {
    if (mon.species < 1 or mon.species > 149 or mon.moves[0] == 0) return false;
    var ended = false;
    for (mon.moves, 0..) |m, i| {
        if (m == 0) { ended = true; continue; }
        if (ended or m > 164 or (freepick.legal_moves[mon.species][m / 64] & (@as(u64, 1) << @as(u6, @intCast(m % 64)))) == 0) return false;
        for (mon.moves[0..i]) |previous| if (previous == m) { return false; };
    }
    for (freepick.forbidden) |bad| {
        if (bad.species != mon.species) continue;
        var count: u8 = 0;
        for (bad.moves[0..bad.count]) |m| for (mon.moves) |chosen| {
            if (chosen == m) count += 1;
        };
        if (count == bad.count) return false;
    }
    return true;
}
export fn pk_start_free(raw: *align(8) [512]u8, seed: u64, teams: *const [12]FreeMon) c_int {
    var mons: [12]pkmn.gen1.helpers.Pokemon = undefined;
    var moves: [12][4]pkmn.gen1.Move = undefined;
    for (teams, 0..) |mon, i| {
        if (!legalMon(mon)) return 4;
        for (teams[(i / 6) * 6 .. i]) |previous| if (previous.species == mon.species) { return 4; };
        var count: usize = 0;
        for (mon.moves, 0..) |m, j| {
            if (m == 0) break;
            moves[i][j] = @enumFromInt(m); count += 1;
        }
        mons[i] = .{.species = @enumFromInt(mon.species), .moves = moves[i][0..count]};
    }
    @memset(raw, 0);
    const s: *State = @ptrCast(raw);
    s.* = .{.battle = pkmn.gen1.helpers.Battle.init(seed, mons[0..6], mons[6..12])};
    return advance(s, .{}, .{});
}
/// One Pokemon of an in-game trainer's party (ocean/pokemon/cartridge/trainers.json): species, level, moves.
pub const TrainerMon = extern struct { species: u8, level: u8, moves: [4]u8 };

/// Starts a battle between two in-game trainer parties (1-6 Pokemon each, repeats allowed) at their own levels, with
/// the cartridge's enemy-trainer DVs (attack 9, defense 8, speed 8, special 8), no stat experience and base PP, or
/// with `perfect` the Showdown standard (all DVs 15, maximum stat experience). teams[0..6] is P1, teams[6..12] P2;
/// sizes holds each party's size.
export fn pk_start_trainers(raw: *align(8) [512]u8, seed: u64, teams: *const [12]TrainerMon, sizes: *const [2]u8,
    perfect: c_int) c_int {
    var mons: [12]pkmn.gen1.helpers.Pokemon = undefined;
    var moves: [12][4]pkmn.gen1.Move = undefined;
    for (0..2) |p| {
        if (sizes[p] < 1 or sizes[p] > 6) return 4;
        for (0..sizes[p]) |k| {
            const mon = teams[p * 6 + k];
            if (mon.species < 1 or mon.species > 151 or mon.level < 1 or mon.level > 100) return 4;
            var count: usize = 0;
            for (mon.moves) |m| {
                if (m == 0) break;
                if (m > 165) return 4;
                moves[p * 6 + k][count] = @enumFromInt(m);
                count += 1;
            }
            if (count == 0) return 4;
            mons[p * 6 + k] = .{
                .species = @enumFromInt(mon.species),
                .level = mon.level,
                .moves = moves[p * 6 + k][0..count],
            };
            if (perfect == 0) {
                mons[p * 6 + k].dvs = .{ .atk = 9, .def = 8, .spe = 8, .spc = 8 };
                mons[p * 6 + k].stats = .{ .hp = 0, .atk = 0, .def = 0, .spe = 0, .spc = 0 };
            }
        }
    }
    @memset(raw, 0);
    const s: *State = @ptrCast(raw);
    s.* = .{ .battle = pkmn.gen1.helpers.Battle.init(seed, mons[0..sizes[0]], mons[6 .. 6 + sizes[1]]) };
    takeText(s);
    // Trainer Pokemon have no PP Ups (helpers gives the PP Up maximum)
    for (&s.battle.sides) |*side| for (&side.pokemon) |*mon| for (&mon.moves) |*slot| {
        if (slot.id != .None) slot.pp = @intCast(pkmn.gen1.Move.pp(slot.id));
    };
    return advance(s, .{}, .{});
}
export fn pk_move_engine_info(move: c_int, out: *[6]u16) void {
    @memset(out, 0);
    if (move < 1 or move > 165) return;
    const id: pkmn.gen1.Move = @enumFromInt(move);
    const m = pkmn.gen1.Move.get(id);
    out.* = .{m.bp, m.accuracy, @intFromEnum(m.type), @intFromEnum(m.effect), pkmn.gen1.Move.pp(id), @intFromEnum(m.target)};
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
// Evaluation bridge to external search agents. Normal observations stay private.
export fn pk_benchmark_state(raw: *align(8) const [512]u8, out: *[384]u8) c_int {
    comptime std.debug.assert(@sizeOf(Battle) == 384);
    const s: *const State = @ptrCast(raw);
    @memcpy(out, std.mem.asBytes(&s.battle));
    return @as(u8, @bitCast(s.result));
}
export fn pk_benchmark_choice(raw: *align(8) const [512]u8, player: c_int, encoded_choice: c_int) c_int {
    if (player < 0 or player > 1 or encoded_choice < 0 or encoded_choice > 255) return -1;
    const s: *const State = @ptrCast(raw);
    var cs: [pkmn.CHOICES_SIZE]pkmn.Choice = undefined;
    const p: usize = @intCast(player);
    for (cs[0..choices(s,p,&cs)]) |c| {
        if (@as(u8,@bitCast(c)) == encoded_choice) return @intCast(action(s,p,c));
    }
    return -1;
}
export fn pk_benchmark_fields(raw: *align(8) const [512]u8, out: *[192]u16) void {
    const s: *const State = @ptrCast(raw);
    for (s.battle.sides,0..) |side,p| {
        for (side.pokemon,0..) |mon,i| {
            const b=(p*6+i)*16;
            out[b..][0..8].* = .{@intFromEnum(mon.species),mon.hp,mon.level,mon.status,mon.stats.hp,mon.stats.atk,mon.stats.def,mon.stats.spe};
            for (mon.moves,0..) |move,j| {
                out[b+8+2*j]=@intFromEnum(move.id);out[b+9+2*j]=move.pp;
            }
        }
    }
}
export fn pk_mask(raw: *align(8) const [512]u8, player: c_int, out: *[ACTIONS]u8) void {
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
export fn pk_observe(raw: *align(8) const [512]u8, player: c_int, out: *[OBS]u8) void {
    @memset(out, 0);
    if (player < 0 or player > 1) return;
    const s: *const State = @ptrCast(raw);
    const observer: usize = @intCast(player);
    out[0] = 2; // Battle phase (species=0, individual moves=1).
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
            // Trainer parties can have fewer than six Pokemon
            if (mon.species == .None) continue;
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
    pk_mask(raw, player, out[480..OBS]);
}
export fn pk_turn(raw: *align(8) const [512]u8) c_int {
    const s: *const State = @ptrCast(raw);
    return s.battle.turn;
}
// Resample only future chance draws, never existing hidden durations or reveals.
export fn pk_reseed(raw: *align(8) [512]u8, seed: u64) void {
    const s: *State = @ptrCast(raw);
    if (pkmn.options.showdown) {
        var rand = pkmn.gen1.PRNG.init(seed);
        s.battle.rng = .{ .src = .{ .seed = rand.newSeed() } };
    } else {
        // Cartridge mode: the link-battle RNG, seeded the way helpers.Battle.init seeds it
        s.battle.rng = pkmn.gen1.helpers.Battle.init(seed, &.{catalog.sets[0]}, &.{catalog.sets[0]}).rng;
    }
}
// Cumulative episode measures: early opponent sleep (0/1), distinct opponents
// successfully paralyzed (0..6). Repeated status/cure/switch cycles cannot grow
// the distinct count. Reward normalization/caps live in behavior.h.
export fn pk_behavior(raw: *align(8) const [512]u8, player: c_int, out: *[2]f32) void {
    @memset(out, 0);
    if (player < 0 or player > 1) return;
    const s: *const State = @ptrCast(raw);
    const p: usize = @intCast(player);
    out[0] = @floatFromInt(s.early_sleep[p]);
    out[1] = @floatFromInt(@popCount(s.paralysis_mask[p]));
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
    var before: [OBS]u8 = undefined;
    var after: [OBS]u8 = undefined;
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

test "behavior events exclude Rest and status reannouncements and cap identities" {
    const teams = [12]u16{445,392,511,365,421,231, 389,526,470,328,336,429};
    var raw: [512]u8 align(8) = undefined;
    try std.testing.expectEqual(0, pk_start(&raw, 73, &teams));
    const s: *State = @ptrCast(&raw);
    const log = Events{ .s = s };
    const a = s.battle.active(.P1);
    var b = s.battle.active(.P2);
    var values: [2]f32 = undefined;
    try log.move(.{ a, pkmn.gen1.Move.ThunderWave, b });
    try log.status(.{ b, @as(u8,64), .Silent });
    pk_behavior(&raw,0,&values);
    try std.testing.expectEqual(@as(f32,0),values[1]);
    try log.status(.{ b, @as(u8,64), .None });
    try log.status(.{ b, @as(u8,64), .None }); // cured/reinflicted same identity
    pk_behavior(&raw,0,&values);
    try std.testing.expectEqual(@as(f32,1),values[1]);
    b.id=2;
    try log.status(.{ b, @as(u8,64), .None });
    pk_behavior(&raw,0,&values);
    try std.testing.expectEqual(@as(f32,2),values[1]);
    try log.status(.{ b, @as(u8,2), .From, pkmn.gen1.Move.Rest });
    pk_behavior(&raw,0,&values);
    try std.testing.expectEqual(@as(f32,0),values[0]);
    s.battle.turn=6;
    try log.move(.{ a, pkmn.gen1.Move.SleepPowder, b });
    try log.status(.{ b, @as(u8,2), .From, pkmn.gen1.Move.SleepPowder });
    pk_behavior(&raw,0,&values);
    try std.testing.expectEqual(@as(f32,0),values[0]);
    s.battle.turn=5;
    try log.status(.{ b, @as(u8,2), .From, pkmn.gen1.Move.SleepPowder });
    pk_behavior(&raw,0,&values);
    try std.testing.expectEqual(@as(f32,1),values[0]);
    pk_behavior(&raw,1,&values);
    try std.testing.expectEqualSlices(f32,&.{0,0},&values);
    // A move event alone (miss, immunity, clause rejection) earns nothing.
    try std.testing.expectEqual(0, pk_start(&raw, 73, &teams));
    try log.move(.{ a, pkmn.gen1.Move.SleepPowder, b });
    pk_behavior(&raw,0,&values);
    try std.testing.expectEqualSlices(f32,&.{0,0},&values);
    // Self-inflicted sleep never credits the opponent.
    try log.status(.{ a, @as(u8,2), .From, pkmn.gen1.Move.Rest });
    pk_behavior(&raw,0,&values);
    try std.testing.expectEqualSlices(f32,&.{0,0},&values);
}
