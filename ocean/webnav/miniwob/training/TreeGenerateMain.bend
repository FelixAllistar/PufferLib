import Base
import ./TreeGenerate.bend as G

def Host.read() -> IO(Array<U32>):
  import "../../bend/read.c"

def Host.write(a: Array<U32>) -> IO(Unit):
  import "../../bend/write.c"

def read_word(index: U32, a: Array<U32>) -> Array<U32> & U32:
  a[index]

def generate_one(pair: Array<U32> & U32, base: U32) -> Array<U32>:
  (a, seed) = pair
  G.generate(seed, base, a)

def generate_batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n:
      a
    case 1n+ +p:
      +base = (U32.from_nat(p) * 256 : U32)
      generate_batch(p, generate_one(read_word(base, a), base))

def main() -> IO(Unit):
  do IO<Unit>:
    a : Array<U32> <- Host.read()
    Host.write(generate_batch(32n, a))
