// Self-play speed (2026-10-29, PufferLib evaluation): random-move games from the gen9 random-battle positions, one
// sampled outcome per turn (weighted by StateInstructions.percentage, damage not branched), until the battle ends.
use poke_engine::engine::generate_instructions::generate_instructions_from_move_pair;
use poke_engine::state::State;
use rand::Rng;
fn main() {
    let text = std::fs::read_to_string("data/gen9randombattle.txt").unwrap();
    let states: Vec<State> = text.lines().filter(|l| !l.is_empty()).map(State::deserialize).collect();
    let games: usize = std::env::args().nth(1).map(|s| s.parse().unwrap()).unwrap_or(2000);
    let mut rng = rand::rng();
    let (mut turns, mut finished, mut capped) = (0u64, 0u64, 0u64);
    let start = std::time::Instant::now();
    for g in 0..games {
        let mut state = states[g % states.len()].clone();
        let mut t = 0;
        while state.battle_is_over() == 0.0 && t < 300 {
            let (o1, o2) = state.root_get_all_options();
            let m1 = o1[rng.random_range(0..o1.len())].clone();
            let m2 = o2[rng.random_range(0..o2.len())].clone();
            let branches = generate_instructions_from_move_pair(&mut state, &m1, &m2, false);
            let total: f32 = branches.iter().map(|b| b.percentage.max(0.0)).sum();
            let mut x = rng.random::<f32>() * total;
            let mut pick = branches.len() - 1;
            for (i, b) in branches.iter().enumerate() { x -= b.percentage.max(0.0); if x <= 0.0 { pick = i; break; } }
            state.apply_instructions(&branches[pick].instruction_list);
            t += 1;
        }
        turns += t as u64;
        if state.battle_is_over() != 0.0 { finished += 1 } else { capped += 1 }
    }
    let s = start.elapsed().as_secs_f64();
    println!("{} games ({} finished, {} hit 300 turns), {} turns in {:.2}s: {:.0} turns/s, {:.0} games/s, {:.1} turns/game (one core)",
        games, finished, capped, turns, s, turns as f64 / s, games as f64 / s, turns as f64 / games as f64);
}
