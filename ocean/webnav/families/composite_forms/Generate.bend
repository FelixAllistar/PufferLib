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

def clear_blocks(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      clear_blocks(p, base,
        clear(128n, (base + U32.from_nat(p) * 128 : U32), a))

def write_text(s: String, +at: U32, a: Array<U32>) -> Array<U32>:
  match s:
    case SNil{}: a
    case SCon{ch, rest}:
      write_text(rest, (at + 1 : U32), a[at] <- Char.to_u32(ch))

def signed_offset(+value: U32) -> String:
  Bool.pick(String, U32.is_lt(value, 10),
    "-" ++ U32.show((10 - value : U32)), U32.show((value - 10 : U32)))

def ordinal(index: U32) -> String:
  match index:
    case 1: "1st"
    case 2: "2nd"
    case _: "3rd"

def query(+value: U32, +box: U32) -> String:
  "Select " ++ signed_offset(value) ++ " with the slider, click the " ++
    ordinal(box) ++ " checkbox, then hit Submit."

def generate(+task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +start = (mix((seed + 19 : U32)) % 21 : U32)
  +target = (mix((seed + 37 : U32)) % 21 : U32)
  +box = (1 + mix((seed + 53 : U32)) % 3 : U32)
  +width = (50 + mix((seed + 71 : U32)) % 65 : U32)
  a = clear_blocks(64n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 2 : U32)] <- 0
  a[(base + 3 : U32)] <- seed
  a[(base + 8 : U32)] <- 0
  a[(base + 9 : U32)] <- 0
  a[(base + 10 : U32)] <- F32.bits(0.0)
  a[(base + 11 : U32)] <- F32.bits(0.0)
  a[(base + 12 : U32)] <- 10000
  a[(base + 32 : U32)] <- start
  a[(base + 33 : U32)] <- 0
  a[(base + 34 : U32)] <- target
  a[(base + 35 : U32)] <- box
  a[(base + 36 : U32)] <- width
  write_text(query(target, box), (base + 128 : U32), a)
