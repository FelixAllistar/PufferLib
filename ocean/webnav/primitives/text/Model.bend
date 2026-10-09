import Base

# Reusable editing state. Goals, rewards, focus and enabled state belong to
# the task/page binding. Units and capacity are chosen by the transport preset.
type Field is Data:
  F{value: +List<U32>, start: Nat, end: Nat}

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

def value(f: Field) -> +List<U32>:
  F{xs, start, end} = f
  xs

def collapse(f: Field, +at: Nat) -> Field:
  F{xs, start, end} = f
  F{xs, at, at}

def replace(f: Field, +inserted: +List<U32>) -> Field:
  F{+xs, +start, end} = f
  +cursor = Nat.add(start, length(inserted))
  F{List.append(&2, U32, List.take(&2, U32, xs, start),
    List.append(&2, U32, inserted, List.drop(&2, U32, xs, end))), cursor, cursor}

def range(f: Field, start: Nat, end: Nat) -> Field:
  F{xs, old_start, old_end} = f
  F{xs, start, end}

def backspace(+f: Field) -> Field:
  F{xs, +start, +end} = f
  replace(range(f, Bool.pick(Nat, Nat.is_eq(start, end), pred(start), start), end), Nil{})

def delete(+f: Field) -> Field:
  F{+xs, +start, +end} = f
  replace(range(f, start, Bool.pick(Nat, Nat.is_eq(start, end), Nat.min(length(xs), 1n+end), end)), Nil{})

def left(+f: Field) -> Field:
  F{xs, +start, end} = f
  collapse(f, Bool.pick(Nat, Nat.is_eq(start, end), pred(start), start))

def right(+f: Field) -> Field:
  F{xs, start, +end} = f
  collapse(f, Bool.pick(Nat, Nat.is_eq(start, end), Nat.min(length(xs), 1n+end), end))

def end(+f: Field) -> Field:
  F{xs, start, end_} = f
  collapse(f, length(xs))

def select_all(+f: Field) -> Field:
  F{xs, start, end_} = f
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
