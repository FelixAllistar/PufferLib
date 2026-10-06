import Base
import ./Number.bend as N
import ./Math.bend as M

# Event/move outcomes must not collapse undefined/null/false/zero/empty text.
# Object references remain typed by family and ID; an object is always truthy.
type Value is Data:
  VUndefined{}
  VNull{}
  VBool{value: Bool}
  VNumber{number: N.Scalar}
  VText{id: U32}
  VObject{family: U32, id: U32}

type Relay is Data:
  R{value: Value, stopped: Bool}

def numeric_truth(number: N.Scalar) -> Bool:
  match number:
    case N.Finite{_, numerator, _}: Bool.not(Nat.is_eq(numerator, 0n))
    case N.NaN{}: False{}
    case N.Infinity{_}: True{}

def truthy(value: Value) -> Bool:
  match value:
    case VUndefined{}: False{}
    case VNull{}: False{}
    case VBool{yes}: yes
    case VNumber{number}: numeric_truth(number)
    case VText{id}: U32.is_ne(id, 0)
    case VObject{_, _}: True{}

# singleEvent defaults only undefined; runEvent defaults undefined or null.
def initial(value: Value, single: Bool) -> Value:
  match value:
    case VUndefined{}: VBool{True{}}
    case VNull{}: Bool.pick(Value, single, VNull{}, VBool{True{}})
    case VBool{yes}: VBool{yes}
    case VNumber{number}: VNumber{number}
    case VText{id}: VText{id}
    case VObject{family, id}: VObject{family, id}

def defined(value: Value) -> Bool:
  match value:
    case VUndefined{}: False{}
    case _: True{}

def replace(+old: Value, +returned: Value, fast_exit: Bool) -> Relay:
  Bool.pick(Relay, defined(returned), R{returned, Bool.not(truthy(returned)) || fast_exit}, R{old, False{}})

def single_result(old: Value, +returned: Value) -> Value:
  Bool.pick(Value, defined(returned), returned, old)

def modifiable(number: N.Scalar) -> Bool:
  match number:
    case N.Finite{negative, +numerator, +denominator}:
      (Bool.not(negative) || Nat.is_eq(numerator, 0n)) &&
        Nat.is_eq((numerator % U32.to_nat(U32.max(denominator, 1)) : Nat), 0n)
    case N.Infinity{negative}: Bool.not(negative)
    case N.NaN{}: False{}

def integer_magnitude(number: N.Scalar) -> Nat:
  match number:
    case N.Finite{_,numerator,denominator}: (numerator / U32.to_nat(U32.max(denominator,1)) : Nat)
    case _: 0n

def final_number(+number: N.Scalar, modifier: U32) -> Value:
  Bool.pick(Value, modifiable(number),
    VNumber{N.Finite{False{}, U32.to_nat(M.modify_integer(integer_magnitude(number), modifier)), 1}}, VNumber{number})

def final_value(value: Value, modifier: U32) -> Value:
  match value:
    case VNumber{number}: final_number(number, modifier)
    case VUndefined{}: VUndefined{}
    case VNull{}: VNull{}
    case VBool{yes}: VBool{yes}
    case VText{id}: VText{id}
    case VObject{family, id}: VObject{family, id}

def literal_kind(kind: Nat, +payload: U32) -> Result<&2, &2, U32, Value>:
  match kind:
    case 0n: Fail{4}
    case 1n: Done{VBool{U32.is_ne(payload, 0)}}
    case 2n: Done{VNumber{N.Finite{False{}, U32.to_nat(payload), 1}}}
    case 3n: Done{VText{payload}}
    case 4n: Done{VUndefined{}}
    case 5n:
      Done{VNumber{N.Finite{U32.is_lt(payload, 32768),
        U32.to_nat((U32.max(payload, 32768) - U32.min(payload, 32768) : U32)), 10}}}
    case _: Fail{3}
