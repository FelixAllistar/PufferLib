// C ABI over poke-engine (Gen 9 singles) for ocean/pokemon9/pokemon9.h (2026-10-30).
// One Game is one battle: a poke-engine State, the information each side has seen of the other, and an RNG.
// Each step plays one turn (or one forced-switch decision) for both sides and applies a single outcome sampled by
// the engine's branch probabilities, with damage branching (crits, KO rolls) on.
//
// Actions (POKEMON9_ACTIONS = 14): 0-3 move slot, 4-7 move slot with Terastallization, 8-13 switch to party slot.
// A side with nothing to choose (the other side's forced switch) gets only action 0, which means "wait".
//
// Observation (pk9_observe, POKEMON9_OBS bf16 values), from one side's point of view, partial information like a
// player: own six Pokemon first, then the opponent's six. Opponent Pokemon are blank until they have been active, their
// moves until used, their Tera type until they Terastallize. v2 (2026-10-30): one-hot ability and item (vocab.rs, every
// one seen in Gen 9 random battles) and move effect features. An opponent's item shows once it changes (consumed,
// knocked off, tricked) or is one players see at once (VISIBLE_ITEMS); its ability once it is one Showdown announces on
// switch-in (ANNOUNCED_ABILITIES). Other reveals (Rough Skin damage, a Choice lock...) are not modelled yet. Species is
// given by stats and types only; a species one-hot (565 in random battles) would double the bytes per step.
mod vocab;
use vocab::{ability_slot, item_slot, ABILITY_SLOTS, ITEM_SLOTS};
use poke_engine::choices::{Choice, Effect, MoveTarget};
use poke_engine::engine::abilities::Abilities;
use poke_engine::engine::items::Items;
use poke_engine::engine::generate_instructions::generate_instructions_from_move_pair;
use poke_engine::engine::state::MoveChoice;
use poke_engine::choices::MoveCategory;
use poke_engine::state::{Pokemon, PokemonIndex, PokemonMoveIndex, Side, State};
use std::ffi::{c_char, CStr};

pub const ACTIONS: usize = 14;
const TYPES: usize = 20;
const STATUSES: usize = 7;
const EFFECT_FEATURES: usize = 16 + 3 + 12 + STATUSES + 4;
const MOVE_FEATURES: usize = 1 + TYPES + 3 + 4 + EFFECT_FEATURES;
const MON_FEATURES: usize = 6 + 5 + TYPES + TYPES + 1 + STATUSES + 4 * MOVE_FEATURES + ABILITY_SLOTS + ITEM_SLOTS;
const SIDE_FEATURES: usize = 7 + 19 + 128 + 4;
const FIELD_FEATURES: usize = 10 + 6 + 4;
pub const OBS: usize = 12 * MON_FEATURES + 2 * SIDE_FEATURES + FIELD_FEATURES;
const MAX_TURNS: u32 = 500;

// Shown to the opponent as soon as the holder is on the field (switch-in message, or end-of-turn heal/recoil text).
const VISIBLE_ITEMS: [Items; 8] = [Items::LEFTOVERS, Items::BLACKSLUDGE, Items::LIFEORB, Items::ROCKYHELMET,
    Items::FLAMEORB, Items::TOXICORB, Items::AIRBALLOON, Items::BOOSTERENERGY];
// Announced on switch-in (Showdown "-ability" or a field effect that names it).
const ANNOUNCED_ABILITIES: [Abilities; 24] = [Abilities::INTIMIDATE, Abilities::DROUGHT, Abilities::DRIZZLE,
    Abilities::SANDSTREAM, Abilities::SNOWWARNING, Abilities::ELECTRICSURGE, Abilities::PSYCHICSURGE,
    Abilities::GRASSYSURGE, Abilities::MISTYSURGE, Abilities::PRESSURE, Abilities::MOLDBREAKER, Abilities::UNNERVE,
    Abilities::DOWNLOAD, Abilities::INTREPIDSWORD, Abilities::DAUNTLESSSHIELD, Abilities::TRACE, Abilities::IMPOSTER,
    Abilities::ORICHALCUMPULSE, Abilities::HADRONENGINE, Abilities::SCREENCLEANER, Abilities::NEUTRALIZINGGAS,
    Abilities::TERAVOLT, Abilities::TURBOBLAZE, Abilities::SUPREMEOVERLORD];

