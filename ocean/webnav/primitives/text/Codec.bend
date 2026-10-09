import Base

# Transport chooses the supported character repertoire. Neither conversion
# reads task goals or interprets instructions.
def units(text: String) -> +List<U32>:
  match text:
    case SNil{}: Nil{}
    case SCon{ch, rest}: Char.to_u32(ch) <> units(rest)

def text(units: +List<U32>) -> String:
  match units:
    case Nil{}: SNil{}
    case Con{ch, rest}: SCon{Char.from_u32(ch), text(rest)}
