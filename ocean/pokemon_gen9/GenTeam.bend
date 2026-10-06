import Base
import ./Mem.bend as S
import ./Lists.bend as L
import ./Catalog.bend as C
import ./GenData.bend as D
import ./GenRng.bend as R
import ./GenSet.bend as Set
import ./data/species.bend as P
import ./data/moves.bend as V
import ./data/abilities.bend as A
import ./data/types.bend as T
import ./data/roles.bend as Roles
import ./data/Rules.bend as Rules

type Team is Data:
  Team{sets: +List<D.GeneratedSet>, size: U32, bases: +List<U32>, types: +List<U32>,
    weak: +List<U32>, double: +List<U32>, freeze: U32, max_level: U32,
    details: +List<U32>, lead: Bool}

def sets(t: Team) -> +List<D.GeneratedSet>:
  Team{ss, _, _, _, _, _, _, _, _, _} = t
  ss

def size(t: Team) -> U32:
  Team{_, n, _, _, _, _, _, _, _, _} = t
  n

def lead(t: Team) -> Bool:
  Team{_, _, _, _, _, _, _, _, _, l} = t
  l

def details(t: Team) -> +List<U32>:
  Team{_, _, _, _, _, _, _, _, ds, _} = t
  ds

def ids(ss: +List<D.GeneratedSet>) -> +List<U32>:
  match ss:
    case Nil{}: Nil{}
    case Con{s, rest}: D.set_id(s) <> ids(rest)

def append(ss: +List<D.GeneratedSet>, tail: +List<D.GeneratedSet>) -> +List<D.GeneratedSet>:
  match ss:
    case Nil{}: tail
    case Con{s, rest}: s <> append(rest, tail)

def pair_bad(+sid: U32, +current: +List<U32>, +left: +List<U32>, +right: +List<U32>) -> Bool:
  L.contains(left, sid) && U32.is_ne(L.length(L.filter_in(current, right)), 0) ||
    L.contains(right, sid) && U32.is_ne(L.length(L.filter_in(current, left)), 0)

def webs() -> +List<U32>:
  [P.species_ariados(), P.species_smeargle(), P.species_masquerain(), P.species_kricketune(),
    P.species_leavanny(), P.species_galvantula(), P.species_vikavolt(), P.species_ribombee(),
    P.species_araquanid(), P.species_spidops()]

def screen_setters() -> +List<U32>:
  [P.species_meowstic(), P.species_grimmsnarl(), P.species_ninetalesalola(), P.species_abomasnow()]

def compatible(+sid: U32, +current: +List<U32>) -> Bool:
  +web = webs()
  +screens = screen_setters()
  Bool.not(pair_bad(sid, current, [P.species_blissey()], [P.species_chansey()]) ||
    pair_bad(sid, current, [P.species_illumise()], [P.species_volbeat()]) ||
    pair_bad(sid, current, web, web) || pair_bad(sid, current, screens, screens) ||
    pair_bad(sid, current, [P.species_toxicroak()],
      [P.species_ninetales(), P.species_torkoal(), P.species_groudon(), P.species_koraidon()]))

def freeze(+mon: D.Species) -> Bool:
  +weak = L.at(D.weaknesses(mon), U32.to_nat(T.types_ice()))
  U32.is_gt(weak, 8) || U32.is_gt(weak, 6) && L.contains(D.types(mon), T.types_water())

def weak_ok(xs: +List<U32>, +counts: +List<U32>, +double: +List<U32>, +index: U32) -> Bool:
  match xs:
    case Nil{}: True{}
    case Con{+weak, rest}:
      (U32.is_le(weak, 8) || U32.is_lt(L.at(counts, U32.to_nat(index)), 3)) &&
      (U32.is_le(weak, 9) || U32.is_eq(L.at(double, U32.to_nat(index)), 0)) &&
      weak_ok(rest, counts, double, (index + 1 : U32))

