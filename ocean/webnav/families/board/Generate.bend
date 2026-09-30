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

def write_opening(s: M.State, +base: U32, a: Array<U32>) -> Array<U32>:
  M.S{xmask, omask, random, outcome} = s
  a[(base + 32 : U32)] <- random
  a[(base + 33 : U32)] <- xmask
  a[(base + 34 : U32)] <- omask

def generate(+task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = clear_blocks(8n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 10000
  a = write_opening(M.opening(seed), base, a)
  text("Playing as 'X', win a game of tic-tac-toe.",
    (base + 256 : U32), a)
