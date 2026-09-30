import Base
import ./ShapeModel.bend as S

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

def groups(xs: +List<U32>) -> +List<S.Shape>:
  match xs:
    case Con{kind, Con{color, Con{x, Con{y, rest}}}}:
      S.Item{kind, color, x, y} <> groups(rest)
    case _: Nil{}

def save_shapes(xs: +List<S.Shape>, +base: U32, +at: U32,
                a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{head, tail}:
      S.Item{kind, color, x, y} = head
      a[(base + 64 + at * 4 : U32)] <- kind
      a[(base + 65 + at * 4 : U32)] <- color
      a[(base + 66 + at * 4 : U32)] <- x
      a[(base + 67 + at * 4 : U32)] <- y
      save_shapes(tail, base, (at + 1 : U32), a)

def outcome(+task: U32, o: S.Outcome, +elapsed: U32, +deadline: U32,
            +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case S.Running{}: a
    case S.Finished{+correct}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(S.raw(task, S.Finished{correct}))
      a[(base + 11 : U32)] <- F32.bits(S.timed(task,
        S.Finished{correct}, elapsed, deadline))
    case S.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(task: U32, s: S.State, +elapsed: U32, +deadline: U32,
         +base: U32, a: Array<U32>) -> Array<U32>:
  S.S{shapes, box_x, box_y, mode, goal, held, result} = s
  a[(base + 2 : U32)] <- 0
  a[(base + 36 : U32)] <- held
  a = save_shapes(shapes, base, 0, a)
  outcome(task, result, elapsed, deadline, base, a)

def decoded(kind: U32, target: U32, arg0: U32, arg1: U32) -> S.Action:
  match kind:
    case 1: S.Submit{}
    case 13: S.Down{target}
    case 14: S.Move{arg0, arg1}
    case 15: S.Up{}
    case _: S.Wait{}

def loaded(+task: U32, +base: U32, kind: U32, target: U32,
           arg0: U32, arg1: U32, +elapsed: U32, +deadline: U32,
           box_x: U32, box_y: U32, mode: U32, goal: U32, held: U32,
           pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, shape_words) = pair
  state = {S.S{groups(shape_words), box_x, box_y, mode, goal, held, S.Running{}} : S.State}
  save(task, S.step(task, state, decoded(kind, target, arg0, arg1),
    elapsed, deadline), elapsed, deadline, base, a)

def fields(+task: U32, +base: U32, kind: U32, target: U32,
           arg0: U32, arg1: U32, elapsed: U32, deadline: U32,
           pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, header) = pair
  match header:
    case Con{box_x, Con{box_y, Con{mode, Con{goal,
         Con{held, Con{count, Nil{}}}}}}}:
      loaded(task, base, kind, target, arg0, arg1, elapsed, deadline,
        box_x, box_y, mode, goal, held,
        read_words(U32.to_nat((count * 4 : U32)), base, 64, Nil{}, a))
    case _: a

def run(task: U32, +base: U32, kind: U32, target: U32,
        arg0: U32, arg1: U32, elapsed: U32, deadline: U32,
        a: Array<U32>) -> Array<U32>:
  fields(task, base, kind, target, arg0, arg1, elapsed, deadline,
    read_words(6n, base, 32, Nil{}, a))
