import Base
import ./TimeModel.bend as T
import ./UnicodeModel.bend as U

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

def time_value(value: T.Value, +base: U32, a: Array<U32>) -> Array<U32>:
  match value:
    case T.Empty{}:
      a[(base + 35 : U32)] <- 0
      a[(base + 36 : U32)] <- 0
      a[(base + 37 : U32)] <- 0
      a[(base + 38 : U32)] <- 0
    case T.Time{hour, minute}:
      a[(base + 35 : U32)] <- hour
      a[(base + 36 : U32)] <- minute
      a[(base + 37 : U32)] <- 0
      a[(base + 38 : U32)] <- 1

def time_outcome(o: T.Outcome, +elapsed: U32, +deadline: U32,
                 +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case T.Running{}: a
    case T.Finished{+success}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(T.raw(T.Finished{success}))
      a[(base + 11 : U32)] <- F32.bits(T.timed(T.Finished{success}, elapsed, deadline))
    case T.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def time_save(s: T.State, elapsed: U32, deadline: U32, +base: U32,
              a: Array<U32>) -> Array<U32>:
  T.S{goal_hour, goal_minute, value, o} = s
  a[(base + 2 : U32)] <- 0
  a = time_value(value, base, a)
  time_outcome(o, elapsed, deadline, base, a)

def time_decoded(kind: U32, target: U32, units: +List<U32>) -> T.Action:
  match kind:
    case 2: T.SetValue{units}
    case 1: Bool.pick(T.Action, U32.is_eq(target, 2), T.Submit{}, T.Wait{})
    case _: T.Wait{}

def time_raw(+base: U32, s: T.State, kind: U32, target: U32,
             +elapsed: U32, +deadline: U32,
             pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, units) = pair
  time_save(T.step(s, time_decoded(kind, target, units), elapsed, deadline),
            elapsed, deadline, base, a)

def time_fields(+base: U32, kind: U32, target: U32, elapsed: U32,
                deadline: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, fields) = pair
  match fields:
    case Con{goal_hour, Con{goal_minute, Con{unused,
         Con{hour, Con{minute, Con{unused2, Con{present, Nil{}}}}}}}}:
      value = {Bool.pick(T.Value, U32.is_eq(present, 1),
                 T.Time{hour, minute}, T.Empty{}) : T.Value}
      state = {T.S{goal_hour, goal_minute, value, T.Running{}} : T.State}
      time_raw(base, state, kind, target, elapsed, deadline,
               read_words(5n, base, 128, Nil{}, a))
    case _: a

def unicode_outcome(o: U.Outcome, +elapsed: U32, +deadline: U32,
                    +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case U.Running{}: a
    case U.Finished{+success}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(U.raw(U.Finished{success}))
      a[(base + 11 : U32)] <- F32.bits(U.timed(U.Finished{success}, elapsed, deadline))
    case U.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def unicode_save(s: U.State, elapsed: U32, deadline: U32, +base: U32,
                 a: Array<U32>) -> Array<U32>:
  U.S{goal, o} = s
  a[(base + 2 : U32)] <- 0
  unicode_outcome(o, elapsed, deadline, base, a)

def unicode_click_loaded(+base: U32, goal: U32, +elapsed: U32,
                         +deadline: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, label) = pair
  unicode_save(U.step(U.S{goal, U.Running{}}, U.Click{label}, elapsed, deadline),
               elapsed, deadline, base, a)

def unicode_decoded(kind: U32, +base: U32, goal: U32, target: U32,
                    +elapsed: U32, +deadline: U32,
                    a: Array<U32>) -> Array<U32>:
  match kind:
    case 1: unicode_click_loaded(base, goal, elapsed, deadline,
              a[(base + 48 + (target - 3 : U32) : U32)])
    case _:
      unicode_save(U.step(U.S{goal, U.Running{}}, U.Wait{}, elapsed, deadline),
                   elapsed, deadline, base, a)

def unicode_goal(+base: U32, kind: U32, target: U32, elapsed: U32,
                 deadline: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, goal) = pair
  unicode_decoded(kind, base, goal, target, elapsed, deadline, a)

def dispatch(task: U32, +base: U32, kind: U32, target: U32,
             elapsed: U32, deadline: U32, a: Array<U32>) -> Array<U32>:
  match task:
    case 1: time_fields(base, kind, target, elapsed, deadline,
              read_words(7n, base, 32, Nil{}, a))
    case _: unicode_goal(base, kind, target, elapsed, deadline,
              a[(base + 32 : U32)])

def run_header(task: U32, +base: U32,
               pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, header) = pair
  match header:
    case Con{kind, Con{target, Con{arg0, Con{arg1, Con{elapsed,
         Con{status, Con{raw, Con{timed, Con{deadline, Nil{}}}}}}}}}}:
      dispatch(task, base, kind, target, elapsed, deadline, a)
    case _: a

def run(task: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  run_header(task, base, read_words(9n, base, 4, Nil{}, a))
