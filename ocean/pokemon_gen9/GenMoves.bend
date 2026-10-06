import Base
import ./Mem.bend as S
import ./Lists.bend as L
import ./Catalog.bend as C
import ./GenData.bend as D
import ./GenRng.bend as R
import ./GenCounter.bend as Q
import ./GenCull.bend as Cull
import ./data/moves.bend as V
import ./data/species.bend as P
import ./data/abilities.bend as A
import ./data/types.bend as T
import ./data/roles.bend as Roles
import ./data/Rules.bend as Rules

def has(+b: D.Builder, id: U32) -> Bool:
  L.contains(D.pool(b), id)

def role(b: D.Builder, id: U32) -> Bool:
  U32.is_eq(D.role(D.info(b)), id)

def team(b: D.Builder, key: Nat) -> Bool:
  U32.is_ne(L.at(D.team(D.info(b)), key), 0)

def ability(b: D.Builder, id: U32) -> Bool:
  L.contains(D.abilities(D.info(b)), id)

def add_present(yes: Bool, +b: D.Builder, +id: U32) -> S.Mem<D.Builder>:
  match yes:
    case False{}: S.raise(D.Builder, 2)
    case True{}:
      do S.Mem<D.Builder>:
        +current : D.Counter <- Q.query(b)
        counter : D.Counter <- S.branch(D.Counter, Bool.not(L.contains(D.moves(b), id)),
          Q.query_moves([id], D.info(b), current), S.Mem.pure(D.Counter, current))
        S.from_result(D.Builder, Cull.cull(D.with_counter(D.with_moves(
          D.with_pool(b, L.remove(D.pool(b), id)),
          L.append(D.moves(b), L.keep_member(Bool.not(L.contains(D.moves(b), id)), id, Nil{}))), counter)))

def add(+b: D.Builder, +id: U32) -> S.Mem<D.Builder>:
  add_present(has(b, id), b, id)

def add_if(yes: Bool, +b: D.Builder, +id: U32) -> S.Mem<D.Builder>:
  S.branch(D.Builder, yes && has(b, id), add(b, id), S.Mem.pure(D.Builder, b))

def fixed(ids: +List<U32>, +b: D.Builder) -> S.Mem<D.Builder>:
  match ids:
    case Nil{}: S.Mem.pure(D.Builder, b)
    case Con{id, rest}:
      do S.Mem<D.Builder>:
        next : D.Builder <- add_if(True{}, b, id)
        fixed(rest, next)

def sampled_empty(yes: Bool, xs: +List<U32>, +b: D.Builder) -> S.Mem<D.Builder>:
  match yes:
    case True{}: S.Mem.pure(D.Builder, b)
    case False{}:
      do S.Mem<D.Builder>:
        id : U32 <- R.sample(xs)
        add(b, id)

def sampled(+xs: +List<U32>, b: D.Builder) -> S.Mem<D.Builder>:
  sampled_empty(U32.is_eq(L.length(xs), 0), xs, b)

def sample_if(yes: Bool, xs: +List<U32>, +b: D.Builder) -> S.Mem<D.Builder>:
  S.branch(D.Builder, yes, sampled(xs, b), S.Mem.pure(D.Builder, b))

# Candidate filters preserve the current pool's insertion order.
type Filter is Data:
  Priority{}
  Stab{types: +List<U32>}
  Attacking{}
  Coverage{current: U32}

def qualifies(f: Filter, +id: U32, +info: D.Info, +type_: U32,
  category: U32, +bp: Bool, +allowed: Bool, priority: U32) -> Bool:
  match f:
    case Priority{}:
      bp && L.contains(D.types(D.mon(info)), type_) &&
        (U32.is_gt(priority, 16) || U32.is_eq(id, V.moves_grassyglide()) &&
          L.contains(D.abilities(info), A.abilities_grassysurge()))
    case Stab{types}: allowed && bp && L.contains(types, type_)
    case Attacking{}: allowed && U32.is_ne(category, 3)
    case Coverage{current}: allowed && bp && U32.is_ne(current, type_)

