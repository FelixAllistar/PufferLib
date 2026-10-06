import Base
import ./Math.bend as M

type Seed is Data:
  S{a: U32, b: U32, c: U32, d: U32}

# sim/prng.ts: Gen5RNG.multiplyAdd/nextFrame. U32 arithmetic deliberately
# matches JS's unsigned carry truncation, including overflowing partial sums.
def next(seed: Seed) -> Seed & U32:
  S{+a, +b, +c, +d} = seed
  +p0 = (d * 35173 + 40643 : U32)
  +p1 = (c * 35173 + d * 27655 + 38 + (p0 >> 16n : U32) : U32)
  +p2 = (b * 35173 + c * 27655 + d * 35685 + (p1 >> 16n : U32) : U32)
  p3 = (a * 35173 + b * 27655 + c * 35685 + d * 23896 + (p2 >> 16n : U32) : U32)
  +s0 = (p3 .&. 65535 : U32)
  +s1 = (p2 .&. 65535 : U32)
  (S{s0, s1, (p1 .&. 65535 : U32), (p0 .&. 65535 : U32)}, ((s0 << 16n : U32) + s1 : U32))

def scale(pair: Seed & U32, bound: U32) -> Seed & U32:
  (seed, value) = pair
  (seed, M.mul_hi(value, bound))

def random(seed: Seed, bound: U32) -> Seed & U32:
  scale(next(seed), bound)

def chance_result(pair: Seed & U32, numerator: U32) -> Seed & Bool:
  (seed, value) = pair
  (seed, U32.is_lt(value, numerator))

def chance(seed: Seed, numerator: U32, denominator: U32) -> Seed & Bool:
  chance_result(random(seed, denominator), numerator)
