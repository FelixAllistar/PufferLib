import Base

# Array ownership plumbing shared by primitive and application transports.
def at(xs: +List<U32>, index: Nat) -> U32:
  match xs index:
    case Nil{} _: 0
    case Con{x, rest} 0n: x
    case Con{x, rest} 1n+p: at(rest, p)

def prepend(pair: Array<U32> & U32, xs: +List<U32>) -> Array<U32> & +List<U32>:
  (a, x) = pair
  (a, x <> xs)

def read(n: Nat, +base: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, xs) = pair
      read(p, base, prepend(a[(base + U32.from_nat(p) : U32)], xs))

def clear(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: clear(p, base, a[(base + U32.from_nat(p) : U32)] <- 0)

def write(xs: +List<U32>, +at: U32, a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{x, rest}: write(rest, (at + 1 : U32), a[at] <- x)
