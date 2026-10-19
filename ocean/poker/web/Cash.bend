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

# Automatic continuation never buys chips for the human.
def auto_ready(terminal: Bool, stack: U32, paused: Bool, remaining: U32) -> U32:
  match terminal:
    case False{}: 0
    case True{}:
      C.choose(paused, 0, (C.choose(U32.is_gt(stack, 0), 1, 0)
        * C.choose(U32.is_eq(remaining, 0), 1, 0) : U32))

def result_delay(showdown: Bool) -> U32:
  C.choose(showdown, 6000, 3000)

# At showdown, a sole contributor's unmatched excess is a refund, not a win.
def award(stack: U32, initial: U32, +total: U32, other_high: U32, showdown: Bool) -> U32:
  (stack + total - initial - C.choose(showdown,
    (total - C.minimum(total, other_high) : U32), 0) : U32)
