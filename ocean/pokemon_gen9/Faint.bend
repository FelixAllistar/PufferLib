import Base
import ./Mem.bend as S

# Deferred faint records retain persistent holder addresses and source effect.
# Every unprocessed record has a distinct faintQueued Pokemon: at most 12.
def count_at() -> U32:
  528

def row(index: U32) -> U32:
  (4096 + index * 4 : U32)

def push(+target: U32, source: U32, effect_kind: U32, effect: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +count : U32 <- S.get(count_at())
    S.branch(Unit, U32.is_lt(count, 12),
      (do S.Mem<Unit>:
        +queue_at : U32 = row(count)
        S.put(queue_at, target)
        S.put((queue_at + 1 : U32), source)
        S.put((queue_at + 2 : U32), effect_kind)
        S.put((queue_at + 3 : U32), effect)
        S.put(count_at(), (count + 1 : U32))), S.raise(Unit, 3))

def enqueue(+at: U32, source: U32, effect_kind: U32, effect: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    queued : U32 <- S.get((at + 80 : U32))
    fainted : U32 <- S.get((at + 81 : U32))
    S.branch(U32, U32.is_eq(queued, 0) && U32.is_eq(fainted, 0),
      (do S.Mem<U32>:
        old : U32 <- S.get((at + 7 : U32))
        push(at, source, effect_kind, effect)
        S.put((at + 7 : U32), 0)
        S.put((at + 82 : U32), 0)
        S.put((at + 80 : U32), 1)
        return old), S.Mem.pure(U32, 0))