def filter(xs: +List<U32>, +f: Filter, +info: D.Info) -> S.Mem<+List<U32>>:
  match xs:
    case Nil{}: S.Mem.pure(+List<U32>, Nil{})
    case Con{+id, rest}:
      do S.Mem<+List<U32>>:
        type_ : U32 <- Q.get_type(id, info)
        category : U32 <- C.move(id, 0)
        power : U32 <- C.move(id, 2)
        flags : U32 <- C.move(id, 6)
        priority : U32 <- C.move(id, 4)
        tail : +List<U32> <- filter(rest, f, info)
        return L.keep_member(qualifies(f, id, info, type_, category,
          U32.is_ne(power, 0) || U32.is_ne((flags .&. 2 : U32), 0),
          Bool.not(L.contains(Rules.no_stab(), id)), priority), id, tail)

def filter_sample(f: Filter, +b: D.Builder) -> S.Mem<D.Builder>:
  do S.Mem<D.Builder>:
    xs : +List<U32> <- filter(D.pool(b), f, D.info(b))
    sampled(xs, b)

def filter_if(yes: Bool, f: Filter, +b: D.Builder) -> S.Mem<D.Builder>:
  S.branch(D.Builder, yes, filter_sample(f, b), S.Mem.pure(D.Builder, b))

def enforce(+type_: U32, +b: D.Builder, +c: D.Counter) -> Bool:
  +info = D.info(b)
  +mon = D.mon(info)
  +types = D.types(mon)
  +sid = D.species(mon)
  +atk = L.at(D.stats(mon), 1n)
  +none = U32.is_eq(Q.count(c, type_), 0)
  Bool.pick(Bool, U32.is_eq(type_, T.types_bug()),
    has(b, V.moves_megahorn()) || has(b, V.moves_xscissor()) || none && (L.contains(types, T.types_electric()) || L.contains(types, T.types_psychic())),
    Bool.pick(Bool, U32.is_eq(type_, T.types_dark()),
    none || U32.is_lt(Q.count(c, type_), 2) && role(b, Roles.roles_wallbreaker()) && L.contains(Rules.priority_pokemon(), sid),
    Bool.pick(Bool, U32.is_eq(type_, T.types_grass()),
    none && (has(b, V.moves_leafstorm()) || U32.is_ge(atk, 100) || L.contains(types, T.types_electric()) || ability(b, A.abilities_seedsower())),
    Bool.pick(Bool, U32.is_eq(type_, T.types_ice()),
    has(b, V.moves_freezedry()) || has(b, V.moves_blizzard()) || none,
    Bool.pick(Bool, U32.is_eq(type_, T.types_normal()),
    has(b, V.moves_boomburst()) || has(b, V.moves_hypervoice()),
    Bool.pick(Bool, U32.is_eq(type_, T.types_poison()),
    none && Bool.not(L.contains(types, T.types_ground())),
    Bool.pick(Bool, U32.is_eq(type_, T.types_psychic()),
    U32.is_eq(sid, P.species_bruxish()) && has(b, V.moves_psychicfangs()) || U32.is_eq(sid, P.species_hoopaunbound()) && has(b, V.moves_psychic()) || none && Bool.not(L.contains(types, T.types_dark()) || L.contains(types, T.types_steel()) || L.contains(types, T.types_water())),
    Bool.pick(Bool, U32.is_eq(type_, T.types_rock()),
    none && U32.is_ge(atk, 80),
    Bool.pick(Bool, U32.is_eq(type_, T.types_steel()),
    none && (U32.is_ge(atk, 90) || has(b, V.moves_gigatonhammer()) || has(b, V.moves_makeitrain())),
    Bool.pick(Bool, U32.is_eq(type_, T.types_water()),
    none && Bool.not(L.contains(types, T.types_ground())),
    none))))))))))

def stab_step(n: Nat, +candidates: +List<U32>, +type_: U32, +b: D.Builder) -> S.Mem<D.Builder>:
  match n:
    case 0n: S.Mem.pure(D.Builder, b)
    case 1n+p:
      do S.Mem<D.Builder>:
        counter : D.Counter <- Q.query(b)
        S.branch(D.Builder, enforce(type_, b, counter),
          (do S.Mem<D.Builder>:
            +id : U32 <- R.sample(candidates)
            next : D.Builder <- add(b, id)
            stab_step(p, L.remove(candidates, id), type_, next)),
          S.Mem.pure(D.Builder, b))