const PARTY: [PokemonIndex; 6] = [PokemonIndex::P0, PokemonIndex::P1, PokemonIndex::P2, PokemonIndex::P3,
    PokemonIndex::P4, PokemonIndex::P5];
const SLOTS: [PokemonMoveIndex; 4] = [PokemonMoveIndex::M0, PokemonMoveIndex::M1, PokemonMoveIndex::M2,
    PokemonMoveIndex::M3];

#[derive(Clone, Copy, Default)]
struct Seen {
    active: bool,       // has been on the field
    moves: u8,          // bit i: move slot i has been used
    tera: bool,         // has Terastallized
    item: bool,         // item known
    ability: bool,      // ability known
}

pub struct Game {
    state: State,
    seen: [[Seen; 6]; 2], // seen[s][p]: what side 1-s knows of side s's party slot p
    start_items: [[Items; 6]; 2],
    rng: u64,
    turns: u32,
}

fn random(rng: &mut u64) -> f32 {
    *rng ^= *rng << 13;
    *rng ^= *rng >> 7;
    *rng ^= *rng << 17;
    (*rng >> 40) as f32 / (1u64 << 24) as f32
}

fn side(state: &State, s: usize) -> &Side {
    if s == 0 { &state.side_one } else { &state.side_two }
}

fn action_of(choice: &MoveChoice) -> usize {
    match choice {
        MoveChoice::Move(m) => *m as usize,
        MoveChoice::MoveTera(m) => 4 + *m as usize,
        MoveChoice::Switch(p) => 8 + *p as usize,
        MoveChoice::None => 0,
        _ => unreachable!("mega evolution and team preview are not used in gen9 random battles"),
    }
}

fn update_seen(game: &mut Game) {
    for s in 0..2 {
        let side = side(&game.state, s);
        let a = side.active_index as usize;
        let active = &side.pokemon[PARTY[a]];
        let seen = &mut game.seen[s][a];
        seen.active = true;
        seen.item |= VISIBLE_ITEMS.contains(&active.item);
        seen.ability |= ANNOUNCED_ABILITIES.contains(&active.ability);
        for p in 0..6 {
            game.seen[s][p].item |= side.pokemon[PARTY[p]].item != game.start_items[s][p];
        }
    }
}

/// A battle from a serialized poke-engine state (e.g. a gen9 random-battle position). Returns null on bad input.
#[no_mangle]
pub extern "C" fn pk9_new(serialized: *const c_char, seed: u64) -> *mut Game {
    let text = unsafe { CStr::from_ptr(serialized) }.to_str().unwrap();
    let state = State::deserialize(text);
    let start_items = [0, 1].map(|s| PARTY.map(|p| side(&state, s).pokemon[p].item));
    let mut game = Game { state, seen: [[Seen::default(); 6]; 2], start_items, rng: seed | 1, turns: 0 };
    update_seen(&mut game);
    Box::into_raw(Box::new(game))
}

#[no_mangle]
pub extern "C" fn pk9_free(game: *mut Game) {
    drop(unsafe { Box::from_raw(game) });
}

/// Legal actions of side s (0 or 1) as a byte mask of ACTIONS entries. Returns how many are legal.
#[no_mangle]
pub extern "C" fn pk9_mask(game: *const Game, s: i32, mask: *mut u8) -> i32 {
    let game = unsafe { &*game };
    let mask = unsafe { std::slice::from_raw_parts_mut(mask, ACTIONS) };
    mask.fill(0);
    let (o1, o2) = game.state.root_get_all_options();
    let options = if s == 0 { o1 } else { o2 };
    for o in &options {
        mask[action_of(o)] = 1;
    }
    options.len() as i32
}

