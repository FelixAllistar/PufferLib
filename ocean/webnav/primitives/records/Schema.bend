import Base
import ./Value.bend as V

type ValueRule is Data:
  Boolean{}
  Integer{minimum: U32, maximum: U32}
  Text{maximum: Nat, nonempty: Bool}
  Reference{collection: U32}
  Optional{inner: ValueRule}

type Field is Data:
  F{kind: ValueRule, editable: Bool}

def accepts(kind: ValueRule, value: V.Value) -> Bool:
  match kind value:
    case Boolean{} V.Flag{flag}: True{}
    case Integer{minimum, maximum} V.Integer{+bits}:
      Bool.not(V.less(V.Integer{bits}, V.Integer{minimum})) &&
        Bool.not(V.less(V.Integer{maximum}, V.Integer{bits}))
    case Text{maximum, nonempty} V.Text{+text}:
      Nat.is_le(String.length(text), maximum) && (Bool.not(nonempty) || Bool.not(String.is_empty(text)))
    case Reference{collection} V.Reference{actual, id}:
      U32.is_eq(collection, actual) && U32.is_gt(id, 0)
    case Optional{inner} V.Null{}: True{}
    case Optional{inner} _: accepts(inner, value)
    case _ _: False{}

def valid(fields: +List<Field>, values: +List<V.Value>) -> Bool:
  match fields values:
    case Nil{} Nil{}: True{}
    case Con{F{kind, writable}, rest} Con{value, tail}:
      accepts(kind, value) && valid(rest, tail)
    case _ _: False{}

def editable(fields: +List<Field>, index: Nat) -> Bool:
  match fields index:
    case Nil{} _: False{}
    case Con{F{kind, allowed}, rest} 0n: allowed
    case Con{field, rest} 1n+p: editable(rest, p)
