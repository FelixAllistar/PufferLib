import Base
import ./ListModel.bend as L
import ./TextModel.bend as Tm
import ./AgreementModel.bend as A
import ../../miniwob/Text.bend as T

# The checked forms transport pattern: every computed array read enters a
# helper before its pair is destructured. Arrays have one owner throughout.
def prepend(pair: Array<U32> & U32, xs: +List<U32>) -> Array<U32> & +List<U32>:
  (a, x) = pair
  (a, x <> xs)

def read_indices(indices: +List<U32>, +base: U32,
                 pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match indices:
    case Nil{}: pair
    case Con{at, rest}:
      (a, xs) = pair
      read_indices(rest, base, prepend(a[(base + at : U32)], xs))

def read_units(n: Nat, +at: U32,
               pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, xs) = pair
      read_units(p, at, prepend(a[(at + U32.from_nat(p) : U32)], xs))

def zero(n: Nat, +at: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: zero(p, at, a[(at + U32.from_nat(p) : U32)] <- 0)

def write_units(xs: +List<U32>, +at: U32, a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{x, rest}: write_units(rest, (at + 1 : U32), a[at] <- x)

def save_status(status: U32, +elapsed: U32, +deadline: U32,
                +base: U32, a: Array<U32>) -> Array<U32>:
  match status:
    case 0: a[(base + 2 : U32)] <- 0
    case 1:
      a[(base + 2 : U32)] <- 0
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- 1065353216
      a[(base + 11 : U32)] <- F32.bits((1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32))
    case 2:
      a[(base + 2 : U32)] <- 0
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864
    case _:
      a[(base + 2 : U32)] <- 0
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save_list(s: L.State, elapsed: U32, deadline: U32,
              +base: U32, a: Array<U32>) -> Array<U32>:
  L.S{position, height, client, count, stride, selected, allowed, required, anchor, status} = s
  a[(base + 32 : U32)] <- position
  a[(base + 37 : U32)] <- selected
  a[(base + 49 : U32)] <- anchor
  save_status(status, elapsed, deadline, base, a)

def list_action(kind: U32, target: U32, arg0: U32) -> L.Action:
  match kind:
    case 12: L.Scroll{arg0}
    case 21: L.Select{(target - 2 : U32), U32.is_eq(arg0, 1)}
    case 1: L.Submit{}
    case _: L.Wait{}

def list_loaded(+base: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, xs) = pair
  match xs:
    case Con{kind, Con{target, Con{arg0, Con{+elapsed,
         Con{+deadline, Con{position, Con{height, Con{client,
         Con{count, Con{stride, Con{selected, Con{allowed, Con{required, Con{anchor, Nil{}}}}}}}}}}}}}}}:
      state = {L.S{position, height, client, count, stride, selected, allowed, required, anchor, 0} : L.State}
      save_list(L.step(state, list_action(kind, target, arg0), elapsed, deadline),
        elapsed, deadline, base, a)
    case _: a

def list_row(+base: U32, a: Array<U32>) -> Array<U32>:
  list_loaded(base, read_indices([49,39,38,37,48,36,34,33,32,12,8,6,5,4],
    base, (a, Nil{})))

def save_field(field: T.Field, +base: U32, a: Array<U32>) -> Array<U32>:
  T.F{+value, start, end, goal} = field
  a[(base + 42 : U32)] <- U32.from_nat(T.length(value))
  a[(base + 44 : U32)] <- U32.from_nat(start)
  a[(base + 45 : U32)] <- U32.from_nat(end)
  write_units(value, (base + 4608 : U32), zero(64n, (base + 4608 : U32), a))

def save_text(s: Tm.State, elapsed: U32, deadline: U32,
              +base: U32, a: Array<U32>) -> Array<U32>:
  Tm.S{position, height, client, field, status} = s
  a[(base + 32 : U32)] <- position
  save_status(status, elapsed, deadline, base, save_field(field, base, a))

def save_agreement(s: A.State, elapsed: U32, deadline: U32,
                   +base: U32, a: Array<U32>) -> Array<U32>:
  A.S{position, height, client, mode, enabled, field, status} = s
  a[(base + 32 : U32)] <- position
  a[(base + 41 : U32)] <- Bool.to_u32(enabled)
  save_status(status, elapsed, deadline, base, save_field(field, base, a))

def text_action(kind: U32, target: U32, arg0: U32,
                incoming: +List<U32>) -> Tm.Action:
  match kind:
    case 12: Tm.Scroll{arg0}
    case 1: Bool.pick(Tm.Action, U32.is_eq(target, 3), Tm.Submit{}, Tm.Wait{})
    case 2: Tm.Edit{T.Insert{incoming}}
    case 3: Tm.Edit{T.Backspace{}}
    case 4: Tm.Edit{T.Delete{}}
    case 5: Tm.Edit{T.Left{}}
    case 6: Tm.Edit{T.Right{}}
    case 7: Tm.Edit{T.Home{}}
    case 8: Tm.Edit{T.End{}}
    case 9: Tm.Edit{T.SelectAll{}}
    case _: Tm.Wait{}

def agreement_action(kind: U32, +target: U32, arg0: U32,
                     incoming: +List<U32>) -> A.Action:
  match kind:
    case 12: A.Scroll{arg0}
    case 1:
      Bool.pick(A.Action, U32.is_eq(target, 3), A.Cancel{},
        Bool.pick(A.Action, U32.is_eq(target, 4), A.Agree{}, A.Wait{}))
    case 2: A.Edit{T.Insert{incoming}}
    case 3: A.Edit{T.Backspace{}}
    case 4: A.Edit{T.Delete{}}
    case 5: A.Edit{T.Left{}}
    case 6: A.Edit{T.Right{}}
    case 7: A.Edit{T.Home{}}
    case 8: A.Edit{T.End{}}
    case 9: A.Edit{T.SelectAll{}}
    case _: A.Wait{}

def apply_field(+task: U32, kind: U32, target: U32, arg0: U32,
                +elapsed: U32, +deadline: U32, position: U32,
                height: U32, client: U32, mode: U32, enabled: U32,
                field: T.Field, incoming: +List<U32>, +base: U32,
                a: Array<U32>) -> Array<U32>:
  match task:
    case 1:
      state = {Tm.S{position, height, client, field, 0} : Tm.State}
      save_text(Tm.step(state, text_action(kind, target, arg0, incoming), elapsed, deadline),
        elapsed, deadline, base, a)
    case _:
      state = {A.S{position, height, client, mode, U32.is_eq(enabled, 1), field, 0} : A.State}
      save_agreement(A.step(state, agreement_action(kind, target, arg0, incoming), elapsed, deadline),
        elapsed, deadline, base, a)

def with_incoming(+task: U32, kind: U32, target: U32, arg0: U32,
                  elapsed: U32, deadline: U32, position: U32,
                  height: U32, client: U32, mode: U32, enabled: U32,
                  field: T.Field, +base: U32,
                  pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, incoming) = pair
  apply_field(task, kind, target, arg0, elapsed, deadline, position,
    height, client, mode, enabled, field, incoming, base, a)

def with_goal(+task: U32, kind: U32, target: U32, arg0: U32,
              elapsed: U32, deadline: U32, position: U32,
              height: U32, client: U32, mode: U32, enabled: U32,
              start: U32, end: U32, incoming_size: U32,
              input: +List<U32>, +base: U32,
              pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, goal) = pair
  field = {T.F{input, U32.to_nat(start), U32.to_nat(end), goal} : T.Field}
  with_incoming(task, kind, target, arg0, elapsed, deadline, position,
    height, client, mode, enabled, field, base,
    read_units(U32.to_nat(incoming_size), (base + 4864 : U32), (a, Nil{})))

def with_input(+task: U32, kind: U32, target: U32, arg0: U32,
               elapsed: U32, deadline: U32, position: U32,
               height: U32, client: U32, mode: U32, enabled: U32,
               goal_size: U32, start: U32, end: U32, incoming_size: U32,
               +base: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, input) = pair
  with_goal(task, kind, target, arg0, elapsed, deadline, position,
    height, client, mode, enabled, start, end, incoming_size, input,
    base, read_units(U32.to_nat(goal_size), (base + 4736 : U32), (a, Nil{})))

def input_loaded(+task: U32, +base: U32,
                 pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, xs) = pair
  match xs:
    case Con{kind, Con{target, Con{arg0, Con{elapsed, Con{deadline,
         Con{position, Con{height, Con{client, Con{mode,
         Con{enabled, Con{input_size, Con{goal_size,
         Con{start, Con{end, Con{incoming_size, Nil{}}}}}}}}}}}}}}}}:
      with_input(task, kind, target, arg0, elapsed, deadline, position,
        height, client, mode, enabled, goal_size, start, end, incoming_size,
        base, read_units(U32.to_nat(input_size), (base + 4608 : U32), (a, Nil{})))
    case _: a

def input_row(task: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  input_loaded(task, base,
    read_indices([46,45,44,43,42,41,40,34,33,32,12,8,6,5,4],
      base, (a, Nil{})))

def run(task: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match task:
    case 0: list_row(base, a)
    case 1: input_row(1, base, a)
    case _: input_row(3, base, a)