def types_ok(xs: +List<U32>, +counts: +List<U32>) -> Bool:
  match xs:
    case Nil{}: True{}
    case Con{type_, rest}: U32.is_lt(L.at(counts, U32.to_nat(type_)), 2) && types_ok(rest, counts)

def eligible(+mon: D.Species, t: Team) -> Bool:
  Team{+ss, n, bases, types, +weak, double, freeze_, max_level, ds, _} = t
  +sid = D.species(mon)
  +fire = L.at(D.weaknesses(mon), U32.to_nat(T.types_fire()))
  +tera_special = L.contains([P.species_ogerpon(), P.species_ogerponhearthflame(), P.species_terapagos()], sid)
  Bool.not(L.contains(bases, D.base(mon))) &&
    Bool.not(tera_special && U32.is_ne(L.at(ds, 12n), 0)) &&
    Bool.not(U32.is_eq(D.base(mon), P.species_zoroark()) && U32.is_ge(n, 5)) &&
    types_ok(D.types(mon), types) && weak_ok(D.weaknesses(mon), weak, double, 0) &&
    Bool.not(U32.is_eq(fire, 8) &&
      (L.contains(D.native_abilities(mon), A.abilities_dryskin()) ||
       L.contains(D.native_abilities(mon), A.abilities_fluffy())) &&
      U32.is_ge(L.at(weak, U32.to_nat(T.types_fire())), 3)) &&
    Bool.not(freeze(mon) && U32.is_ge(freeze_, 4)) &&
    Bool.not(U32.is_eq(D.level(mon), 100) && U32.is_ge(max_level, 1)) &&
    compatible(sid, ids(ss))

def update_types(xs: +List<U32>, counts: +List<U32>) -> +List<U32>:
  match xs:
    case Nil{}: counts
    case Con{type_, rest}: update_types(rest, L.increment(counts, type_))

def update_weak(xs: +List<U32>, +counts: +List<U32>, +minimum: U32, +index: U32) -> +List<U32>:
  match xs:
    case Nil{}: counts
    case Con{weak, rest}:
      update_weak(rest,
        Bool.pick(+List<U32>, U32.is_gt(weak, minimum), L.increment(counts, index), counts),
        minimum, (index + 1 : U32))

def mark(+ds: +List<U32>, key: Nat, yes: Bool) -> +List<U32>:
  Bool.pick(+List<U32>, yes, L.replace(ds, key, 1), ds)

def update_details(+ds: +List<U32>, +s: D.GeneratedSet) -> +List<U32>:
  +ability = D.set_ability(s)
  +moves = D.set_moves(s)
  +ds = mark(ds, 0n, U32.is_eq(ability, A.abilities_drizzle()) || L.contains(moves, V.moves_raindance()))
  +ds = mark(ds, 1n, U32.is_eq(ability, A.abilities_drought()) || U32.is_eq(ability, A.abilities_orichalcumpulse()) ||
    L.contains(moves, V.moves_sunnyday()))
  +ds = mark(ds, 2n, U32.is_eq(ability, A.abilities_sandstream()))
  +ds = mark(ds, 3n, U32.is_eq(ability, A.abilities_snowwarning()) || L.contains(moves, V.moves_snowscape()) ||
    L.contains(moves, V.moves_chillyreception()))
  +ds = mark(ds, 4n, L.contains(moves, V.moves_healbell()))
  +ds = Bool.pick(+List<U32>, L.contains(moves, V.moves_spikes()) || L.contains(moves, V.moves_ceaselessedge()),
    L.increment(ds, 5), ds)
  +ds = mark(ds, 6n, L.contains(moves, V.moves_toxicspikes()) || U32.is_eq(ability, A.abilities_toxicdebris()))
  +ds = mark(ds, 7n, L.contains(moves, V.moves_stealthrock()) || L.contains(moves, V.moves_stoneaxe()))
  +ds = mark(ds, 8n, L.contains(moves, V.moves_stickyweb()))
  +ds = mark(ds, 9n, L.contains(moves, V.moves_defog()))
  +ds = mark(ds, 10n, L.contains(moves, V.moves_rapidspin()) || L.contains(moves, V.moves_mortalspin()))
  +ds = mark(ds, 11n, L.contains(moves, V.moves_auroraveil()) ||
    L.contains(moves, V.moves_reflect()) && L.contains(moves, V.moves_lightscreen()))
  mark(ds, 12n, U32.is_eq(D.set_role(s), Roles.roles_terablastuser()) ||
    L.contains([P.species_ogerpon(), P.species_ogerponhearthflame(), P.species_terapagos()], D.set_id(s)))