/// Plays one decision for both sides. Returns 0 while the battle goes on, 1 if side one won, 2 if side two won,
/// 3 for a draw at the turn cap, 4 if the engine panicked (the position and moves go to stderr; the battle is over).
/// Random self-play over 800K games finds no panics, but a trained policy reaches other positions, and one engine
/// bug should cost a battle, not a training run.
#[no_mangle]
pub extern "C" fn pk9_step(game: *mut Game, a1: i32, a2: i32) -> i32 {
    let game = unsafe { &mut *game };
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| step(game, a1, a2))) {
        Ok(result) => result,
        Err(_) => {
            // The state may be part way through instruction generation; still the best hint for a repro.
            eprintln!("pk9_step engine panic: actions {} {} state {}", a1, a2, game.state.serialize());
            4
        }
    }
}

fn step(game: &mut Game, a1: i32, a2: i32) -> i32 {
    let (o1, o2) = game.state.root_get_all_options();
    let pick = |options: &Vec<MoveChoice>, a: i32| -> MoveChoice {
        options.iter().find(|o| action_of(o) == a as usize).expect("illegal action").clone()
    };
    let (m1, m2) = (pick(&o1, a1), pick(&o2, a2));
    for (s, m) in [(0, &m1), (1, &m2)] {
        let a = side(&game.state, s).active_index as usize;
        match m {
            MoveChoice::Move(i) => game.seen[s][a].moves |= 1 << *i as u8,
            MoveChoice::MoveTera(i) => { game.seen[s][a].moves |= 1 << *i as u8; game.seen[s][a].tera = true }
            _ => {}
        }
    }
    // branch_on_damage = true: critical hits and "this roll knocks out" become their own outcomes. Without it the
    // engine plays every hit at average damage and never crits (selfplay_bench: 300K vs 415K turns/s per core).
    let branches = generate_instructions_from_move_pair(&mut game.state, &m1, &m2, true);
    let total: f32 = branches.iter().map(|b| b.percentage.max(0.0)).sum();
    let mut x = random(&mut game.rng) * total;
    let mut chosen = branches.len() - 1;
    for (i, b) in branches.iter().enumerate() {
        x -= b.percentage.max(0.0);
        if x <= 0.0 {
            chosen = i;
            break;
        }
    }
    game.state.apply_instructions(&branches[chosen].instruction_list);
    update_seen(game);
    game.turns += 1;
    let over = game.state.battle_is_over();
    if over > 0.0 { 1 } else if over < 0.0 { 2 } else if game.turns >= MAX_TURNS { 3 } else { 0 }
}

fn one_hot(out: &mut [f32], i: usize) {
    out[i.min(out.len() - 1)] = 1.0;
}

fn boosts(out: &mut [f32], b: &poke_engine::choices::StatBoosts, scale: f32) {
    let v = [b.attack, b.defense, b.special_attack, b.special_defense, b.speed, b.accuracy];
    for (i, x) in v.iter().enumerate() {
        out[i] += *x as f32 * scale / 2.0;
    }
}

