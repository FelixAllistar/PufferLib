import Base
import ./Sodium.bend as R
import ./Mem.bend as S

# Private header seed; diagnostic arrays and catalog never contain policy data.
def read() -> S.Mem<R.Seed>:
  do S.Mem<R.Seed>:
    a : U32 <- S.get(560)
    b : U32 <- S.get(561)
    c : U32 <- S.get(562)
    d : U32 <- S.get(563)
    e : U32 <- S.get(564)
    f : U32 <- S.get(565)
    g : U32 <- S.get(566)
    h : U32 <- S.get(567)
    return R.K{a,b,c,d,e,f,g,h}

def write(seed: R.Seed) -> S.Mem<Unit>:
  R.K{a,b,c,d,e,f,g,h} = seed
  do S.Mem<Unit>:
    S.put(560,a)
    S.put(561,b)
    S.put(562,c)
    S.put(563,d)
    S.put(564,e)
    S.put(565,f)
    S.put(566,g)
    S.put(567,h)

def save(pair: R.Seed & U32) -> S.Mem<U32>:
  (seed, value) = pair
  do S.Mem<U32>:
    write(seed)
    return value

def random(bound: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    seed : R.Seed <- read()
    save(R.random(seed, bound))
