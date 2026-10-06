import Base
import ./Mem.bend as S
import ./data/Layout.bend as L
import ./Sodium.bend as Sodium
import ./SodiumMem.bend as SodiumMem
import ./EffectStore.bend as Effects

# Internal flat state, private to the simulator. This is not a policy view.
def header() -> U32:
  512

def mon(+side: U32, position: U32) -> U32:
  (576 + (side * 6 + position) * 128 : U32)

def slot(+mon_at: U32, index: U32) -> U32:
  (mon_at + 32 + index * 8 : U32)

def clear_words(n: Nat, +at: U32) -> S.Mem<Unit>:
  match n:
    case 0n: S.Mem.pure(Unit, Unit{})
    case 1n+p:
      do S.Mem<Unit>:
        S.put(at, 0)
        clear_words(p, (at + 1 : U32))

def clear_blocks(n: Nat, +at: U32) -> S.Mem<Unit>:
  match n:
    case 0n: S.Mem.pure(Unit, Unit{})
    case 1n+p:
      do S.Mem<Unit>:
        clear_words(128n, at)
        clear_blocks(p, (at + 128 : U32))

def clear() -> S.Mem<Unit>:
  # Clear the complete private region, including queued actions and scratch.
  # The generated layout keeps the read-only catalog outside reset storage.
  do S.Mem<Unit>:
    S.set_heap(Effects.empty(0))
    mode : U32 <- S.get(4)
    seed : Sodium.Seed <- SodiumMem.read()
    clear_blocks(U32.to_nat(((L.mutable_words() - 512 : U32) / 128 : U32)), header())
    S.branch(Unit, U32.is_eq(mode, 1), SodiumMem.write(seed), S.Mem.pure(Unit, Unit{}))
