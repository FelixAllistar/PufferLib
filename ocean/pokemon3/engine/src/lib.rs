// C ABI over poke-engine (Gen 3 singles) for ocean/pokemon3/pokemon3.h (2026-11-06; port of ocean/pokemon9's).
// One Game is one battle: a poke-engine State, the information each side has seen of the other, and an RNG.
// Each step plays one turn (or one forced-switch decision) for both sides and applies a single outcome sampled by
// the engine's branch probabilities, with damage branching (crits, KO rolls) on.
//
// Actions (POKEMON3_ACTIONS = 128): in battle 0-3 move slot, 4-9 switch to party slot. A side with nothing to choose
// (the other side's forced switch) gets only action 0, which means "wait". The rest are for the free-pick draft
// (pokemon3.h): a species of the draft catalog (0-127), then one of its sets (0-5).
//
// Observation (pk3_observe, POKEMON3_OBS values), from one side's point of view, partial information like a player:
// own six Pokemon first, then the opponent's six. Opponent Pokemon are blank until they have been active, their moves
// until used. One-hot ability and item (vocab.rs, every one seen in Gen 3 random battles) and move effect features.
// An opponent's item shows once it changes (consumed, knocked off, tricked) or is Leftovers (it heals in view every
// turn); its ability once Showdown's Gen 3 announces it on switch-in (ANNOUNCED_ABILITIES). Other reveals (Rough Skin
// damage, a Choice lock...) are not modelled yet. Species is given by stats and types only. Gen 3 has no
// Terastallization, terrain or Trick Room, so those parts of the Gen 9 observation are gone.
mod basestats;
mod vocab;
use vocab::{ability_slot, item_slot, ABILITY_SLOTS, ITEM_SLOTS};
use poke_engine::choices::{Choice, Effect, MoveTarget};
use poke_engine::engine::abilities::Abilities;
use poke_engine::engine::items::Items;
use poke_engine::engine::generate_instructions::generate_instructions_from_move_pair;
use poke_engine::engine::state::MoveChoice;
use poke_engine::choices::MoveCategory;
use poke_engine::engine::abilities::ability_on_switch_in;
use poke_engine::instruction::StateInstructions;
use poke_engine::state::{Pokemon, PokemonIndex, PokemonMoveIndex, Side, SideReference, State};
use poke_engine::engine::damage_calc::type_effectiveness_modifier;
use poke_engine::choices::Choices;
use poke_engine::engine::choice_effects::modify_choice;
use poke_engine::engine::abilities::{ability_modify_attack_against, ability_modify_attack_being_used};
use poke_engine::engine::damage_calc::{calculate_damage, DamageRolls};
use poke_engine::engine::items::{item_modify_attack_against, item_modify_attack_being_used};
use poke_engine::state::PokemonType;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::OnceLock;
use std::ffi::{c_char, CStr};

pub const ACTIONS: usize = 128;
const TYPES: usize = 20;
const STATUSES: usize = 7;
const EFFECT_FEATURES: usize = 16 + 3 + 12 + STATUSES + 4;
const MOVE_FEATURES: usize = 1 + TYPES + 3 + 4 + EFFECT_FEATURES;
const MON_FEATURES: usize = 6 + 5 + TYPES + STATUSES + 4 * MOVE_FEATURES + ABILITY_SLOTS + ITEM_SLOTS;
const SIDE_FEATURES: usize = 7 + 19 + 128 + 4;
// Weather (5), weather turns, turn count; then the draft (zero in battle): drafting, picking a species, picking a set,
// Pokemon drafted / 6.
const FIELD_FEATURES: usize = 5 + 2 + 4;
// Matchups (2026-11-06; zero when pk3_set_matchup(0), the A/B control), appended after the field so the older
// offsets stay. Per Pokemon (own six, then the opponent's), against the other side's active Pokemon: per move slot its
// type effectiveness / 4, max-roll damage as a fraction of the target's max HP (capped at 1) and whether that knocks
// the target out; then the most damage (same fraction) and the highest effectiveness any of the other active's
// moves would deal it. The opponent's Pokemon use estimated stats (estimated_stats) and only moves seen; blank
// Pokemon stay blank. Computed by the engine's own damage calc (abilities, items, weather, screens, boosts of the
// active; a benched attacker is computed as if it were in, without the boosts).
const MATCHUP_MON: usize = 4 * 3 + 2;
const MATCHUP_FEATURES: usize = 12 * MATCHUP_MON;
pub const OBS: usize = 12 * MON_FEATURES + 2 * SIDE_FEATURES + FIELD_FEATURES + MATCHUP_FEATURES;
static MATCHUP: AtomicBool = AtomicBool::new(true);

