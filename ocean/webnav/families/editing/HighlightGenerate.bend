import Base
import ./Generate.bend as G

def ordinal(i: Nat) -> String:
  match i:
    case 0n: "1st"
    case 1n: "2nd"
    case _: "3rd"

def chosen_text(i: Nat, a: String, b: String, c: String) -> String:
  match i:
    case 0n: a
    case 1n: b
    case _: c

def one(+task: U32, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +count = (3 + G.mix((seed + 31 : U32)) % 7 : U32)
  +body = G.paragraph(U32.to_nat(count), seed)
  +size = G.length(body)
  a = G.blocks(64n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 10000
  a[(base + 32 : U32)] <- 0
  a[(base + 33 : U32)] <- 1
  a[(base + 36 : U32)] <- size
  a[(base + 37 : U32)] <- size
  a[(base + 38 : U32)] <- (G.mix((seed + 47 : U32)) % 2 : U32)
  a[(base + 40 : U32)] <- 0
  a[(base + 41 : U32)] <- size
  a = G.text(body, (base + 2048 : U32), a)
  a = G.text(body, (base + 512 : U32), a)
  G.text("Highlight the text in the paragraph below and click submit.",
    (base + 256 : U32), a)

def three(+task: U32, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +n0 = (5 + G.mix((seed + 41 : U32)) % 3 : U32)
  +n1 = (5 + G.mix((seed + 43 : U32)) % 3 : U32)
  +n2 = (5 + G.mix((seed + 47 : U32)) % 3 : U32)
  +p0 = G.paragraph(U32.to_nat(n0), (seed + 101 : U32))
  +p1 = G.paragraph(U32.to_nat(n1), (seed + 211 : U32))
  +p2 = G.paragraph(U32.to_nat(n2), (seed + 307 : U32))
  +l0 = G.length(p0)
  +l1 = G.length(p1)
  +l2 = G.length(p2)
  +choice = (G.mix((seed + 701 : U32)) % 3 : U32)
  +wanted = chosen_text(U32.to_nat(choice), p0, p1, p2)
  +whole = p0 ++ "\n" ++ p1 ++ "\n" ++ p2
  a = G.blocks(64n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 10000
  a[(base + 32 : U32)] <- choice
  a[(base + 33 : U32)] <- 3
  a[(base + 36 : U32)] <- G.length(whole)
  a[(base + 37 : U32)] <- G.length(wanted)
  a[(base + 38 : U32)] <- (G.mix((seed + 67 : U32)) % 2 : U32)
  a[(base + 40 : U32)] <- 0
  a[(base + 41 : U32)] <- l0
  a[(base + 42 : U32)] <- (l0 + 1 : U32)
  a[(base + 43 : U32)] <- l1
  a[(base + 44 : U32)] <- (l0 + l1 + 2 : U32)
  a[(base + 45 : U32)] <- l2
  a = G.text(wanted, (base + 2048 : U32), a)
  a = G.text(whole, (base + 512 : U32), a)
  G.text("Highlight the text in the " ++ ordinal(U32.to_nat(choice)) ++
    " paragraph and click submit.", (base + 256 : U32), a)

def generate(+task: U32, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match task:
    case 1: one(task, seed, base, a)
    case _: three(task, seed, base, a)
