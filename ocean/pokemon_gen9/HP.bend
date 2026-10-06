import Base
import ./Mem.bend as S
import ./Number.bend as N
import ./Faint.bend as Faint

type Outcome is Data:
  Changed{negative: Bool, amount: U32}
  Refused{}

def damage(+at: U32, +number: N.Scalar, source: U32, kind: U32, effect: U32) -> S.Mem<Outcome>:
  do S.Mem<Outcome>:
    +hp : U32 <- S.get((at + 7 : U32))
    S.branch(Outcome, U32.is_ne(hp, 0) && N.positive(number),
      (do S.Mem<Outcome>:
        +value : U32 = Bool.pick(U32, N.below_one(number), 1, N.trunc(number))
        S.branch(Unit, U32.is_ge(value, hp),
          (do S.Mem<Unit>:
            unused : U32 <- Faint.enqueue(at, source, kind, effect)
            S.put((at + 7 : U32), 0)),
          S.put((at + 7 : U32), (hp - value : U32)))
        return Changed{False{}, U32.min(hp, value)}),
      S.Mem.pure(Outcome, Changed{False{}, 0}))

def heal(+at: U32, number: N.Scalar) -> S.Mem<Outcome>:
  do S.Mem<Outcome>:
    +hp : U32 <- S.get((at + 7 : U32))
    +maximum : U32 <- S.get((at + 8 : U32))
    +value : U32 = N.trunc(number)
    S.branch(Outcome, U32.is_ne(hp, 0) && U32.is_ne(value, 0) && U32.is_lt(hp, maximum),
      (do S.Mem<Outcome>:
        +paid : U32 = U32.min(value, (maximum - hp : U32))
        S.put((at + 7 : U32), (hp + paid : U32))
        return Changed{False{}, paid}), S.Mem.pure(Outcome, Refused{}))

def sethp(+at: U32, number: N.Scalar) -> S.Mem<Outcome>:
  do S.Mem<Outcome>:
    +hp : U32 <- S.get((at + 7 : U32))
    maximum : U32 <- S.get((at + 8 : U32))
    S.branch(Outcome, U32.is_ne(hp, 0),
      (do S.Mem<Outcome>:
        +next : U32 = U32.min(maximum, U32.max(1, N.trunc(number)))
        S.put((at + 7 : U32), next)
        return Changed{U32.is_lt(next, hp), (U32.max(next, hp) - U32.min(next, hp) : U32)}),
      S.Mem.pure(Outcome, Changed{False{}, 0}))
