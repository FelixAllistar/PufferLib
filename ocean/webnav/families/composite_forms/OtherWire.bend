import Base
import ./Model.bend as M
import ./OtherModel.bend as O
import ../../miniwob/Text.bend as T
import ../../miniwob/training/TextGenerate.bend as TG

def prepend(pair: Array<U32> & U32, xs: +List<U32>) -> Array<U32> & +List<U32>:
  (a, x) = pair
  (a, x <> xs)

def read_units(n: Nat, +at: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, xs) = pair
      read_units(p, at, prepend(a[(at + U32.from_nat(p) : U32)], xs))

def field_value_loaded(goal: +List<U32>,
    pair: Array<U32> & +List<U32>) -> Array<U32> & T.Field:
  (a, value) = pair
  (a, T.F{value, 0n, 0n, goal})

def field_value(index: U32, +base: U32, goal: +List<U32>,
    pair: Array<U32> & U32) -> Array<U32> & T.Field:
  (a, length) = pair
  field_value_loaded(goal, read_units(U32.to_nat(length),
    (base + 512 + index * 128 : U32), (a, Nil{})))

def field_goal_loaded(+index: U32, +base: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32> & T.Field:
  (a, goal) = pair
  field_value(index, base, goal, a[(base + 40 + index : U32)])

def field_goal(+index: U32, +base: U32,
    pair: Array<U32> & U32) -> Array<U32> & T.Field:
  (a, length) = pair
  field_goal_loaded(index, base, read_units(U32.to_nat(length),
    (base + 1024 + index * 128 : U32), (a, Nil{})))

def load_field(+index: U32, +base: U32, a: Array<U32>) -> Array<U32> & T.Field:
  field_goal(index, base, a[(base + 44 + index : U32)])

def field_prepend(pair: Array<U32> & T.Field,
    rest: +List<T.Field>) -> Array<U32> & +List<T.Field>:
  (a, field) = pair
  (a, field <> rest)

def load_fields(n: Nat, +base: U32,
    pair: Array<U32> & +List<T.Field>) -> Array<U32> & +List<T.Field>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, fields) = pair
      load_fields(p, base, field_prepend(load_field(U32.from_nat(p), base, a), fields))

def clear(n: Nat, +at: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: clear(p, at, a[(at + U32.from_nat(p) : U32)] <- 0)

def save_field(field: T.Field, +index: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  T.F{+value, start, end_, goal} = field
  +at = (base + 512 + index * 128 : U32)
  a = clear(128n, at, a)
  a[(base + 40 + index : U32)] <- U32.from_nat(List.length(&2, U32, value))
  TG.write_units(value, at, a)

def save_fields(fields: +List<T.Field>, +index: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match fields:
    case Nil{}: a
    case Con{field, rest}:
      save_fields(rest, (index + 1 : U32), base, save_field(field, index, base, a))

def save_outcome(outcome: M.Outcome, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match outcome:
    case M.Running{}: a
    case M.Finished{+success}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(M.raw(M.Finished{success}))
      a[(base + 11 : U32)] <- F32.bits(M.timed(M.Finished{success}, elapsed, deadline))
    case M.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(s: O.State, elapsed: U32, deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  O.S{kind, selected, requested, secondary, fields, outcome} = s
  a = save_fields(fields, 0, base, a)
  a[(base + 32 : U32)] <- selected
  a[(base + 2 : U32)] <- 0
  save_outcome(outcome, elapsed, deadline, base, a)

def button_action(kind: Nat, +target: U32) -> O.Action:
  match kind:
    case 1n:
      Bool.pick(O.Action, U32.is_eq(target, 7), O.Submit{0}, O.Select{target})
    case 2n: O.Submit{(target - 1 : U32)}
    case _: O.Submit{0}

def text_index(kind: Nat, target: U32) -> Nat:
  match kind:
    case 1n: U32.to_nat((target - 4 : U32))
    case _: U32.to_nat((target - 1 : U32))

def decode(kind: Nat, command: U32, +target: U32, value: U32,
    units: +List<U32>) -> O.Action:
  match command:
    case 21: O.Select{value}
    case 1: button_action(kind, target)
    case 2: O.SetText{text_index(kind, target), units}
    case _: O.Wait{}

def outcome(status: U32, raw: U32) -> M.Outcome:
  match status:
    case 0: M.Running{}
    case 1: M.Finished{U32.is_eq(raw, 1065353216)}
    case _: M.TimedOut{}

def ready(+task: U32, command: U32, target: U32, value: U32,
    +elapsed: U32, status: U32, raw: U32, +deadline: U32,
    selected: U32, requested: U32, secondary: U32,
    fields: +List<T.Field>, units: +List<U32>, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +kind = U32.to_nat(task)
  s = {O.S{kind, selected, requested, secondary, fields, outcome(status, raw)} : O.State}
  save(O.step(s, decode(kind, command, target, value, units), elapsed, deadline),
    elapsed, deadline, base, a)

def payload_loaded(task: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    selected: U32, requested: U32, secondary: U32,
    fields: +List<T.Field>, +base: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, units) = pair
  ready(task, command, target, value, elapsed, status, raw, deadline,
    selected, requested, secondary, fields, units, base, a)

def fields_loaded(task: U32, command: U32, target: U32, +value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    selected: U32, requested: U32, secondary: U32, +base: U32,
    pair: Array<U32> & +List<T.Field>) -> Array<U32>:
  (a, fields) = pair
  payload_loaded(task, command, target, value, elapsed, status, raw, deadline,
    selected, requested, secondary, fields, base,
    read_units(U32.to_nat(value), (base + 2048 : U32), (a, Nil{})))

def scalars_ready(task: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    selected: U32, requested: U32, secondary: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  fields_loaded(task, command, target, value, elapsed, status, raw, deadline,
    selected, requested, secondary, base, load_fields(3n, base, (a, Nil{})))

def load_secondary(task: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    selected: U32, requested: U32, +base: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, secondary) = pair
  scalars_ready(task, command, target, value, elapsed, status, raw, deadline,
    selected, requested, secondary, base, a)

def load_requested(task: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    selected: U32, +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, requested) = pair
  load_secondary(task, command, target, value, elapsed, status, raw, deadline,
    selected, requested, base, a[(base + 35 : U32)])

def load_selected(task: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, selected) = pair
  load_requested(task, command, target, value, elapsed, status, raw, deadline,
    selected, base, a[(base + 34 : U32)])

def load_deadline(task: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32,
    +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, deadline) = pair
  load_selected(task, command, target, value, elapsed, status, raw, deadline,
    base, a[(base + 32 : U32)])

def load_raw(task: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, +base: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, raw) = pair
  load_deadline(task, command, target, value, elapsed, status, raw,
    base, a[(base + 12 : U32)])

def load_status(task: U32, command: U32, target: U32, value: U32,
    elapsed: U32, +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  load_raw(task, command, target, value, elapsed, status,
    base, a[(base + 10 : U32)])

def load_elapsed(task: U32, command: U32, target: U32, value: U32,
    +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, elapsed) = pair
  load_status(task, command, target, value, elapsed,
    base, a[(base + 9 : U32)])

def load_value(task: U32, command: U32, target: U32,
    +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, value) = pair
  load_elapsed(task, command, target, value, base, a[(base + 8 : U32)])

def load_target(task: U32, command: U32, +base: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, target) = pair
  load_value(task, command, target, base, a[(base + 6 : U32)])

def load_command(task: U32, +base: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, command) = pair
  load_target(task, command, base, a[(base + 5 : U32)])

def run(task: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  load_command(task, base, a[(base + 4 : U32)])
