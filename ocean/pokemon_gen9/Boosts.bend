import Base
import ./Mem.bend as S
import ./BoostTable.bend as Table

# Stored stages use signed stage +6. Requested/returned deltas use +32768.
# This module implements Pokemon.getCappedBoost/boostBy/positiveBoosts,
# without the higher-level Battle.boost event dispatch or battle messages.
type Change is Data:
  D{stat: U32, delta: U32}

def next(+stage: U32, delta: U32) -> U32:
  U32.min((U32.max((stage + delta : U32), 32768) - 32768 : U32), 12)

def capped(+stage: U32, delta: U32) -> U32:
  (next(stage, delta) + 32768 - stage : U32)

def apply(changes: +List<Change>, +at: U32, last: U32) -> S.Mem<U32>:
  match changes:
    case Nil{}: S.Mem.pure(U32, last)
    case Con{D{+stat, +delta}, +rest}:
      S.branch(U32, U32.is_eq(delta, 32768), apply(rest, at, last),
        (do S.Mem<U32>:
          +old : U32 <- Table.current(at,stat)
          +value : U32 = next(old, delta)
          Table.write(at,stat,value)
          apply(rest, at, (value + 32768 - old : U32))))

def positive(n: Nat, +at: U32, +index: U32, total: U32) -> S.Mem<U32>:
  match n:
    case 0n: S.Mem.pure(U32, total)
    case 1n+p:
      do S.Mem<U32>:
        stage : U32 <- Table.current(at,index)
        positive(p,at,(index + 1 : U32),(total + U32.max(stage, 6) - 6 : U32))

def clear(at: U32) -> S.Mem<Unit>:
  Table.clear_existing(at)

def set(changes: +List<Change>, +at: U32) -> S.Mem<Unit>:
  match changes:
    case Nil{}: S.Mem.pure(Unit, Unit{})
    case Con{D{stat, stage}, rest}:
      do S.Mem<Unit>:
        Table.write(at,stat,stage)
        set(rest, at)