/// Matchup features on (1, the default) or zero (0). Process wide.
#[no_mangle]
pub extern "C" fn pk3_set_matchup(on: i32) {
    MATCHUP.store(on != 0, Ordering::Relaxed);
}
const MAX_TURNS: u32 = 500;

// Shown to the opponent as soon as the holder is on the field (Leftovers heals in view at the end of every turn).
const VISIBLE_ITEMS: [Items; 1] = [Items::LEFTOVERS];
// Announced on switch-in by Showdown's Gen 3 ("-ability" or the weather it starts; Pressure is silent in Gen 3).
const ANNOUNCED_ABILITIES: [Abilities; 5] = [Abilities::INTIMIDATE, Abilities::DROUGHT, Abilities::DRIZZLE,
    Abilities::SANDSTREAM, Abilities::TRACE];

const PARTY: [PokemonIndex; 6] = [PokemonIndex::P0, PokemonIndex::P1, PokemonIndex::P2, PokemonIndex::P3,
    PokemonIndex::P4, PokemonIndex::P5];
const SLOTS: [PokemonMoveIndex; 4] = [PokemonMoveIndex::M0, PokemonMoveIndex::M1, PokemonMoveIndex::M2,
    PokemonMoveIndex::M3];

#[derive(Clone, Copy, Default)]
struct Seen {
    active: bool,       // has been on the field
    moves: u8,          // bit i: move slot i has been used
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
        MoveChoice::Switch(p) => 4 + *p as usize,
        MoveChoice::None => 0,
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

/// A battle from a serialized poke-engine state (e.g. a gen3 random-battle position). Returns null on bad input.
#[no_mangle]
pub extern "C" fn pk3_new(serialized: *const c_char, seed: u64) -> *mut Game {
    let text = unsafe { CStr::from_ptr(serialized) }.to_str().unwrap();
    Box::into_raw(Box::new(game_from(State::deserialize(text), seed)))
}

fn game_from(state: State, seed: u64) -> Game {
    let start_items = [0, 1].map(|s| PARTY.map(|p| side(&state, s).pokemon[p].item));
    let mut game = Game { state, seen: [[Seen::default(); 6]; 2], start_items, rng: seed | 1, turns: 0 };
    update_seen(&mut game);
    game
}

// Team mode (2026-11-06): a table of Pokemon (ocean/pokemon3/build_sets.js, one poke-engine Pokemon string per line)
// that teams index. Loaded once per process.
static SETS: OnceLock<Vec<String>> = OnceLock::new();

/// Loads the set table at path (once per process; later calls return the loaded count). Returns the number of sets,
/// or -1 if the file cannot be read.
#[no_mangle]
pub extern "C" fn pk3_sets_load(path: *const c_char) -> i64 {
    if let Some(sets) = SETS.get() { return sets.len() as i64; }
    let path = unsafe { CStr::from_ptr(path) }.to_str().unwrap_or("");
    match std::fs::read_to_string(path) {
        Ok(text) => SETS.get_or_init(|| text.lines().map(String::from).collect()).len() as i64,
        Err(_) => -1,
    }
}

/// valid[i] = 1 if set i parses as a Pokemon (unknown moves, items or abilities do not).
#[no_mangle]
pub extern "C" fn pk3_sets_check(valid: *mut u8) {
    let sets = SETS.get().expect("pk3_sets_load first");
    let valid = unsafe { std::slice::from_raw_parts_mut(valid, sets.len()) };
    let hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(|_| {}));
    for (v, s) in valid.iter_mut().zip(sets) {
        *v = std::panic::catch_unwind(|| { Pokemon::deserialize(s); }).is_ok() as u8;
    }
    std::panic::set_hook(hook);
}

