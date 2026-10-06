import Base
import ./Mem.bend as S
import ./State.bend as State
import ./data/moves.bend as V
import ./data/species.bend as P
import ./data/types.bend as T
import ./data/targets.bend as Targets
import ./MoveSlots.bend as Slots

# Numeric projection of Pokemon.getMoves after lock events are resolved.
# Source slots are 1-based; zero denotes a synthetic/fallback request. Presence
# bits distinguish omitted locked-request fields from ordinary zero values.
type Choice is Data:
  M{move: U32, slot: U32, pp: U32, maximum: U32, target: U32,
    disabled: Bool, present: U32, synthetic: U32}

type Lock is Data:
  Unlocked{}
  Locked{move: U32}
  Recharge{}

type Scan is Data:
  Rows{moves: +List<Choice>, valid: Bool}

def disabled(+pp: U32, +hidden: U32, restrict_data: Bool) -> Bool:
  U32.is_eq(pp, 0) || U32.is_eq(hidden, 1) || U32.is_eq(hidden, 2) && Bool.not(restrict_data)

def target(+move: U32, base: U32, ghost: Bool, heal_block: Bool, stellar: Bool) -> U32:
  Bool.pick(U32, U32.is_eq(move, V.moves_curse()) && Bool.not(ghost), Targets.targets_self(),
    Bool.pick(U32, U32.is_eq(move, V.moves_pollenpuff()) && heal_block, Targets.targets_adjacentfoe(),
      Bool.pick(U32, U32.is_eq(move, V.moves_terastarstorm()) && stellar, Targets.targets_alladjacentfoes(), base)))

def live(+at: U32, +index: U32, restrict_data: Bool) -> S.Mem<Choice>:
  +slot_at = State.slot(at, index)
  do S.Mem<Choice>:
    +id : U32 <- Slots.field_id(at,index)
    +pp : U32 <- Slots.field_number(at,index,41,1)
    maximum : U32 <- Slots.field_number(at,index,42,2)
    hidden : U32 <- Slots.field_disabled(at,index)
    base_target : U32 <- Slots.field_target(at,index)
    first : U32 <- S.get((at + 11 : U32))
    second : U32 <- S.get((at + 12 : U32))
    third : U32 <- S.get((at + 83 : U32))
    flags : U32 <- S.get((at + 23 : U32))
    species : U32 <- S.get((at + 1 : U32))
    +ghost : Bool = U32.is_eq(first, T.types_ghost()) || U32.is_eq(second, T.types_ghost()) ||
      U32.is_eq(third, T.types_ghost())
    return M{id, (index + 1 : U32), pp, maximum,
      target(id, base_target, ghost, U32.is_ne((flags .&. 128 : U32), 0),
        U32.is_eq(species, P.species_terapagosstellar())),
      disabled(pp, hidden, restrict_data), 15, 0}

def prepend(+move: Choice, rows: Scan) -> Scan:
  M{_, _, _, _, _, off, _, _} = move
  Rows{moves, valid} = rows
  Rows{move <> moves, Bool.not(off) || valid}

def scan(n: Nat, +at: U32, +index: U32, +restrict_data: Bool) -> S.Mem<Scan>:
  match n:
    case 0n: S.Mem.pure(Scan, Rows{Nil{}, False{}})
    case 1n+p:
      do S.Mem<Scan>:
        move : Choice <- live(at, index, restrict_data)
        tail : Scan <- scan(p, at, (index + 1 : U32), restrict_data)
        return prepend(move, tail)

def choose_valid(rows: Scan) -> +List<Choice>:
  Rows{moves, valid} = rows
  Bool.pick(+List<Choice>, valid, moves, Nil{})

def find_slot(n: Nat, +at: U32, +index: U32, +id: U32) -> S.Mem<U32>:
  match n:
    case 0n: S.Mem.pure(U32, 0)
    case 1n+p:
      do S.Mem<U32>:
        slot_id : U32 <- Slots.field_id(at,index)
        S.branch(U32, U32.is_eq(slot_id, id), S.Mem.pure(U32, (index + 1 : U32)),
          find_slot(p, at, (index + 1 : U32), id))

def with_lock(lock: Lock, +at: U32, +count: U32, restrict_data: Bool) -> S.Mem<+List<Choice>>:
  match lock:
    case Recharge{}: S.Mem.pure(+List<Choice>, [M{0,0,0,0,0,False{},0,1}])
    case Locked{+id}:
      do S.Mem<+List<Choice>>:
        slot : U32 <- find_slot(U32.to_nat(count), at, 0, id)
        return [M{id,slot,0,0,0,False{},0,0}]
    case Unlocked{}:
      do S.Mem<+List<Choice>>:
        rows : Scan <- scan(U32.to_nat(count), at, 0, restrict_data)
        return choose_valid(rows)

def get(+at: U32, lock: Lock, restrict_data: Bool) -> S.Mem<+List<Choice>>:
  do S.Mem<+List<Choice>>:
    +count : U32 <- Slots.count(at)
    S.branch(+List<Choice>, U32.is_le(count, 4), with_lock(lock, at, count, restrict_data),
      S.raise(+List<Choice>, 3))
