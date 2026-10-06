import Base
import ./Mem.bend as S
import ./MoveView.bend as Moves
import ./data/moves.bend as V
import ./data/targets.bend as T

# Gen 9 singles Pokemon.getMoveRequestData. Lock events, type events and
# possibleSwitches are resolved by the caller; this does not replace those
# engine operations. Native state 84..88 is trapped/maybeDisabled/maybeLocked/
# maybeTrapped/canTerastallize. Hidden trapping is encoded 2, true is 1.
type Projection is Data:
  RD{moves: +List<Moves.Choice>, flags: U32, tera: U32}

def locked(lock: Moves.Lock) -> Bool:
  match lock:
    case Moves.Unlocked{}: False{}
    case _: True{}

def empty(rows: +List<Moves.Choice>) -> Bool:
  match rows:
    case Nil{}: True{}
    case _: False{}

def with_struggle(rows: +List<Moves.Choice>) -> +List<Moves.Choice>:
  match rows:
    case Nil{}: [Moves.M{V.moves_struggle(),0,0,0,T.targets_randomnormal(),False{},12,2}]
    case Con{head, tail}: head <> tail

def visible_flags(+trapped: U32, disabled: U32, locked_: U32,
  maybe_trapped: U32, +can_switch: Bool) -> U32:
  (Bool.to_u32(can_switch && U32.is_eq(trapped, 1)) +
    2 * Bool.to_u32(can_switch && U32.is_ne(trapped, 1) && U32.is_ne(maybe_trapped, 0)) +
    4 * Bool.to_u32(U32.is_ne(disabled, 0)) + 8 * Bool.to_u32(U32.is_ne(locked_, 0)) : U32)

def visibility(+at: U32, +hard: Bool, last_active: Bool, +can_switch: Bool) -> S.Mem<U32>:
  S.branch(U32, hard || Bool.not(last_active),
    (do S.Mem<U32>:
      trapped : U32 <- S.get((at + 84 : U32))
      S.put((at + 85 : U32), 0)
      S.put((at + 86 : U32), 0)
      S.put((at + 87 : U32), 0)
      return Bool.to_u32((hard || can_switch) && U32.is_ne(trapped, 0))),
    (do S.Mem<U32>:
      trapped : U32 <- S.get((at + 84 : U32))
      +disabled : U32 <- S.get((at + 85 : U32))
      old : U32 <- S.get((at + 86 : U32))
      maybe_trapped : U32 <- S.get((at + 87 : U32))
      +next : U32 = Bool.to_u32(U32.is_ne(old, 0) || U32.is_ne(disabled, 0))
      S.put((at + 86 : U32), next)
      return visible_flags(trapped, disabled, next, maybe_trapped, can_switch)))

def get(+at: U32, +hard: Moves.Lock, semi: Moves.Lock, can_switch: Bool) -> S.Mem<Projection>:
  +hard_locked = locked(hard)
  do S.Mem<Projection>:
    active_flags : U32 <- S.get((at + 23 : U32))
    +last_active : Bool = U32.is_ne((active_flags .&. 1 : U32), 0)
    maybe_locked : U32 <- S.get((at + 86 : U32))
    +lock : Moves.Lock = Bool.pick(Moves.Lock, hard_locked, hard,
      Bool.pick(Moves.Lock, U32.is_ne(maybe_locked, 0), Moves.Unlocked{}, semi))
    S.when(hard_locked, S.put((at + 84 : U32), 1))
    +rows : +List<Moves.Choice> <- Moves.get(at, lock, last_active)
    flags : U32 <- visibility(at, hard_locked, last_active, can_switch)
    tera : U32 <- S.get((at + 88 : U32))
    return RD{with_struggle(rows), flags,
      Bool.pick(U32, locked(lock) || empty(rows), 0, tera)}
