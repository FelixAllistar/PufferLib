import Base
import ./Codec.bend as Text
import ../records/Value.bend as V

# Parse a nonnegative signed-32-bit decimal field. Incomplete/invalid input
# remains text in the draft; schema validation can then display an error.
type Parsed is Data:
  Valid{value: U32}
  Invalid{}

def finish(valid: Bool, value: U32) -> Parsed:
  match valid:
    case True{}: Valid{value}
    case False{}: Invalid{}

def scan(units: +List<U32>, +value: U32, valid: Bool) -> Parsed:
  match units:
    case Nil{}: finish(valid, value)
    case Con{+ch, rest}:
      +digit = (ch - 48 : U32)
      scan(rest, (value * 10 + digit : U32), valid &&
        U32.is_ge(ch, 48) && U32.is_le(ch, 57) &&
        U32.is_le(value, ((2147483647 - digit : U32) / 10 : U32)))

def parse(units: +List<U32>) -> Parsed:
  match units:
    case Nil{}: Invalid{}
    case Con{ch, rest}: scan(ch <> rest, 0, True{})

def value(result: Parsed, units: +List<U32>) -> V.Value:
  match result:
    case Valid{number}: V.Integer{number}
    case Invalid{}: V.Text{Text.text(units)}

def field(+units: +List<U32>) -> V.Value:
  value(parse(units), units)
