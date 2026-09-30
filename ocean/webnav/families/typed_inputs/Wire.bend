import Base
import ./Model.bend as M
import ./Generate.bend as G
import ./OtherWire.bend as O

# Array reads are affine. Keep the checked pair/prepend/read pattern intact.
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

def save_value(value: M.Value, +base: U32, a: Array<U32>) -> Array<U32>:
  match value:
    case M.Empty{}:
      a[(base + 35 : U32)] <- 0
      a[(base + 36 : U32)] <- 0
      a[(base + 37 : U32)] <- 0
      a[(base + 38 : U32)] <- 0
    case M.Date{year, month, day}:
      a[(base + 35 : U32)] <- year
      a[(base + 36 : U32)] <- month
      a[(base + 37 : U32)] <- day
      a[(base + 38 : U32)] <- 1

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

def save(s: M.State, elapsed: U32, deadline: U32, +base: U32,
         a: Array<U32>) -> Array<U32>:
  M.S{goal_year, goal_month, goal_day, value, o} = s
  a[(base + 2 : U32)] <- 0
  a = save_value(value, base, a)
  save_outcome(o, elapsed, deadline, base, a)

def decoded(kind: U32, target: U32, units: +List<U32>) -> M.Action:
  match kind:
    case 2: M.SetValue{units}
    case 1: Bool.pick(M.Action, U32.is_eq(target, 2), M.Submit{}, M.Wait{})
    case _: M.Wait{}

def run_raw(+base: U32, s: M.State, kind: U32, target: U32,
            +elapsed: U32, +deadline: U32,
            pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, units) = pair
  save(M.step(s, decoded(kind, target, units), elapsed, deadline),
       elapsed, deadline, base, a)

def run_fields(+base: U32, kind: U32, target: U32, elapsed: U32,
               deadline: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, fields) = pair
  match fields:
    case Con{goal_year, Con{goal_month, Con{goal_day,
         Con{year, Con{month, Con{day, Con{present, Nil{}}}}}}}}:
      value = {Bool.pick(M.Value, U32.is_eq(present, 1),
                 M.Date{year, month, day}, M.Empty{}) : M.Value}
      state = {M.S{goal_year, goal_month, goal_day, value, M.Running{}} : M.State}
      run_raw(base, state, kind, target, elapsed, deadline,
              read_words(10n, base, 128, Nil{}, a))
    case _: a

def run_status(status: U32, +base: U32, kind: U32, target: U32,
               elapsed: U32, deadline: U32, a: Array<U32>) -> Array<U32>:
  match status:
    case 0: run_fields(base, kind, target, elapsed, deadline,
              read_words(7n, base, 32, Nil{}, a))
    case _: a[(base + 2 : U32)] <- 0

def run_header(+base: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, header) = pair
  match header:
    case Con{kind, Con{target, Con{arg0, Con{arg1, Con{elapsed,
         Con{status, Con{raw, Con{timed, Con{deadline, Nil{}}}}}}}}}}:
      run_status(status, base, kind, target, elapsed, deadline, a)
    case _: a

def reset_seed(+base: U32, task: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  G.generate(task, seed, base, a)

def step_task(task: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match task:
    case 0: run_header(base, read_words(9n, base, 4, Nil{}, a))
    case _: O.run(task, base, a)

def step_running(status: U32, +base: U32, task: U32,
                 a: Array<U32>) -> Array<U32>:
  match status:
    case 0: step_task(task, base, a)
    case _: a[(base + 2 : U32)] <- 0

def step_status(+base: U32, task: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  step_running(status, base, task, a)

def operation_kind(op: Nat, +base: U32, task: U32, a: Array<U32>) -> Array<U32>:
  match op:
    case 0n: a
    case 1n: reset_seed(base, task, a[(base + 3 : U32)])
    case _: step_status(base, task, a[(base + 9 : U32)])

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
