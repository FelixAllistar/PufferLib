import Base
import ./Widgets.bend as W

# Text units are explicit. The current browser/ABI preset accepts ASCII;
# Unicode grapheme navigation and IME require a separate contract.
type Field is Data:
  F{value: +List<U32>, start: Nat, end: Nat, goal: +List<U32>}

type Edit is Data:
  Insert{units: +List<U32>}
  Backspace{}
  Delete{}
  Left{}
  Right{}
  Home{}
  End{}
  SelectAll{}

def length(xs: +List<U32>) -> Nat:
  List.length(&2, U32, xs)

def pred(n: Nat) -> Nat:
  match n:
    case 0n: 0n
    case 1n+p: p

def equal(a: +List<U32>, b: +List<U32>) -> Bool:
  match a b:
    case Nil{} Nil{}: True{}
    case Con{x, xs} Con{y, ys}: U32.is_eq(x, y) && equal(xs, ys)
    case _ _: False{}

def goal(f: Field) -> +List<U32>:
  F{value, start, end, target} = f
  target

def value(f: Field) -> +List<U32>:
  F{xs, start, end, target} = f
  xs

def collapse(f: Field, +at: Nat) -> Field:
  F{xs, start, end, target} = f
  F{xs, at, at, target}

def replace(f: Field, +inserted: +List<U32>) -> Field:
  F{+xs, +start, end, target} = f
  +cursor = Nat.add(start, length(inserted))
  F{List.append(&2, U32, List.take(&2, U32, xs, start), List.append(&2, U32, inserted, List.drop(&2, U32, xs, end))), cursor, cursor, target}

def range(f: Field, start: Nat, end: Nat) -> Field:
  F{xs, old_start, old_end, target} = f
  F{xs, start, end, target}

def backspace(+f: Field) -> Field:
  F{xs, +start, +end, target} = f
  replace(range(f, Bool.pick(Nat, Nat.is_eq(start, end), pred(start), start), end), Nil{})

def delete(+f: Field) -> Field:
  F{+xs, +start, +end, target} = f
  replace(range(f, start, Bool.pick(Nat, Nat.is_eq(start, end), Nat.min(length(xs), 1n+end), end)), Nil{})

def left(+f: Field) -> Field:
  F{xs, +start, end, target} = f
  collapse(f, Bool.pick(Nat, Nat.is_eq(start, end), pred(start), start))

def right(+f: Field) -> Field:
  F{xs, start, +end, target} = f
  collapse(f, Bool.pick(Nat, Nat.is_eq(start, end), Nat.min(length(xs), 1n+end), end))

def end(+f: Field) -> Field:
  F{xs, start, end_, target} = f
  collapse(f, length(xs))

def select_all(+f: Field) -> Field:
  F{xs, start, end_, target} = f
  range(f, 0n, length(xs))

def edit(action: Edit, f: Field) -> Field:
  match action:
    case Insert{units}: replace(f, units)
    case Backspace{}: backspace(f)
    case Delete{}: delete(f)
    case Left{}: left(f)
    case Right{}: right(f)
    case Home{}: collapse(f, 0n)
    case End{}: end(f)
    case SelectAll{}: select_all(f)

def modify(fs: +List<Field>, index: Nat, action: Edit) -> +List<Field>:
  match fs index:
    case Nil{} _: Nil{}
    case Con{f, rest} 0n: edit(action, f) <> rest
    case Con{f, rest} 1n+p: f <> modify(rest, p, action)

def targets(fs: +List<Field>) -> +List<+List<U32>>:
  match fs:
    case Nil{}: Nil{}
    case Con{f, rest}: goal(f) <> targets(rest)

def correct(fs: +List<Field>) -> Bool:
  match fs:
    case Nil{}: True{}
    case Con{F{xs, start, end_, target}, rest}: equal(xs, target) && correct(rest)

type State is Data:
  S{fields: +List<Field>, focus: Nat, outcome: W.Outcome}

type Action is Data:
  Wait{}
  FocusEnd{index: Nat}
  Editing{edit: Edit}
  Submit{}
  Timeout{}

def focus_if(valid: Bool, fs: +List<Field>, focus: Nat, +index: Nat) -> State:
  match valid:
    case False{}: S{fs, focus, W.Running{}}
    case True{}: S{modify(fs, index, End{}), 1n+index, W.Running{}}

def edit_focused(focus: Nat, fs: +List<Field>, action: Edit) -> State:
  match focus:
    case 0n: S{fs, 0n, W.Running{}}
    case 1n+ +index: S{modify(fs, index, action), 1n+index, W.Running{}}

def active(action: Action, +fs: +List<Field>, focus: Nat) -> State:
  match action:
    case Wait{}: S{fs, focus, W.Running{}}
    case FocusEnd{+index}: focus_if(Nat.is_lt(index, List.length(&2, Field, fs)), fs, focus, index)
    case Editing{command}: edit_focused(focus, fs, command)
    case Submit{}: S{fs, 1n+List.length(&2, Field, fs), W.binary(correct(fs))}
    case Timeout{}: S{fs, focus, W.TimedOut{}}

def step(s: State, action: Action) -> State:
  match s:
    case S{fs, focus, W.Running{}}: active(action, fs, focus)
    case S{fs, focus, outcome}: S{fs, focus, outcome}

def deadline(expired: Bool, +s: State, action: Action) -> State:
  match expired:
    case True{}: step(s, Timeout{})
    case False{}: step(s, action)

def step_at(s: State, action: Action, elapsed: U32) -> State:
  deadline(U32.is_ge(elapsed, 10000), s, action)
