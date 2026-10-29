// Differential check helper (2026-10-29, PufferLib engine-diff): for each stdin line "STATE\tMOVE1\tMOVE2", prints one
// line with every outcome poke-engine generates for that move pair (damage rolls branched), as
// "percentage|summary" entries joined by "\t". The summary is what the Showdown side records too: per side the
// active Pokémon, every Pokémons HP and status, and the actives boosts.
use poke_engine::engine::generate_instructions::generate_instructions_from_move_pair;
use poke_engine::engine::state::MoveChoice;
use poke_engine::state::{Side, State};
use std::io::BufRead;
fn side(s: &Side) -> String {
    let mons: Vec<String> = s.pokemon.pkmn.iter().map(|p| format!("{}:{}:{:?}", p.id, p.hp, p.status)).collect();
    format!("{}#{}#{},{},{},{},{}", s.pokemon.pkmn[s.active_index as usize].id, mons.join(";"), s.attack_boost,
        s.defense_boost, s.special_attack_boost, s.special_defense_boost, s.speed_boost)
}
fn main() {
    for line in std::io::stdin().lock().lines() {
        let line = line.unwrap();
        let f: Vec<&str> = line.split("\t").collect();
        let mut state = State::deserialize(f[0]);
        let (m1, m2) = match (MoveChoice::from_string(f[1], &state.side_one), MoveChoice::from_string(f[2], &state.side_two)) {
            (Some(a), Some(b)) => (a, b),
            _ => { println!("ERROR bad move {} / {}", f[1], f[2]); continue; }
        };
        let branches = generate_instructions_from_move_pair(&mut state, &m1, &m2, true);
        let mut out = Vec::new();
        for b in branches {
            state.apply_instructions(&b.instruction_list);
            out.push(format!("{}|{}/{}", b.percentage, side(&state.side_one), side(&state.side_two)));
            state.reverse_instructions(&b.instruction_list);
        }
        println!("{}", out.join("\t"));
    }
}
