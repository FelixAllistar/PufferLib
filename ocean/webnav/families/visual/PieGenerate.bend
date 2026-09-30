import Base
import ./Generate.bend as G

def nth(s: String, index: Nat) -> U32:
  match s index:
    case SNil{} _: 0
    case SCon{ch, rest} 0n: Char.to_u32(ch)
    case SCon{ch, rest} 1n+p: nth(rest, p)

def alphabet() -> String:
  "qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM1234567890"

def item(+start: U32, +stride: U32, index: U32) -> U32:
  nth(alphabet(), U32.to_nat(((start + index * stride : U32) % 62 : U32)))

def write_items(n: Nat, +start: U32, +stride: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      a = a[(base + 64 + U32.from_nat(p) : U32)] <-
        item(start, stride, U32.from_nat(p))
      write_items(p, start, stride, base, a)

def query(goal: U32) -> String:
  "Expand the pie menu below and click on the item labeled \"" ++
    SCon{Char.from_u32(goal), SNil{}} ++ "\"."

# Caller clears the whole row and writes the common ABI header.
def generate(+task: U32, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +start = (G.mix((seed + 511 : U32)) % 62 : U32)
  +stride = (1 + (G.mix((seed + 521 : U32)) % 8 : U32) * 2 : U32)
  +count = (4 + (G.mix((seed + 523 : U32)) % 5 : U32) : U32)
  +goal_index = (G.mix((seed + 541 : U32)) % count : U32)
  +goal = item(start, stride, goal_index)
  a = G.header(task, seed, base, a)
  a[(base + 32 : U32)] <- count
  a[(base + 33 : U32)] <- goal
  a[(base + 34 : U32)] <- 0
  a[(base + 35 : U32)] <- 0
  a = write_items(U32.to_nat(count), start, stride, base, a)
  G.text(query(goal), (base + 512 : U32), a)
