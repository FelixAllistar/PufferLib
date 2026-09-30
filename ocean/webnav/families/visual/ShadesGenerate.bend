import Base
import ./Generate.bend as G

def color(index: Nat) -> String:
  match index:
    case 0n: "red"
    case 1n: "green"
    case _: "blue"

def step_index(index: Nat) -> U32:
  match index:
    case 0n: 1
    case 1n: 5
    case 2n: 7
    case _: 11

def group_color(+rotation: U32, flip: Bool, group: Nat) -> U32:
  match flip group:
    case False{} 0n: rotation
    case False{} 1n: ((rotation + 1 : U32) % 3 : U32)
    case False{} _: ((rotation + 2 : U32) % 3 : U32)
    case True{} 0n: rotation
    case True{} 1n: ((rotation + 2 : U32) % 3 : U32)
    case True{} _: ((rotation + 1 : U32) % 3 : U32)

def hue(+rank: U32, +first: U32, second: U32,
    +rotation: U32, +flip: Bool) -> U32:
  Bool.pick(U32, U32.is_lt(rank, first),
    group_color(rotation, flip, 0n),
    Bool.pick(U32, U32.is_lt(rank, (first + second : U32)),
      group_color(rotation, flip, 1n),
      group_color(rotation, flip, 2n)))

def write_shades(n: Nat, +seed: U32, +start: U32, +stride: U32,
    +first: U32, +second: U32, +rotation: U32, +flip: Bool,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +i = U32.from_nat(p)
      +rank = ((start + i * stride : U32) % 12 : U32)
      +shade_hue = hue(rank, first, second, rotation, flip)
      +sat = (30 + (G.mix((seed + i * 41 + 601 : U32)) % 60 : U32) : U32)
      +light = (30 + (G.mix((seed + i * 43 + 607 : U32)) % 60 : U32) : U32)
      a[(base + 64 + i : U32)] <- shade_hue
      a[(base + 80 + i : U32)] <- sat
      a[(base + 96 + i : U32)] <- light
      write_shades(p, seed, start, stride, first, second,
        rotation, flip, base, a)

def generate(task: U32, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +expected = (G.mix((seed + 611 : U32)) % 3 : U32)
  +first = (3 + (G.mix((seed + 613 : U32)) % 3 : U32) : U32)
  +second = (3 + (G.mix((seed + 617 : U32)) % 3 : U32) : U32)
  +rotation = (G.mix((seed + 619 : U32)) % 3 : U32)
  +flip = U32.is_eq((G.mix((seed + 631 : U32)) % 2 : U32), 1)
  +start = (G.mix((seed + 641 : U32)) % 12 : U32)
  +stride = step_index(U32.to_nat((G.mix((seed + 643 : U32)) % 4 : U32)))
  a = G.header(task, seed, base, a)
  a[(base + 12 : U32)] <- 15000
  a[(base + 32 : U32)] <- expected
  a[(base + 33 : U32)] <- 0
  a[(base + 34 : U32)] <- 12
  a = write_shades(12n, seed, start, stride, first, second,
    rotation, flip, base, a)
  G.text("Select all the shades of " ++
    color(U32.to_nat(expected)) ++ " and press Submit.",
    (base + 512 : U32), a)
