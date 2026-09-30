import Base
import ./Model.bend as M
import ./Generate.bend as G
import ./DrawWire.bend as DW

def outcome_index(status: Nat, raw: U32) -> M.Outcome:
  match status:
    case 0n: M.Running{}
    case 1n: M.Finished{U32.is_eq(raw, 1065353216)}
    case _: M.TimedOut{}

def outcome(status: U32, raw: U32) -> M.Outcome:
  outcome_index(U32.to_nat(status), raw)

def action_index(kind: Nat, target: U32) -> M.Action:
  match kind:
    case 1n: M.Click{(target - 1 : U32)}
    case _: M.Wait{}

def save_outcome(result: M.Outcome, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match result:
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
  M.S{goal, selected, result} = s
  a[(base + 2 : U32)] <- 0
  a[(base + 33 : U32)] <- selected
  save_outcome(result, elapsed, deadline, base, a)

def loaded(+base: U32, status: U32, raw: U32,
    +elapsed: U32, +deadline: U32, kind: U32, target: U32,
    goal: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, selected) = pair
  s = {M.S{goal, selected, outcome(status, raw)} : M.State}
  save(M.step(s, action_index(U32.to_nat(kind), target), elapsed, deadline),
    elapsed, deadline, base, a)

def load_selected(+base: U32, status: U32, raw: U32,
    elapsed: U32, deadline: U32, kind: U32, target: U32,
    goal: U32, a: Array<U32>) -> Array<U32>:
  loaded(base, status, raw, elapsed, deadline, kind, target,
    goal, a[(base + 33 : U32)])

def load_goal(+base: U32, status: U32, raw: U32,
    elapsed: U32, deadline: U32, kind: U32, target: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, goal) = pair
  load_selected(base, status, raw, elapsed, deadline,
    kind, target, goal, a)

def load_target(+base: U32, status: U32, raw: U32,
    elapsed: U32, deadline: U32, kind: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, target) = pair
  load_goal(base, status, raw, elapsed, deadline,
    kind, target, a[(base + 32 : U32)])

def load_kind(+base: U32, status: U32, raw: U32,
    elapsed: U32, deadline: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, kind) = pair
  load_target(base, status, raw, elapsed, deadline,
    kind, a[(base + 5 : U32)])

def load_deadline(+base: U32, status: U32, raw: U32,
    elapsed: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, deadline) = pair
  load_kind(base, status, raw, elapsed, deadline,
    a[(base + 4 : U32)])

def load_elapsed(+base: U32, status: U32, raw: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, elapsed) = pair
  load_deadline(base, status, raw, elapsed,
    a[(base + 12 : U32)])

def load_raw(+base: U32, status: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, raw) = pair
  load_elapsed(base, status, raw, a[(base + 8 : U32)])

def active_status(status: Nat, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match status:
    case 0n: load_raw(base, 0, a[(base + 10 : U32)])
    case _: a[(base + 2 : U32)] <- 0

def load_status(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  active_status(U32.to_nat(status), base, a)

def step(+base: U32, a: Array<U32>) -> Array<U32>:
  load_status(base, a[(base + 9 : U32)])

def reset_seed(+base: U32, task: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  G.generate(task, seed, base, a)

def task_index(which: Nat, task: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match which:
    case 3n: step(base, a)
    case _: DW.step(base, task, a)

def run_task(+base: U32, +task: U32,
    a: Array<U32>) -> Array<U32>:
  task_index(U32.to_nat(task), task, base, a)

def operation_index(op: Nat, +base: U32, task: U32,
    a: Array<U32>) -> Array<U32>:
  match op:
    case 0n: a
    case 1n: reset_seed(base, task, a[(base + 3 : U32)])
    case _: run_task(base, task, a)

def operation(op: U32, +base: U32, task: U32,
    a: Array<U32>) -> Array<U32>:
  operation_index(U32.to_nat(op), base, task, a)

def load_task(+base: U32, op: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, task) = pair
  operation(op, base, task, a)

def row(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, op) = pair
  load_task(base, op, a[(base + 1 : U32)])

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +base = (U32.from_nat(p) * 2048 : U32)
      batch(p, row(base, a[(base + 2 : U32)]))
