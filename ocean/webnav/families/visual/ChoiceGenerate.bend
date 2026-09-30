import Base
import ./Generate.bend as G
import ./PieGenerate.bend as PG

# Regular-polygon vertices for the source's radius-35, center-(75,50)
# canvas, rounded to whole pixels. This is an explicitly controlled
# zero-rotation preset. The browser oracle will constrain source rotation.
def vertex_x(sides: Nat, index: Nat) -> U32:
  match sides index:
    case 3n 0n: 110
    case 3n 1n: 58
    case 3n _: 58
    case 4n 0n: 110
    case 4n 1n: 75
    case 4n 2n: 40
    case 4n _: 75
    case 5n 0n: 110
    case 5n 1n: 86
    case 5n 2n: 47
    case 5n 3n: 47
    case 5n _: 86
    case 6n 0n: 110
    case 6n 1n: 93
    case 6n 2n: 58
    case 6n 3n: 40
    case 6n 4n: 58
    case 6n _: 93
    case 7n 0n: 110
    case 7n 1n: 97
    case 7n 2n: 67
    case 7n 3n: 43
    case 7n 4n: 43
    case 7n 5n: 67
    case _ _: 97

def vertex_y(sides: Nat, index: Nat) -> U32:
  match sides index:
    case 3n 0n: 50
    case 3n 1n: 80
    case 3n _: 20
    case 4n 0n: 50
    case 4n 1n: 85
    case 4n 2n: 50
    case 4n _: 15
    case 5n 0n: 50
    case 5n 1n: 83
    case 5n 2n: 71
    case 5n 3n: 29
    case 5n _: 17
    case 6n 0n: 50
    case 6n 1n: 80
    case 6n 2n: 80
    case 6n 3n: 50
    case 6n 4n: 20
    case 6n _: 20
    case 7n 0n: 50
    case 7n 1n: 77
    case 7n 2n: 84
    case 7n 3n: 65
    case 7n 4n: 35
    case 7n 5n: 16
    case _ _: 23

def write_vertices(n: Nat, +sides: Nat, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +index = U32.from_nat(p)
      a[(base + 64 + index * 2 : U32)] <- vertex_x(sides,p)
      a[(base + 65 + index * 2 : U32)] <- vertex_y(sides,p)
      write_vertices(p,sides,base,a)

def sides_generate(task: U32, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +sides = (3 + (G.mix((seed + 701 : U32)) % 5 : U32) : U32)
  a = G.header(task,seed,base,a)
  a[(base + 32 : U32)] <- sides
  a[(base + 33 : U32)] <- sides
  a[(base + 34 : U32)] <- 0
  a = write_vertices(U32.to_nat(sides),U32.to_nat(sides),base,a)
  G.text("Press the button that correctly denotes how many sides the shape has.",
    (base + 512 : U32),a)

def tag(goal: Nat) -> U32:
  match goal:
    case 0n: 2
    case 1n: 1
    case 2n: 3
    case _: 0

def glyph(goal: Nat, seed: U32) -> U32:
  match goal:
    case 3n: PG.nth(PG.alphabet(),
      U32.to_nat((G.mix((seed + 727 : U32)) % 52 : U32)))
    case 4n: PG.nth("1234567890",
      U32.to_nat((G.mix((seed + 733 : U32)) % 10 : U32)))
    case _: 0

def identify_generate(task: U32, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +goal = (G.mix((seed + 709 : U32)) % 5 : U32)
  +color = (G.mix((seed + 719 : U32)) % 7 : U32)
  a = G.header(task,seed,base,a)
  a[(base + 32 : U32)] <- goal
  a[(base + 33 : U32)] <- tag(U32.to_nat(goal))
  a[(base + 34 : U32)] <- glyph(U32.to_nat(goal),seed)
  a[(base + 35 : U32)] <- color
  G.text("Click the button that best describes the figure below.",
    (base + 512 : U32),a)
