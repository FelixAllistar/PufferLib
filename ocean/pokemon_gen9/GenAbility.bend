import Base
import ./Mem.bend as S
import ./Lists.bend as L
import ./GenData.bend as D
import ./GenCounter.bend as Counter
import ./GenRng.bend as R
import ./Catalog.bend as C
import ./data/abilities.bend as A
import ./data/moves.bend as V
import ./data/species.bend as P
import ./data/types.bend as T

def team_has(info: D.Info, key: Nat) -> Bool:
  U32.is_ne(L.at(D.team(info), key), 0)

def cull(+ability: U32, +info: D.Info, +moves: +List<U32>, +counter: D.Counter) -> Bool:
  ((U32.is_eq(ability, A.abilities_chlorophyll()) || U32.is_eq(ability, A.abilities_solarpower())) &&
    Bool.not(team_has(info, 1n))) ||
  (U32.is_eq(ability, A.abilities_defiant()) && U32.is_eq(D.species(D.mon(info)), P.species_thundurus()) &&
    U32.is_ne(Counter.count(counter, 22), 0)) ||
  ((U32.is_eq(ability, A.abilities_hydration()) || U32.is_eq(ability, A.abilities_swiftswim())) &&
    Bool.not(team_has(info, 0n))) ||
  (U32.is_eq(ability, A.abilities_ironfist()) && U32.is_eq(Counter.count(counter, 31), 0)) ||
  (U32.is_eq(ability, A.abilities_skilllink()) && U32.is_eq(Counter.count(counter, 25), 0)) ||
  (U32.is_eq(ability, A.abilities_overgrow()) && U32.is_eq(Counter.count(counter, T.types_grass()), 0)) ||
  (U32.is_eq(ability, A.abilities_prankster()) && U32.is_eq(Counter.count(counter, 22), 0)) ||
  ((U32.is_eq(ability, A.abilities_sandforce()) || U32.is_eq(ability, A.abilities_sandrush())) &&
    Bool.not(team_has(info, 2n))) ||
  (U32.is_eq(ability, A.abilities_slushrush()) && Bool.not(team_has(info, 3n))) ||
  (U32.is_eq(ability, A.abilities_swarm()) && U32.is_eq(Counter.count(counter, T.types_bug()), 0)) ||
  (U32.is_eq(ability, A.abilities_torrent()) && U32.is_eq(Counter.count(counter, T.types_water()), 0) &&
    Bool.not(L.contains(moves, V.moves_flipturn())))

def allowed(xs: +List<U32>, +info: D.Info, +moves: +List<U32>, +counter: D.Counter) -> +List<U32>:
  match xs:
    case Nil{}: Nil{}
    case Con{+head, tail}:
      L.keep_member(Bool.not(cull(head, info, moves, counter)), head, allowed(tail, info, moves, counter))

def special(+info: D.Info, +moves: +List<U32>, +counter: D.Counter, fire_weak: Bool) -> U32:
  +species = D.species(D.mon(info))
  +abilities = D.abilities(info)
  L.pick(U32.is_eq(species, P.species_drifblim()),
    L.pick(L.contains(moves, V.moves_defog()), A.abilities_aftermath(), A.abilities_unburden()),
    L.pick(L.contains(abilities, A.abilities_flashfire()) && fire_weak, A.abilities_flashfire(),
      L.pick((U32.is_eq(species, P.species_thundurus()) || U32.is_eq(species, P.species_tornadus())) &&
        U32.is_eq(Counter.count(counter, 20), 0), A.abilities_prankster(),
        L.pick(U32.is_eq(species, P.species_swampert()) &&
          (U32.is_ne(Counter.count(counter, T.types_water()), 0) || L.contains(moves, V.moves_flipturn())),
          A.abilities_torrent(),
          L.pick(U32.is_eq(species, P.species_toucannon()) && U32.is_ne(Counter.count(counter, 25), 0),
            A.abilities_skilllink(),
            L.pick(L.contains(abilities, A.abilities_slushrush()) && L.contains(moves, V.moves_snowscape()),
              A.abilities_slushrush(),
              L.pick(U32.is_eq(species, P.species_golduck()) && team_has(info, 0n), A.abilities_swiftswim(), 0)))))))

def pick_weather(yes: Bool, available: +List<U32>, original: +List<U32>) -> S.Mem<U32>:
  match yes:
    case True{}: R.sample(available)
    case False{}: R.sample(original)

def fallback(yes: Bool, available: +List<U32>, +original: +List<U32>) -> S.Mem<U32>:
  match yes:
    case True{}: R.sample(available)
    case False{}:
      +weather = L.filter_in(original, [A.abilities_chlorophyll(), A.abilities_hydration(),
        A.abilities_sandforce(), A.abilities_sandrush(), A.abilities_slushrush(),
        A.abilities_solarpower(), A.abilities_swiftswim()])
      pick_weather(U32.is_ne(L.length(weather), 0), weather, original)

def choose_special(yes: Bool, hardcode: U32, +info: D.Info, +moves: +List<U32>, +counter: D.Counter) -> S.Mem<U32>:
  match yes:
    case True{}: S.Mem.pure(U32, hardcode)
    case False{}:
      +abilities = D.abilities(info)
      +available = allowed(abilities, info, moves, counter)
      fallback(U32.is_ne(L.length(available), 0), available, abilities)

def multiple(yes: Bool, +info: D.Info, +moves: +List<U32>, +counter: D.Counter, fire_weak: Bool) -> S.Mem<U32>:
  match yes:
    case False{}: S.Mem.pure(U32, L.at(D.abilities(info), 0n))
    case True{}:
      +hardcode = special(info, moves, counter, fire_weak)
      choose_special(U32.is_ne(hardcode, 0), hardcode, info, moves, counter)

def get(+b: D.Builder) -> S.Mem<U32>:
  +info = D.info(b)
  do S.Mem<U32>:
    c : D.Counter <- Counter.query(b)
    effectiveness : U32 <- C.effectiveness(T.types_fire(), D.tera(info))
    multiple(U32.is_gt(L.length(D.abilities(info)), 1), info, D.moves(b), c, U32.is_ge(effectiveness, 9))