fn side_string(team: &[i32]) -> String {
    let sets = SETS.get().expect("pk3_sets_load first");
    let mut parts: Vec<String> = team.iter().map(|&i| sets[i as usize].clone()).collect();
    // Active slot 0 (the lead), no side conditions, volatiles, durations, substitute, boosts, wish or last move: the
    // fields of ocean/pokemon9/showdown_convert.js sideString at the start of a battle.
    parts.push("0".into());
    parts.push(vec!["0"; 19].join(";"));
    parts.push(String::new());
    parts.push("0;0;0;0;0;0".into());
    parts.extend(["0"; 12].iter().map(|x| x.to_string()));
    parts.extend(["false", "NONE", "false", "false", "false", "move:none", "false"].iter().map(|x| x.to_string()));
    parts.join("=")
}

/// A battle between two teams of six set indices (lead first), as Showdown starts it: both leads in, their switch-in
/// abilities (Trace, Intimidate, Drizzle / Drought / Sand Stream) applied by the engine in speed order, faster first (a
/// speed tie by coin flip), so the slower lead's weather stands. Turn 0.
#[no_mangle]
pub extern "C" fn pk3_new_teams(t1: *const i32, t2: *const i32, seed: u64) -> *mut Game {
    let (t1, t2) = unsafe { (std::slice::from_raw_parts(t1, 6), std::slice::from_raw_parts(t2, 6)) };
    let text = format!("{}/{}/NONE;0/NONE;0/false;0/false", side_string(t1), side_string(t2));
    let mut state = State::deserialize(&text);
    let mut rng = seed | 1;
    let (v1, v2) = (state.side_one.get_active_immutable().speed, state.side_two.get_active_immutable().speed);
    let one_first = v1 > v2 || (v1 == v2 && random(&mut rng) < 0.5);
    let order = if one_first { [SideReference::SideOne, SideReference::SideTwo] }
        else { [SideReference::SideTwo, SideReference::SideOne] };
    let mut instructions = StateInstructions::default();
    for side_ref in order.iter() {
        ability_on_switch_in(&mut state, side_ref, &mut instructions);
    }
    Box::into_raw(Box::new(game_from(state, rng)))
}

/// Test hook: the battle's state, serialized (to compare a built battle with Showdown's).
#[no_mangle]
pub extern "C" fn pk3_serialize(game: *const Game, out: *mut c_char, cap: i32) -> i32 {
    write_c(&unsafe { &*game }.state.serialize(), out, cap)
}

/// Free-pick draft observation (out: OBS floats, or bf16 if bf16 != 0). Own slots: the n_own sets drafted so far
/// (lead first), as in battle at full HP. The opponent's slots (hidden in the draft) show the n_cand candidate sets
/// when picking a set. Draft features: picking = 0 waiting / done, 1 a species, 2 a set.
#[no_mangle]
pub extern "C" fn pk3_draft_observe(own: *const i32, n_own: i32, cand: *const i32, n_cand: i32, picking: i32,
                                    out: *mut std::ffi::c_void, bf16: i32) {
    let sets = SETS.get().expect("pk3_sets_load first");
    let own = unsafe { std::slice::from_raw_parts(own, n_own.max(0) as usize) };
    let cand = unsafe { std::slice::from_raw_parts(cand, n_cand.max(0) as usize) };
    SCRATCH.with(|scratch| {
        let mut scratch = scratch.borrow_mut();
        let f = &mut scratch[..];
        f.fill(0.0);
        for (k, list) in [own, cand].iter().enumerate() {
            for (i, &x) in list.iter().take(6).enumerate() {
                let at = (k * 6 + i) * MON_FEATURES;
                write_mon(&mut f[at..at + MON_FEATURES], &Pokemon::deserialize(&sets[x as usize]), false, None);
            }
        }
        let d = 12 * MON_FEATURES + 2 * SIDE_FEATURES + 7;
        f[d] = 1.0;
        f[d + 1] = (picking == 1) as u8 as f32;
        f[d + 2] = (picking == 2) as u8 as f32;
        f[d + 3] = own.len() as f32 / 6.0;
        if bf16 != 0 {
            let dst = unsafe { std::slice::from_raw_parts_mut(out as *mut u16, OBS) };
            for (d, x) in dst.iter_mut().zip(f.iter()) { *d = (x.to_bits() >> 16) as u16; }
        } else {
            unsafe { std::slice::from_raw_parts_mut(out as *mut f32, OBS) }.copy_from_slice(f);
        }
    });
}

