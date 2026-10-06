import Base
import ./Mem.bend as S
import ./Lists.bend as L
import ./GenData.bend as D
import ./Catalog.bend as C
import ./data/moves.bend as V
import ./data/abilities.bend as A
import ./data/species.bend as P
import ./data/types.bend as T
import ./data/Rules.bend as Rules

# Keys 1..19 are native types. These indices represent MoveCounter's named
# counts; counts of qualifying damaging/base-power moves retain insertion order.
def count(c: D.Counter, key: U32) -> U32:
  D.C{counts, _, _} = c
  L.at(counts, U32.to_nat(key))

def damaging(c: D.Counter) -> +List<U32>:
  D.C{_, xs, _} = c
  xs

def base_power(c: D.Counter) -> +List<U32>:
  D.C{_, _, xs} = c
  xs

def add_count_when(yes: Bool, c: D.Counter, key: U32) -> D.Counter:
  match yes:
    case False{}: c
    case True{}:
      D.C{counts, dmg, bp} = c
      D.C{L.increment(counts, key), dmg, bp}

def add_count(c: D.Counter, key: U32, yes: Bool) -> D.Counter:
  add_count_when(yes, c, key)

def add_damaging_when(yes: Bool, c: D.Counter, +id: U32) -> D.Counter:
  match yes:
    case False{}: c
    case True{}:
      D.C{counts, +dmg, bp} = c
      D.C{counts, L.append(dmg, L.keep_member(Bool.not(L.contains(dmg, id)), id, Nil{})), bp}

def add_damaging(c: D.Counter, id: U32, yes: Bool) -> D.Counter:
  add_damaging_when(yes, c, id)

def add_base_power_when(yes: Bool, c: D.Counter, +id: U32) -> D.Counter:
  match yes:
    case False{}: c
    case True{}:
      D.C{counts, dmg, +bp} = c
      D.C{counts, dmg, L.append(bp, L.keep_member(Bool.not(L.contains(bp, id)), id, Nil{}))}

def add_base_power(c: D.Counter, id: U32, yes: Bool) -> D.Counter:
  add_base_power_when(yes, c, id)

def move_type(+id: U32, +info: D.Info, +native: U32) -> U32:
  +mon = D.mon(info)
  +sid = D.species(mon)
  +abilities = D.abilities(info)
  +terrain = L.pick(L.contains(abilities, A.abilities_grassysurge()) || L.contains(abilities, A.abilities_seedsower()),
    T.types_grass(), L.pick(L.contains(abilities, A.abilities_psychicsurge()), T.types_psychic(),
      L.pick(L.contains(abilities, A.abilities_electricsurge()), T.types_electric(), native)))
  +weather = L.pick(L.contains(abilities, A.abilities_chlorophyll()) || L.contains(abilities, A.abilities_drought()) ||
    L.contains(abilities, A.abilities_megasol()) || L.contains(abilities, A.abilities_orichalcumpulse()),
    T.types_fire(), L.pick(L.contains(abilities, A.abilities_drizzle()), T.types_water(), native))
  +normal = L.pick(L.contains(abilities, A.abilities_aerilate()), T.types_flying(),
    L.pick(L.contains(abilities, A.abilities_galvanize()), T.types_electric(),
      L.pick(L.contains(abilities, A.abilities_pixilate()), T.types_fairy(),
        L.pick(L.contains(abilities, A.abilities_refrigerate()), T.types_ice(), native))))
  +bull = L.pick(U32.is_eq(sid, P.species_taurospaldeacombat()), T.types_fighting(),
    L.pick(U32.is_eq(sid, P.species_taurospaldeablaze()), T.types_fire(),
      L.pick(U32.is_eq(sid, P.species_taurospaldeaaqua()), T.types_water(), native)))
  +ivy = L.pick(U32.is_eq(sid, P.species_ogerponwellspring()), T.types_water(),
    L.pick(U32.is_eq(sid, P.species_ogerponhearthflame()), T.types_fire(),
      L.pick(U32.is_eq(sid, P.species_ogerponcornerstone()), T.types_rock(), native)))
  L.pick(U32.is_eq(id, V.moves_terablast()), D.tera(info),
    L.pick(L.contains([V.moves_judgment(), V.moves_multiattack(), V.moves_revelationdance()], id), D.first(mon),
      L.pick(U32.is_eq(id, V.moves_ragingbull()), bull,
        L.pick(U32.is_eq(id, V.moves_ivycudgel()), ivy,
          L.pick(U32.is_eq(id, V.moves_terrainpulse()), terrain,
            L.pick(U32.is_eq(id, V.moves_weatherball()), weather,
              L.pick(U32.is_eq(native, T.types_normal()), normal, native)))))))

def get_type(+id: U32, info: D.Info) -> S.Mem<U32>:
  do S.Mem<U32>:
    native : U32 <- C.move(id, 1)
    return move_type(id, info, native)