// What a move does besides damage: flags, drain/recoil/heal, stat changes (user, target), status it can inflict
// (weighted by chance), and volatile / side-condition / item-removal effects.
fn write_effects(f: &mut [f32], c: &Choice) {
    let fl = &c.flags;
    let flags = [fl.bite, fl.bullet, fl.charge, fl.contact, fl.drag, fl.heal, fl.powder, fl.protect, fl.pulse,
        fl.punch, fl.recharge, fl.reflectable, fl.slicing, fl.sound, fl.pivot, fl.wind];
    for (i, x) in flags.iter().enumerate() {
        f[i] = *x as u8 as f32;
    }
    f[16] = c.drain.unwrap_or(0.0);
    f[17] = c.recoil.unwrap_or(0.0) + c.crash.unwrap_or(0.0);
    if let Some(h) = &c.heal {
        f[18] = if h.target == MoveTarget::User { h.amount } else { -h.amount };
    }
    let (user, foe) = (19, 25);
    let status = 31;
    let extra = status + STATUSES;
    if let Some(b) = &c.boost {
        boosts(&mut f[if b.target == MoveTarget::User { user } else { foe }..], &b.boosts, 1.0);
    }
    if let Some(st) = &c.status {
        f[status + st.status as usize] += 1.0;
    }
    f[extra] = c.volatile_status.is_some() as u8 as f32;
    f[extra + 1] = c.side_condition.is_some() as u8 as f32;
    for sec in c.secondaries.iter().flatten() {
        let p = sec.chance / 100.0;
        match &sec.effect {
            Effect::Status(st) => f[status + *st as usize] += p,
            Effect::Boost(b) => boosts(&mut f[if sec.target == MoveTarget::User { user } else { foe }..], b, p),
            Effect::VolatileStatus(_) => f[extra] += p,
            Effect::RemoveItem => f[extra + 2] += p,
            Effect::Heal(h) => f[18] += h * p,
        }
        f[extra + 3] = f[extra + 3].max(p);
    }
}

fn write_mon(out: &mut [f32], p: &Pokemon, active: bool, seen: Option<&Seen>) {
    // seen = None: own Pokemon, everything known.
    let known = seen.map_or(true, |s| s.active);
    out[0] = known as u8 as f32;
    if !known {
        return;
    }
    out[1] = active as u8 as f32;
    out[2] = (p.hp <= 0) as u8 as f32;
    out[3] = p.hp.max(0) as f32 / p.maxhp.max(1) as f32;
    out[4] = p.maxhp as f32 / 500.0;
    out[5] = p.level as f32 / 100.0;
    let stats = [p.attack, p.defense, p.special_attack, p.special_defense, p.speed];
    for (i, v) in stats.iter().enumerate() {
        out[6 + i] = *v as f32 / 500.0;
    }
    let mut o = 11;
    one_hot(&mut out[o..o + TYPES], p.types.0 as usize);
    one_hot(&mut out[o..o + TYPES], p.types.1 as usize);
    o += TYPES;
    if seen.map_or(true, |s| s.tera) || p.terastallized {
        one_hot(&mut out[o..o + TYPES], p.tera_type as usize);
    }
    o += TYPES;
    out[o] = p.terastallized as u8 as f32;
    o += 1;
    one_hot(&mut out[o..o + STATUSES], p.status as usize);
    o += STATUSES;
    for (i, slot) in SLOTS.iter().enumerate() {
        let m = &p.moves[slot];
        let f = &mut out[o + i * MOVE_FEATURES..o + (i + 1) * MOVE_FEATURES];
        if !seen.map_or(true, |s| s.moves >> i & 1 == 1) {
            continue;
        }
        f[0] = 1.0;
        one_hot(&mut f[1..1 + TYPES], m.choice.move_type as usize);
        let c = match m.choice.category { MoveCategory::Physical => 0, MoveCategory::Special => 1, _ => 2 };
        f[1 + TYPES + c] = 1.0;
        let g = 1 + TYPES + 3;
        f[g] = m.choice.base_power / 150.0;
        f[g + 1] = m.choice.accuracy / 100.0;
        f[g + 2] = m.choice.priority as f32 / 5.0;
        f[g + 3] = m.pp.max(0) as f32 / 32.0 * (!m.disabled) as u8 as f32;
        write_effects(&mut f[g + 4..], &m.choice);
    }
    o += 4 * MOVE_FEATURES;
    if seen.map_or(true, |s| s.ability) {
        out[o + ability_slot(p.ability)] = 1.0;
    }
    o += ABILITY_SLOTS;
    if seen.map_or(true, |s| s.item) {
        out[o + item_slot(p.item)] = 1.0;
    }
}