def stabs(types: +List<U32>, +b: D.Builder) -> S.Mem<D.Builder>:
  match types:
    case Nil{}: S.Mem.pure(D.Builder, b)
    case Con{+type_, rest}:
      do S.Mem<D.Builder>:
        +xs : +List<U32> <- filter(D.pool(b), Stab{[type_]}, D.info(b))
        next : D.Builder <- stab_step(U32.to_nat(L.length(xs)), xs, type_, b)
        stabs(rest, next)

def pair_fill(pairs: +List<D.MovePair>, +id: U32, +b: D.Builder) -> S.Mem<D.Builder>:
  match pairs:
    case Nil{}: S.Mem.pure(D.Builder, b)
    case Con{D.Pair{left, right}, rest}:
      +first = L.at(left, 0n)
      +second = L.at(right, 0n)
      do S.Mem<D.Builder>:
        +next : D.Builder <- add_if(U32.is_eq(id, first), b, second)
        next2 : D.Builder <- add_if(U32.is_eq(id, second), next, first)
        pair_fill(rest, id, next2)

def all_remaining(+b: D.Builder) -> D.Builder:
  D.with_moves(b, L.append(D.moves(b), D.pool(b)))

def fill(n: Nat, +b: D.Builder) -> S.Mem<D.Builder>:
  match n:
    case 0n: S.Mem.pure(D.Builder, b)
    case 1n+p:
      S.branch(D.Builder, U32.is_lt(L.length(D.moves(b)), 4) && U32.is_ne(L.length(D.pool(b)), 0),
        S.branch(D.Builder, Cull.done(b), S.Mem.pure(D.Builder, all_remaining(b)),
          (do S.Mem<D.Builder>:
            +id : U32 <- R.sample(D.pool(b))
            next : D.Builder <- add(b, id)
            next2 : D.Builder <- pair_fill(Rules.move_pairs(), id, next)
            fill(p, next2))),
        S.Mem.pure(D.Builder, b))

def coverage(+b: D.Builder) -> S.Mem<D.Builder>:
  do S.Mem<D.Builder>:
    +counter : D.Counter <- Q.query(b)
    S.branch(D.Builder, U32.is_eq(L.length(Q.damaging(counter)), 1),
      (do S.Mem<D.Builder>:
        current : U32 <- C.move(L.at(Q.damaging(counter), 0n), 1)
        filter_sample(Coverage{current}, b)),
      S.Mem.pure(D.Builder, b))

def late(+b: D.Builder) -> S.Mem<D.Builder>:
  do S.Mem<D.Builder>:
    +c : D.Counter <- Q.query(b)
    +b : D.Builder <- filter_if(U32.is_eq(L.length(Q.damaging(c)), 0), Attacking{}, b)
    +b : D.Builder <- S.branch(D.Builder,
      Bool.not(role(b, Roles.roles_avpivot()) || role(b, Roles.roles_fastsupport()) ||
        role(b, Roles.roles_bulkysupport())), coverage(b), S.Mem.pure(D.Builder, b))
    fill(U32.to_nat(L.length(D.pool(b))), b)

def setup(+b: D.Builder) -> S.Mem<D.Builder>:
  +xs = L.filter_out(L.filter_in(D.pool(b), Rules.setup()), Rules.speed_setup())
  sampled(L.pick_list(U32.is_ne(L.length(xs), 0), xs, L.filter_in(D.pool(b), Rules.setup())), b)

