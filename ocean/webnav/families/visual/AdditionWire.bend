import Base
import ../../miniwob/Text.bend as T
import ./AdditionModel.bend as M

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

def write_units(xs: +List<U32>, +at: U32, a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{ch, rest}:
      write_units(rest, (at + 1 : U32), a[at] <- ch)

def clear(n: Nat, +at: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      clear(p, at, a[(at + U32.from_nat(p) : U32)] <- 0)

def edit_kind(kind: Nat, units: +List<U32>) -> M.Action:
  match kind:
    case 2n: M.Edit{T.Insert{units}}
    case 3n: M.Edit{T.Backspace{}}
    case 4n: M.Edit{T.Delete{}}
    case 5n: M.Edit{T.Left{}}
    case 6n: M.Edit{T.Right{}}
    case 7n: M.Edit{T.Home{}}
    case 8n: M.Edit{T.End{}}
    case 9n: M.Edit{T.SelectAll{}}
    case _: M.Wait{}

def click_action(submit: Bool) -> M.Action:
  match submit:
    case True{}: M.Submit{}
    case False{}: M.Focus{}

def action_kind(kind: Nat, target: U32, units: +List<U32>) -> M.Action:
  match kind:
    case 1n: click_action(U32.is_eq(target, 2))
    case _: edit_kind(kind, units)

def save_outcome(result: M.Outcome, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match result:
    case M.Running{}: a
    case M.Finished{+success}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(M.raw(M.Finished{success}))
      a[(base + 11 : U32)] <- F32.bits(M.timed(M.Finished{success},elapsed,deadline))
    case M.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save_field(field: T.Field, +base: U32,
    a: Array<U32>) -> Array<U32>:
  T.F{+units,start,end_,goal} = field
  a[(base + 34 : U32)] <- U32.from_nat(T.length(units))
  a[(base + 35 : U32)] <- U32.from_nat(start)
  a[(base + 36 : U32)] <- U32.from_nat(end_)
  write_units(units,(base + 64 : U32),clear(64n,(base + 64 : U32),a))

def save(state: M.State, elapsed: U32, deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  M.S{left_count,right_count,field,focused,result} = state
  a[(base + 2 : U32)] <- 0
  a[(base + 37 : U32)] <- Bool.to_u32(focused)
  a = save_field(field,base,a)
  save_outcome(result,elapsed,deadline,base,a)

def run_text(+base: U32, kind: U32, target: U32,
    +elapsed: U32, +deadline: U32,
    left_count: U32, right_count: U32,
    focused: U32, start: U32, end_: U32,
    value_units: +List<U32>,
    pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, action_units) = pair
  field = {T.F{value_units,U32.to_nat(start),U32.to_nat(end_),Nil{}} : T.Field}
  state = {M.S{left_count,right_count,field,U32.is_eq(focused,1),M.Running{}} : M.State}
  save(M.step(state,action_kind(U32.to_nat(kind),target,action_units),
    elapsed,deadline),elapsed,deadline,base,a)

def run_value(+base: U32, kind: U32, target: U32,
    elapsed: U32, deadline: U32, action_length: U32,
    left_count: U32, right_count: U32, focused: U32,
    start: U32, end_: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, value_units) = pair
  run_text(base,kind,target,elapsed,deadline,left_count,right_count,
    focused,start,end_,value_units,
    read_words(U32.to_nat(action_length),base,256,Nil{},a))

def run_fields(+base: U32, kind: U32, target: U32,
    elapsed: U32, deadline: U32, action_length: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, fields) = pair
  match fields:
    case Con{left_count,Con{right_count,Con{value_length,
         Con{start,Con{end_,Con{focused,Nil{}}}}}}}:
      run_value(base,kind,target,elapsed,deadline,action_length,
        left_count,right_count,focused,start,end_,
        read_words(U32.to_nat(value_length),base,64,Nil{},a))
    case _: a

def run_deadline(+base: U32, kind: U32, target: U32,
    elapsed: U32, action_length: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, deadline) = pair
  run_fields(base,kind,target,elapsed,deadline,action_length,
    read_words(6n,base,32,Nil{},a))

def run_header(+base: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, header) = pair
  match header:
    case Con{kind,Con{target,Con{arg0,Con{arg1,Con{elapsed,Nil{}}}}}}:
      run_deadline(base,kind,target,elapsed,arg0,a[(base + 12 : U32)])
    case _: a

def active_status(status: Nat, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match status:
    case 0n: run_header(base,read_words(5n,base,4,Nil{},a))
    case _: a[(base + 2 : U32)] <- 0

def load_status(+base: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  active_status(U32.to_nat(status),base,a)

def step(+base: U32,a: Array<U32>) -> Array<U32>:
  load_status(base,a[(base + 9 : U32)])
