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
      blocks(p, base, clear(128n, (base + U32.from_nat(p) * 128 : U32), a))

def text(s: String, +at: U32, a: Array<U32>) -> Array<U32>:
  match s:
    case SNil{}: a
    case SCon{ch, rest}: text(rest, (at + 1 : U32), a[at] <- Char.to_u32(ch))

def generate(+task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +horizontal = (mix((seed + 811 : U32)) % 2 : U32)
  a = blocks(16n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 10000
  a[(base + 32 : U32)] <- ((30 + mix((seed + 19 : U32)) % 100) * 65536 : U32)
  a[(base + 33 : U32)] <- ((30 + mix((seed + 373 : U32)) % 60) * 65536 : U32)
  a[(base + 34 : U32)] <- horizontal
  query = Bool.pick(String, U32.is_eq(task, 0),
    "Draw a circle centered around the marked point by dragging the mouse. Press submit when done.",
    "Draw a " ++ Bool.pick(String, U32.is_eq(horizontal, 1), "horizontal", "vertical") ++
    " line that runs through the dot, then press submit.")
  text(query, (base + 768 : U32), a)
