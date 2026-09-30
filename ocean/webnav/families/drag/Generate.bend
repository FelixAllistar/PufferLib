import Base
import ./Model.bend as M

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

def text(s: String, +at: U32, a: Array<U32>) -> Array<U32>:
  match s:
    case SNil{}: a
    case SCon{ch, rest}: text(rest, (at + 1 : U32), a[at] <- Char.to_u32(ch))

def next(seed: U32) -> U32:
  (seed * 73 + 19 : U32)

def generate(+task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = clear_blocks(8n, base, a)
  +r1 = next(seed)
  +r2 = next(r1)
  +r3 = next(r2)
  +r4 = next(r3)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 10000
  a[(base + 32 : U32)] <- ((r1 % 129 : U32) + 256 : U32)
  a[(base + 33 : U32)] <- ((r2 % 75 : U32) + 256 : U32)
  a[(base + 34 : U32)] <- ((r3 % 99 : U32) + 256 : U32)
  a[(base + 35 : U32)] <- ((r4 % 45 : U32) + 256 : U32)
  text("Drag the smaller box so that it is completely inside the larger box.",
    (base + 256 : U32), a)