def track(t: Team, +mon: D.Species, +s: D.GeneratedSet) -> Team:
  Team{ss, n, bases, types, weak, double, freeze_, max_level, ds, lead_} = t
  +ability = D.set_ability(s)
  +weak = update_weak(D.weaknesses(mon), weak, 8, 0)
  weak = Bool.pick(+List<U32>,
    L.contains([A.abilities_dryskin(), A.abilities_fluffy()], ability) &&
      U32.is_eq(L.at(D.weaknesses(mon), U32.to_nat(T.types_fire())), 8),
    L.increment(weak, T.types_fire()), weak)
  Team{ss, n, D.base(mon) <> bases, update_types(D.types(mon), types), weak,
    update_weak(D.weaknesses(mon), double, 9, 0),
    (freeze_ + Bool.to_u32(freeze(mon)) : U32),
    (max_level + Bool.to_u32(U32.is_eq(D.set_level(s), 100)) : U32),
    update_details(ds, s), lead_}

def insert(t: Team, +s: D.GeneratedSet, +is_lead: Bool) -> Team:
  Team{+ss, n, bases, types, weak, double, freeze_, max_level, ds, lead_} = t
  Team{Bool.pick(+List<D.GeneratedSet>, is_lead, s <> ss, append(ss, [s])),
    (n + 1 : U32), bases, types, weak, double, freeze_, max_level, ds, lead_ && Bool.not(is_lead)}

def try_set(+t: Team, +mon: D.Species) -> S.Mem<Team>:
  +no_lead = L.contains(Rules.no_lead_pokemon(), D.species(mon))
  S.branch(Team, lead(t) && no_lead && U32.is_eq(size(t), 5), S.Mem.pure(Team, t),
    (do S.Mem<Team>:
      +s : D.GeneratedSet <- Set.get(mon, details(t), lead(t) && Bool.not(no_lead))
      +next : Team = insert(t, s, lead(t) && Bool.not(no_lead))
      return Bool.pick(Team, U32.is_eq(size(next), 6), next, track(next, mon, s))))

def loop(n: Nat, +pool: +List<U32>, +t: Team) -> S.Mem<Team>:
  match n:
    case 0n: S.Mem.pure(Team, t)
    case 1n+p:
      S.branch(Team, U32.is_eq(size(t), 6), S.Mem.pure(Team, t),
        (do S.Mem<Team>:
          +index : U32 <- R.random(L.length(pool))
          +base : U32 = L.at(pool, U32.to_nat(index))
          forms : +List<U32> <- C.form_pool(base)
          sid : U32 <- R.sample(forms)
          +mon : D.Species <- C.species(sid)
          next : Team <- S.branch(Team, eligible(mon, t), try_set(t, mon), S.Mem.pure(Team, t))
          loop(p, L.pop(pool, index), next)))

def get() -> S.Mem<+List<D.GeneratedSet>>:
  do S.Mem<+List<D.GeneratedSet>>:
    unused_type : U32 <- R.random(18)
    +pool : +List<U32> <- C.base_pool()
    +zero : +List<U32> = [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]
    +next : Team <- loop(U32.to_nat(L.length(pool)), pool,
      Team{Nil{}, 0, Nil{}, zero, zero, zero, 0, 0, [0,0,0,0,0,0,0,0,0,0,0,0,0], True{}})
    S.branch(+List<D.GeneratedSet>, U32.is_eq(size(next), 6),
      S.Mem.pure(+List<D.GeneratedSet>, sets(next)), S.raise(+List<D.GeneratedSet>, 2))
