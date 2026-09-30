import Base
import ./Generate.bend as G

def criterion_name(code: Nat) -> String:
  match code:
    case 0n: "cheapest cost"
    case 1n: "shortest duration"
    case 2n: "longest duration"
    case _: "most expensive cost"

def price(+seed: U32, +index: U32) -> U32:
  (50 + G.mix((seed + index * 101 + 3109 : U32)) % 1950 : U32)

def duration(+seed: U32, +index: U32) -> U32:
  (60000 + G.mix((seed + index * 131 + 7109 : U32)) % 129540000 : U32)

def write_tickets(n: Nat, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +index = U32.from_nat(p)
      +at = (base + 64 + index * 4 : U32)
      a[at] <- price(seed, index)
      a[(at + 1 : U32)] <- duration(seed, index)
      write_tickets(p, seed, base, a)

def generate(task: U32, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +criterion = (G.mix((seed + 211 : U32)) % 4 : U32)
  +day = (1 + G.mix((seed + 223 : U32)) % 150 : U32)
  a = G.clear_blocks(64n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 2 : U32)] <- 0
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 10000
  a[(base + 32 : U32)] <- criterion
  a[(base + 33 : U32)] <- day
  a[(base + 40 : U32)] <- 4
  a = write_tickets(4n, seed, base, a)
  G.write_text("Buy the ticket with the " ++
    criterion_name(U32.to_nat(criterion)) ++ ".", (base + 512 : U32), a)
