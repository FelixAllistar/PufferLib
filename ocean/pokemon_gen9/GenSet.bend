import Base
import ./Mem.bend as S
import ./Lists.bend as L
import ./Math.bend as M
import ./Catalog.bend as C
import ./GenData.bend as D
import ./GenRng.bend as R
import ./GenMoves.bend as Moves
import ./GenAbility.bend as Ability
import ./GenItem.bend as Item
import ./data/species.bend as P
import ./data/moves.bend as V
import ./data/abilities.bend as A
import ./data/items.bend as I
import ./data/types.bend as T
import ./data/roles.bend as Roles

def form(+mon: D.Species) -> S.Mem<U32>:
  S.branch(U32, D.sample_form(mon), R.sample(D.forms(mon)),
    S.Mem.pure(U32, L.at(D.forms(mon), 0n)))

def candidates(ids: +List<U32>, +mon: D.Species, +lead: Bool, +team: +List<U32>) -> S.Mem<+List<U32>>:
  match ids:
    case Nil{}: S.Mem.pure(+List<U32>, Nil{})
    case Con{+id, rest}:
      do S.Mem<+List<U32>>:
        +b : D.Builder <- C.template(id, mon, lead, team)
        +info : D.Info = D.info(b)
        yes : Bool = Bool.not(
          lead && U32.is_eq(D.role(info), Roles.roles_fastbulkysetup()) &&
            (L.contains(D.abilities(info), A.abilities_protosynthesis()) ||
             L.contains(D.abilities(info), A.abilities_quarkdrive())) ||
          U32.is_ne(L.at(team, 12n), 0) && U32.is_eq(D.role(info), Roles.roles_terablastuser()))
        tail : +List<U32> <- candidates(rest, mon, lead, team)
        return L.keep_member(yes, id, tail)

def hp_good(+hp: U32, +moves: +List<U32>, +sid: U32, +ability: U32, +item: U32, +weak: U32) -> Bool:
  Bool.pick(Bool, L.contains(moves, V.moves_substitute()) && U32.is_eq(item, I.items_sitrusberry()) || U32.is_eq(sid, P.species_minior()),
    U32.is_eq((hp % 4 : U32), 0),
    Bool.pick(Bool, (L.contains(moves, V.moves_bellydrum()) || L.contains(moves, V.moves_filletaway()) || L.contains(moves, V.moves_shedtail())) && (U32.is_eq(item, I.items_sitrusberry()) || U32.is_eq(ability, A.abilities_gluttony())),
    U32.is_eq((hp % 2 : U32), 0),
    Bool.pick(Bool, L.contains(moves, V.moves_substitute()) && L.contains(moves, V.moves_endeavor()),
    U32.is_ne((hp % 4 : U32), 0),
    Bool.pick(Bool, U32.is_eq(weak, 0) || U32.is_eq(ability, A.abilities_regenerator()) || U32.is_eq(item, I.items_leftovers()) || U32.is_eq(item, I.items_lifeorb()),
    True{},
    Bool.not(Bool.xor(U32.is_eq((hp % (4 / weak : U32) : U32), 0), U32.is_eq(item, I.items_sitrusberry())))))))

def hp_evs(n: Nat, +ev: U32, +mon: D.Species, +moves: +List<U32>,
  +ability: U32, +item: U32, +weak: U32) -> U32:
  match n:
    case 0n: ev
    case 1n+p:
      +hp = M.hp(L.at(D.stats(mon), 0n), 31, ev, D.level(mon))
      Bool.pick(U32, hp_good(hp, moves, D.species(mon), ability, item, weak),
        ev, hp_evs(p, (ev - 4 : U32), mon, moves, ability, item, weak))

def optimize_hp(+mon: D.Species, +moves: +List<U32>, +ability: U32, +item: U32) -> U32:
  +encoded = L.at(D.weaknesses(mon), U32.to_nat(T.types_rock()))
  +weak = L.pick(U32.is_eq(ability, A.abilities_magicguard()) || U32.is_eq(item, I.items_heavydutyboots()) ||
    U32.is_le(encoded, 8), 0, (encoded - 8 : U32))
  +crash = L.contains(moves, V.moves_axekick()) || L.contains(moves, V.moves_highjumpkick()) ||
    L.contains(moves, V.moves_jumpkick()) || L.contains(moves, V.moves_supercellslam())
  hp_evs(21n, 85, mon, moves, ability, item, L.pick(crash, 2, weak))

