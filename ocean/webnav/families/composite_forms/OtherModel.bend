import Base
import ./Model.bend as M
import ../../miniwob/Text.bend as T

# Kinds 1..4 have different widgets, but all have at most three text fields.
# Task 1's requested radio is retained for the query; its source checker only
# tests whether some radio is selected.
type State is Data:
  S{kind: Nat, selected: U32, requested: U32, secondary: U32,
    fields: +List<T.Field>, outcome: M.Outcome}

type Action is Data:
  Wait{}
  Select{choice: U32}
  SetText{index: Nat, units: +List<U32>}
  Submit{button: U32}

def set_field(fields: +List<T.Field>, index: Nat, units: +List<U32>) -> +List<T.Field>:
  match fields index:
    case Nil{} _: Nil{}
    case Con{T.F{value, start, end_, goal}, rest} 0n:
      T.F{units, 0n, 0n, goal} <> rest
    case Con{field, rest} 1n+p:
      field <> set_field(rest, p, units)

def lower(+unit: U32) -> U32:
  Bool.pick(U32, U32.is_ge(unit, 65) && U32.is_le(unit, 90),
    (unit + 32 : U32), unit)

def lower_all(xs: +List<U32>) -> +List<U32>:
  match xs:
    case Nil{}: Nil{}
    case Con{x, rest}: lower(x) <> lower_all(rest)

def drop_spaces(xs: +List<U32>) -> +List<U32>:
  match xs:
    case Nil{}: Nil{}
    case Con{x, rest}:
      match x:
        case 32: drop_spaces(rest)
        case 9: drop_spaces(rest)
        case _: x <> rest

def reverse(xs: +List<U32>, acc: +List<U32>) -> +List<U32>:
  match xs:
    case Nil{}: acc
    case Con{x, rest}: reverse(rest, x <> acc)

def trim_lower(xs: +List<U32>) -> +List<U32>:
  reverse(drop_spaces(reverse(drop_spaces(lower_all(xs)), Nil{})), Nil{})

def movie_field_ok(field: T.Field, exact: Bool) -> Bool:
  T.F{value, start, end_, goal} = field
  match exact:
    case True{}: T.equal(value, goal)
    case False{}: T.equal(trim_lower(value), trim_lower(goal))

def movie_correct(fields: +List<T.Field>, +index: Nat) -> Bool:
  match fields:
    case Nil{}: True{}
    case Con{field, rest}:
      movie_field_ok(field, Nat.is_eq(index, 2n)) &&
        movie_correct(rest, 1n+index)

def success_for(kind: Nat, +selected: U32, requested: U32,
    +secondary: U32, +fields: +List<T.Field>, +button: U32) -> Bool:
  match kind:
    case 1n: U32.is_gt(selected, 0) && T.correct(fields)
    case 2n: U32.is_eq(selected, requested) && U32.is_eq(button, secondary)
    case _: movie_correct(fields, 0n)

def submit(s: State, button: U32) -> State:
  S{+kind, +selected, +requested, +secondary, +fields, outcome} = s
  +ok = success_for(kind, selected, requested, secondary, fields, button)
  S{kind, selected, requested, secondary, fields, M.Finished{ok}}

def select(valid: Bool, s: State, choice: U32) -> State:
  match valid s:
    case False{} _: s
    case True{} S{kind, selected, requested, secondary, fields, outcome}:
      S{kind, choice, requested, secondary, fields, outcome}

def set_text(s: State, index: Nat, units: +List<U32>) -> State:
  S{kind, selected, requested, secondary, fields, outcome} = s
  S{kind, selected, requested, secondary, set_field(fields, index, units), outcome}

def active(action: Action, +s: State) -> State:
  match action s:
    case Wait{} _: s
    case Select{+choice} S{+kind, selected, requested, secondary, fields, outcome}:
      select(U32.is_le(choice, Bool.pick(U32, Nat.is_eq(kind, 2n), 6, 3)), s, choice)
    case SetText{index, units} _: set_text(s, index, units)
    case Submit{button} _: submit(s, button)

def guard(expired: Bool, s: State, action: Action) -> State:
  match expired s:
    case True{} S{kind, selected, requested, secondary, fields, outcome}:
      S{kind, selected, requested, secondary, fields, M.TimedOut{}}
    case False{} _: active(action, s)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{kind, selected, requested, secondary, fields, M.Running{}}:
      guard(U32.is_ge(elapsed, deadline),
        S{kind, selected, requested, secondary, fields, M.Running{}}, action)
    case S{kind, selected, requested, secondary, fields, M.Finished{success}}:
      S{kind, selected, requested, secondary, fields, M.Finished{success}}
    case S{kind, selected, requested, secondary, fields, M.TimedOut{}}:
      S{kind, selected, requested, secondary, fields, M.TimedOut{}}
