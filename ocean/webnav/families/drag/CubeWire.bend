import Base
import ./Model.bend as B
import ./CubeModel.bend as C

def prepend(pair: Array<U32> & U32, xs: +List<U32>) -> Array<U32> & +List<U32>:
  (a, value) = pair
  (a, value <> xs)

def read_units(n: Nat, +at: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, xs) = pair
      read_units(p, at, prepend(a[(at + U32.from_nat(p) : U32)], xs))

def read_words(n: Nat, base: U32, offset: U32, xs: +List<U32>, a: Array<U32>) -> Array<U32> & +List<U32>:
  read_units(n, (base + offset : U32), (a, xs))

def outcome(o: B.Outcome, +elapsed: U32, +deadline: U32,
            +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case B.Running{}: a
    case B.Win{}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(B.raw(B.Win{}))
      a[(base + 11 : U32)] <- F32.bits(B.timed(B.Win{}, elapsed, deadline))
    case B.Loss{}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(B.raw(B.Loss{}))
      a[(base + 11 : U32)] <- F32.bits(B.timed(B.Loss{}, elapsed, deadline))
    case B.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(s: C.State, +elapsed: U32, +deadline: U32,
         +base: U32, a: Array<U32>) -> Array<U32>:
  C.S{goal, face, held, result} = s
  a[(base + 2 : U32)] <- 0
  a[(base + 33 : U32)] <- face
  a[(base + 34 : U32)] <- held
  outcome(result, elapsed, deadline, base, a)

def decoded(kind: U32, target: U32, arg0: U32) -> C.Action:
  match kind:
    case 1: C.Submit{}
    case 13: C.Down{}
    case 14: C.Rotate{arg0}
    case 15: C.Up{}
    case _: C.Wait{}

def fields(+base: U32, kind: U32, target: U32, arg0: U32,
           +elapsed: U32, +deadline: U32,
           pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, cube_words) = pair
  match cube_words:
    case Con{goal, Con{face, Con{held, Nil{}}}}:
      save(C.step(C.S{goal, face, held, B.Running{}},
        decoded(kind, target, arg0), elapsed, deadline),
        elapsed, deadline, base, a)
    case _: a

def run(+base: U32, kind: U32, target: U32, arg0: U32,
        elapsed: U32, deadline: U32, a: Array<U32>) -> Array<U32>:
  fields(base, kind, target, arg0, elapsed, deadline,
    read_words(3n, base, 32, Nil{}, a))
