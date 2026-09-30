import Base
import ./Model.bend as B
import ./SortModel.bend as S

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

def write_order(xs: +List<U32>, +base: U32, +at: U32,
                a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{head, tail}:
      write_order(tail, base, (at + 1 : U32),
        a[(base + 37 + at : U32)] <- head)

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

def save(s: S.State, +elapsed: U32, +deadline: U32,
         +base: U32, a: Array<U32>) -> Array<U32>:
  S.S{order, values, held, old, ix, goal, result} = s
  a[(base + 2 : U32)] <- 0
  a[(base + 33 : U32)] <- held
  a[(base + 34 : U32)] <- old
  a = write_order(order, base, 0, a)
  outcome(result, elapsed, deadline, base, a)

def decoded(kind: U32, target: U32, arg0: U32) -> S.Action:
  match kind:
    case 1: S.Submit{}
    case 13: S.Down{target}
    case 14: S.Move{arg0}
    case 15: S.Up{}
    case _: S.Wait{}

def values_loaded(+task: U32, +base: U32, kind: U32, target: U32,
                  arg0: U32, +elapsed: U32, +deadline: U32,
                  held: U32, old: U32, ix: U32, goal: U32,
                  order: +List<U32>, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, values) = pair
  state = {S.S{order, values, held, old, ix, goal, B.Running{}} : S.State}
  save(S.step(task, state, decoded(kind, target, arg0), elapsed, deadline),
    elapsed, deadline, base, a)

def order_loaded(task: U32, +base: U32, kind: U32, target: U32,
                 arg0: U32, elapsed: U32, deadline: U32,
                 +n: U32, held: U32, old: U32, ix: U32, goal: U32,
                 pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, order) = pair
  values_loaded(task, base, kind, target, arg0, elapsed, deadline,
    held, old, ix, goal, order,
    read_words(U32.to_nat(n), base, 64, Nil{}, a))

def fields(task: U32, +base: U32, kind: U32, target: U32,
           arg0: U32, elapsed: U32, deadline: U32,
           pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, header_words) = pair
  match header_words:
    case Con{+n, Con{held, Con{old, Con{ix, Con{goal, Nil{}}}}}}:
      order_loaded(task, base, kind, target, arg0, elapsed, deadline,
        n, held, old, ix, goal,
        read_words(U32.to_nat(n), base, 37, Nil{}, a))
    case _: a

def run(task: U32, +base: U32, kind: U32, target: U32,
        arg0: U32, elapsed: U32, deadline: U32,
        a: Array<U32>) -> Array<U32>:
  fields(task, base, kind, target, arg0, elapsed, deadline,
    read_words(5n, base, 32, Nil{}, a))