def support(+b: D.Builder) -> S.Mem<D.Builder>:
  do S.Mem<D.Builder>:
    +b : D.Builder <- sample_if(role(b, Roles.roles_bulkysupport()) || role(b, Roles.roles_bulkyattacker()) ||
      role(b, Roles.roles_bulkysetup()), L.filter_in(D.pool(b), Rules.recovery_moves()), b)
    +b : D.Builder <- sample_if(role(b, Roles.roles_avpivot()),
      L.filter_in(D.pool(b), [V.moves_uturn(), V.moves_voltswitch()]), b)
    +b : D.Builder <- S.branch(D.Builder, role(b, Roles.roles_bulkysetup()) ||
      role(b, Roles.roles_fastbulkysetup()) || role(b, Roles.roles_setupsweeper()) ||
      role(b, Roles.roles_terablastuser()), setup(b), S.Mem.pure(D.Builder, b))
    late(b)

def attacking_stabs(+b: D.Builder) -> S.Mem<D.Builder>:
  do S.Mem<D.Builder>:
    +c : D.Counter <- Q.query(b)
    +b : D.Builder <- filter_if(U32.is_eq(Q.count(c, 29), 0) && Bool.not(role(b, Roles.roles_bulkysupport())),
      Stab{[D.tera(D.info(b))]}, b)
    c2 : D.Counter <- Q.query(b)
    +b : D.Builder <- filter_if(U32.is_eq(Q.count(c2, 28), 0), Stab{D.types(D.mon(D.info(b)))}, b)
    support(b)

def priority_stabs(+b: D.Builder) -> S.Mem<D.Builder>:
  do S.Mem<D.Builder>:
    +b : D.Builder <- filter_if(role(b, Roles.roles_bulkyattacker()) || role(b, Roles.roles_bulkysetup()) ||
      role(b, Roles.roles_wallbreaker()) ||
      L.contains(Rules.priority_pokemon(), D.species(D.mon(D.info(b)))), Priority{}, b)
    +b : D.Builder <- stabs(D.types(D.mon(D.info(b))), b)
    attacking_stabs(b)

def enforced(+b: D.Builder) -> S.Mem<D.Builder>:
  do S.Mem<D.Builder>:
    +b : D.Builder <- S.branch(D.Builder, role(b, Roles.roles_terablastuser()),
      add(b, V.moves_terablast()), S.Mem.pure(D.Builder, b))
    +required : U32 = D.required(D.mon(D.info(b)))
    +b : D.Builder <- S.branch(D.Builder, U32.is_ne(required, 0), add(b, required), S.Mem.pure(D.Builder, b))
    +b : D.Builder <- add_if(ability(b, A.abilities_guts()), b, V.moves_facade())
    +b : D.Builder <- fixed([V.moves_nightshade(), V.moves_revelationdance(), V.moves_revivalblessing(),
      V.moves_stickyweb()], b)
    +removal : Bool = role(b, Roles.roles_bulkysupport()) && Bool.not(team(b, 9n) || team(b, 10n))
    +b : D.Builder <- add_if(removal, b, V.moves_rapidspin())
    +b : D.Builder <- add_if(removal, b, V.moves_defog())
    +b : D.Builder <- add_if((has(b, V.moves_dragontail()) || has(b, V.moves_roar()) || has(b, V.moves_whirlwind())) &&
      U32.is_le(L.at(D.stats(D.mon(D.info(b))), 5n), 60) && Bool.not(team(b, 7n)), b, V.moves_stealthrock())
    +b : D.Builder <- add_if(Bool.not(team(b, 11n)), b, V.moves_auroraveil())
    +types : +List<U32> = D.types(D.mon(D.info(b)))
    +b : D.Builder <- add_if(U32.is_eq(L.length(types), 1) &&
      (L.contains(types, T.types_normal()) || L.contains(types, T.types_fighting())), b, V.moves_knockoff())
    +b : D.Builder <- add_if(U32.is_eq(D.species(D.mon(D.info(b))), P.species_smeargle()), b, V.moves_spore())
    priority_stabs(b)

def get(+b: D.Builder) -> S.Mem<D.Builder>:
  do S.Mem<D.Builder>:
    +next : D.Builder <- S.from_result(D.Builder, Cull.cull(b))
    result : D.Builder <- S.branch(D.Builder, U32.is_le(L.length(D.pool(next)), 4),
      S.Mem.pure(D.Builder, all_remaining(next)), enforced(next))
    Q.cache(result)
