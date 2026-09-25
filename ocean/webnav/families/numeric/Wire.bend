import Base
import ../../miniwob/Widgets.bend as W
import ../../miniwob/Text.bend as T
import ./Model.bend as M
import ./Generate.bend as G

def task_index(k: Nat) -> M.Task:
  match k:
    case 0n: M.Ascending{}
    case 1n: M.Greatest{}
    case 2n: M.GenerateNumber{}
    case 3n: M.GuessNumber{}
    case 4n: M.HotCold{}
    case 5n: M.NumberCheckboxes{}
    case 6n: M.OddOrEven{}
    case 7n: M.SimpleAlgebra{}
    case _: M.SimpleArithmetic{}

def task(k: U32) -> M.Task:
  task_index(U32.to_nat(k))

def role_index(k: Nat) -> W.Role:
  match k:
    case 0n: W.Button{}
    case 1n: W.Checkbox{}
    case 2n: W.Textbox{}
    case 3n: W.PointerTarget{}
    case _: W.Other{}

def role(k: U32) -> W.Role:
  role_index(U32.to_nat(k))

def load_target(kind: U32, selected: U32, rest: +List<W.Node>, pair: Array<U32> & U32) -> Array<U32> & +List<W.Node>:
  (a, target) = pair
  (a, W.N{role(kind), U32.is_eq(selected, 1), U32.is_eq(target, 1)} <> rest)

def load_selected(kind: U32, at: U32, rest: +List<W.Node>, pair: Array<U32> & U32) -> Array<U32> & +List<W.Node>:
  (a, selected) = pair
  target_pair = a[(at + 2 : U32)]
  load_target(kind, selected, rest, target_pair)

def load_kind(+at: U32, rest: +List<W.Node>, pair: Array<U32> & U32) -> Array<U32> & +List<W.Node>:
  (a, kind) = pair
  selected_pair = a[(at + 1 : U32)]
  load_selected(kind, at, rest, selected_pair)

def load_nodes(n: Nat, +base: U32, pair: Array<U32> & +List<W.Node>) -> Array<U32> & +List<W.Node>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, rest) = pair
      +at = (base + 64 + U32.from_nat(p) * 48 : U32)
      load_nodes(p, base, load_kind(at, rest, a[at]))

