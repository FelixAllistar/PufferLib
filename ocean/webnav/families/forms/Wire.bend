import Base
import ./Model.bend as M
import ./Generate.bend as G
import ../../miniwob/Text.bend as T
import ../../miniwob/Widgets.bend as W

# Convert the ABI command into a closed event type before task transitions.
type Event is Data:
  Noop{}
  Click{}
  Insert{}
  Backspace{}
  Delete{}
  Left{}
  Right{}
  Home{}
  End{}
  SelectAll{}
  Enter{}
  TabKey{}
  Scroll{}
  PointerDown{}
  PointerMove{}
  PointerUp{}
  KeyDown{}
  KeyUp{}
  SelectRange{}
  Copy{}
  Paste{}

type EventEnvelope is Data:
  Envelope{event: Event}

def next_event(event: Event) -> Event:
  match event:
    case Noop{}: Click{}
    case Click{}: Insert{}
    case Insert{}: Backspace{}
    case Backspace{}: Delete{}
    case Delete{}: Left{}
    case Left{}: Right{}
    case Right{}: Home{}
    case Home{}: End{}
    case End{}: SelectAll{}
    case SelectAll{}: Enter{}
    case Enter{}: TabKey{}
    case TabKey{}: Scroll{}
    case Scroll{}: PointerDown{}
    case PointerDown{}: PointerMove{}
    case PointerMove{}: PointerUp{}
    case PointerUp{}: KeyDown{}
    case KeyDown{}: KeyUp{}
    case KeyUp{}: SelectRange{}
    case SelectRange{}: Copy{}
    case Copy{}: Paste{}
    case Paste{}: Noop{}

def event_loop(n: Nat, +event: Event) -> Event:
  match n:
    case 0n: event
    case 1n+p: event_loop(p, next_event(event))

def event(code: U32) -> Event:
  event_loop(U32.to_nat(code), Noop{})

def decode_click(field_target: Bool, target: U32, static_action: U32) -> M.Action:
  match field_target:
    case True{}: M.Focus{Nat.sub(U32.to_nat(target), 1n)}
    case False{}: M.Press{M.button_from_code(static_action)}

def decode(kind: Event, target: U32, arg0: U32, arg1: U32, payload: +List<U32>) -> M.Action:
  match kind:
    case Noop{}: M.Wait{}
    case Click{}: M.Wait{}
    case Insert{}: M.Editing{T.Insert{payload}}
    case Backspace{}: M.Editing{T.Backspace{}}
    case Delete{}: M.Editing{T.Delete{}}
    case Left{}: M.Editing{T.Left{}}
    case Right{}: M.Editing{T.Right{}}
    case Home{}: M.Editing{T.Home{}}
    case End{}: M.Editing{T.End{}}
    case SelectAll{}: M.Editing{T.SelectAll{}}
    case Enter{}: M.Wait{}
    case TabKey{}: M.Tab{}
    case SelectRange{}: M.SelectRange{U32.to_nat(arg0), U32.to_nat(arg1)}
    case Copy{}: M.Copy{}
    case Paste{}: M.Paste{}
    case _: M.Wait{}

def prepend(pair: Array<U32> & U32, xs: +List<U32>) -> Array<U32> & +List<U32>:
  (a, x) = pair
  (a, x <> xs)

def read_units(n: Nat, +at: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, xs) = pair
      read_units(p, at, prepend(a[(at + U32.from_nat(p) : U32)], xs))

