import Base
import ../../miniwob/tree/Wire.bend as T
import ../../miniwob/training/TreeGenerate.bend as G

# Preserve the already modeled tree body inside a v2 row. The wrapper only
# moves transport fields; the old pure Tree model owns every transition.
# Header 0..31; legacy tree body 32..287; row stride 512; eight lanes.
def publish_reward(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, reward) = pair
  a[(base + 11 : U32)] <- reward

def publish_raw(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, raw) = pair
  a = a[(base + 10 : U32)] <- raw
  publish_reward(base, a[(base + 44 : U32)])

def publish_elapsed(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, elapsed) = pair
  a = a[(base + 8 : U32)] <- elapsed
  publish_raw(base, a[(base + 43 : U32)])

def publish_status(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  a = a[(base + 9 : U32)] <- status
  a = a[(base + 2 : U32)] <- 0
  publish_elapsed(base, a[(base + 42 : U32)])

def publish(+base: U32, a: Array<U32>) -> Array<U32>:
  publish_status(base, a[(base + 34 : U32)])

def reset(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = G.clear(512n, base, a)
  a = a[base] <- 2
  a = a[(base + 3 : U32)] <- seed
  a = a[(base + 12 : U32)] <- 10000
  G.generate(seed, (base + 32 : U32), a)

def dispatch(op: Nat, seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match op:
    case 1n: reset(seed, base, a)
    case 2n: publish(base, T.row((base + 32 : U32), a))
    case _: a

def seed_ready(op: U32, base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  dispatch(U32.to_nat(op), seed, base, a)

def op_ready(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, op) = pair
  seed_ready(op, base, a[(base + 3 : U32)])

def row(+base: U32, a: Array<U32>) -> Array<U32>:
  op_ready(base, a[(base + 2 : U32)])

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: batch(p, row((U32.from_nat(p) * 512 : U32), a))
