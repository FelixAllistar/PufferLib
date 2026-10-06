import Base

# Exact bounded rational inputs to cartridge integer primitives. Numerators
# fit stock Bend's 48-bit Nat; no floating-point approximation is introduced.
# Non-finite tags preserve JS comparisons and ToUint32 (NaN/infinity -> zero).
type Scalar is Data:
  Finite{negative: Bool, numerator: Nat, denominator: U32}
  NaN{}
  Infinity{negative: Bool}

def trunc(number: Scalar) -> U32:
  match number:
    case Finite{negative, numerator, denominator}:
      +value = U32.from_nat((numerator / U32.to_nat(denominator) : Nat))
      Bool.pick(U32, negative, (0 - value : U32), value)
    case _: 0

def positive(number: Scalar) -> Bool:
  match number:
    case Finite{negative, numerator, _}: Bool.not(negative) && Bool.not(Nat.is_eq(numerator, 0n))
    case Infinity{negative}: Bool.not(negative)
    case NaN{}: False{}

def below_one(number: Scalar) -> Bool:
  match number:
    case Finite{negative, +numerator, denominator}:
      Bool.not(negative) && Bool.not(Nat.is_eq(numerator, 0n)) &&
        Nat.is_lt(numerator, U32.to_nat(denominator))
    case _: False{}

def numerator(high: U32, low: U32) -> Nat:
  (U32.to_nat(high) * U32.to_nat(65536) * U32.to_nat(65536) + U32.to_nat(low) : Nat)
