import Base
import ./Controller.bend as C

# A seat keeps its actual stack; only an empty seat needs a fresh buy-in.
def next_stack(+stack: U32, buyin: U32) -> U32:
  C.choose(U32.is_eq(stack, 0), buyin, stack)

def should_record(terminal: Bool, hand: U32, recorded: U32) -> U32:
  match terminal:
    case False{}: 0
    case True{}: C.choose(U32.is_gt(hand, recorded), 1, 0)

def percent(+part: U32, +total: U32) -> U32:
  C.choose(U32.is_eq(total, 0), 0, (part * 10000 / C.maximum(total, 1) : U32))