def add_flags(c: D.Counter, +flags: U32, +id: U32, +actual_id: U32, +info: D.Info, +move_type_: U32,
  +category: U32, +power: U32, +accuracy: U32, +priority: U32) -> D.Counter:
  +fixed = U32.is_ne((flags .&. 5 : U32), 0)
  +bp = U32.is_ne(power, 0) || U32.is_ne((flags .&. 2 : U32), 0)
  +qualifies = bp && (Bool.not(L.contains(Rules.no_stab(), id)) ||
    L.contains(Rules.priority_pokemon(), D.species(D.mon(info))) && U32.is_gt(priority, 16))
  c = add_count(c, 20, Bool.not(fixed) && U32.is_eq(category, 1))
  c = add_count(c, 21, Bool.not(fixed) && U32.is_eq(category, 2))
  c = add_count(c, 22, Bool.not(fixed) && U32.is_eq(category, 3))
  c = add_count(c, 23, fixed)
  c = add_count(c, 24, U32.is_eq(id, V.moves_lowkick()) ||
    U32.is_ne(power, 0) && U32.is_le(power, 60) && Bool.not(L.contains([V.moves_nuzzle(), V.moves_rapidspin()], id)))
  c = add_count(c, 25, U32.is_ne((flags .&. 64 : U32), 0))
  c = add_count(c, 26, U32.is_ne((flags .&. 24 : U32), 0))
  c = add_count(c, 27, U32.is_ne((flags .&. 32 : U32), 0))
  c = add_count(c, move_type_, qualifies)
  c = add_count(c, 28, qualifies && L.contains(D.types(D.mon(info)), move_type_))
  c = add_count(c, 29, qualifies && U32.is_eq(D.tera(info), move_type_))
  c = add_count(c, 30, bp && U32.is_ne((flags .&. 128 : U32), 0))
  c = add_count(c, 31, bp && U32.is_ne((flags .&. 256 : U32), 0))
  c = add_count(c, 32, bp && U32.is_ne((flags .&. 512 : U32), 0))
  c = add_count(c, 33, bp && (U32.is_gt(priority, 16) ||
    U32.is_eq(id, V.moves_grassyglide()) && L.contains(D.abilities(info), A.abilities_grassysurge())))
  c = add_count(c, 34, U32.is_ne((flags .&. 3072 : U32), 0))
  c = add_count(c, 35, U32.is_ne(accuracy, 0) && U32.is_lt(accuracy, 90))
  c = add_count(c, 36, L.contains(Rules.recovery_moves(), id))
  c = add_count(c, 37, L.contains(Rules.contrary_moves(), id))
  c = add_count(c, 38, L.contains(Rules.physical_setup(), id))
  c = add_count(c, 39, L.contains(Rules.special_setup(), id))
  c = add_count(c, 40, L.contains(Rules.mixed_setup(), id))
  c = add_count(c, 41, L.contains(Rules.speed_setup(), id))
  c = add_count(c, 42, L.contains(Rules.speed_control(), id))
  c = add_count(c, 43, L.contains(Rules.setup(), id))
  c = add_count(c, 44, L.contains(Rules.hazards(), id))
  c = add_damaging(c, actual_id, fixed || qualifies)
  add_base_power(c, actual_id, bp)

def query_moves(xs: +List<U32>, +info: D.Info, c: D.Counter) -> S.Mem<D.Counter>:
  match xs:
    case Nil{}: S.Mem.pure(D.Counter, c)
    case Con{+id, rest}:
      +lookup = L.pick(U32.is_eq(id, V.moves_naturepower()), V.moves_triattack(), id)
      do S.Mem<D.Counter>:
        category : U32 <- C.move(lookup, 0)
        native : U32 <- C.move(lookup, 1)
        power : U32 <- C.move(lookup, 2)
        accuracy : U32 <- C.move(lookup, 3)
        priority : U32 <- C.move(lookup, 4)
        flags : U32 <- C.move(lookup, 6)
        query_moves(rest, info, add_flags(c, flags, id, lookup, info, move_type(lookup, info, native),
          category, power, accuracy, priority))

def empty() -> D.Counter:
  D.C{[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0], Nil{}, Nil{}}

def query_cached(cache: D.CounterCache, moves: +List<U32>, info: D.Info) -> S.Mem<D.Counter>:
  match cache:
    case D.Counted{counter}: S.Mem.pure(D.Counter, counter)
    case D.Uncounted{}: query_moves(moves, info, empty())

def query(b: D.Builder) -> S.Mem<D.Counter>:
  D.B{info, _, moves, cache} = b
  query_cached(cache, moves, info)

def cache(+b: D.Builder) -> S.Mem<D.Builder>:
  do S.Mem<D.Builder>:
    counter : D.Counter <- query(b)
    return D.with_counter(b, counter)
