// Trainer vs trainer under the cartridge's trainer AI on both sides (2026-10-30): our pkmn/engine fork
// (github.com/FelixAllistar/engine, branch single-player) in single-player mirror mode. Called from arena.c.
const pkmn = @import("pkmn");

const helpers = pkmn.gen1.helpers;
const single = pkmn.gen1.single_player;
const Choice = pkmn.Choice;
const Player = pkmn.Player;

pub const Mon = extern struct { dex: u8, level: u8, moves: [4]u8 };
pub const Team = extern struct { class: u8, n: u8, mons: [6]Mon };

/// The overworld Random the cartridge uses outside link battles is close to uniform; any good byte source will do.
const Uniform = extern struct {
    x: u64,

    pub fn next(self: *Uniform) u8 {
        self.x ^= self.x << 13;
        self.x ^= self.x >> 7;
        self.x ^= self.x << 17;
        return @truncate(self.x >> 32);
    }
};

/// A trainer party: fixed trainer DVs (attack 9, defense 8, speed 8, special 8) and no stat experience, or with
/// `perfect` the Showdown standard (DVs 15, maximum stat experience).
fn side(t: *const Team, perfect: bool) pkmn.gen1.Side {
    var mons: [6]helpers.Pokemon = undefined;
    var moves: [6][4]pkmn.gen1.Move = undefined;
    for (0..t.n) |i| {
        var k: usize = 0;
        for (t.mons[i].moves) |m| if (m != 0) {
            moves[i][k] = @enumFromInt(m);
            k += 1;
        };
        mons[i] = .{ .species = @enumFromInt(t.mons[i].dex), .level = t.mons[i].level, .moves = moves[i][0..k] };
        if (!perfect) {
            mons[i].dvs = .{ .atk = 9, .def = 8, .spe = 8, .spc = 8 };
            mons[i].stats = .{ .hp = 0, .atk = 0, .def = 0, .spe = 0, .spc = 0 };
        }
    }
    return helpers.Side.init(mons[0..t.n]);
}

fn choose(battle: anytype, player: Player, request: Choice.Type, sp: *single.SinglePlayer) Choice {
    var buf: [pkmn.CHOICES_SIZE]Choice = undefined;
    return switch (request) {
        .Pass => .{},
        .Switch => .{ .type = .Switch, .data = @intCast(single.trainerReplacement(battle.side(player))) },
        .Move => move: {
            // A forced move (data 0: recharging, trapped, Struggle...) is the AI's only option, as in SelectEnemyMove
            for (buf[0..battle.choices(player, .Move, &buf)]) |c| if (c.type == .Move and c.data == 0) break :move c;
            break :move .{ .type = .Move, .data = single.trainerMove(battle, player, sp.get(player)) };
        },
    };
}

fn hp(battle: anytype) u32 {
    var total: u32 = 0;
    for (battle.sides) |s| for (s.pokemon) |p| {
        total += p.hp;
    };
    return total;
}

/// Plays `n` battles of `a` against `b`, alternating which is P1. out: a wins, b wins, draws (PimanRules' rules: 1000
/// turns, or 75 turns without any HP change). Returns the number of battles that ended in an engine error or a link
/// desync result (counted as draws).
export fn arena_play(a: *const Team, b: *const Team, n: u32, seed: u64, perfect: c_int, out: *[3]u32) u32 {
    var errors: u32 = 0;
    out.* = .{ 0, 0, 0 };
    for (0..n) |i| {
        const swap = i & 1 == 1;
        const t1 = if (swap) b else a;
        const t2 = if (swap) a else b;
        var sp = single.SinglePlayer{ .p1 = .{ .class = t1.class }, .p2 = .{ .class = t2.class }, .mirror = true };
        const options = pkmn.battle.options(pkmn.protocol.NULL, pkmn.gen1.chance.NULL, pkmn.gen1.calc.NULL);
        var full = .{ .log = options.log, .chance = options.chance, .calc = options.calc, .trainer = &sp };
        var battle = pkmn.gen1.Battle(Uniform){
            .rng = .{ .x = (seed ^ (@as(u64, i) *% 0x9E3779B97F4A7C15)) | 1 },
            .sides = .{ side(t1, perfect != 0), side(t2, perfect != 0) },
        };
        var result = battle.update(.{}, .{}, &full) catch {
            errors += 1;
            out[2] += 1;
            continue;
        };
        var turns: u32 = 0;
        var quiet: u32 = 0;
        var last = hp(&battle);
        while (result.type == .None and turns < 1000 and quiet < 75) : (turns += 1) {
            const c1 = choose(&battle, .P1, result.p1, &sp);
            const c2 = choose(&battle, .P2, result.p2, &sp);
            result = battle.update(c1, c2, &full) catch {
                errors += 1;
                break;
            };
            const now = hp(&battle);
            quiet = if (now == last) quiet + 1 else 0;
            last = now;
        }
        if (result.type == .Error) errors += 1;
        const p1_won = result.type == .Win;
        const p2_won = result.type == .Lose;
        if (p1_won or p2_won) {
            const a_won = p1_won != swap;
            out[if (a_won) 0 else 1] += 1;
        } else out[2] += 1;
    }
    return errors;
}
