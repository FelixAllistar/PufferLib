import Base
import ./Model.bend as M
import ./Generate.bend as G
import ./OtherWire.bend as O

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
    case M.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(s: M.State, elapsed: U32, deadline: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  M.S{position, height, client, offset, bottom, o} = s
  a[(base + 32 : U32)] <- position
  a[(base + 2 : U32)] <- 0
  save_outcome(o, elapsed, deadline, base, a)

def decoded(kind: U32, target: U32, arg0: U32) -> M.Action:
  match kind:
    case 12: M.Scroll{arg0}
    case 1: Bool.pick(M.Action, U32.is_eq(target, 2), M.Submit{}, M.Wait{})
    case _: M.Wait{}

def ready(+base: U32, kind: U32, target: U32, arg0: U32,
          +elapsed: U32, +deadline: U32, position: U32, height: U32,
          client: U32, offset: U32, bottom: U32, a: Array<U32>) -> Array<U32>:
  state = {M.S{position, height, client, offset, U32.is_eq(bottom, 1), M.Running{}} : M.State}
  save(M.step(state, decoded(kind, target, arg0), elapsed, deadline), elapsed, deadline, base, a)

def load_bottom(+base: U32, kind: U32, target: U32, arg0: U32,
                elapsed: U32, deadline: U32, position: U32, height: U32,
                client: U32, offset: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, bottom) = pair
  ready(base, kind, target, arg0, elapsed, deadline, position, height, client, offset, bottom, a)

def load_offset(+base: U32, kind: U32, target: U32, arg0: U32,
                elapsed: U32, deadline: U32, position: U32, height: U32,
                client: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, offset) = pair
  load_bottom(base, kind, target, arg0, elapsed, deadline, position, height,
    client, offset, a[(base + 35 : U32)])

def load_client(+base: U32, kind: U32, target: U32, arg0: U32,
                elapsed: U32, deadline: U32, position: U32, height: U32,
                pair: Array<U32> & U32) -> Array<U32>:
  (a, client) = pair
  load_offset(base, kind, target, arg0, elapsed, deadline, position, height, client, a[(base + 47 : U32)])

def load_height(+base: U32, kind: U32, target: U32, arg0: U32,
                elapsed: U32, deadline: U32, position: U32,
                pair: Array<U32> & U32) -> Array<U32>:
  (a, height) = pair
  load_client(base, kind, target, arg0, elapsed, deadline, position, height, a[(base + 34 : U32)])

def load_position(+base: U32, kind: U32, target: U32, arg0: U32,
                  elapsed: U32, deadline: U32,
                  pair: Array<U32> & U32) -> Array<U32>:
  (a, position) = pair
  load_height(base, kind, target, arg0, elapsed, deadline, position, a[(base + 33 : U32)])

def load_deadline(+base: U32, kind: U32, target: U32, arg0: U32,
                  elapsed: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, deadline) = pair
  load_position(base, kind, target, arg0, elapsed, deadline, a[(base + 32 : U32)])

def load_elapsed(+base: U32, kind: U32, target: U32, arg0: U32,
                 pair: Array<U32> & U32) -> Array<U32>:
  (a, elapsed) = pair
  load_deadline(base, kind, target, arg0, elapsed, a[(base + 12 : U32)])

def load_arg0(+base: U32, kind: U32, target: U32,
              pair: Array<U32> & U32) -> Array<U32>:
  (a, arg0) = pair
  load_elapsed(base, kind, target, arg0, a[(base + 8 : U32)])

def load_target(+base: U32, kind: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, target) = pair
  load_arg0(base, kind, target, a[(base + 6 : U32)])

def load_kind(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, kind) = pair
  load_target(base, kind, a[(base + 5 : U32)])

def step_task(task: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match task:
    case 0: O.run(0, base, a)
    case 1: O.run(1, base, a)
    case 2: load_kind(base, a[(base + 4 : U32)])
    case _: O.run(3, base, a)

def step_loaded(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, task) = pair
  step_task(task, base, a)

def step_running(status: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match status:
    case 0: step_loaded(base, a[(base + 1 : U32)])
    case _: a[(base + 2 : U32)] <- 0

def step_status(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  step_running(status, base, a)

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