#[no_mangle]
pub extern "C" fn pk3_free(game: *mut Game) {
    drop(unsafe { Box::from_raw(game) });
}

/// Legal actions of side s (0 or 1) as a byte mask of ACTIONS entries. Returns how many are legal.
#[no_mangle]
pub extern "C" fn pk3_mask(game: *const Game, s: i32, mask: *mut u8) -> i32 {
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
pub extern "C" fn pk3_step(game: *mut Game, a1: i32, a2: i32) -> i32 {
    let game = unsafe { &mut *game };
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| step(game, a1, a2))) {
        Ok(result) => result,
        Err(_) => {
            // The state may be part way through instruction generation; still the best hint for a repro.
            eprintln!("pk3_step engine panic: actions {} {} state {}", a1, a2, game.state.serialize());
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
        if let MoveChoice::Move(i) = m {
            game.seen[s][a].moves |= 1 << *i as u8;
        }
    }
    // branch_on_damage = true: critical hits and "this roll knocks out" become their own outcomes. Without it the
    // engine plays every hit at average damage and never crits.
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

/// Max HP and stats from base stats at the Pokemon's level with 85 EVs, 31 IVs and a neutral nature (how Gen 3 random
/// battles build most sets; the Showdown bridge's tracker.js estimates the same way). Real values if the species has no
/// Gen 3 base stats.
fn estimated_stats(p: &Pokemon) -> (i16, [i16; 5]) {
    match basestats::base(p.id) {
        Some(b) => {
            let l = p.level as i32;
            let stat = |x: u16| ((2 * x as i32 + 31 + 21) * l / 100 + 5) as i16;
            let hp = if b[0] == 1 { 1 } else { ((2 * b[0] as i32 + 31 + 21) * l / 100 + l + 10) as i16 };
            (hp, [stat(b[1]), stat(b[2]), stat(b[3]), stat(b[4]), stat(b[5])])
        }
        None => (p.maxhp, [p.attack, p.defense, p.special_attack, p.special_defense, p.speed]),
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
    // The opponent's max HP and stats as a player estimates them (estimated_stats), not their real ones: the real
    // ones would give away the set (a 0 Attack EV special attacker, an OU spread) where Showdown never shows them.
    let (maxhp, stats) = match seen {
        Some(_) => estimated_stats(p),
        None => (p.maxhp, [p.attack, p.defense, p.special_attack, p.special_defense, p.speed]),
    };
    out[4] = maxhp as f32 / 500.0;
    out[5] = p.level as f32 / 100.0;
    for (i, v) in stats.iter().enumerate() {
        out[6 + i] = *v as f32 / 500.0;
    }
    let mut o = 11;
    one_hot(&mut out[o..o + TYPES], p.types.0 as usize);
    one_hot(&mut out[o..o + TYPES], p.types.1 as usize);
    o += TYPES;
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
    // Bit 0 is PokemonVolatileStatus::NONE: never set in play, but a serialize/deserialize round trip (the Showdown
    // bridge, pk3_view) sets it, so it is left out.
    for i in 1..128 {
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
pub extern "C" fn pk3_observe(game: *const Game, s: i32, out: *mut f32) {
    observe(unsafe { &*game }, s as usize, unsafe { std::slice::from_raw_parts_mut(out, OBS) });
}

/// The observation as bf16 (truncated from f32), for the trainer's bf16 rollouts: a straight device copy, half the bytes.
#[no_mangle]
pub extern "C" fn pk3_observe_bf16(game: *const Game, s: i32, out: *mut u16) {
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

fn side_ref(s: usize) -> SideReference {
    if s == 0 { SideReference::SideOne } else { SideReference::SideTwo }
}

fn side_mut(state: &mut State, s: usize) -> &mut Side {
    if s == 0 { &mut state.side_one } else { &mut state.side_two }
}

/// Max-roll damage (no crit) of attacker's move slot m against the defender's active, with attacker's party slot a
/// put in as its active (boosts cleared if it was benched; the state is restored). None: a status move, or nothing to
/// hit. The engine's damage calc with the move, ability and item modifiers of before_move (Explosion, Reversal,
/// Levitate, Thick Fat, Choice Band...), and the fixed-damage moves as calculate_damage_rolls has them, without its clone of the state.
fn move_damage(st: &mut State, attacker: usize, a: usize, m: usize) -> Option<i16> {
    let att = side_mut(st, attacker);
    let saved = (att.active_index, [att.attack_boost, att.defense_boost, att.special_attack_boost,
        att.special_defense_boost, att.speed_boost, att.accuracy_boost, att.evasion_boost]);
    if att.active_index as usize != a {
        att.active_index = PARTY[a];
        att.attack_boost = 0;
        att.defense_boost = 0;
        att.special_attack_boost = 0;
        att.special_defense_boost = 0;
        att.speed_boost = 0;
        att.accuracy_boost = 0;
        att.evasion_boost = 0;
    }
    let result = damage_of_active(st, attacker, m);
    let att = side_mut(st, attacker);
    att.active_index = saved.0;
    [att.attack_boost, att.defense_boost, att.special_attack_boost, att.special_defense_boost, att.speed_boost,
        att.accuracy_boost, att.evasion_boost] = saved.1;
    result
}

fn damage_of_active(st: &State, attacker: usize, m: usize) -> Option<i16> {
    let p = side(st, attacker).get_active_immutable();
    let defender = side(st, 1 - attacker).get_active_immutable();
    let mut choice = p.moves[&SLOTS[m]].choice.clone();
    if p.hp <= 0 || defender.hp <= 0 || choice.category == MoveCategory::Status || choice.category == MoveCategory::Switch {
        return None;
    }
    let immune = |t: PokemonType| type_effectiveness_modifier(&t, defender) == 0.0;
    match choice.move_id {
        Choices::SEISMICTOSS => return if immune(PokemonType::NORMAL) { None } else { Some(p.level as i16) },
        Choices::NIGHTSHADE => return if immune(PokemonType::GHOST) { None } else { Some(p.level as i16) },
        Choices::SUPERFANG => return if immune(PokemonType::NORMAL) { None } else { Some(defender.hp / 2) },
        _ => {}
    }
    choice.flags.charge = false;
    let r = side_ref(attacker);
    let defending = Choice::default();
    modify_choice(st, &mut choice, &defending, &r);
    ability_modify_attack_being_used(st, &mut choice, &defending, &r);
    ability_modify_attack_against(st, &mut choice, &defending, &r);
    item_modify_attack_being_used(st, &mut choice, &r);
    item_modify_attack_against(st, &mut choice, &r);
    calculate_damage(st, &r, &choice, DamageRolls::Max).map(|d| d.0)
}

/// The state as side s sees it: the opponent's Pokemon with estimated max HP and stats (HP scaled to the same
/// fraction), as tracker.js builds them on Showdown.
fn estimated_view(game: &Game, s: usize) -> State {
    let mut st = game.state.clone();
    let foe = side_mut(&mut st, 1 - s);
    for p in PARTY.iter() {
        let mon = &mut foe.pokemon[*p];
        let (maxhp, stats) = estimated_stats(mon);
        if mon.hp > 0 {
            mon.hp = ((mon.hp as i32 * maxhp as i32 + mon.maxhp.max(1) as i32 - 1) / mon.maxhp.max(1) as i32).max(1) as i16;
        }
        mon.maxhp = maxhp;
        mon.attack = stats[0];
        mon.defense = stats[1];
        mon.special_attack = stats[2];
        mon.special_defense = stats[3];
        mon.speed = stats[4];
    }
    st
}

fn write_matchups(out: &mut [f32], game: &Game, s: usize) {
    let mut st = estimated_view(game, s);
    for (k, who) in [s, 1 - s].iter().map(|w| (0, *w)).enumerate().map(|(k, (_, w))| (k, w)) {
        let other_active = side(&st, 1 - who).active_index as usize;
        for i in 0..6 {
            if k == 1 && !game.seen[who][i].active {
                continue;
            }
            if side(&st, who).pokemon[PARTY[i]].hp <= 0 {
                continue;
            }
            let f = &mut out[(k * 6 + i) * MATCHUP_MON..(k * 6 + i + 1) * MATCHUP_MON];
            for m in 0..4 {
                if k == 1 && game.seen[who][i].moves >> m & 1 == 0 {
                    continue;
                }
                let mv = &side(&st, who).pokemon[PARTY[i]].moves[&SLOTS[m]];
                if mv.choice.category == MoveCategory::Status {
                    continue;
                }
                let target = side(&st, 1 - who).get_active_immutable();
                f[3 * m] = type_effectiveness_modifier(&mv.choice.move_type, target) / 4.0;
                let (thp, tmax) = (target.hp, target.maxhp.max(1));
                if let Some(d) = move_damage(&mut st, who, i, m) {
                    f[3 * m + 1] = (d as f32 / tmax as f32).min(1.0);
                    f[3 * m + 2] = (d >= thp) as u8 as f32;
                }
            }
            // What the other side's active would deal this Pokemon if it came in (or is in): its moves (seen ones only
            // when the other side is the opponent) against party slot i put in as the active.
            let mine = side_mut(&mut st, who);
            let saved = mine.active_index;
            mine.active_index = PARTY[i];
            let (hp_max, types) = { let p = &mine.pokemon[PARTY[i]]; (p.maxhp.max(1), p.clone()) };
            for m in 0..4 {
                if k == 0 && game.seen[1 - who][other_active].moves >> m & 1 == 0 {
                    continue;
                }
                let mv = &side(&st, 1 - who).pokemon[PARTY[other_active]].moves[&SLOTS[m]];
                if mv.choice.category == MoveCategory::Status {
                    continue;
                }
                f[13] = f[13].max(type_effectiveness_modifier(&mv.choice.move_type, &types) / 4.0);
                if let Some(x) = move_damage(&mut st, 1 - who, other_active, m) {
                    f[12] = f[12].max((x as f32 / hp_max as f32).min(1.0));
                }
            }
            side_mut(&mut st, who).active_index = saved;
        }
    }
}

/// The max-damage baseline (bot_policy 2), with full information: the legal move with the most expected damage
/// (max roll x accuracy, capped at the target's HP) against the opponent's active; status moves score 0 and it never
/// switches by choice. Forced switches go to the healthiest Pokemon. As ocean/pokemon3/showdown's maxdmg.mjs, but
/// with the engine's damage calc instead of base power x STAB x effectiveness.
#[no_mangle]
pub extern "C" fn pk3_maxdmg_action(game: *const Game, s: i32) -> i32 {
    let game = unsafe { &*game };
    let s = s as usize;
    let (o1, o2) = game.state.root_get_all_options();
    let options = if s == 0 { o1 } else { o2 };
    let me = side(&game.state, s);
    let foe_hp = side(&game.state, 1 - s).get_active_immutable().hp.max(1) as f32;
    let (mut best, mut score) = (None, -1.0f32);
    for o in &options {
        if let MoveChoice::Move(m) = o {
            let a = me.active_index as usize;
            let mv = &me.pokemon[PARTY[a]].moves[m];
            let d = damage_of_active(&game.state, s, *m as usize).unwrap_or(0) as f32;
            let x = d.min(foe_hp) * mv.choice.accuracy.min(100.0) / 100.0;
            if x > score {
                score = x;
                best = Some(o);
            }
        }
    }
    if best.is_none() {
        let mut hp = -1.0f32;
        for o in &options {
            if let MoveChoice::Switch(p) = o {
                let mon = &me.pokemon[*p];
                let f = mon.hp as f32 / mon.maxhp.max(1) as f32;
                if f > hp {
                    hp = f;
                    best = Some(o);
                }
            }
        }
    }
    best.or(options.first()).map_or(0, |o| action_of(o) as i32)
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
    one_hot(&mut f[0..5], game.state.weather.weather_type as usize);
    f[5] = game.state.weather.turns_remaining.max(0) as f32 / 8.0;
    f[6] = game.turns as f32 / MAX_TURNS as f32;
    if MATCHUP_FEATURES > 0 && MATCHUP.load(Ordering::Relaxed) {
        let at = 12 * MON_FEATURES + 2 * SIDE_FEATURES + FIELD_FEATURES;
        write_matchups(&mut out[at..at + MATCHUP_FEATURES], game, s);
    }
}

/// Showdown bridge (2026-11-06): the observation of side one for a tracked live battle. STATE is a poke-engine state
/// with side one = us (opponent Pokemon we have not seen are placeholders; the observation blanks them). SEEN gives,
/// for the opponent's six party slots, "active,moves,item,ability" joined by ';' (active and item/ability 0 or 1; moves a
/// bitmask of used slots). TURNS is the turn number. Returns 0, or -1 if the state does not parse.
#[no_mangle]
pub extern "C" fn pk3_view(state: *const c_char, seen: *const c_char, turns: i32, out: *mut f32) -> i32 {
    let state = unsafe { CStr::from_ptr(state) }.to_str().unwrap_or("").to_string();
    let seen = unsafe { CStr::from_ptr(seen) }.to_str().unwrap_or("").to_string();
    let out = unsafe { std::slice::from_raw_parts_mut(out, OBS) };
    let built = std::panic::catch_unwind(|| {
        let state = State::deserialize(&state);
        let mut game = Game { state, seen: [[Seen::default(); 6]; 2], start_items: [[Items::NONE; 6]; 2], rng: 1,
            turns: turns.max(0) as u32 };
        for (p, f) in seen.split(';').enumerate().take(6) {
            let v: Vec<u8> = f.split(',').map(|x| x.parse().unwrap_or(0)).collect();
            if v.len() == 4 {
                game.seen[1][p] = Seen { active: v[0] != 0, moves: v[1], item: v[2] != 0, ability: v[3] != 0 };
            }
        }
        game
    });
    match built {
        Ok(game) => { observe(&game, 0, out); 0 }
        Err(_) => -1,
    }
}

fn write_c(text: &str, out: *mut c_char, cap: i32) -> i32 {
    let bytes = text.as_bytes();
    if bytes.len() + 1 > cap as usize { return -1; }
    unsafe {
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), out as *mut u8, bytes.len());
        *out.add(bytes.len()) = 0;
    }
    bytes.len() as i32
}

/// Test hook (showdown/check_view.c): the battle as pk3_view's STATE and SEEN strings, and its turn count, so a check can
/// rebuild side one's observation through the bridge path and compare it with pk3_observe.
#[no_mangle]
pub extern "C" fn pk3_debug_view(game: *const Game, state: *mut c_char, seen: *mut c_char, cap: i32) -> i32 {
    let game = unsafe { &*game };
    let s = game.seen[1].iter().map(|x| format!("{},{},{},{}", x.active as u8, x.moves, x.item as u8, x.ability as u8))
        .collect::<Vec<_>>().join(";");
    if write_c(&game.state.serialize(), state, cap) < 0 || write_c(&s, seen, cap) < 0 { return -1; }
    game.turns as i32
}

#[no_mangle]
pub extern "C" fn pk3_obs_size() -> i32 {
    OBS as i32
}
