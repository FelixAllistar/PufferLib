import Base
import ./Mem.bend as S
import ./Lists.bend as L
import ./GenData.bend as D
import ./Catalog.bend as C
import ./Stats.bend as Stats
import ./State.bend as State
import ./EffectInit.bend as Effects
import ./TypeInit.bend as Types
import ./StatTable.bend as StatTable
import ./MoveSlots.bend as MoveSlots
import ./BoostTable.bend as BoostTable

def store(xs: +List<U32>, +at: U32) -> S.Mem<Unit>:
  match xs:
    case Nil{}: S.Mem.pure(Unit, Unit{})
    case Con{x, rest}:
      do S.Mem<Unit>:
        S.put(at, x)
        store(rest, (at + 1 : U32))

def stats(n: Nat, +index: U32, +species: U32, +level: U32, +evs: +List<U32>,
  +ivs: +List<U32>, +at: U32) -> S.Mem<Unit>:
  match n:
    case 0n: S.Mem.pure(Unit, Unit{})
    case 1n+p:
      do S.Mem<Unit>:
        base : U32 <- C.species_field(species, (4 + index : U32))
        value : U32 = Stats.stat(base, L.at(ivs, U32.to_nat(index)),
          L.at(evs, U32.to_nat(index)), level, 0)
        S.put((at + 16 + index : U32), value)
        stats(p, (index + 1 : U32), species, level, evs, ivs, at)

def slots(moves: +List<U32>, +index: U32, +at: U32) -> S.Mem<Unit>:
  match moves:
    case Nil{}: S.Mem.pure(Unit, Unit{})
    case Con{+id, rest}:
      +slot_at = State.slot(at, index)
      do S.Mem<Unit>:
        base : U32 <- C.move(id, 5)
        no_boost : U32 <- C.move(id, 7)
        target : U32 <- C.move(id, 14)
        +maximum : U32 = Stats.pp(id, base, no_boost)
        S.put(slot_at, id)
        S.put((slot_at + 1 : U32), maximum)
        S.put((slot_at + 2 : U32), maximum)
        S.put((slot_at + 7 : U32), target)
        slots(rest, (index + 1 : U32), at)

def pokemon(set: D.GeneratedSet, +side: U32, +position: U32) -> S.Mem<Unit>:
  D.Set{original, +display, _, tera, +ability, +item, +level, gender, shiny, +moves, +evs, +ivs} = set
  +at = State.mon(side, position)
  do S.Mem<Unit>:
    +sid : U32 <- C.species_field(display, 27)
    first : U32 <- C.species_field(display, 2)
    second : U32 <- C.species_field(display, 3)
    base_hp : U32 <- C.species_field(display, 4)
    fixed_hp : U32 <- C.species_field(display, 25)
    weight : U32 <- C.species_field(display, 24)
    +hp : U32 = Stats.max_hp(base_hp, L.at(ivs, 0n), L.at(evs, 0n), level, fixed_hp)
    S.put(at, original)
    S.put((at + 1 : U32), sid)
    S.put((at + 2 : U32), sid)
    S.put((at + 3 : U32), level)
    S.put((at + 4 : U32), gender)
    S.put((at + 5 : U32), ability)
    S.put((at + 6 : U32), item)
    S.put((at + 7 : U32), hp)
    S.put((at + 8 : U32), hp)
    S.put((at + 9 : U32), hp)
    S.put((at + 10 : U32), tera)
    S.put((at + 11 : U32), first)
    S.put((at + 12 : U32), second)
    S.put((at + 14 : U32), L.length(moves))
    S.put((at + 15 : U32), Bool.to_u32(shiny))
    S.put((at + 16 : U32), hp)
    stats(5n, 1, display, level, evs, ivs, at)
    S.put((at + 22 : U32), weight)
    store([6,6,6,6,6,6,6], (at + 24 : U32))
    S.put((at + 31 : U32), position)
    slots(moves, 0, at)
    store(evs, (at + 64 : U32))
    store(ivs, (at + 70 : U32))
    S.put((at + 76 : U32), ability)
    S.put((at + 77 : U32), display)
    S.put((at + 106 : U32),level)
    Effects.pokemon(at,sid,ability,item)
    S.put(Effects.party_slot(side,position),at)
    S.put((at + 94 : U32),1)
    speed : U32 <- S.get((at + 21 : U32))
    S.put((at + 95 : U32),speed)

def team(ss: +List<D.GeneratedSet>, +side: U32, +position: U32) -> S.Mem<Unit>:
  match ss:
    case Nil{}: S.Mem.pure(Unit, Unit{})
    case Con{s, rest}:
      do S.Mem<Unit>:
        pokemon(s, side, position)
        team(rest, side, (position + 1 : U32))

def constructed() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    Types.party(12n,0)
    StatTable.party(12n,0)
    MoveSlots.party(12n,0)
    BoostTable.party(12n,0)
    S.put(State.header(), 1)
    S.put((State.header() + 1 : U32), 1)
    S.put((State.header() + 8 : U32), 6)
    S.put((State.header() + 9 : U32), 6)
    S.put((State.header() + 10 : U32), 4294967295)
    S.put((State.header() + 11 : U32), 4294967295)