fn write_side(out: &mut [f32], s: &Side) {
    let boosts = [s.attack_boost, s.defense_boost, s.special_attack_boost, s.special_defense_boost, s.speed_boost,
        s.accuracy_boost, s.evasion_boost];
    for (i, b) in boosts.iter().enumerate() {
        out[i] = *b as f32 / 6.0;
    }
    let c = &s.side_conditions;
    let conditions = [c.aurora_veil, c.crafty_shield, c.healing_wish, c.light_screen, c.lucky_chant, c.lunar_dance,
        c.mat_block, c.mist, c.protect, c.quick_guard, c.reflect, c.safeguard, c.spikes, c.stealth_rock, c.sticky_web,
        c.tailwind, c.toxic_count, c.toxic_spikes, c.wide_guard];
    for (i, v) in conditions.iter().enumerate() {
        out[7 + i] = *v as f32 / 4.0;
    }
    for i in 0..128 {
        out[26 + i] = (s.volatile_statuses.0 >> i & 1) as f32;
    }
    let active = s.pokemon[s.active_index].maxhp.max(1) as f32;
    out[154] = s.substitute_health as f32 / active;
    out[155] = s.force_switch as u8 as f32;
    out[156] = s.force_trapped as u8 as f32;
    out[157] = (s.wish.0 > 0) as u8 as f32;
}

thread_local!(static SCRATCH: std::cell::RefCell<Vec<f32>> = std::cell::RefCell::new(vec![0.0; OBS]));

/// The observation of side s (0 or 1) into out (OBS floats).
#[no_mangle]
pub extern "C" fn pk9_observe(game: *const Game, s: i32, out: *mut f32) {
    observe(unsafe { &*game }, s as usize, unsafe { std::slice::from_raw_parts_mut(out, OBS) });
}

/// The observation as bf16 (truncated from f32), for the trainer's bf16 rollouts: a straight device copy, half the bytes.
#[no_mangle]
pub extern "C" fn pk9_observe_bf16(game: *const Game, s: i32, out: *mut u16) {
    let game = unsafe { &*game };
    let dst = unsafe { std::slice::from_raw_parts_mut(out, OBS) };
    SCRATCH.with(|scratch| {
        let mut scratch = scratch.borrow_mut();
        observe(game, s as usize, &mut scratch);
        for (d, x) in dst.iter_mut().zip(scratch.iter()) {
            *d = (x.to_bits() >> 16) as u16;
        }
    });
}

fn observe(game: &Game, s: usize, out: &mut [f32]) {
    out.fill(0.0);
    for (k, who) in [s, 1 - s].iter().enumerate() {
        let side = side(&game.state, *who);
        for (i, p) in PARTY.iter().enumerate() {
            let at = (k * 6 + i) * MON_FEATURES;
            let seen = if k == 0 { None } else { Some(&game.seen[*who][i]) };
            write_mon(&mut out[at..at + MON_FEATURES], &side.pokemon[*p], side.active_index == *p, seen);
        }
        let at = 12 * MON_FEATURES + k * SIDE_FEATURES;
        write_side(&mut out[at..at + SIDE_FEATURES], side);
    }
    let f = &mut out[12 * MON_FEATURES + 2 * SIDE_FEATURES..];
    one_hot(&mut f[0..10], game.state.weather.weather_type as usize);
    one_hot(&mut f[10..16], game.state.terrain.terrain_type as usize);
    f[16] = game.state.weather.turns_remaining.max(0) as f32 / 8.0;
    f[17] = game.state.terrain.turns_remaining.max(0) as f32 / 8.0;
    f[18] = game.state.trick_room.active as u8 as f32;
    f[19] = game.turns as f32 / MAX_TURNS as f32;
}

#[no_mangle]
pub extern "C" fn pk9_obs_size() -> i32 {
    OBS as i32
}
