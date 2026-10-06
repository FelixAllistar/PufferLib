import Base
import ./Rng.bend as R
import ./Mem.bend as S
import ./Lists.bend as L
import ./SodiumMem.bend as Sodium

def save(pair: R.Seed & U32) -> S.Mem<U32>:
  (R.S{a, b, c, d}, value) = pair
  do S.Mem<U32>:
    S.put(8, a)
    S.put(9, b)
    S.put(10, c)
    S.put(11, d)
    return value

def gen5(bound: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    a : U32 <- S.get(8)
    b : U32 <- S.get(9)
    c : U32 <- S.get(10)
    d : U32 <- S.get(11)
    save(R.random(R.S{a, b, c, d}, bound))

def mode(kind: Nat, bound: U32) -> S.Mem<U32>:
  match kind:
    case 0n: gen5(bound)
    case 1n: Sodium.random(bound)
    case _: S.raise(U32, 3)

def random(bound: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    kind : U32 <- S.get(4)
    mode(U32.to_nat(kind), bound)

def chance(numerator: U32, denominator: U32) -> S.Mem<Bool>:
  do S.Mem<Bool>:
    value : U32 <- random(denominator)
    return U32.is_lt(value, numerator)

def sample_nonempty(+xs: +List<U32>) -> S.Mem<U32>:
  do S.Mem<U32>:
    index_ : U32 <- random(L.length(xs))
    return L.at(xs, U32.to_nat(index_))

def sample_empty(yes: Bool, xs: +List<U32>) -> S.Mem<U32>:
  match yes:
    case True{}: S.raise(U32, 2)
    case False{}: sample_nonempty(xs)

def sample(+xs: +List<U32>) -> S.Mem<U32>:
  sample_empty(U32.is_eq(L.length(xs), 0), xs)

def shuffle(n: Nat, +pos: U32, +size: U32, +xs: +List<U32>) -> S.Mem<+List<U32>>:
  match n:
    case 0n: S.Mem.pure(+List<U32>, xs)
    case 1n+p:
      do S.Mem<+List<U32>>:
        draw : U32 <- random((size - pos : U32))
        +at : U32 = (pos + draw : U32)
        left : U32 = L.at(xs, U32.to_nat(pos))
        right : U32 = L.at(xs, U32.to_nat(at))
        shuffled : +List<U32> = L.replace(L.replace(xs, U32.to_nat(pos), right), U32.to_nat(at), left)
        shuffle(p, (pos + 1 : U32), size, shuffled)

def shuffle_size(yes: Bool, +size: U32, xs: +List<U32>) -> S.Mem<+List<U32>>:
  match yes:
    case True{}: S.Mem.pure(+List<U32>, xs)
    case False{}: shuffle(U32.to_nat((size - 1 : U32)), 0, size, xs)

def shuffle_moves(+xs: +List<U32>) -> S.Mem<+List<U32>>:
  +size = L.length(xs)
  shuffle_size(U32.is_le(size, 1), size, xs)
