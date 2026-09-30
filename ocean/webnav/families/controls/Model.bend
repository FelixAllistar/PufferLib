import Base
import ../../miniwob/Text.bend as T

# Integer controls use a +100 encoding in the wire row. RGB is 0xRRGGBB.
type Task is Data:
  ChooseList{}
  Slider{}
  SliderThree{}
  Spinner{}
  ColorName{}
  ColorSwatch{}

type Outcome is Data:
  Running{}
  Finished{}
  TimedOut{}

type State is Data:
  S{task: Task, low: U32, high: U32, vertical: Bool,
    one: U32, two: U32, three: U32,
    goal_one: U32, goal_two: U32, goal_three: U32,
    selected: U32, goal_option: U32, options: U32,
    color: T.Field, desired: U32, focus: U32, drag: U32,
    outcome: Outcome}

type Action is Data:
  Wait{}
  Submit{}
  Select{index: U32}
  Spin{increase: Bool}
  SliderAt{ref: U32, fraction: U32, begin: Bool}
  Move{ref: U32, fraction: U32}
  Release{}
  Focus{ref: U32}
  Arrow{ref: U32, increase: Bool}
  Edge{ref: U32, high: Bool}
  Key{ref: U32, code: U32}
  EditColor{command: T.Edit}
  ColorRange{start: Nat, end: Nat}

def clamp(+x: U32, +low: U32, +high: U32) -> U32:
  U32.min(high, U32.max(low, x))

def slider_allowed(task: Task, +ref: U32) -> Bool:
  match task:
    case Slider{}: U32.is_eq(ref, 1)
    case SliderThree{}: U32.is_ge(ref, 1) && U32.is_le(ref, 3)
    case _: False{}

def is_spinner(task: Task) -> Bool:
  match task:
    case Spinner{}: True{}
    case _: False{}

def slider_value(+low: U32, high: U32, vertical: Bool, +fraction: U32) -> U32:
  +f = U32.min(fraction, 1000)
  +pos = Bool.pick(U32, vertical, (1000 - f : U32), f)
  (low + ((high - low) * pos + 500) / 1000 : U32)

def set_slot_state(+s: State, ref: Nat, value: U32) -> State:
  match s ref:
    case S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result} 1n:
      S{task, low, high, vertical, value, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result}
    case S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result} 2n:
      S{task, low, high, vertical, one, value, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result}
    case S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result} 3n:
      S{task, low, high, vertical, one, two, value, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result}
    case _ _: s

def set_slot(ref: Nat, value: U32, s: State) -> State:
  set_slot_state(s, ref, value)

def slot_state(s: State, ref: Nat) -> U32:
  match s ref:
    case S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result} 1n: one
    case S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result} 2n: two
    case S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result} 3n: three
    case _ _: 0

def slot(ref: Nat, s: State) -> U32:
  slot_state(s, ref)

def color_digit(+ch: U32) -> U32:
  Bool.pick(U32, U32.is_ge(ch, 48) && U32.is_le(ch, 57), (ch - 48 : U32),
    Bool.pick(U32, U32.is_ge(ch, 65) && U32.is_le(ch, 70), (ch - 55 : U32),
      Bool.pick(U32, U32.is_ge(ch, 97) && U32.is_le(ch, 102), (ch - 87 : U32), 256)))

def color_parse(xs: +List<U32>, acc: U32, +n: Nat) -> U32:
  match xs:
    case Nil{}: Bool.pick(U32, Nat.is_eq(n, 6n), acc, 16777216)
    case Con{x, rest}:
      +digit = color_digit(x)
      Bool.pick(U32, U32.is_lt(digit, 16) && Nat.is_lt(n, 6n),
        color_parse(rest, (acc * 16 + digit : U32), 1n+n), 16777216)

def channel(color: U32, shift: Nat) -> U32:
  ((color >> shift) .&. 255 : U32)

def distance(+a: U32, +b: U32) -> U32:
  (U32.max(a, b) - U32.min(a, b) : U32)

def decrement(+old: U32, +low: U32) -> U32:
  Bool.pick(U32, U32.is_le(old, low), low, (old - 1 : U32))

def color_distance(+selected: U32, +desired: U32) -> U32:
  (distance(channel(selected, 16n), channel(desired, 16n)) +
    distance(channel(selected, 8n), channel(desired, 8n)) +
    distance(channel(selected, 0n), channel(desired, 0n)) : U32)

def color_reward(selected: U32, desired: U32) -> F32:
  (1.0 - U32.to_f32(color_distance(selected, desired)) / 765.0 : F32)

def typed_color_reward(+selected: U32, desired: U32) -> F32:
  Bool.pick(F32, U32.is_le(selected, 16777215), color_reward(selected, desired), (0.0 - 1.0 : F32))

def score(s: State) -> F32:
  match s:
    case S{ChooseList{}, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result}:
      Bool.pick(F32, U32.is_eq(sel, goal), 1.0, (0.0 - 1.0 : F32))
    case S{Slider{}, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result}:
      Bool.pick(F32, U32.is_eq(one, g1), 1.0, (0.0 - 1.0 : F32))
    case S{SliderThree{}, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result}:
      Bool.pick(F32, U32.is_eq(one, g1) && U32.is_eq(two, g2) && U32.is_eq(three, g3), 1.0, (0.0 - 1.0 : F32))
    case S{Spinner{}, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result}:
      Bool.pick(F32, U32.is_eq(one, g1), 1.0, (0.0 - 1.0 : F32))
    case S{ColorName{}, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result}:
      typed_color_reward(color_parse(T.value(color), 0, 0n), desired)
    case S{ColorSwatch{}, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result}:
      typed_color_reward(color_parse(T.value(color), 0, 0n), desired)

