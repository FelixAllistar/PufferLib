import Base
import ./Model.bend as M
import ./Generate.bend as G
import ./OtherGenerate.bend as OG
import ./OtherWire.bend as OW

def outcome(status: U32, raw: U32) -> M.Outcome:
  match status:
    case 0: M.Running{}
    case 1: M.Finished{U32.is_eq(raw, 1065353216)}
    case _: M.TimedOut{}

def save_outcome(o: M.Outcome, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case M.Running{}: a
    case M.Finished{+success}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(M.raw(M.Finished{success}))
      a[(base + 11 : U32)] <- F32.bits(M.timed(M.Finished{success}, elapsed, deadline))
    case M.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(s: M.State, elapsed: U32, deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  M.S{slider, checked, goal_slider, goal_checkbox, o} = s
  a[(base + 32 : U32)] <- slider
  a[(base + 33 : U32)] <- checked
  a[(base + 2 : U32)] <- 0
  save_outcome(o, elapsed, deadline, base, a)

def action(kind: U32, +target: U32, value: U32) -> M.Action:
  match kind:
    case 21: M.SetSlider{value}
    case 1:
      Bool.pick(M.Action, U32.is_eq(target, 5), M.Submit{},
        M.Toggle{(target - 1 : U32)})
    case _: M.Wait{}

def ready(+base: U32, kind: U32, target: U32, value: U32, +elapsed: U32,
    status: U32, raw: U32, +deadline: U32, slider: U32, checked: U32,
    goal_slider: U32, goal_checkbox: U32, a: Array<U32>) -> Array<U32>:
  s = {M.S{slider, checked, goal_slider, goal_checkbox, outcome(status, raw)} : M.State}
  save(M.step(s, action(kind, target, value), elapsed, deadline), elapsed, deadline, base, a)

def load_goal_checkbox(+base: U32, kind: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32, slider: U32,
    checked: U32, goal_slider: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, goal_checkbox) = pair
  ready(base, kind, target, value, elapsed, status, raw, deadline,
    slider, checked, goal_slider, goal_checkbox, a)

def load_goal_slider(+base: U32, kind: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32, slider: U32,
    checked: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, goal_slider) = pair
  load_goal_checkbox(base, kind, target, value, elapsed, status, raw, deadline,
    slider, checked, goal_slider, a[(base + 35 : U32)])

def load_checked(+base: U32, kind: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32, slider: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, checked) = pair
  load_goal_slider(base, kind, target, value, elapsed, status, raw, deadline,
    slider, checked, a[(base + 34 : U32)])

def load_slider(+base: U32, kind: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, slider) = pair
  load_checked(base, kind, target, value, elapsed, status, raw, deadline,
    slider, a[(base + 33 : U32)])

def load_deadline(+base: U32, kind: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, deadline) = pair
  load_slider(base, kind, target, value, elapsed, status, raw, deadline,
    a[(base + 32 : U32)])

def load_raw(+base: U32, kind: U32, target: U32, value: U32,
    elapsed: U32, status: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, raw) = pair
  load_deadline(base, kind, target, value, elapsed, status, raw,
    a[(base + 12 : U32)])

def load_status(+base: U32, kind: U32, target: U32, value: U32,
    elapsed: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  load_raw(base, kind, target, value, elapsed, status, a[(base + 10 : U32)])

def load_elapsed(+base: U32, kind: U32, target: U32, value: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, elapsed) = pair
  load_status(base, kind, target, value, elapsed, a[(base + 9 : U32)])

def load_value(+base: U32, kind: U32, target: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, value) = pair
  load_elapsed(base, kind, target, value, a[(base + 8 : U32)])

def load_target(+base: U32, kind: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, target) = pair
  load_value(base, kind, target, a[(base + 6 : U32)])

def load_kind(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, kind) = pair
  load_target(base, kind, a[(base + 5 : U32)])

def reset_choice(kind: Nat, task: U32, seed: U32,
    base: U32, a: Array<U32>) -> Array<U32>:
  match kind:
    case 0n: G.generate(task, seed, base, a)
    case _: OG.generate(task, seed, base, a)

def reset_seed(+task: U32, +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  reset_choice(U32.to_nat(task), task, seed, base, a)

def reset_task(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, task) = pair
  reset_seed(task, base, a[(base + 3 : U32)])

def step_choice(kind: Nat, task: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match kind:
    case 0n: load_kind(base, a[(base + 4 : U32)])
    case _: OW.run(task, base, a)

def step_task(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, +task) = pair
  step_choice(U32.to_nat(task), task, base, a)

def operation(op: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match op:
    case 0: a
    case 1: reset_task(base, a[(base + 1 : U32)])
    case _: step_task(base, a[(base + 1 : U32)])

def row(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, op) = pair
  operation(op, base, a)

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +base = (U32.from_nat(p) * 8192 : U32)
      batch(p, row(base, a[(base + 2 : U32)]))
