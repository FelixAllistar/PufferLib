import Base
import ./Number.bend as N
import ./EventValue.bend as V

# Weight is an integer number of hectograms in reachable source state. Heavy
# Metal doubles before event finalization; it must not wrap to U32 early.
def doubled_magnitude(negative: Bool, magnitude: Nat) -> N.Scalar:
  N.Finite{negative,(magnitude * 2n : Nat),1}

def double_checked(allowed: Bool, negative: Bool, magnitude: Nat) -> Result<&2,&2,U32,N.Scalar>:
  match allowed:
    case True{}: Done{doubled_magnitude(negative,magnitude)}
    case False{}: Fail{3}

def doubled(number: N.Scalar) -> Result<&2,&2,U32,N.Scalar>:
  match number:
    case N.Finite{negative,+numerator,+denominator}:
      +den : Nat = U32.to_nat(U32.max(denominator,1))
      +magnitude : Nat = (numerator / den : Nat)
      double_checked(U32.is_ne(denominator,0) &&
        Nat.is_eq((numerator % den : Nat),0n) && Nat.is_le(magnitude,N.numerator(32767,4294967295)),
        negative,magnitude)
    case N.NaN{}: Done{N.NaN{}}
    case N.Infinity{negative}: Done{N.Infinity{negative}}

# Source this.trunc(weight / 2) truncates toward zero, then converts to U32.
# Divide the magnitude before unsigned conversion, including negative inputs.
def halved(number: N.Scalar) -> U32:
  match number:
    case N.Finite{negative,numerator,denominator}:
      N.trunc(N.Finite{negative,(numerator / (2n * U32.to_nat(U32.max(denominator,1)) : Nat) : Nat),1})
    case _: 0

def double_value(value: V.Value) -> Result<&2,&2,U32,N.Scalar>:
  match value:
    case V.VNumber{number}: doubled(number)
    case _: Fail{3}

def half_value(value: V.Value) -> Result<&2,&2,U32,U32>:
  match value:
    case V.VNumber{number}: Done{halved(number)}
    case _: Fail{3}

def minimum(number: U32) -> U32:
  U32.max(number,1)