def submitted(+s: State) -> State:
  S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result} = s
  S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, 0, Finished{}}

def timed_out(s: State) -> State:
  S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result} = s
  S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, 0, TimedOut{}}

def selected(s: State, index: U32) -> State:
  S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result} = s
  S{task, low, high, vertical, one, two, three, g1, g2, g3, index, goal, count, color, desired, focus, drag, result}

def focus_on(s: State, ref: U32) -> State:
  S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result} = s
  S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, ref, drag, result}

def dragging(s: State, ref: U32) -> State:
  S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result} = s
  S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, ref, result}

def editing(s: State, edit: T.Edit) -> State:
  S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result} = s
  S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, T.edit(edit, color), desired, focus, drag, result}

def ranging(s: State, start: Nat, end: Nat) -> State:
  S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result} = s
  S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, T.range(color, start, end), desired, focus, drag, result}

def bounds(s: State) -> U32 & U32 & Bool:
  S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result} = s
  (low, high, vertical)

def moved_bounds(pair: U32 & U32 & Bool, s: State, ref: U32, fraction: U32) -> State:
  (low, high, vertical) = pair
  set_slot(U32.to_nat(ref), slider_value(low, high, vertical, fraction), s)

def moved(+s: State, ref: U32, fraction: U32) -> State:
  moved_bounds(bounds(s), s, ref, fraction)

def stepped_bounds(pair: U32 & U32 & Bool, +s: State, +ref: U32, increase: Bool) -> State:
  (low, high, vertical) = pair
  +old = slot(U32.to_nat(ref), s)
  +new = Bool.pick(U32, increase, U32.min(high, (old + 1 : U32)), decrement(old, low))
  set_slot(U32.to_nat(ref), new, s)

def stepped(+s: State, ref: U32, increase: Bool) -> State:
  stepped_bounds(bounds(s), s, ref, increase)

def spin_step(s: State, increase: Bool) -> State:
  S{task, low, high, vertical, +one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result} = s
  S{task, low, high, vertical,
    Bool.pick(U32, increase, (one + 1 : U32), (one - 1 : U32)),
    two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result}

def edged_bounds(pair: U32 & U32 & Bool, s: State, ref: U32, upper: Bool) -> State:
  (low, high, vertical) = pair
  set_slot(U32.to_nat(ref), Bool.pick(U32, upper, high, low), s)

def edged(+s: State, ref: U32, upper: Bool) -> State:
  edged_bounds(bounds(s), s, ref, upper)

def key_step(s: State, ref: U32, code: Nat) -> State:
  match code:
    case 35n: edged(s, ref, True{})
    case 36n: edged(s, ref, False{})
    case 37n: stepped(s, ref, False{})
    case 38n: stepped(s, ref, True{})
    case 39n: stepped(s, ref, True{})
    case 40n: stepped(s, ref, False{})
    case _: s

def task_of(s: State) -> Task:
  S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result} = s
  task

def option_count(s: State) -> U32:
  S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result} = s
  count

def drag_ref(s: State) -> U32:
  S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, result} = s
  drag

def valid_selection(s: State, index: U32) -> Bool:
  U32.is_lt(index, option_count(s))

def action(+s: State, a: Action) -> State:
  match a:
    case Wait{}: s
    case Submit{}: submitted(s)
    case Select{+index}: Bool.pick(State, valid_selection(s, index), selected(s, index), s)
    case Spin{increase}: Bool.pick(State, is_spinner(task_of(s)), spin_step(s, increase), s)
    case SliderAt{+ref, fraction, begin}:
      +v = Bool.pick(State, slider_allowed(task_of(s), ref), moved(s, ref, fraction), s)
      +focused = focus_on(v, ref)
      Bool.pick(State, begin && slider_allowed(task_of(v), ref), dragging(focused, ref), focused)
    case Move{+ref, fraction}: Bool.pick(State, U32.is_eq(drag_ref(s), ref) && slider_allowed(task_of(s), ref), moved(s, ref, fraction), s)
    case Release{}: dragging(s, 0)
    case Focus{ref}: focus_on(s, ref)
    case Arrow{+ref, increase}: Bool.pick(State, slider_allowed(task_of(s), ref), stepped(s, ref, increase), s)
    case Edge{+ref, high}: Bool.pick(State, slider_allowed(task_of(s), ref), edged(s, ref, high), s)
    case Key{+ref, code}: Bool.pick(State, slider_allowed(task_of(s), ref), key_step(s, ref, U32.to_nat(code)), s)
    case EditColor{command}: editing(s, command)
    case ColorRange{start, end}: ranging(s, start, end)

def step(+s: State, a: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, Running{}}:
      +current = {S{task, low, high, vertical, one, two, three, g1, g2, g3, sel, goal, count, color, desired, focus, drag, Running{}} : State}
      Bool.pick(State, U32.is_ge(elapsed, deadline), timed_out(current), action(current, a))
    case _: s
