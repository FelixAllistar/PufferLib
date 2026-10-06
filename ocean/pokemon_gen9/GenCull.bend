import Base
import ./Lists.bend as L
import ./GenData.bend as D
import ./data/moves.bend as V
import ./data/abilities.bend as A
import ./data/species.bend as P
import ./data/types.bend as T
import ./data/Rules.bend as Rules

type TeamCull is Data:
  TC{enabled: Bool, ids: +List<U32>}

def done(+b: D.Builder) -> Bool:
  U32.is_le((L.length(D.moves(b)) + L.length(D.pool(b)) : U32), 4)

def choose(-A: Data, yes: Bool, left: Result<&2, &2, U32, A>,
  right: Result<&2, &2, U32, A>) -> Result<&2, &2, U32, A>:
  match yes:
    case True{}: left
    case False{}: right

def remove_checked(yes: Bool, xs: +List<U32>, id: U32) -> Result<&2, &2, U32, +List<U32>>:
  match yes:
    case True{}: Done{L.remove(xs, id)}
    case False{}: Fail{2}

def remove(+xs: +List<U32>, +id: U32) -> Result<&2, &2, U32, +List<U32>>:
  remove_checked(L.contains(xs, id), xs, id)

def remove_present(yes: Bool, +b: D.Builder, id: U32) -> Result<&2, &2, U32, D.Builder>:
  match yes:
    case False{}: Done{b}
    case True{}:
      do Result<&2, &2, U32, D.Builder>:
        pool : +List<U32> <- remove(D.pool(b), id)
        return D.with_pool(b, pool)

def remove_if_present(+b: D.Builder, +id: U32) -> Result<&2, &2, U32, D.Builder>:
  remove_present(L.contains(D.pool(b), id), b, id)

def remove_ids(ids: +List<U32>, b: D.Builder) -> Result<&2, &2, U32, D.Builder>:
  match ids:
    case Nil{}: Done{b}
    case Con{id, tail}:
      do Result<&2, &2, U32, D.Builder>:
        next : D.Builder <- remove_if_present(b, id)
        remove_ids(tail, next)

def unpaired(pairs: +List<D.MovePair>, +original: +List<U32>,
  +remaining: +List<U32>) -> Result<&2, &2, U32, +List<U32>>:
  match pairs:
    case Nil{}: Done{remaining}
    case Con{D.Pair{+left, +right}, tail}:
      +first = L.at(left, 0n)
      +second = L.at(right, 0n)
      do Result<&2, &2, U32, +List<U32>>:
        next : +List<U32> <- choose(+List<U32>,
          L.contains(original, first) && L.contains(original, second),
          (do Result<&2, &2, U32, +List<U32>>:
            step : +List<U32> <- remove(remaining, first)
            remove(step, second)),
          Done{remaining})
        unpaired(tail, original, next)

def no_room(pairs: +List<D.MovePair>, +b: D.Builder) -> Result<&2, &2, U32, D.Builder>:
  match pairs:
    case Nil{}: Done{b}
    case Con{D.Pair{+left, +right}, tail}:
      +first = L.at(left, 0n)
      +second = L.at(right, 0n)
      do Result<&2, &2, U32, D.Builder>:
        next : D.Builder <- choose(D.Builder,
          L.contains(D.pool(b), first) && L.contains(D.pool(b), second),
          remove_ids([first, second], b), Done{b})
        no_room(tail, next)

def pairs_two(+b: D.Builder) -> Result<&2, &2, U32, D.Builder>:
  do Result<&2, &2, U32, D.Builder>:
    +xs : +List<U32> <- unpaired(Rules.move_pairs(), D.pool(b), D.pool(b))
    choose(D.Builder, U32.is_eq(L.length(xs), 1), remove_if_present(b, L.at(xs, 0n)), Done{b})

def pair_culls(+b: D.Builder) -> Result<&2, &2, U32, D.Builder>:
  do Result<&2, &2, U32, D.Builder>:
    +next : D.Builder <- choose(D.Builder, U32.is_eq(L.length(D.moves(b)), 2), pairs_two(b), Done{b})
    choose(D.Builder, U32.is_eq(L.length(D.moves(next)), 3), no_room(Rules.move_pairs(), next), Done{next})

def screens(+b: D.Builder) -> Result<&2, &2, U32, D.Builder>:
  do Result<&2, &2, U32, D.Builder>:
    +next : D.Builder <- remove_if_present(b, V.moves_auroraveil())
    choose(D.Builder, U32.is_ge(L.length(D.pool(next)), 6),
      remove_ids([V.moves_reflect(), V.moves_lightscreen()], next), Done{next})

# One incompatibility: selected move insertion order, then A, then B.
def incompat_remove(ids: +List<U32>, +selected: U32, +b: D.Builder) -> Result<&2, &2, U32, D.Builder>:
  match ids:
    case Nil{}: Done{b}
    case Con{+id, tail}:
      do Result<&2, &2, U32, D.Builder>:
        +next : D.Builder <- choose(D.Builder,
          Bool.not(done(b)) && U32.is_ne(id, selected),
          remove_if_present(b, id), Done{b})
        incompat_remove(tail, selected, next)

