import Base
import ./Generate.bend as G
import ./ShapeModel.bend as S

def shape_name(kind: Nat) -> String:
  match kind:
    case 0n: "circles"
    case 1n: "rectangles"
    case _: "triangles"

def color_name(color: Nat) -> String:
  match color:
    case 0n: "red"
    case 1n: "green"
    case 2n: "blue"
    case 3n: "aqua"
    case 4n: "black"
    case 5n: "magenta"
    case _: "yellow"

def query(task: U32, mode: U32, +goal: U32) -> String:
  Bool.pick(String, U32.is_eq(task, 5),
    "Drag all " ++ shape_name(U32.to_nat(goal)) ++ " into the black box.",
    "Drag all " ++ Bool.pick(String, U32.is_eq(mode, 0),
      shape_name(U32.to_nat(goal)), color_name(U32.to_nat(goal)) ++ " shapes") ++
      " into the left box, and everything else into the right box.")

def width(task: U32) -> U32:
  Bool.pick(U32, U32.is_eq(task, 5), 80, 70)

def height(task: U32) -> U32:
  Bool.pick(U32, U32.is_eq(task, 5), 45, 50)

def total(task: U32) -> U32:
  Bool.pick(U32, U32.is_eq(task, 5), 28, 21)

def grid_y(task: U32, +index: U32) -> U32:
  Bool.pick(U32, U32.is_eq(task, 5),
    (index / 7 : U32), (3 + (index / 7 : U32) : U32))

def overlap(+x: U32, +y: U32, +box_x: U32, +box_y: U32,
            +w: U32, +h: U32) -> Bool:
  U32.is_lt(box_x, (x + 20 : U32)) &&
  U32.is_gt((box_x + w : U32), x) &&
  U32.is_lt(box_y, (y + 20 : U32)) &&
  U32.is_gt((box_y + h : U32), y)

def eligible(+task: U32, +index: U32, box_x: U32, box_y: U32) -> Bool:
  +x = ((index % 7 : U32) * 20 : U32)
  +y = (grid_y(task, index) * 20 : U32)
  Bool.not(overlap(x, y, box_x, box_y, width(task), height(task)))

def item(+seed: U32, +task: U32, +index: U32) -> S.Shape:
  +x = (2560 + (index % 7 : U32) * 200 + 15 : U32)
  +y = (2560 + grid_y(task, index) * 200 + 15 : U32)
  S.Item{(G.next((seed + index * 101 : U32)) % 3 : U32),
         (G.next((seed + index * 307 + 71 : U32)) % 7 : U32), x, y}

def add(found: Bool, shape: S.Shape, xs: +List<S.Shape>) -> +List<S.Shape>:
  match found:
    case True{}: shape <> xs
    case False{}: xs

def build(n: Nat, +task: U32, +seed: U32, +start: U32,
          +box_x: U32, +box_y: U32, +needed: U32,
          +count: U32, xs: +List<S.Shape>) -> +List<S.Shape>:
  match n:
    case 0n: xs
    case 1n+ +p:
      +index = ((start + U32.from_nat(p) * 5 : U32) % total(task) : U32)
      +found = U32.is_lt(count, needed) &&
        eligible(task, index, box_x, box_y)
      build(p, task, seed, start, box_x, box_y, needed,
        (count + Bool.to_u32(found) : U32),
        add(found, item(seed, task, index), xs))

def first_color(xs: +List<S.Shape>) -> U32:
  match xs:
    case Nil{}: 0
    case Con{head, tail}:
      S.Item{kind, color, x, y} = head
      color

def write_shapes(xs: +List<S.Shape>, +base: U32, +at: U32,
                 a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{head, tail}:
      S.Item{kind, color, x, y} = head
      a[(base + 64 + at * 4 : U32)] <- kind
      a[(base + 65 + at * 4 : U32)] <- color
      a[(base + 66 + at * 4 : U32)] <- x
      a[(base + 67 + at * 4 : U32)] <- y
      write_shapes(tail, base, (at + 1 : U32), a)

def generate(+task: U32, +seed: U32, +base: U32,
             a: Array<U32>) -> Array<U32>:
  a = G.clear_blocks(8n, base, a)
  +r1 = G.next((seed + task * 37 : U32))
  +r2 = G.next(r1)
  +r3 = G.next(r2)
  +r4 = G.next(r3)
  +box_x = Bool.pick(U32, U32.is_eq(task, 5),
    (4 + (r1 % 66 : U32) : U32), (4 + (r1 % 76 : U32) : U32))
  +box_y = Bool.pick(U32, U32.is_eq(task, 5),
    (4 + (r2 % 81 : U32) : U32), (4 + (r2 % 66 : U32) : U32))
  +needed = Bool.pick(U32, U32.is_eq(task, 5), 4, 5)
  +shapes = build(U32.to_nat(total(task)), task, seed, (r3 % total(task) : U32),
    box_x, box_y, needed, 0, Nil{})
  +mode = Bool.pick(U32, U32.is_eq(task, 5), 0, (r4 % 2 : U32))
  +goal = Bool.pick(U32, U32.is_eq(mode, 0),
    (G.next((seed + 709 : U32)) % 3 : U32), first_color(shapes))
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- Bool.pick(U32, U32.is_eq(task, 5), 15000, 30000)
  a[(base + 32 : U32)] <- box_x
  a[(base + 33 : U32)] <- box_y
  a[(base + 34 : U32)] <- mode
  a[(base + 35 : U32)] <- goal
  a[(base + 37 : U32)] <- needed
  a = write_shapes(shapes, base, 0, a)
  G.text(query(task, mode, goal), (base + 256 : U32), a)
