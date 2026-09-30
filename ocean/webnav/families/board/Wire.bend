import Base
import ./Model.bend as M
import ./Generate.bend as G

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
    case M.Draw{}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(M.raw(M.Draw{}))
      a[(base + 11 : U32)] <- F32.bits(M.timed(M.Draw{}, elapsed, deadline))
    case M.Loss{}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(M.raw(M.Loss{}))
      a[(base + 11 : U32)] <- F32.bits(M.timed(M.Loss{}, elapsed, deadline))
    case M.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(s: M.State, elapsed: U32, deadline: U32, +base: U32,
         a: Array<U32>) -> Array<U32>:
  M.S{random_x, random_o, random, outcome} = s
  a[(base + 2 : U32)] <- 0
  a[(base + 32 : U32)] <- random
  a[(base + 33 : U32)] <- random_x
  a[(base + 34 : U32)] <- random_o
  save_outcome(outcome, elapsed, deadline, base, a)

def decoded(kind: U32, target: U32) -> M.Action:
  match kind:
    case 1: M.Click{(target - 1 : U32)}
    case _: M.Wait{}

def run_fields(+base: U32, kind: U32, target: U32, +elapsed: U32,
               +deadline: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, fields) = pair
  match fields:
    case Con{random, Con{xmask, Con{omask, Nil{}}}}:
      state = {M.S{xmask, omask, random, M.Running{}} : M.State}
      save(M.step(state, decoded(kind, target), elapsed, deadline),
        elapsed, deadline, base, a)
    case _: a

def run_header(+base: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, header) = pair
  match header:
    case Con{kind, Con{target, Con{arg0, Con{arg1, Con{elapsed,
         Con{status, Con{raw, Con{timed, Con{deadline, Nil{}}}}}}}}}}:
      run_fields(base, kind, target, elapsed, deadline,
        read_words(3n, base, 32, Nil{}, a))
    case _: a

def step_running(status: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match status:
    case 0: run_header(base, read_words(9n, base, 4, Nil{}, a))
    case _: a[(base + 2 : U32)] <- 0

def step_status(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  step_running(status, base, a)

def reset_seed(+base: U32, task: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  G.generate(task, seed, base, a)

def operation_kind(op: Nat, +base: U32, task: U32, a: Array<U32>) -> Array<U32>:
  match op:
    case 0n: a
    case 1n: reset_seed(base, task, a[(base + 3 : U32)])
    case _: step_status(base, a[(base + 9 : U32)])

def operation(op: U32, +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, task) = pair
  operation_kind(U32.to_nat(op), base, task, a)

def row(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, op) = pair
  operation(op, base, a[(base + 1 : U32)])

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +base = (U32.from_nat(p) * 1024 : U32)
      batch(p, row(base, a[(base + 2 : U32)]))
