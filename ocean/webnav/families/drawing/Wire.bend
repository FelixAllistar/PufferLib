import Base
import ./Model.bend as M
import ./Points.bend as P
import ./Generate.bend as G
import ./Curve.bend as C

def prepend(pair: Array<U32> & U32, xs: +List<U32>) -> Array<U32> & +List<U32>:
  (a, x) = pair
  (a, x <> xs)

def units(n: Nat, +at: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, xs) = pair
      units(p, at, prepend(a[(at + U32.from_nat(p) : U32)], xs))

def point_y(x: U32, pair: Array<U32> & U32, rest: +List<P.Point>) -> Array<U32> & +List<P.Point>:
  (a, y) = pair
  (a, P.P{(U32.to_f32(x) / 256.0 : F32), (U32.to_f32(y) / 256.0 : F32)} <> rest)

def point_x(+at: U32, pair: Array<U32> & U32, rest: +List<P.Point>) -> Array<U32> & +List<P.Point>:
  (a, x) = pair
  point_y(x, a[(at + 1 : U32)], rest)

def points(n: Nat, +at: U32, pair: Array<U32> & +List<P.Point>) -> Array<U32> & +List<P.Point>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, xs) = pair
      +address = (at + U32.from_nat(p) * 2 : U32)
      points(p, at, point_x(address, a[address], xs))

def save_points(xs: +List<P.Point>, +at: U32, a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{P.P{x, y}, rest}:
      a[at] <- F32.to_u32((x * 256.0 : F32))
      a[(at + 1 : U32)] <- F32.to_u32((y * 256.0 : F32))
      save_points(rest, (at + 2 : U32), a)


def save_outcome(o: M.Outcome, points: +List<P.Point>, elapsed: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case M.Running{}: a
    case M.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864
    case M.Finished{+reward}:
      +box = C.path_bounds(points)
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(reward)
      a[(base + 11 : U32)] <- F32.bits(Bool.pick(F32, F32.is_gt(reward, 0.0),
        (reward * (1.0 - U32.to_f32(elapsed) / 10000.0) : F32), reward))
      a[(base + 38 : U32)] <- F32.bits(C.width(box))
      a[(base + 39 : U32)] <- F32.bits(C.height(box))

def save(s: M.State, +base: U32, elapsed: U32, a: Array<U32>) -> Array<U32>:
  M.S{+xs, active, outcome} = s
  a[(base + 2 : U32)] <- 0
  a[(base + 36 : U32)] <- U32.from_nat(List.length(&2, P.Point, xs))
  a[(base + 37 : U32)] <- Bool.pick(U32, active, 1, 0)
  save_outcome(outcome, xs, elapsed, base, save_points(xs, (base + 128 : U32), a))

def decode(kind: Nat, x: U32, y: U32) -> M.Action:
  match kind:
    case 1n: M.Submit{}
    case 13n: M.Down{P.P{U32.to_f32((x / 256 : U32)), U32.to_f32((y / 256 : U32))}}
    case 14n: M.Move{P.P{U32.to_f32((x / 256 : U32)), U32.to_f32((y / 256 : U32))}}
    case 15n: M.Up{}
    case _: M.Wait{}

def loaded(+base: U32, task: U32, kind: U32, x: U32, y: U32, +elapsed: U32,
    active: U32, cx: U32, cy: U32, horizontal: U32,
    pair: Array<U32> & +List<P.Point>) -> Array<U32>:
  (a, xs) = pair
  save(M.step_task(M.S{xs, U32.is_eq(active, 1), M.Running{}},
    decode(U32.to_nat(kind), x, y), elapsed, task,
    P.P{(U32.to_f32(cx) / 65536.0 : F32), (U32.to_f32(cy) / 65536.0 : F32)},
    U32.is_eq(horizontal, 1)), base, elapsed, a)

def config(+base: U32, task: U32, kind: U32, x: U32, y: U32, elapsed: U32,
    count: U32, active: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, fields) = pair
  match fields:
    case Con{cx, Con{cy, Con{horizontal, Nil{}}}}:
      loaded(base, task, kind, x, y, elapsed, active, cx, cy, horizontal,
        points(U32.to_nat(count), (base + 128 : U32), (a, Nil{})))
    case _: a

def rest_header(+base: U32, task: U32, kind: U32, x: U32, y: U32, elapsed: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, words) = pair
  match words:
    case Con{count, Con{active, Nil{}}}:
      config(base, task, kind, x, y, elapsed, count, active,
        units(3n, (base + 32 : U32), (a, Nil{})))
    case _: a

def header(+base: U32, task: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, words) = pair
  match words:
    case Con{kind, Con{target, Con{x, Con{y, Con{elapsed, Con{status, Nil{}}}}}}}:
      rest_header(base, task, kind, x, y, elapsed,
        units(2n, (base + 36 : U32), (a, Nil{})))
    case _: a

def status(kind: Nat, task: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match kind:
    case 0n: header(base, task, units(6n, (base + 4 : U32), (a, Nil{})))
    case _: a[(base + 2 : U32)] <- 0

def operation_fields(opcode: Nat, fields: +List<U32>, base: U32, a: Array<U32>) -> Array<U32>:
  match opcode fields:
    case 1n Con{task, Con{command, Con{seed, rest}}}: G.generate(task, seed, base, a)
    case 2n Con{task, Con{command, Con{seed, Con{action, Con{target, Con{x, Con{y, Con{elapsed, Con{state, rest}}}}}}}}}:
      status(U32.to_nat(state), task, base, a)
    case _ _: a

def op(opcode: Nat, base: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, fields) = pair
  operation_fields(opcode, fields, base, a)

def row(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, opcode) = pair
  op(U32.to_nat(opcode), base, units(9n, (base + 1 : U32), (a, Nil{})))

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +base = (U32.from_nat(p) * 2048 : U32)
      batch(p, row(base, a[(base + 2 : U32)]))
