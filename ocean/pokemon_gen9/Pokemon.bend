import Base
import ./Mem.bend as S
import ./State.bend as T
import ./MoveSlots.bend as Slots

# Persistent per-Pokemon operations. Move slots are live slots, so Transform
# and temporary move replacement will feed these functions the changed list.
def deduct_scan(n: Nat, +at: U32, +slot: U32, +move: U32, +amount: U32) -> S.Mem<U32>:
  match n:
    case 0n: S.Mem.pure(U32, 0)
    case 1n+p:
      do S.Mem<U32>:
        +slot_at : U32 = T.slot(at, slot)
        +id : U32 <- Slots.field_id(at,slot)
        S.branch(U32, U32.is_eq(id, move),
          (do S.Mem<U32>:
            +pp : U32 <- Slots.field_number(at,slot,41,1)
            +paid : U32 = U32.min(pp, U32.max(amount, 1))
            Slots.mark_used(at,slot)
            Slots.put_pp(at,slot,(pp - paid : U32))
            return paid),
          deduct_scan(p, at, (slot + 1 : U32), move, amount))

def deduct(+at: U32, move: U32, amount: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    +count : U32 <- Slots.count(at)
    S.branch(U32, U32.is_le(count, 4),
      (do S.Mem<U32>:
        paid : U32 <- deduct_scan(U32.to_nat(count), at, 0, move, amount)
        Slots.sync(at)
        return paid), S.raise(U32, 3))

# disabled: 0 false, 1 true, 2 hidden. Existing true is absorbing: a later
# hidden disable must not change it. Effect kind/ID preserve disabledSource.
def disable_scan(n: Nat, +at: U32, +slot: U32, +move: U32, +hidden: Bool,
  +effect_kind: U32, +effect: U32) -> S.Mem<Unit>:
  match n:
    case 0n: S.Mem.pure(Unit, Unit{})
    case 1n+p:
      do S.Mem<Unit>:
        +slot_at : U32 = T.slot(at, slot)
        +id : U32 <- Slots.field_id(at,slot)
        old : U32 <- Slots.field_disabled(at,slot)
        S.when(U32.is_eq(id, move) && U32.is_ne(old, 1),
          (do S.Mem<Unit>:
            Slots.put_disabled(at,slot,hidden,effect_kind,effect)
            S.put((slot_at + 5 : U32), Bool.pick(U32, U32.is_eq(effect, 0), 0, effect_kind))
            S.put((slot_at + 6 : U32), Bool.pick(U32, U32.is_eq(effect, 0), id, effect))))
        disable_scan(p, at, (slot + 1 : U32), move, hidden, effect_kind, effect)

def disable(+at: U32, move: U32, hidden: Bool, effect_kind: U32, effect: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +count : U32 <- Slots.count(at)
    S.branch(Unit, U32.is_le(count, 4),
      (do S.Mem<Unit>:
        disable_scan(U32.to_nat(count), at, 0, move, hidden, effect_kind, effect)
        Slots.sync(at)), S.raise(Unit, 3))
