import Base
import ./Generate.bend as G
import ./SortGenerate.bend as SG
import ./ShapeGenerate.bend as HG
import ./CubeGenerate.bend as CG

def direction_name(direction: Nat) -> String:
  match direction:
    case 0n: "left"
    case 1n: "right"
    case 2n: "up"
    case _: "down"

def shape_name(shape: Nat) -> String:
  match shape:
    case 0n: "circle"
    case 1n: "rectangle"
    case _: "triangle"

def direction_query(task: U32, +direction: U32, shape: U32) -> String:
  Bool.pick(String, U32.is_eq(task, 1),
    "Drag the circle " ++ direction_name(U32.to_nat(direction)) ++
      " then press Submit.",
    "Drag the item " ++ direction_name(U32.to_nat(direction)) ++
      " then press Submit.")

def dimension_name(dimension: Nat) -> String:
  match dimension:
    case 0n: "width"
    case _: "height"

def resize_query(dimension: U32, direction: U32) -> String:
  "Resize the textarea so that the " ++ dimension_name(U32.to_nat(dimension)) ++
    " is " ++ Bool.pick(String, U32.is_eq(direction, 0), "smaller", "larger") ++
    " than its initial size then press Submit."

def direction(+task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = G.clear_blocks(8n, base, a)
  +r1 = G.next((seed + task * 113 : U32))
  +r2 = G.next(r1)
  +r3 = G.next(r2)
  +r4 = G.next(r3)
  +r5 = G.next(r4)
  +r6 = G.next(r5)
  +r7 = G.next(r6)
  +shape = Bool.pick(U32, U32.is_eq(task, 1), 0, (r1 % 3 : U32))
  +size = Bool.pick(U32, U32.is_eq(task, 1),
    (13 + (r2 % 7 : U32) : U32),
    Bool.pick(U32, U32.is_eq(shape, 0),
      (13 + (r2 % 7 : U32) : U32),
      Bool.pick(U32, U32.is_eq(shape, 1),
        (25 + (r2 % 15 : U32) : U32),
        (22 + (r2 % 13 : U32) : U32))))
  +is_circle = U32.is_eq(shape, 0)
  +x_span = Bool.pick(U32, U32.is_eq(task, 1),
    (120 - size * 2 : U32), (100 - size * 2 : U32))
  +y_span = (80 - size * 2 : U32)
  +x_raw = (20 + size + (r3 % x_span : U32) : U32)
  +y_raw = (20 + size + (r4 % y_span : U32) : U32)
  +x_tenths = Bool.pick(U32, is_circle, (x_raw * 10 - size * 10 : U32),
    Bool.pick(U32, U32.is_eq(shape, 1), (x_raw * 10 : U32),
      (x_raw * 10 - size * 5 : U32)))
  +y_tenths = Bool.pick(U32, is_circle, (y_raw * 10 - size * 10 : U32),
    Bool.pick(U32, U32.is_eq(shape, 1), (y_raw * 10 : U32),
      (y_raw * 10 - size * 5 : U32)))
  +direction_code = (r5 % 4 : U32)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 10000
  a[(base + 32 : U32)] <- (2560 + x_tenths : U32)
  a[(base + 33 : U32)] <- (2560 + y_tenths : U32)
  a[(base + 34 : U32)] <- (2560 + x_tenths : U32)
  a[(base + 35 : U32)] <- (2560 + y_tenths : U32)
  a[(base + 36 : U32)] <- direction_code
  a[(base + 38 : U32)] <- shape
  a[(base + 39 : U32)] <- size
  a[(base + 40 : U32)] <-
    (r6 % Bool.pick(U32, U32.is_eq(task, 1), 7, 8) : U32)
  G.text(direction_query(task, direction_code, shape), (base + 256 : U32), a)

def resize(+task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = G.clear_blocks(8n, base, a)
  +r1 = G.next(seed)
  +r2 = G.next(r1)
  +r3 = G.next(r2)
  +r4 = G.next(r3)
  +r5 = G.next(r4)
  +r6 = G.next(r5)
  +width = (50 + (r2 % 10 : U32) * 5 : U32)
  +height = (50 + (r1 % 5 : U32) * 5 : U32)
  +dimension = (r5 % 2 : U32)
  +direction_code = (r6 % 2 : U32)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 10000
  a[(base + 32 : U32)] <- width
  a[(base + 33 : U32)] <- height
  a[(base + 34 : U32)] <- width
  a[(base + 35 : U32)] <- height
  a[(base + 36 : U32)] <- (5 + (r3 % 7 : U32) * 5 : U32)
  a[(base + 37 : U32)] <- (5 + (r4 % 7 : U32) * 5 : U32)
  a[(base + 38 : U32)] <- dimension
  a[(base + 39 : U32)] <- direction_code
  G.text(resize_query(dimension, direction_code), (base + 256 : U32), a)

def dispatch_index(task: Nat, +task_code: U32, seed: U32, base: U32,
                   a: Array<U32>) -> Array<U32>:
  match task:
    case 0n: G.generate(task_code, seed, base, a)
    case 1n: direction(task_code, seed, base, a)
    case 2n: CG.generate(task_code, seed, base, a)
    case 3n: SG.items(task_code, seed, base, a)
    case 4n: SG.items(task_code, seed, base, a)
    case 5n: HG.generate(task_code, seed, base, a)
    case 6n: HG.generate(task_code, seed, base, a)
    case 7n: direction(task_code, seed, base, a)
    case 8n: SG.numbers(task_code, seed, base, a)
    case 9n: resize(task_code, seed, base, a)
    case _: G.generate(task_code, seed, base, a)

def dispatch(+task: U32, seed: U32, base: U32,
             a: Array<U32>) -> Array<U32>:
  dispatch_index(U32.to_nat(task), task, seed, base, a)
