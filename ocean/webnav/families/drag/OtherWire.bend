import Base
import ./Model.bend as B
import ./DirectionModel.bend as D
import ./ResizeModel.bend as R
import ./SortWire.bend as SW
import ./ShapeWire.bend as HW
import ./CubeWire.bend as CW

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

def direction_save(s: D.State, +elapsed: U32, +deadline: U32,
                   +base: U32, a: Array<U32>) -> Array<U32>:
  D.S{initial_x, initial_y, x, y, direction, held, result} = s
  a[(base + 2 : U32)] <- 0
  a[(base + 34 : U32)] <- x
  a[(base + 35 : U32)] <- y
  a[(base + 37 : U32)] <- held
  outcome(result, elapsed, deadline, base, a)

def svg_offset(shape_kind: Nat, size: U32) -> U32:
  match shape_kind:
    case 0n: (size * 12 : U32) # circle bbox is twice its radius; 0.6 width
    case 1n: (size * 5 : U32)  # rectangle: 0.5 width
    case _: (size * 4 : U32)   # triangle: 0.4 width

def svg_quantize(coordinate: U32, +offset: U32) -> U32:
  ((((coordinate + offset + 5 : U32) / 10 : U32) * 10 : U32) - offset : U32)

def direction_action(kind: U32, target: U32, arg0: U32, arg1: U32,
                     shape_kind: U32, size: U32) -> D.Action:
  match kind:
    case 1: D.Submit{}
    case 13: D.Down{}
    case 14:
      +offset = svg_offset(U32.to_nat(shape_kind), size)
      D.Move{svg_quantize(arg0, offset), svg_quantize(arg1, offset)}
    case 15: D.Up{}
    case _: D.Wait{}

def direction_fields(+base: U32, kind: U32, target: U32, arg0: U32,
                     arg1: U32, +elapsed: U32, +deadline: U32,
                     pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, fields) = pair
  match fields:
    case Con{initial_x, Con{initial_y, Con{x, Con{y,
         Con{direction, Con{held, Con{shape_kind,
         Con{size, Con{color, Nil{}}}}}}}}}}:
      state = {D.S{initial_x, initial_y, x, y, direction, held, B.Running{}} : D.State}
      direction_save(D.step(state, direction_action(kind, target, arg0, arg1,
        shape_kind, size),
        elapsed, deadline), elapsed, deadline, base, a)
    case _: a

def resize_save(s: R.State, +elapsed: U32, +deadline: U32,
                +base: U32, a: Array<U32>) -> Array<U32>:
  R.S{initial_width, initial_height, width, height, dimension,
      direction, held, result} = s
  a[(base + 2 : U32)] <- 0
  a[(base + 32 : U32)] <- width
  a[(base + 33 : U32)] <- height
  a[(base + 40 : U32)] <- held
  outcome(result, elapsed, deadline, base, a)

def resize_action(kind: U32, target: U32, arg0: U32, arg1: U32) -> R.Action:
  match kind:
    case 1: R.Submit{}
    case 13: R.Down{}
    case 14: R.Resize{arg0, arg1}
    case 15: R.Up{}
    case _: R.Wait{}

def resize_fields(+base: U32, kind: U32, target: U32, arg0: U32,
                  arg1: U32, +elapsed: U32, +deadline: U32,
                  pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, fields) = pair
  match fields:
    case Con{width, Con{height, Con{initial_width, Con{initial_height,
         Con{left, Con{top, Con{dimension, Con{direction,
         Con{held, Nil{}}}}}}}}}}:
      state = {R.S{initial_width, initial_height, width, height,
        dimension, direction, held, B.Running{}} : R.State}
      resize_save(R.step(state, resize_action(kind, target, arg0, arg1),
        elapsed, deadline), elapsed, deadline, base, a)
    case _: a

def dispatch_index(task: Nat, +base: U32, kind: U32, target: U32,
                   arg0: U32, arg1: U32, +elapsed: U32, +deadline: U32,
                   a: Array<U32>) -> Array<U32>:
  match task:
    case 1n: direction_fields(base, kind, target, arg0, arg1,
      elapsed, deadline, read_words(9n, base, 32, Nil{}, a))
    case 2n: CW.run(base, kind, target, arg0, elapsed, deadline, a)
    case 3n: SW.run(3, base, kind, target, arg0, elapsed, deadline, a)
    case 4n: SW.run(4, base, kind, target, arg0, elapsed, deadline, a)
    case 5n: HW.run(5, base, kind, target, arg0, arg1, elapsed, deadline, a)
    case 6n: HW.run(6, base, kind, target, arg0, arg1, elapsed, deadline, a)
    case 7n: direction_fields(base, kind, target, arg0, arg1,
      elapsed, deadline, read_words(9n, base, 32, Nil{}, a))
    case 8n: SW.run(8, base, kind, target, arg0, elapsed, deadline, a)
    case 9n: resize_fields(base, kind, target, arg0, arg1,
      elapsed, deadline, read_words(9n, base, 32, Nil{}, a))
    case _: a

def dispatch(task: U32, +base: U32, kind: U32, target: U32,
             arg0: U32, arg1: U32, elapsed: U32, deadline: U32,
             a: Array<U32>) -> Array<U32>:
  dispatch_index(U32.to_nat(task), base, kind, target, arg0, arg1,
    elapsed, deadline, a)