def save_nodes(xs: +List<W.Node>, +index: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{n, rest}:
      +at = (base + 64 + index * 48 : U32)
      save_nodes(rest, (index + 1 : U32), base, a[(at + 1 : U32)] <- Bool.to_u32(W.checked(n)))

def meta_append(pair: Array<U32> & U32, values: +List<U32>) -> Array<U32> & +List<U32>:
  (a, value) = pair
  (a, value <> values)

def read_meta(n: Nat, +base: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, values) = pair
      read_meta(p, base, meta_append(a[(base + U32.from_nat(p) : U32)], values))

def save_meta(xs: +List<U32>, +index: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{value, rest}:
      save_meta(rest, (index + 1 : U32), base, a[(base + index : U32)] <- value)

def input_at(k: U32) -> U32:
  0

def unit_append(pair: Array<U32> & U32, units: +List<U32>) -> Array<U32> & +List<U32>:
  (a, unit) = pair
  (a, unit <> units)

def load_units(n: Nat, +at: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, units) = pair
      load_units(p, at, unit_append(a[(at + U32.from_nat(p) : U32)], units))

def take_until_zero(xs: +List<Nat>) -> +List<Nat>:
  match xs:
    case Nil{}: Nil{}
    case Con{head, rest}:
      match head:
        case 0n: Nil{}
        case _: head <> take_until_zero(rest)

def units_from_nats(xs: +List<Nat>) -> +List<U32>:
  match xs:
    case Nil{}: Nil{}
    case Con{head, rest}: U32.from_nat(head) <> units_from_nats(rest)

def field_value(start: U32, end: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & T.Field:
  (a, all_units) = pair
  +trimmed = take_until_zero(M.nat_units(all_units))
  +value = units_from_nats(trimmed)
  (a, T.F{value, U32.to_nat(start), U32.to_nat(end), Nil{}})

def field_end(start: U32, +at: U32, pair: Array<U32> & U32) -> Array<U32> & T.Field:
  (a, end) = pair
  field_value(start, end, load_units(24n, at, (a, Nil{})))

def field_start(+base: U32, +at: U32, pair: Array<U32> & U32) -> Array<U32> & T.Field:
  (a, start) = pair
  field_end(start, at, a[(base + 43 : U32)])

def load_field(task_id: U32, +base: U32, a: Array<U32>) -> Array<U32> & T.Field:
  +node = input_at(task_id)
  +at = (base + 64 + node * 48 + 20 : U32)
  field_start(base, at, a[(base + 42 : U32)])

def load_action_text(n: Nat, +base: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  load_units(n, (base + 1856 : U32), pair)

def edit(action: Nat, units: +List<U32>) -> M.Action:
  match action:
    case 2n: M.Edit{T.Insert{units}}
    case 3n: M.Edit{T.Backspace{}}
    case 4n: M.Edit{T.Delete{}}
    case 5n: M.Edit{T.Left{}}
    case 6n: M.Edit{T.Right{}}
    case 7n: M.Edit{T.Home{}}
    case 8n: M.Edit{T.End{}}
    case 9n: M.Edit{T.SelectAll{}}
    case _: M.Wait{}

def decode_code(action: Nat, target: U32, arg0: U32, arg1: U32, +text: +List<U32>) -> M.Action:
  match action:
    case 0n: M.Wait{}
    case 1n: M.Click{(target - 1 : U32), arg0, arg1}
    case 2n: edit(action, text)
    case 3n: edit(action, text)
    case 4n: edit(action, text)
    case 5n: edit(action, text)
    case 6n: edit(action, text)
    case 7n: edit(action, text)
    case 8n: edit(action, text)
    case 9n: edit(action, text)
    case 10n: M.Submit{}
    case 14n: M.PointerMove{arg0, arg1}
    case _: M.Wait{}

def decode(+action: U32, target: U32, arg0: U32, arg1: U32, +text: +List<U32>) -> M.Action:
  decode_code(U32.to_nat(action), target, arg0, arg1, text)

def result(o: M.Outcome, +elapsed: U32, +deadline: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case M.Running{}: a
    case M.Finished{+raw, +scaled}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(M.raw(M.Finished{raw, scaled}))
      a[(base + 11 : U32)] <- F32.bits(M.timed(M.Finished{raw, scaled}, elapsed, deadline))
    case M.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def task_id(task: M.Task) -> U32:
  match task:
    case M.Ascending{}: 0
    case M.Greatest{}: 1
    case M.GenerateNumber{}: 2
    case M.GuessNumber{}: 3
    case M.HotCold{}: 4
    case M.NumberCheckboxes{}: 5
    case M.OddOrEven{}: 6
    case M.SimpleAlgebra{}: 7
    case M.SimpleArithmetic{}: 8

def save_field_chars(xs: +List<U32>, +at: U32, a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a[at] <- 0
    case Con{ch, rest}:
      save_field_chars(rest, (at + 1 : U32), a[at] <- ch)

def save_field(xs: +List<U32>, node: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +at = (base + 64 + node * 48 + 20 : U32)
  save_field_chars(xs, at, a)

def save_state(s: M.State, +elapsed: U32, +deadline: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  M.S{task_, phase, nodes, cursor, selected, secret, limit, mode,
      expected, feedback, candidate, rng, T.F{value, start, end, goal}, outcome} = s
  a = a[(base + 2 : U32)] <- 0
  a = a[(base + 32 : U32)] <- phase
  a = a[(base + 34 : U32)] <- cursor
  a = a[(base + 35 : U32)] <- selected
  a = a[(base + 36 : U32)] <- secret
  a = a[(base + 37 : U32)] <- limit
  a = a[(base + 38 : U32)] <- mode
  a = a[(base + 39 : U32)] <- expected
  a = a[(base + 40 : U32)] <- feedback
  a = a[(base + 41 : U32)] <- candidate
  a = a[(base + 44 : U32)] <- rng
  a = a[(base + 42 : U32)] <- U32.from_nat(start)
  a = a[(base + 43 : U32)] <- U32.from_nat(end)
  a = save_nodes(nodes, 0, base, a)
  a = save_field(value, input_at(task_id(task_)), base, a)
  result(outcome, elapsed, deadline, base, a)

def build_state(xs: +List<U32>, t: M.Task, nodes: +List<W.Node>, field: T.Field) -> M.State:
  match xs:
    case Con{phase, Con{count, Con{cursor, Con{selected, Con{secret, Con{limit, Con{mode, Con{expected, Con{feedback, Con{candidate, Con{input, Con{submit, Con{rng, Nil{}}}}}}}}}}}}}}:
      M.make(t, phase, nodes, cursor, selected, secret, limit, mode, expected, feedback, candidate, rng, field, M.Running{})
    case _: M.make(t, 0, nodes, 0, 0, 0, 0, 0, 0, 0, 0, 0, field, M.Running{})

def step_field_loaded(+base: U32, task_id_: U32, +elapsed: U32, +deadline: U32,
                      values: +List<U32>, nodes: +List<W.Node>, action: U32,
                      target: U32, arg0: U32, arg1: U32, text: +List<U32>,
                      pair: Array<U32> & T.Field) -> Array<U32>:
  (a, field) = pair
  +s = build_state(values, task(task_id_), nodes, field)
  +next = M.step(s, decode(action, target, arg0, arg1, text), elapsed, deadline)
  save_state(next, elapsed, deadline, base, a)

def step_text_loaded(+base: U32, +task_id_: U32, elapsed: U32, deadline: U32,
                     values: +List<U32>, nodes: +List<W.Node>, action: U32,
                     target: U32, arg0: U32, arg1: U32,
                     pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, text) = pair
  step_field_loaded(base, task_id_, elapsed, deadline, values, nodes,
                    action, target, arg0, arg1, text, load_field(task_id_, base, a))

def step_text_length(+base: U32, task_id_: U32, elapsed: U32, deadline: U32,
                     values: +List<U32>, nodes: +List<W.Node>, action: U32,
                     target: U32, arg0: U32, arg1: U32,
                     pair: Array<U32> & U32) -> Array<U32>:
  (a, text_length) = pair
  step_text_loaded(base, task_id_, elapsed, deadline, values, nodes, action,
                   target, arg0, arg1,
                   load_action_text(U32.to_nat(text_length), base, (a, Nil{})))

def step_arg1(+base: U32, task_id_: U32, elapsed: U32, deadline: U32,
              values: +List<U32>, nodes: +List<W.Node>, action: U32,
              target: U32, arg0: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, arg1) = pair
  step_text_length(base, task_id_, elapsed, deadline, values, nodes, action,
                   target, arg0, arg1, a[(base + 14 : U32)])

def step_arg0(+base: U32, task_id_: U32, elapsed: U32, deadline: U32,
              values: +List<U32>, nodes: +List<W.Node>, action: U32,
              target: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, arg0) = pair
  step_arg1(base, task_id_, elapsed, deadline, values, nodes, action,
            target, arg0, a[(base + 7 : U32)])

def step_target(+base: U32, task_id_: U32, elapsed: U32, deadline: U32,
                values: +List<U32>, nodes: +List<W.Node>, action: U32,
                pair: Array<U32> & U32) -> Array<U32>:
  (a, target) = pair
  step_arg0(base, task_id_, elapsed, deadline, values, nodes, action,
            target, a[(base + 6 : U32)])

def step_action(+base: U32, task_id_: U32, elapsed: U32, deadline: U32,
                values: +List<U32>, nodes: +List<W.Node>, pair: Array<U32> & U32) -> Array<U32>:
  (a, action) = pair
  step_target(base, task_id_, elapsed, deadline, values, nodes, action,
              a[(base + 5 : U32)])

def step_values(+base: U32, task_id_: U32, elapsed: U32, deadline: U32,
                nodes: +List<W.Node>, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, values) = pair
  step_action(base, task_id_, elapsed, deadline, values, nodes, a[(base + 4 : U32)])

def step_nodes(+base: U32, task_id_: U32, elapsed: U32, deadline: U32,
               pair: Array<U32> & +List<W.Node>) -> Array<U32>:
  (a, nodes) = pair
  step_values(base, task_id_, elapsed, deadline, nodes,
              read_meta(13n, (base + 32 : U32), (a, Nil{})))

def step_count(+base: U32, task_id_: U32, elapsed: U32, deadline: U32,
               pair: Array<U32> & U32) -> Array<U32>:
  (a, count) = pair
  step_nodes(base, task_id_, elapsed, deadline,
             load_nodes(U32.to_nat(count), base, (a, Nil{})))

def step_deadline(+base: U32, task_id_: U32, elapsed: U32,
                  pair: Array<U32> & U32) -> Array<U32>:
  (a, deadline) = pair
  step_count(base, task_id_, elapsed, deadline, a[(base + 33 : U32)])

def step_elapsed(+base: U32, task_id_: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, elapsed) = pair
  step_deadline(base, task_id_, elapsed, a[(base + 12 : U32)])

def step_task_id(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, task_id_) = pair
  step_elapsed(base, task_id_, a[(base + 8 : U32)])

def step_state(+base: U32, a: Array<U32>) -> Array<U32>:
  step_task_id(base, a[(base + 1 : U32)])

def generate_seed(+base: U32, task_id_: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  G.generate(task_id_, seed, base, a)

def generate_task(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, task_id_) = pair
  generate_seed(base, task_id_, a[(base + 3 : U32)])

def operation_code(op: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match op:
    case 0n: a
    case 1n: generate_task(base, a[(base + 1 : U32)])
    case _:
      step_state(base, a)

def operation(op: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  operation_code(U32.to_nat(op), base, a)

def row(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, op) = pair
  operation(op, base, a)

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +base = (U32.from_nat(p) * 4096 : U32)
      batch(p, row(base, a[(base + 2 : U32)]))
