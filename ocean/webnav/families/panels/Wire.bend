import Base
import ./Model.bend as M
import ./Generate.bend as G

def outcome(status: U32, raw: U32) -> M.Outcome:
  match status:
    case 0: M.Running{}
    case 1: M.Finished{U32.is_eq(raw, 1065353216)}
    case _: M.TimedOut{}

def save_outcome(o: M.Outcome, +elapsed: U32, +deadline: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case M.Running{}: a
    case M.Finished{+success}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(M.raw(M.Finished{success}))
      a[(base + 11 : U32)] <- F32.bits(M.timed(M.Finished{success}, elapsed, deadline))
      a[(base + 39 : U32)] <- elapsed
    case M.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864
      a[(base + 39 : U32)] <- elapsed

def save(s: M.State, elapsed: U32, deadline: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  M.S{active, panels, accordion, target, medium, o} = s
  a[(base + 33 : U32)] <- active
  a[(base + 2 : U32)] <- 0
  save_outcome(o, elapsed, deadline, base, a)

def decoded(kind: U32, panel: U32, correct: U32) -> M.Action:
  match kind:
    case 0: M.Header{panel}
    case 1: M.Link{panel, U32.is_eq(correct, 1)}
    case 2: M.Submit{}
    case _: M.Wait{}

def node_goal(s: M.State, kind: U32, panel: U32, +elapsed: U32, +deadline: U32, +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, correct) = pair
  save(M.step(s, decoded(kind, panel, correct), elapsed, deadline), elapsed, deadline, base, a)

def node_panel(s: M.State, kind: U32, +at: U32, elapsed: U32, deadline: U32, base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, panel) = pair
  node_goal(s, kind, panel, elapsed, deadline, base, a[(at + 2 : U32)])

def node_kind(s: M.State, +at: U32, elapsed: U32, deadline: U32, base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, kind) = pair
  node_panel(s, kind, at, elapsed, deadline, base, a[(at + 1 : U32)])

def dispatch(valid: Bool, s: M.State, target: U32, +elapsed: U32, +deadline: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match valid:
    case False{}: save(M.step(s, M.Wait{}, elapsed, deadline), elapsed, deadline, base, a)
    case True{}:
      +at = (base + 64 + (target - 1) * 48 : U32)
      node_kind(s, at, elapsed, deadline, base, a[at])

def ready(+base: U32, action: U32, +target_ref: U32, +elapsed: U32, status: U32, raw: U32, +deadline: U32, panels: U32, active: U32, accordion: U32, target_tab: U32, medium: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, count) = pair
  state = {M.S{active, panels, U32.is_eq(accordion, 1), target_tab, medium, outcome(status, raw)} : M.State}
  dispatch(U32.is_eq(action, 1) && U32.is_gt(target_ref, 0) && U32.is_le(target_ref, count), state, target_ref, elapsed, deadline, base, a)

def load10(+base: U32, action: U32, target_ref: U32, elapsed: U32, status: U32, raw: U32, deadline: U32, panels: U32, active: U32, accordion: U32, target_tab: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, medium) = pair
  ready(base, action, target_ref, elapsed, status, raw, deadline, panels, active, accordion, target_tab, medium, a[(base + 38 : U32)])

def load9(+base: U32, action: U32, target_ref: U32, elapsed: U32, status: U32, raw: U32, deadline: U32, panels: U32, active: U32, accordion: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, target_tab) = pair
  load10(base, action, target_ref, elapsed, status, raw, deadline, panels, active, accordion, target_tab, a[(base + 36 : U32)])

def load8(+base: U32, action: U32, target_ref: U32, elapsed: U32, status: U32, raw: U32, deadline: U32, panels: U32, active: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, accordion) = pair
  load9(base, action, target_ref, elapsed, status, raw, deadline, panels, active, accordion, a[(base + 35 : U32)])

def load7(+base: U32, action: U32, target_ref: U32, elapsed: U32, status: U32, raw: U32, deadline: U32, panels: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, active) = pair
  load8(base, action, target_ref, elapsed, status, raw, deadline, panels, active, a[(base + 34 : U32)])

def load6(+base: U32, action: U32, target_ref: U32, elapsed: U32, status: U32, raw: U32, deadline: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, panels) = pair
  load7(base, action, target_ref, elapsed, status, raw, deadline, panels, a[(base + 33 : U32)])

def load5(+base: U32, action: U32, target_ref: U32, elapsed: U32, status: U32, raw: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, deadline) = pair
  load6(base, action, target_ref, elapsed, status, raw, deadline, a[(base + 32 : U32)])

def load4(+base: U32, action: U32, target_ref: U32, elapsed: U32, status: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, raw) = pair
  load5(base, action, target_ref, elapsed, status, raw, a[(base + 12 : U32)])

def load3(+base: U32, action: U32, target_ref: U32, elapsed: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  load4(base, action, target_ref, elapsed, status, a[(base + 10 : U32)])

def load2(+base: U32, action: U32, target_ref: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, elapsed) = pair
  load3(base, action, target_ref, elapsed, a[(base + 9 : U32)])

def load1(+base: U32, action: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, target_ref) = pair
  load2(base, action, target_ref, a[(base + 8 : U32)])

def load0(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, action) = pair
  load1(base, action, a[(base + 5 : U32)])

def step_running(status: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match status:
    case 0: load0(base, a[(base + 4 : U32)])
    case _: a[(base + 2 : U32)] <- 0

def step_status(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  step_running(status, base, a)

def reset_seed(task: U32, base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  G.generate(task, seed, base, a)

def reset_task(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, task) = pair
  reset_seed(task, base, a[(base + 3 : U32)])

def operation(op: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match op:
    case 0: a
    case 1: reset_task(base, a[(base + 1 : U32)])
    case _: step_status(base, a[(base + 9 : U32)])

def row(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, op) = pair
  operation(op, base, a)

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +base = (U32.from_nat(p) * 8192 : U32)
      batch(p, row(base, a[(base + 2 : U32)]))
