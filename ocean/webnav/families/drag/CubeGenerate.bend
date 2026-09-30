import Base
import ./Generate.bend as G

def generate(+task: U32, +seed: U32, +base: U32,
             a: Array<U32>) -> Array<U32>:
  a = G.clear_blocks(8n, base, a)
  +goal = (1 + (G.next(seed) % 6 : U32) : U32)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 15000
  a[(base + 32 : U32)] <- goal
  a[(base + 33 : U32)] <- 6
  G.text("Move the cube around so that \"" ++ U32.show(goal) ++
    "\" is the active side facing the user.", (base + 256 : U32), a)
