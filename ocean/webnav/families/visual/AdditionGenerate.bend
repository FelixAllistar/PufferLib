import Base
import ./Generate.bend as G

# Source samples two independent values from 1..10. The plus sign printed
# between them is presentation; no answer is serialized into public data.
def generate(task: U32, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  left_count = (1 + (G.mix((seed + 811 : U32)) % 10 : U32) : U32)
  right_count = (1 + (G.mix((seed + 821 : U32)) % 10 : U32) : U32)
  a = G.header(task, seed, base, a)
  a[(base + 12 : U32)] <- 15000
  a[(base + 32 : U32)] <- left_count
  a[(base + 33 : U32)] <- right_count
  a = a[(base + 34 : U32)] <- 0
  G.text("Type the total number of blocks into the textbox and press Submit.",
    (base + 512 : U32), a)