def zero(n: Nat, +at: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      a[(at + U32.from_nat(p) : U32)] <- 0
      zero(p, at, a)

def write_units(xs: +List<U32>, +at: U32, a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{x, rest}:
      a[at] <- x
      write_units(rest, (at + 1 : U32), a)

def save_fields(fs: +List<T.Field>, +base: U32, +index: U32, a: Array<U32>) -> Array<U32>:
  match fs:
    case Nil{}: a
    case Con{T.F{+xs, start, end, goal}, rest}:
      +at = (base + 128 + index * 544 : U32)
      a = a[(at + 1 : U32)] <- U32.from_nat(T.length(xs))
      a = a[(at + 2 : U32)] <- U32.from_nat(start)
      a = a[(at + 3 : U32)] <- U32.from_nat(end)
      +value_at = (at + 8 : U32)
      save_fields(rest, base, (index + 1 : U32), write_units(xs, value_at, zero(256n, value_at, a)))

def save_clipboard(+clipboard: +List<U32>, +base: U32, a: Array<U32>) -> Array<U32>:
  a = a[(base + 37 : U32)] <- U32.from_nat(List.length(&2, U32, clipboard))
  write_units(clipboard, (base + 5120 : U32), zero(256n, (base + 5120 : U32), a))

def outcome_of(status: U32, raw: U32) -> W.Outcome:
  match status:
    case 0: W.Running{}
    case 1: W.binary(U32.is_eq(raw, 1065353216))
    case _: W.TimedOut{}

def save_result(previous: U32, +result: W.Outcome, +elapsed: U32, +limit: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match previous:
    case 0:
      match result:
        case W.Running{}: a
        case W.Finished{matches, total, scaled}:
          a = a[(base + 9 : U32)] <- 1
          a = a[(base + 10 : U32)] <- F32.bits(M.reward_raw(result))
          a[(base + 11 : U32)] <- F32.bits(M.reward_at(result, elapsed, limit))
        case W.TimedOut{}:
          a = a[(base + 9 : U32)] <- 2
          a = a[(base + 10 : U32)] <- F32.bits((0.0 - 1.0 : F32))
          a[(base + 11 : U32)] <- F32.bits((0.0 - 1.0 : F32))
    case _: a

def save(s: M.State, previous: U32, elapsed: U32, limit: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  M.S{kind, +fields, focus, +clipboard, popup, mode, seen, +result} = s
  a = a[(base + 33 : U32)] <- U32.from_nat(focus)
  a = a[(base + 34 : U32)] <- Bool.to_u32(popup)
  a = a[(base + 36 : U32)] <- Bool.to_u32(seen)
  a = save_fields(fields, base, 0, a)
  a = save_clipboard(clipboard, base, a)
  save_result(previous, result, elapsed, limit, base, a)

def field_with_goal(fields: +List<T.Field>, value: +List<U32>, start: U32, end: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32> & +List<T.Field>:
  (a, goal) = pair
  (a, T.F{value, U32.to_nat(start), U32.to_nat(end), goal} <> fields)

def field_with_value(fields: +List<T.Field>, +base: U32, +index: U32,
    start: U32, end: U32, goal_size: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32> & +List<T.Field>:
  (a, value) = pair
  goal_pair = read_units(U32.to_nat(goal_size), (base + 128 + index * 544 + 264 : U32), (a, Nil{}))
  field_with_goal(fields, value, start, end, goal_pair)

def field_meta3(fields: +List<T.Field>, +base: U32, +index: U32,
    size: U32, start: U32, end: U32, pair: Array<U32> & U32) -> Array<U32> & +List<T.Field>:
  (a, goal_size) = pair
  value_pair = read_units(U32.to_nat(size), (base + 128 + index * 544 + 8 : U32), (a, Nil{}))
  field_with_value(fields, base, index, start, end, goal_size, value_pair)

def field_meta2(fields: +List<T.Field>, +base: U32, +index: U32,
    size: U32, start: U32, pair: Array<U32> & U32) -> Array<U32> & +List<T.Field>:
  (a, end) = pair
  field_meta3(fields, base, index, size, start, end,
    a[(base + 128 + index * 544 + 4 : U32)])

def field_meta1(fields: +List<T.Field>, +base: U32, +index: U32,
    size: U32, pair: Array<U32> & U32) -> Array<U32> & +List<T.Field>:
  (a, start) = pair
  field_meta2(fields, base, index, size, start,
    a[(base + 128 + index * 544 + 3 : U32)])

def field_meta0(fields: +List<T.Field>, +base: U32, +index: U32,
    pair: Array<U32> & U32) -> Array<U32> & +List<T.Field>:
  (a, size) = pair
  field_meta1(fields, base, index, size,
    a[(base + 128 + index * 544 + 2 : U32)])

def load_fields(n: Nat, +base: U32, pair: Array<U32> & +List<T.Field>) -> Array<U32> & +List<T.Field>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, fields) = pair
      +index = U32.from_nat(p)
      load_fields(p, base,
        field_meta0(fields, base, index, a[(base + 128 + index * 544 + 1 : U32)]))

def state6_clipboard(kind: M.TaskKind, +base: U32, fields: +List<T.Field>, focus: U32, popup: U32, mode: U32, seen: U32,
    status: U32, raw: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & M.State:
  (a, clipboard) = pair
  (a, M.S{kind, fields, U32.to_nat(focus), clipboard, U32.is_eq(popup, 1),
      U32.to_nat(mode), U32.is_eq(seen, 1), outcome_of(status, raw)})

def state6(kind: M.TaskKind, +base: U32, fields: +List<T.Field>, focus: U32, popup: U32, mode: U32, seen: U32,
    status: U32, raw: U32, pair: Array<U32> & U32) -> Array<U32> & M.State:
  (a, size) = pair
  clipboard_pair = read_units(U32.to_nat(size), (base + 5120 : U32), (a, Nil{}))
  state6_clipboard(kind, base, fields, focus, popup, mode, seen, status, raw, clipboard_pair)

def state5(kind: M.TaskKind, +base: U32, fields: +List<T.Field>, focus: U32, popup: U32, mode: U32, seen: U32, status: U32,
    pair: Array<U32> & U32) -> Array<U32> & M.State:
  (a, raw) = pair
  state6(kind, base, fields, focus, popup, mode, seen, status, raw, a[(base + 37 : U32)])

def state4(kind: M.TaskKind, +base: U32, fields: +List<T.Field>, focus: U32, popup: U32, mode: U32, seen: U32,
    pair: Array<U32> & U32) -> Array<U32> & M.State:
  (a, status) = pair
  state5(kind, base, fields, focus, popup, mode, seen, status, a[(base + 10 : U32)])

def state3(kind: M.TaskKind, +base: U32, fields: +List<T.Field>, focus: U32, popup: U32, mode: U32,
    pair: Array<U32> & U32) -> Array<U32> & M.State:
  (a, seen) = pair
  state4(kind, base, fields, focus, popup, mode, seen, a[(base + 9 : U32)])

def state2(kind: M.TaskKind, +base: U32, fields: +List<T.Field>, focus: U32, popup: U32,
    pair: Array<U32> & U32) -> Array<U32> & M.State:
  (a, mode) = pair
  state3(kind, base, fields, focus, popup, mode, a[(base + 36 : U32)])

def state1(kind: M.TaskKind, +base: U32, fields: +List<T.Field>, focus: U32,
    pair: Array<U32> & U32) -> Array<U32> & M.State:
  (a, popup) = pair
  state2(kind, base, fields, focus, popup, a[(base + 35 : U32)])

def state0(kind: M.TaskKind, +base: U32, fields: +List<T.Field>,
    pair: Array<U32> & U32) -> Array<U32> & M.State:
  (a, focus) = pair
  state1(kind, base, fields, focus, a[(base + 34 : U32)])

def state_fields(kind: M.TaskKind, +base: U32,
    pair: Array<U32> & +List<T.Field>) -> Array<U32> & M.State:
  (a, fields) = pair
  state0(kind, base, fields, a[(base + 33 : U32)])

def static_code(pair: Array<U32> & U32) -> Array<U32> & U32:
  (a, code) = pair
  (a, code)

def static_action(+base: U32, field_count: U32, target: U32,
    pair: Array<U32> & U32) -> Array<U32> & U32:
  (a, _) = pair
  +index = U32.from_nat(Nat.sub(U32.to_nat(target), Nat.add(U32.to_nat(field_count), 1n)))
  static_code(a[(base + 2400 + index * 128 + 1 : U32)])

def press_static(+base: U32, pair: Array<U32> & U32) -> Array<U32> & M.Action:
  (a, code) = pair
  (a, M.Press{M.button_from_code(code)})

def field_count(fields: +List<T.Field>) -> U32:
  U32.from_nat(List.length(&2, T.Field, fields))

def click_action(field_target: Bool, target: U32, field_count: U32,
    +base: U32, a: Array<U32>) -> Array<U32> & M.Action:
  match field_target:
    case True{}: (a, M.Focus{Nat.sub(U32.to_nat(target), 1n)})
    case False{}:
      press_static(base, static_action(base, field_count, target, a[(base + 0 : U32)]))

def event_action(+event_kind: Event, +target: U32, arg0: U32, arg1: U32,
    +field_count: U32, +base: U32, a: Array<U32>, payload: +List<U32>) -> Array<U32> & M.Action:
  match event_kind:
    case Click{}:
      click_action(U32.is_gt(target, 0) && U32.is_le(target, field_count),
        target, field_count, base, a)
    case _:
      (a, decode(event_kind, target, arg0, arg1, payload))

def envelope_action(envelope: EventEnvelope, target: U32, arg0: U32, arg1: U32,
    field_count: U32, +base: U32, a: Array<U32>, payload: +List<U32>) -> Array<U32> & M.Action:
  match envelope:
    case Envelope{event_kind}:
      event_action(event_kind, target, arg0, arg1, field_count, base, a, payload)

def apply_action_pair(pair: Array<U32> & M.Action, s: M.State, previous: U32,
    +elapsed: U32, +limit: U32, +base: U32) -> Array<U32>:
  (a, command) = pair
  save(M.step(command, s, elapsed, limit), previous, elapsed, limit, base, a)

def payload_insert(+base: U32, pair: Array<U32> & U32) -> Array<U32> & +List<U32>:
  (a, size) = pair
  read_units(U32.to_nat(size), (base + 5376 : U32), (a, Nil{}))

def payload_empty(+base: U32, pair: Array<U32> & U32) -> Array<U32> & +List<U32>:
  (a, size) = pair
  read_units(0n, (base + 5376 : U32), (a, Nil{}))

def payload_units(insert: Bool, +base: U32, pair: Array<U32> & U32) -> Array<U32> & +List<U32>:
  match insert:
    case True{}: payload_insert(base, pair)
    case False{}: payload_empty(base, pair)

def execute_payload(+s: M.State, code: U32, +target: U32, arg0: U32, arg1: U32,
    previous: U32, +elapsed: U32, +limit: U32, +base: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32>:
  M.S{kind, fields, focus, clipboard, popup, mode, seen, outcome} = s
  (a, payload) = pair
  command_pair = envelope_action(Envelope{event(code)}, target, arg0, arg1,
    field_count(fields), base, a, payload)
  apply_action_pair(command_pair, s, previous, elapsed, limit, base)

def execute(+s: M.State, +code: U32, target: U32, arg0: U32, arg1: U32,
    previous: U32, +elapsed: U32, +limit: U32, +base: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  execute_payload(s, code, target, arg0, arg1, previous, elapsed, limit, base,
    payload_units(U32.is_eq(code, 2), base, pair))

def ready(s: M.State, previous: U32, elapsed: U32, limit: U32, code: U32,
    target: U32, arg0: U32, arg1: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  execute(s, code, target, arg0, arg1, previous, elapsed, limit, base, a[(base + 6 : U32)])

def args1(s: M.State, previous: U32, elapsed: U32, limit: U32, code: U32,
    target: U32, arg0: U32, +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, arg1) = pair
  ready(s, previous, elapsed, limit, code, target, arg0, arg1, base, a)

def args0(s: M.State, previous: U32, elapsed: U32, limit: U32, code: U32,
    target: U32, +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, arg0) = pair
  args1(s, previous, elapsed, limit, code, target, arg0, base, a[(base + 7 : U32)])

def args_target(s: M.State, previous: U32, elapsed: U32, limit: U32, code: U32,
    +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, target) = pair
  args0(s, previous, elapsed, limit, code, target, base, a[(base + 6 : U32)])

def args_code(s: M.State, previous: U32, elapsed: U32, limit: U32,
    +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, code) = pair
  args_target(s, previous, elapsed, limit, code, base, a[(base + 5 : U32)])

def args_elapsed(s: M.State, previous: U32, limit: U32, +base: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, elapsed) = pair
  args_code(s, previous, elapsed, limit, base, a[(base + 4 : U32)])

def args_limit(s: M.State, previous: U32, +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, limit) = pair
  args_elapsed(s, previous, limit, base, a[(base + 8 : U32)])

def args_status(s: M.State, +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, previous) = pair
  args_limit(s, previous, base, a[(base + 12 : U32)])

def run_ready(s: M.State, +base: U32, a: Array<U32>) -> Array<U32>:
  args_status(s, base, a[(base + 9 : U32)])

def run_state(+base: U32, pair: Array<U32> & M.State) -> Array<U32>:
  (a, s) = pair
  run_ready(s, base, a)

def run_loaded(kind: M.TaskKind, +base: U32,
    pair: Array<U32> & +List<T.Field>) -> Array<U32>:
  (a, fields) = pair
  state_pair = state_fields(kind, base, (a, fields))
  run_state(base, state_pair)

def load_kind_count(kind: M.TaskKind, +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, count) = pair
  run_loaded(kind, base, load_fields(U32.to_nat(count), base, (a, Nil{})))

def run_kind(+base: U32, pair: Array<U32> & M.TaskKind) -> Array<U32>:
  (a, kind) = pair
  load_kind_count(kind, base, a[(base + 32 : U32)])

def task_kind_pair(pair: Array<U32> & U32) -> Array<U32> & M.TaskKind:
  (a, task) = pair
  (a, M.task_kind(task))

def run_step(+base: U32, a: Array<U32>) -> Array<U32>:
  run_kind(base, task_kind_pair(a[(base + 1 : U32)]))

def reset_seed(task: U32, +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  G.generate(task, seed, base, a)

def reset_task(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, task) = pair
  reset_seed(task, base, a[(base + 3 : U32)])

def operation(op: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match op:
    case 0: a
    case 1: reset_task(base, a[(base + 1 : U32)])
    case _: run_step(base, a)

def row(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, op) = pair
  operation(op, base, a)

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +base = (U32.from_nat(p) * 8192 : U32)
      batch(p, row(base, a[(base + 2 : U32)]))