def no_attack_move(+id: U32, +mon: D.Species, moves: +List<U32>,
  +ability: U32, category: U32, flags: U32) -> Bool:
  Bool.pick(Bool, U32.is_ne((flags .&. 5 : U32), 0),
    True{},
    Bool.pick(Bool, U32.is_eq(id, V.moves_shellsidearm()),
    False{},
    Bool.pick(Bool, U32.is_eq(id, V.moves_terablast()) && (U32.is_eq(D.species(mon), P.species_porygon2()) || U32.is_eq(ability, A.abilities_contrary()) || U32.is_eq(ability, A.abilities_defiant()) || L.contains(moves, V.moves_shiftgear()) || U32.is_gt(L.at(D.stats(mon), 1n), L.at(D.stats(mon), 3n))),
    False{},
    U32.is_ne(category, 1) || U32.is_eq(id, V.moves_bodypress()) || U32.is_eq(id, V.moves_foulplay()))))

def no_attack(xs: +List<U32>, +mon: D.Species, +moves: +List<U32>, +ability: U32) -> S.Mem<Bool>:
  match xs:
    case Nil{}: S.Mem.pure(Bool, True{})
    case Con{+id, rest}:
      do S.Mem<Bool>:
        category : U32 <- C.move(id, 0)
        flags : U32 <- C.move(id, 6)
        S.branch(Bool, no_attack_move(id, mon, moves, ability, category, flags),
          no_attack(rest, mon, moves, ability), S.Mem.pure(Bool, False{}))

def gender(+mon: D.Species) -> S.Mem<U32>:
  S.branch(U32, U32.is_eq(D.base(mon), P.species_greninja()), S.Mem.pure(U32, 1),
    S.branch(U32, U32.is_ne(D.gender(mon), 0), S.Mem.pure(U32, D.gender(mon)),
      (do S.Mem<U32>:
        draw : U32 <- R.random(2)
        return (1 + draw : U32))))

def finish(+b: D.Builder, display: U32, +ability: U32, +item: U32) -> S.Mem<D.GeneratedSet>:
  +info = D.info(b)
  +mon = D.mon(info)
  +moves = D.moves(b)
  do S.Mem<D.GeneratedSet>:
    zero_attack : Bool <- no_attack(moves, mon, moves, ability)
    +atk : Bool = zero_attack && Bool.not(L.contains(moves, V.moves_transform()))
    +slow : Bool = L.contains(moves, V.moves_gyroball()) || L.contains(moves, V.moves_trickroom())
    evs : +List<U32> = [optimize_hp(mon, moves, ability, item), L.pick(atk, 0, 85), 85, 85, 85, L.pick(slow, 0, 85)]
    ivs : +List<U32> = [31, L.pick(atk, 0, 31), 31, 31, 31, L.pick(slow, 0, 31)]
    shuffled : +List<U32> <- R.shuffle_moves(moves)
    gender_ : U32 <- gender(mon)
    shiny : Bool <- R.chance(1, 1024)
    return D.Set{D.species(mon), display, D.role(info), D.tera(info), ability, item,
      D.level(mon), gender_, shiny, shuffled, evs, ivs}

def build(b: D.Builder, display: U32, tera: U32) -> S.Mem<D.GeneratedSet>:
  D.B{info, pool, _, _} = b
  do S.Mem<D.GeneratedSet>:
    +next : D.Builder <- Moves.get(D.B{D.with_tera(info, tera), pool, Nil{}, D.Uncounted{}})
    +ability : U32 <- Ability.get(next)
    item : U32 <- Item.get(next, ability)
    finish(next, display, ability, item)

def get(+mon: D.Species, +team: +List<U32>, +lead: Bool) -> S.Mem<D.GeneratedSet>:
  do S.Mem<D.GeneratedSet>:
    display : U32 <- form(mon)
    possible : +List<U32> <- candidates(D.templates(mon), mon, lead, team)
    +id : U32 <- R.sample(possible)
    teras : +List<U32> <- C.template_teras(id)
    tera : U32 <- R.sample(teras)
    b : D.Builder <- C.template(id, mon, lead, team)
    build(b, display, tera)