def incompat_selected(xs: +List<U32>, +left: +List<U32>, +right: +List<U32>,
  +b: D.Builder) -> Result<&2, &2, U32, D.Builder>:
  match xs:
    case Nil{}: Done{b}
    case Con{+id, rest}:
      do Result<&2, &2, U32, D.Builder>:
        +next : D.Builder <- choose(D.Builder,
          Bool.not(done(b)) && L.contains(right, id), incompat_remove(left, id, b), Done{b})
        next2 : D.Builder <- choose(D.Builder,
          Bool.not(done(next)) && L.contains(left, id), incompat_remove(right, id, next), Done{next})
        incompat_selected(rest, left, right, next2)

def incompat(+b: D.Builder, left: +List<U32>, right: +List<U32>) -> Result<&2, &2, U32, D.Builder>:
  incompat_selected(D.moves(b), left, right, b)

def incompat_pairs(pairs: +List<D.MovePair>, b: D.Builder) -> Result<&2, &2, U32, D.Builder>:
  match pairs:
    case Nil{}: Done{b}
    case Con{D.Pair{left, right}, rest}:
      do Result<&2, &2, U32, D.Builder>:
        next : D.Builder <- incompat(b, left, right)
        incompat_pairs(rest, next)

def special_pairs(+b: D.Builder) -> Result<&2, &2, U32, D.Builder>:
  +info = D.info(b)
  +types = D.types(D.mon(info))
  +sid = D.species(D.mon(info))
  do Result<&2, &2, U32, D.Builder>:
    +next : D.Builder <- incompat_pairs(Rules.incompatible_pairs(), b)
    +next : D.Builder <- choose(D.Builder, Bool.not(L.contains(types, T.types_ice())),
      incompat(next, [V.moves_icebeam()], [V.moves_icywind()]), Done{next})
    +next : D.Builder <- incompat(next, [V.moves_taunt()], [V.moves_encore()])
    +next : D.Builder <- choose(D.Builder,
      Bool.not(L.contains(types, T.types_dark())) && U32.is_ne(D.tera(info), T.types_dark()),
      incompat(next, [V.moves_knockoff()], [V.moves_suckerpunch()]), Done{next})
    +next : D.Builder <- choose(D.Builder,
      Bool.not(L.contains(D.abilities(info), A.abilities_prankster())),
      incompat(next, [V.moves_thunderwave()], [V.moves_yawn()]), Done{next})
    +next : D.Builder <- choose(D.Builder, U32.is_eq(sid, P.species_barraskewda()),
      incompat(next, [V.moves_psychicfangs(), V.moves_throatchop()], [V.moves_poisonjab(), V.moves_throatchop()]), Done{next})
    +next : D.Builder <- choose(D.Builder, U32.is_eq(sid, P.species_quagsire()),
      incompat(next, [V.moves_spikes()], [V.moves_icebeam()]), Done{next})
    +next : D.Builder <- choose(D.Builder, U32.is_eq(sid, P.species_cyclizar()),
      incompat(next, [V.moves_taunt()], [V.moves_knockoff()]), Done{next})
    choose(D.Builder, U32.is_eq(sid, P.species_coalossal()),
      incompat(next, [V.moves_flamethrower()], [V.moves_overheat()]), Done{next})

def team_culls(rules: +List<TeamCull>, +b: D.Builder) -> Result<&2, &2, U32, D.Builder>:
  match rules:
    case Nil{}: special_pairs(b)
    case Con{TC{+enabled, ids}, rest}:
      do Result<&2, &2, U32, D.Builder>:
        +next : D.Builder <- choose(D.Builder, enabled, remove_ids(ids, b), Done{b})
        choose(D.Builder, enabled && done(next), Done{next}, team_culls(rest, next))

def team_rules(+info: D.Info) -> +List<TeamCull>:
  +t = D.team(info)
  [TC{U32.is_ne(L.at(t, 8n), 0), [V.moves_stickyweb()]},
   TC{U32.is_ne(L.at(t, 7n), 0), [V.moves_stealthrock()]},
   TC{U32.is_ne(L.at(t, 9n), 0) || U32.is_ne(L.at(t, 10n), 0), [V.moves_defog(), V.moves_rapidspin()]},
   TC{U32.is_ne(L.at(t, 6n), 0), [V.moves_toxicspikes()]},
   TC{U32.is_ge(L.at(t, 5n), 2), [V.moves_spikes()]},
   TC{U32.is_ne(L.at(t, 4n), 0), [V.moves_healbell()]}]

def cull_active(+b: D.Builder) -> Result<&2, &2, U32, D.Builder>:
  +info = D.info(b)
  do Result<&2, &2, U32, D.Builder>:
    +next : D.Builder <- pair_culls(b)
    next2 : D.Builder <- choose(D.Builder, U32.is_ne(L.at(D.team(info), 11n), 0), screens(next), Done{next})
    team_culls(team_rules(info), next2)

def cull(+b: D.Builder) -> Result<&2, &2, U32, D.Builder>:
  choose(D.Builder, done(b), Done{b}, cull_active(b))
