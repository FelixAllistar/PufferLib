import Base
import ./Model.bend as M
import ./Generate.bend as G
import ./OtherGenerate.bend as OG
import ./OtherWire.bend as O

def prepend(pair: Array<U32> & U32, xs: +List<U32>) -> Array<U32> & +List<U32>:
  (a, value) = pair
  (a, value <> xs)

def read_units(n: Nat, +at: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, xs) = pair
      read_units(p, at, prepend(a[(at + U32.from_nat(p) : U32)], xs))

def read_words(n: Nat, base: U32, offset: U32, xs: +List<U32>, a: Array<U32>) -> Array<U32> & +List<U32>:
  read_units(n, (base + offset : U32), (a, xs))

def save_outcome(o: M.Outcome, +elapsed: U32, +deadline: U32,
                 +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case M.Running{}: a
    case M.Win{}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(M.raw(M.Win{}))
      a[(base + 11 : U32)] <- F32.bits(M.timed(M.Win{}, elapsed, deadline))
    case M.Loss{}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(M.raw(M.Loss{}))
      a[(base + 11 : U32)] <- F32.bits(M.timed(M.Loss{}, elapsed, deadline))
    case M.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- F32.bits(M.raw(M.TimedOut{}))
      a[(base + 11 : U32)] <- F32.bits(M.timed(M.TimedOut{}, elapsed, deadline))

def save(s: M.State, elapsed: U32, deadline: U32, +base: U32,
         a: Array<U32>) -> Array<U32>:
  M.S{small_x, small_y, large_x, large_y, held, outcome} = s
  a[(base + 2 : U32)] <- 0
  a[(base + 32 : U32)] <- small_x
  a[(base + 33 : U32)] <- small_y
  a[(base + 34 : U32)] <- large_x
  a[(base + 35 : U32)] <- large_y
  a[(base + 36 : U32)] <- held
  save_outcome(outcome, elapsed, deadline, base, a)

def decoded(kind: U32, target: U32, arg0: U32, arg1: U32) -> M.Action:
  match kind:
    case 1: M.Submit{}
    case 13: M.Down{target}
    case 14: M.Move{arg0, arg1}
    case 15: M.Up{}
    case _: M.Wait{}

def run_fields(+base: U32, kind: U32, target: U32, arg0: U32, arg1: U32,
               +elapsed: U32, +deadline: U32,
               pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, fields) = pair
  match fields:
    case Con{small_x, Con{small_y, Con{large_x, Con{large_y,
         Con{held, Nil{}}}}}}:
      state = {M.S{small_x, small_y, large_x, large_y, held, M.Running{}} : M.State}
      save(M.step(state, decoded(kind, target, arg0, arg1), elapsed, deadline),
        elapsed, deadline, base, a)
    case _: a

def run_task(task: Nat, task_code: U32, +base: U32, kind: U32, target: U32,
             arg0: U32, arg1: U32, elapsed: U32, deadline: U32,
             a: Array<U32>) -> Array<U32>:
  match task:
    case 0n: run_fields(base, kind, target, arg0, arg1, elapsed, deadline,
      read_words(5n, base, 32, Nil{}, a))
    case _: O.dispatch(task_code, base, kind, target, arg0, arg1,
      elapsed, deadline, a)

def run_header(+task: U32, +base: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, header) = pair
  match header:
    case Con{kind, Con{target, Con{arg0, Con{arg1, Con{elapsed,
         Con{status, Con{raw, Con{timed, Con{deadline, Nil{}}}}}}}}}}:
      run_task(U32.to_nat(task), task, base, kind, target, arg0, arg1,
        elapsed, deadline, a)
    case _: a

def step_running(task: U32, status: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match status:
    case 0: run_header(task, base, read_words(9n, base, 4, Nil{}, a))
    case _: a[(base + 2 : U32)] <- 0

def step_status(task: U32, +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  step_running(task, status, base, a)

def reset_seed(+base: U32, kind: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  OG.dispatch(kind, seed, base, a)

def operation_kind(op: Nat, +base: U32, kind: U32, a: Array<U32>) -> Array<U32>:
  match op:
    case 0n: a
    case 1n: reset_seed(base, kind, a[(base + 3 : U32)])
    case _: step_status(kind, base, a[(base + 9 : U32)])

def operation(op: U32, +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, kind) = pair
  operation_kind(U32.to_nat(op), base, kind, a)

def row(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, op) = pair
  operation(op, base, a[(base + 1 : U32)])

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +base = (U32.from_nat(p) * 1024 : U32)
      batch(p, row(base, a[(base + 2 : U32)]))
