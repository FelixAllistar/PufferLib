import Base

# Multiple-select values are checked as the source checks them: exact selected
# count, then membership of each selected option value in the expected values.
# The allowed mask may contain both options when the source has duplicate text.
type State is Data:
  S{position: U32, height: U32, client: U32, count: U32, stride: U32,
    selected: U32, allowed: U32, required: U32, anchor: U32, status: U32}

type Action is Data:
  Wait{}
  Scroll{position: U32}
  Select{index: U32, chosen: Bool}
  Submit{}

def bit(n: Nat) -> U32:
  match n:
    case 0n: 1
    case 1n+p: (2 * bit(p) : U32)

def has(mask: U32, index: U32) -> Bool:
  U32.is_eq(((mask / bit(U32.to_nat(index))) % 2 : U32), 1)

def set(+mask: U32, +index: U32, chosen: Bool) -> U32:
  +flag = bit(U32.to_nat(index))
  +old = has(mask, index)
  Bool.pick(U32, chosen, Bool.pick(U32, old, mask, (mask + flag : U32)),
    Bool.pick(U32, old, (mask - flag : U32), mask))

def selected_count(n: Nat, +mask: U32) -> U32:
  match n:
    case 0n: 0
    case 1n+ +p:
      (selected_count(p, mask) + Bool.to_u32(has(mask, U32.from_nat(p))) : U32)

def all_allowed(n: Nat, +selected: U32, +allowed: U32) -> Bool:
  match n:
    case 0n: True{}
    case 1n+ +p:
      (Bool.not(has(selected, U32.from_nat(p))) || has(allowed, U32.from_nat(p))) &&
        all_allowed(p, selected, allowed)

def correct(+count: U32, +selected: U32, allowed: U32, required: U32) -> Bool:
  U32.is_eq(selected_count(U32.to_nat(count), selected), required) &&
    all_allowed(U32.to_nat(count), selected, allowed)

def max_scroll(+height: U32, +client: U32) -> U32:
  Bool.pick(U32, U32.is_gt(height, client), (height - client : U32), 0)

def reveal(+position: U32, +client: U32, +stride: U32, index: U32) -> U32:
  +top = (index * stride : U32)
  +bottom = (top + stride : U32)
  +lower = (U32.max(bottom, client) - client : U32)
  Bool.pick(U32, U32.is_gt(position, top), top,
    Bool.pick(U32, U32.is_gt(bottom, (position + client : U32)), lower, position))

# Chromium 153 keeps the first programmatically selected option as its active
# selection end. Later changed selections, including deselections, reveal that
# persistent anchor. An unchanged selected property schedules no scroll.
# Anchor is one-based; zero means no option has been selected in this instance.
def selected_anchor(+anchor: U32, index: U32, chosen: Bool) -> U32:
  Bool.pick(U32, U32.is_eq(anchor, 0) && chosen, (index + 1 : U32), anchor)

def selected_position(+position: U32, client: U32, stride: U32,
                      +anchor: U32, changed: Bool) -> U32:
  Bool.pick(U32, changed && U32.is_gt(anchor, 0),
    reveal(position, client, stride, (anchor - 1 : U32)), position)

def active(s: State, action: Action) -> State:
  match s action:
    case S{position, height, client, count, stride, selected, allowed, required, anchor, status} Wait{}:
      S{position, height, client, count, stride, selected, allowed, required, anchor, status}
    case S{position, +height, +client, count, stride, selected, allowed, required, anchor, status} Scroll{next}:
      S{U32.min(next, max_scroll(height, client)), height, client, count, stride, selected, allowed, required, anchor, status}
    case S{+position, height, +client, count, +stride, +selected, allowed, required, anchor, status} Select{+index, +chosen}:
      +next = set(selected, index, chosen)
      +next_anchor = selected_anchor(anchor, index, chosen)
      changed = Bool.not(U32.is_eq(selected, next))
      S{selected_position(position, client, stride, next_anchor, changed), height, client, count, stride,
        next, allowed, required, next_anchor, status}
    case S{position, height, client, +count, stride, +selected, +allowed, +required, anchor, status} Submit{}:
      S{position, height, client, count, stride, selected, allowed, required, anchor,
        Bool.pick(U32, correct(count, selected, allowed, required), 1, 2)}

def guarded(expired: Bool, s: State, action: Action) -> State:
  match expired s:
    case True{} S{position, height, client, count, stride, selected, allowed, required, anchor, status}:
      S{position, height, client, count, stride, selected, allowed, required, anchor, 3}
    case False{} _: active(s, action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{position, height, client, count, stride, selected, allowed, required, anchor, 0}:
      guarded(U32.is_ge(elapsed, deadline),
        S{position, height, client, count, stride, selected, allowed, required, anchor, 0}, action)
    case _: s
