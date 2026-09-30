import Base

def mix(+x: U32) -> U32:
  +a = (x .^. (x >> 16n) : U32)
  +b = (a * 2146121005 : U32)
  +c = (b .^. (b >> 15n) : U32)
  +d = (c * 2221713035 : U32)
  (d .^. (d >> 16n) : U32)

def clear(n: Nat, +at: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: clear(p, at, a[(at + U32.from_nat(p) : U32)] <- 0)

def blocks(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      blocks(p, base,
        clear(128n, (base + U32.from_nat(p) * 128 : U32), a))

def text(s: String, +at: U32, a: Array<U32>) -> Array<U32>:
  match s:
    case SNil{}: a
    case SCon{ch, rest}: text(rest, (at + 1 : U32), a[at] <- Char.to_u32(ch))

def length(s: String) -> U32:
  match s:
    case SNil{}: 0
    case SCon{ch, rest}: (1 + length(rest) : U32)

def lower(i: Nat) -> String:
  match i:
    case 0n: "lorem"
    case 1n: "ipsum"
    case 2n: "dolor"
    case 3n: "amet"
    case 4n: "tempor"
    case 5n: "nunc"
    case 6n: "mattis"
    case _: "viverra"

def upper(i: Nat) -> String:
  match i:
    case 0n: "Lorem"
    case 1n: "Ipsum"
    case 2n: "Dolor"
    case 3n: "Amet"
    case 4n: "Tempor"
    case 5n: "Nunc"
    case 6n: "Mattis"
    case _: "Viverra"

def word(+seed: U32, +index: U32) -> String:
  +id = (mix((seed + index * 31 + 137 : U32)) % 8 : U32)
  Bool.pick(String, U32.is_eq(index, 0), upper(U32.to_nat(id)),
    lower(U32.to_nat(id)))

def paragraph(n: Nat, +seed: U32) -> String:
  match n:
    case 0n: ""
    case 1n+ +p:
      paragraph(p, seed) ++ word(seed, U32.from_nat(p)) ++ " "

def suffix(i: Nat) -> String:
  match i:
    case 0n: "1st"
    case 1n: "2nd"
    case 2n: "3rd"
    case 3n: "4th"
    case 4n: "5th"
    case _: "6th"

def generate(+task: U32, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +chosen = (mix((seed + 701 : U32)) % 6 : U32)
  +wanted = word(seed, chosen)
  a = blocks(64n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 10000
  a[(base + 32 : U32)] <- chosen
  a[(base + 33 : U32)] <- 6
  a[(base + 38 : U32)] <- length(wanted)
  a = text(wanted, (base + 1024 : U32), a)
  a = text(paragraph(6n, seed), (base + 512 : U32), a)
  text("Find the " ++ suffix(U32.to_nat(chosen)) ++
    " word in the paragraph, type that into the textbox and press \"Submit\".",
    (base + 256 : U32), a)
