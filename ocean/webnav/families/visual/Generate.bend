import Base

def mix(+x: U32) -> U32:
  +a = (x .^. (x >> 16n) : U32)
  +b = (a * 2146121005 : U32)
  +c = (b .^. (b >> 15n) : U32)
  +d = (c * 2221713035 : U32)
  (d .^. (d >> 16n) : U32)

def clear(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: clear(p, base, a[(base + U32.from_nat(p) : U32)] <- 0)

def blocks(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      blocks(p, base, clear(128n,
        (base + U32.from_nat(p) * 128 : U32), a))

def text(s: String, +at: U32, a: Array<U32>) -> Array<U32>:
  match s:
    case SNil{}: a
    case SCon{ch, rest}: text(rest, (at + 1 : U32), a[at] <- Char.to_u32(ch))

def name_index(index: Nat) -> String:
  match index:
    case 1n: "red"
    case 2n: "blue"
    case 3n: "olive"
    case 4n: "lime"
    case 5n: "black"
    case 6n: "white"
    case 7n: "grey"
    case 8n: "purple"
    case 9n: "orange"
    case 10n: "yellow"
    case 11n: "cyan"
    case 12n: "pink"
    case _: "magenta"

def name(code: U32) -> String:
  name_index(U32.to_nat(code))

def color(start: U32, step: U32, index: U32) -> U32:
  ((start + index * step : U32) % 13 + 1 : U32)

def chosen(index: Nat, a: U32, b: U32, c: U32, d: U32) -> U32:
  match index:
    case 0n: a
    case 1n: b
    case 2n: c
    case _: d

def query(swatch: Bool, expected: U32) -> String:
  match swatch:
    case True{}: "Click on the  colored box."
    case False{}: "Click on the " ++ name(expected) ++ " colored box."

def header(task: U32, seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  a = blocks(16n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 10000

def generate(task: U32, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +start = (mix((seed + 101 : U32)) % 13 : U32)
  +stride = (1 + (mix((seed + 103 : U32)) % 12 : U32) : U32)
  +ca = color(start, stride, 0)
  +cb = color(start, stride, 1)
  +cc = color(start, stride, 2)
  +cd = color(start, stride, 3)
  +which = U32.to_nat((mix((seed + 107 : U32)) % 4 : U32))
  +expected = chosen(which, ca, cb, cc, cd)
  +swatch = U32.is_ge((mix((seed + 109 : U32)) % 100 : U32), 40)
  a = header(task, seed, base, a)
  a[(base + 32 : U32)] <- ca
  a[(base + 33 : U32)] <- cb
  a[(base + 34 : U32)] <- cc
  a[(base + 35 : U32)] <- cd
  a[(base + 36 : U32)] <- expected
  a[(base + 37 : U32)] <- Bool.to_u32(swatch)
  text(query(swatch, expected), (base + 512 : U32), a)
