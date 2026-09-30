import Base
import ./DrawModel.bend as M

def prepend(pair: Array<U32> & U32,
    xs: +List<U32>) -> Array<U32> & +List<U32>:
  (a, value) = pair
  (a, value <> xs)

def read_units(n: Nat, +at: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, xs) = pair
      read_units(p, at,
        prepend(a[(at + U32.from_nat(p) : U32)], xs))

def read_words(n: Nat, base: U32, offset: U32,
    xs: +List<U32>, a: Array<U32>) -> Array<U32> & +List<U32>:
  read_units(n, (base + offset : U32), (a, xs))

def task_index(task: Nat) -> M.Task:
  match task:
    case 0n: M.Bisect{}
    case 1n: M.Center{}
    case 2n: M.Midpoint{}
    case _: M.Right{}

def action_index(kind: Nat, target: Nat,
    arg0: U32, arg1: U32) -> M.Action:
  match kind target:
    case 1n 1n: M.Click{M.fixed_point(arg0, arg1)}
    case 1n 2n: M.Submit{}
    case _ _: M.Wait{}

def save_outcome(result: M.Outcome, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match result:
    case M.Running{}: a
    case M.Finished{+reward, +scaled}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(M.raw(M.Finished{reward, scaled}))
      a[(base + 11 : U32)] <- F32.bits(M.timed(M.Finished{reward, scaled}, elapsed, deadline))
    case M.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(state: M.State, elapsed: U32, deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  M.S{task, p, q, vertex, radius, has_user, user, result} = state
  M.P{ux, uy} = user
  a[(base + 2 : U32)] <- 0
  a[(base + 47 : U32)] <- F32.to_u32((ux * 256.0 : F32))
  a[(base + 48 : U32)] <- F32.to_u32((uy * 256.0 : F32))
  a[(base + 49 : U32)] <- Bool.to_u32(has_user)
  save_outcome(result, elapsed, deadline, base, a)

def run_fields(+base: U32, task: U32, kind: U32, target: U32,
    arg0: U32, arg1: U32, +elapsed: U32, +deadline: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, fields) = pair
  match fields:
    case Con{ax, Con{ay, Con{bx, Con{by, Con{cx,
         Con{cy, Con{radius, Con{ux, Con{uy,
         Con{has_user, Nil{}}}}}}}}}}}:
      state = {M.S{task_index(U32.to_nat(task)),
        M.make_point(ax, ay), M.make_point(bx, by), M.make_point(cx, cy),
        U32.to_f32(radius), U32.is_eq(has_user, 1),
        M.fixed_point(ux, uy), M.Running{}} : M.State}
      save(M.step(state,
        action_index(U32.to_nat(kind), U32.to_nat(target), arg0, arg1),
        elapsed, deadline), elapsed, deadline, base, a)
    case _: a

def run_deadline(+base: U32, task: U32, kind: U32, target: U32,
    arg0: U32, arg1: U32, elapsed: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, deadline) = pair
  run_fields(base, task, kind, target, arg0, arg1, elapsed, deadline,
    read_words(10n, base, 40, Nil{}, a))

def run_header(+base: U32, task: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, header) = pair
  match header:
    case Con{kind, Con{target, Con{arg0, Con{arg1,
         Con{elapsed, Nil{}}}}}}:
      run_deadline(base, task, kind, target, arg0, arg1, elapsed,
        a[(base + 12 : U32)])
    case _: a

def active_status(status: Nat, +base: U32, task: U32,
    a: Array<U32>) -> Array<U32>:
  match status:
    case 0n: run_header(base, task, read_words(5n, base, 4, Nil{}, a))
    case _: a[(base + 2 : U32)] <- 0

def load_status(+base: U32, task: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  active_status(U32.to_nat(status), base, task, a)

def step(+base: U32, task: U32, a: Array<U32>) -> Array<U32>:
  load_status(base, task, a[(base + 9 : U32)])
