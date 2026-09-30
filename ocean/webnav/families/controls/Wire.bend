import Base
import ./Model.bend as M
import ./Generate.bend as G
import ../../miniwob/Text.bend as T

def task(index: Nat) -> M.Task:
  match index:
    case 0n: M.ChooseList{}
    case 1n: M.Slider{}
    case 2n: M.SliderThree{}
    case 3n: M.Spinner{}
    case 4n: M.ColorName{}
    case _: M.ColorSwatch{}

def outcome(status: Nat) -> M.Outcome:
  match status:
    case 0n: M.Running{}
    case 1n: M.Finished{}
    case _: M.TimedOut{}

def prepend(pair: Array<U32> & U32, xs: +List<U32>) -> Array<U32> & +List<U32>:
  (a, ch) = pair
  (a, ch <> xs)

def read_units(n: Nat, +at: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, xs) = pair
      read_units(p, at, prepend(a[(at + U32.from_nat(p) : U32)], xs))

def write_units(xs: +List<U32>, +at: U32, a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{ch, rest}: write_units(rest, (at + 1 : U32), a[at] <- ch)

def clear_units(n: Nat, +at: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: clear_units(p, at, a[(at + U32.from_nat(p) : U32)] <- 0)

def empty_state() -> M.State:
  M.S{M.ChooseList{}, 0, 0, False{}, 0, 0, 0, 0, 0, 0,
    0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, 0, 0, 0, M.Running{}}

def loaded_color(task_code: U32, status: U32, count: U32, low: U32, high: U32,
    vertical: U32, one: U32, g1: U32, two: U32, g2: U32, three: U32, g3: U32,
    selected: U32, goal_option: U32, focus: U32, drag: U32, desired: U32,
    start: U32, end: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & M.State:
  (a, units) = pair
  (a, M.S{task(U32.to_nat(task_code)), low, high, U32.is_eq(vertical, 1),
    one, two, three, g1, g2, g3, selected, goal_option, count,
    T.F{units, U32.to_nat(start), U32.to_nat(end), Nil{}},
    desired, focus, drag, outcome(U32.to_nat(status))})

def loaded_meta(task_code: U32, status: U32, +base: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32> & M.State:
  (a, meta) = pair
  match meta:
    case Con{count, Con{low, Con{high, Con{vertical, Con{one, Con{g1, Con{two, Con{g2, Con{three, Con{g3, Con{selected, Con{goal_option, Con{focus, Con{drag, Con{desired, Con{start, Con{end, Con{length, Nil{}}}}}}}}}}}}}}}}}}}:
      loaded_color(task_code, status, count, low, high, vertical,
        one, g1, two, g2, three, g3, selected, goal_option, focus, drag,
        desired, start, end,
        read_units(U32.to_nat(length), (base + 768 : U32), (a, Nil{})))
    case _: (a[(base + 13 : U32)] <- 1, empty_state())

def loaded_status(task_code: U32, +base: U32,
    pair: Array<U32> & U32) -> Array<U32> & M.State:
  (a, status) = pair
  loaded_meta(task_code, status, base, read_units(18n, (base + 32 : U32), (a, Nil{})))

def loaded_task(+base: U32, pair: Array<U32> & U32) -> Array<U32> & M.State:
  (a, task_code) = pair
  loaded_status(task_code, base, a[(base + 9 : U32)])

def loaded(+base: U32, a: Array<U32>) -> Array<U32> & M.State:
  loaded_task(base, a[(base + 1 : U32)])

def status_saved(o: M.Outcome, +s: M.State, +elapsed: U32, +limit: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case M.Running{}:
      a = a[(base + 9 : U32)] <- 0
      a = a[(base + 10 : U32)] <- F32.bits(0.0)
      a[(base + 11 : U32)] <- F32.bits(0.0)
    case M.Finished{}:
      +raw = M.score(s)
      +scaled = Bool.pick(F32, F32.is_gt(raw, 0.0),
        (raw * F32.max(0.0, (1.0 - U32.to_f32(elapsed) / U32.to_f32(limit) : F32)) : F32), raw)
      a = a[(base + 9 : U32)] <- 1
      a = a[(base + 10 : U32)] <- F32.bits(raw)
      a[(base + 11 : U32)] <- F32.bits(scaled)
    case M.TimedOut{}:
      a = a[(base + 9 : U32)] <- 2
      a = a[(base + 10 : U32)] <- F32.bits((0.0 - 1.0 : F32))
      a[(base + 11 : U32)] <- F32.bits((0.0 - 1.0 : F32))

def save(+s: M.State, elapsed: U32, limit: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  M.S{kind, low, high, vertical, one, two, three, g1, g2, g3, selected, goal_option, count,
    T.F{+units, start, end, goal}, desired, focus, drag, result} = s
  a = a[(base + 2 : U32)] <- 0
  a = a[(base + 36 : U32)] <- one
  a = a[(base + 38 : U32)] <- two
  a = a[(base + 40 : U32)] <- three
  a = a[(base + 42 : U32)] <- selected
  a = a[(base + 44 : U32)] <- focus
  a = a[(base + 45 : U32)] <- drag
  a = a[(base + 47 : U32)] <- U32.from_nat(start)
  a = a[(base + 48 : U32)] <- U32.from_nat(end)
  a = a[(base + 49 : U32)] <- U32.from_nat(T.length(units))
  a = write_units(units, (base + 768 : U32), clear_units(64n, (base + 768 : U32), a))
  status_saved(result, s, elapsed, limit, base, a)

def color_action(kind: Nat, units: +List<U32>) -> M.Action:
  match kind:
    case 2n: M.EditColor{T.Insert{units}}
    case 3n: M.EditColor{T.Backspace{}}
    case 4n: M.EditColor{T.Delete{}}
    case 5n: M.EditColor{T.Left{}}
    case 6n: M.EditColor{T.Right{}}
    case 7n: M.EditColor{T.Home{}}
    case 8n: M.EditColor{T.End{}}
    case 9n: M.EditColor{T.SelectAll{}}
    case _: M.Wait{}

def decode_task(code: Nat, kind: Nat, +target: U32, +arg0: U32, arg1: U32, units: +List<U32>) -> M.Action:
  match code kind:
    case 0n 21n: M.Select{arg0}
    case 0n 1n: Bool.pick(M.Action, U32.is_eq(target, 2), M.Submit{}, M.Wait{})
    case 1n 1n: Bool.pick(M.Action, U32.is_eq(target, 2), M.Submit{}, M.SliderAt{target, arg0, False{}})
    case 1n 13n: M.SliderAt{target, arg0, True{}}
    case 1n 14n: M.Move{target, arg0}
    case 1n 15n: M.Release{}
    case 1n 5n: M.Arrow{target, False{}}
    case 1n 6n: M.Arrow{target, True{}}
    case 1n 7n: M.Edge{target, False{}}
    case 1n 8n: M.Edge{target, True{}}
    case 1n 16n: M.Key{target, arg0}
    case 2n 1n: Bool.pick(M.Action, U32.is_eq(target, 4), M.Submit{}, M.SliderAt{target, arg0, False{}})
    case 2n 13n: M.SliderAt{target, arg0, True{}}
    case 2n 14n: M.Move{target, arg0}
    case 2n 15n: M.Release{}
    case 2n 5n: M.Arrow{target, False{}}
    case 2n 6n: M.Arrow{target, True{}}
    case 2n 7n: M.Edge{target, False{}}
    case 2n 8n: M.Edge{target, True{}}
    case 2n 16n: M.Key{target, arg0}
    case 3n 1n:
      Bool.pick(M.Action, U32.is_eq(target, 3), M.Submit{}, M.Spin{U32.is_eq(target, 1)})
    case 4n 1n: Bool.pick(M.Action, U32.is_eq(target, 2), M.Submit{}, M.Focus{target})
    case 5n 1n: Bool.pick(M.Action, U32.is_eq(target, 2), M.Submit{}, M.Focus{target})
    case 4n 18n: M.ColorRange{U32.to_nat(arg0), U32.to_nat(arg1)}
    case 5n 18n: M.ColorRange{U32.to_nat(arg0), U32.to_nat(arg1)}
    case 4n k: color_action(k, units)
    case 5n k: color_action(k, units)
    case _ _: M.Wait{}

def decoded(task_code: U32, action: U32, target: U32, arg0: U32, arg1: U32, units: +List<U32>) -> M.Action:
  decode_task(U32.to_nat(task_code), U32.to_nat(action), target, arg0, arg1, units)

def execute_state(task_code: U32, action_code: U32, target: U32, arg0: U32,
    arg1: U32, units: +List<U32>, +elapsed: U32, +limit: U32,
    +base: U32, pair: Array<U32> & M.State) -> Array<U32>:
  (a, state) = pair
  save(M.step(state, decoded(task_code, action_code, target, arg0, arg1, units), elapsed, limit),
    elapsed, limit, base, a)

def execute_payload(task_code: U32, action_code: U32, target: U32, arg0: U32,
    arg1: U32, elapsed: U32, limit: U32, +base: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, units) = pair
  execute_state(task_code, action_code, target, arg0, arg1, units, elapsed, limit,
    base, loaded(base, a))

def execute_header(task_code: U32, +base: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, words) = pair
  match words:
    case Con{action_code, Con{target, Con{arg0, Con{arg1, Con{elapsed, Con{status, Con{raw, Con{timed, Con{limit, Con{error, Con{text_length, Nil{}}}}}}}}}}}}:
      execute_payload(task_code, action_code, target, arg0, arg1, elapsed, limit,
        base, read_units(U32.to_nat(text_length), (base + 800 : U32), (a, Nil{})))
    case _: a[(base + 13 : U32)] <- 1

def execute_task(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, task_code) = pair
  execute_header(task_code, base, read_units(11n, (base + 4 : U32), (a, Nil{})))

def execute(+base: U32, a: Array<U32>) -> Array<U32>:
  execute_task(base, a[(base + 1 : U32)])

def reset_seed(task_code: U32, +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  G.generate(task_code, seed, base, a)

def reset_task(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, task_code) = pair
  reset_seed(task_code, base, a[(base + 3 : U32)])

def reset(+base: U32, a: Array<U32>) -> Array<U32>:
  reset_task(base, a[(base + 1 : U32)])

def operation(op: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match op:
    case 0n: a
    case 1n: reset(base, a)
    case _: execute(base, a)

def row_op(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, op) = pair
  operation(U32.to_nat(op), base, a)

def row(+base: U32, a: Array<U32>) -> Array<U32>:
  row_op(base, a[(base + 2 : U32)])

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +base = (U32.from_nat(p) * 2048 : U32)
      batch(p, row(base, a))
