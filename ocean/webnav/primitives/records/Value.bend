import Base

# Signed integers use two's-complement U32 bits. Money/date units are schema
# metadata (for example cents or epoch days); no floating-point rounding here.
type Value is Data:
  Null{}
  Flag{value: Bool}
  Integer{bits: U32}
  Text{value: String}
  Reference{collection: U32, key: U32}

def rank(value: Value) -> U32:
  match value:
    case Null{}: 0
    case Flag{flag}: 1
    case Integer{bits}: 2
    case Text{text}: 3
    case Reference{collection, key}: 4

def then_compare(first: Cmp, second: Cmp) -> Cmp:
  match first:
    case EQ{}: second
    case _: first

def compare(a: Value, b: Value) -> Cmp:
  match a b:
    case Null{} Null{}: EQ{}
    case Flag{x} Flag{y}: U32.cmp(Bool.to_u32(x), Bool.to_u32(y))
    case Integer{x} Integer{y}: U32.cmp((x .^. 2147483648 : U32), (y .^. 2147483648 : U32))
    case Text{x} Text{y}: String.order(x, y)
    case Reference{ac, ak} Reference{bc, bk}: then_compare(U32.cmp(ac, bc), U32.cmp(ak, bk))
    case _ _: U32.cmp(rank(a), rank(b))

def is_equal(order: Cmp) -> Bool:
  match order:
    case EQ{}: True{}
    case _: False{}

def equal(a: Value, b: Value) -> Bool:
  is_equal(compare(a, b))

def is_less(order: Cmp) -> Bool:
  match order:
    case LT{}: True{}
    case _: False{}

def less(a: Value, b: Value) -> Bool:
  is_less(compare(a, b))

def contains(a: Value, b: Value) -> Bool:
  match a b:
    case Text{value} Text{part}: String.contains(value, part)
    case _ _: False{}

def prefix(a: Value, b: Value) -> Bool:
  match a b:
    case Text{value} Text{part}: String.starts_with(value, part)
    case _ _: False{}
