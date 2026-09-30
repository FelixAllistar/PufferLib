import Base
import ./Model.bend as M
import ./Generate.bend as G
import ./HighlightWire.bend as HW
import ./EditorWire.bend as EW
import ./TerminalWire.bend as TW

def prepend(pair: Array<U32> & U32, xs: +List<U32>) -> Array<U32> & +List<U32>:
  (a, x) = pair
  (a, x <> xs)

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
    case Con{x, rest}:
      write_units(rest, (at + 1 : U32), a[at] <- x)

def outcome_index(status: Nat, raw: U32) -> M.Outcome:
  match status:
    case 0n: M.Running{}
    case 1n: M.Finished{U32.is_eq(raw, 1065353216)}
    case _: M.TimedOut{}

def outcome(status: U32, raw: U32) -> M.Outcome:
  outcome_index(U32.to_nat(status), raw)

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

def save(s: M.State, elapsed: U32, deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  M.S{expected, +input, focused, o} = s
  +size = U32.from_nat(List.length(&2, U32, input))
  a = a[(base + 2 : U32)] <- 0
  a = a[(base + 34 : U32)] <- size
  a = a[(base + 35 : U32)] <- size
  a = a[(base + 36 : U32)] <- size
  a = a[(base + 37 : U32)] <- Bool.to_u32(focused)
  a = write_units(input, (base + 1152 : U32),
    zero(128n, (base + 1152 : U32), a))
  save_outcome(o, elapsed, deadline, base, a)

def decoded_index(kind: Nat, ref: U32, payload: +List<U32>) -> M.Action:
  match kind:
    case 1n:
      Bool.pick(M.Action, U32.is_eq(ref, 1), M.Focus{}, M.Submit{})
    case 2n: M.Insert{payload}
    case _: M.Wait{}

def after_payload(pair: Array<U32> & +List<U32>,
    expected: +List<U32>, input: +List<U32>, focus: U32,
    kind: U32, ref: U32, status: U32, raw: U32,
    +elapsed: U32, +deadline: U32, +base: U32) -> Array<U32>:
  (a, payload) = pair
  s = {M.S{expected, input, U32.is_eq(focus, 1),
    outcome(status, raw)} : M.State}
  save(M.step(s, decoded_index(U32.to_nat(kind), ref, payload),
    elapsed, deadline), elapsed, deadline, base, a)

def after_input(pair: Array<U32> & +List<U32>,
    expected: +List<U32>, focus: U32, kind: U32,
    ref: U32, payload_len: U32, status: U32,
    raw: U32, elapsed: U32, deadline: U32,
    +base: U32) -> Array<U32>:
  (a, input) = pair
  after_payload(read_units(U32.to_nat(payload_len),
    (base + 2048 : U32), (a, Nil{})), expected, input,
    focus, kind, ref, status, raw, elapsed, deadline, base)

def after_expected(pair: Array<U32> & +List<U32>,
    input_len: U32, focus: U32, kind: U32,
    ref: U32, payload_len: U32, status: U32,
    raw: U32, elapsed: U32, deadline: U32,
    +base: U32) -> Array<U32>:
  (a, expected) = pair
  after_input(read_units(U32.to_nat(input_len),
    (base + 1152 : U32), (a, Nil{})), expected, focus,
    kind, ref, payload_len, status, raw, elapsed, deadline, base)

def meta_loaded(pair: Array<U32> & +List<U32>,
    kind: U32, ref: U32, payload_len: U32,
    status: U32, raw: U32, elapsed: U32,
    deadline: U32, +base: U32) -> Array<U32>:
  (a, fields) = pair
  match fields:
    case Con{input_len, Con{start, Con{end_,
         Con{focus, Con{expected_len, Nil{}}}}}}:
      after_expected(read_units(U32.to_nat(expected_len),
        (base + 1024 : U32), (a, Nil{})), input_len,
        focus, kind, ref, payload_len, status, raw,
        elapsed, deadline, base)
    case _: a

def header_loaded(pair: Array<U32> & +List<U32>,
    +base: U32) -> Array<U32>:
  (a, fields) = pair
  match fields:
    case Con{kind, Con{ref, Con{payload_len, Con{arg1,
         Con{elapsed, Con{status, Con{raw, Con{timed,
         Con{deadline, Nil{}}}}}}}}}}:
      meta_loaded(read_units(5n, (base + 34 : U32),
        (a, Nil{})), kind, ref, payload_len, status,
        raw, elapsed, deadline, base)
    case _: a

def reset_seed(pair: Array<U32> & U32, +base: U32) -> Array<U32>:
  (a, seed) = pair
  G.generate(0, seed, base, a)

def operation_index(op: Nat, task: Nat, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match op task:
    case 0n _: a
    case 1n 0n: reset_seed(a[(base + 3 : U32)], base)
    case 1n 1n: HW.reset(base, a)
    case 1n 2n: HW.reset(base, a)
    case 1n 3n: EW.reset(base, a)
    case 1n _: TW.reset(base, a)
    case _ 0n: header_loaded(read_units(9n, (base + 4 : U32),
      (a, Nil{})), base)
    case _ 1n: HW.run(base, a)
    case _ 2n: HW.run(base, a)
    case _ 3n: EW.run(base, a)
    case _ _: TW.run(base, a)

def task_loaded(pair: Array<U32> & U32, op: U32,
    +base: U32) -> Array<U32>:
  (a, task) = pair
  operation_index(U32.to_nat(op), U32.to_nat(task), base, a)

def operation(pair: Array<U32> & U32, +base: U32) -> Array<U32>:
  (a, op) = pair
  task_loaded(a[(base + 1 : U32)], op, base)

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +base = (U32.from_nat(p) * 8192 : U32)
      batch(p, operation(a[(base + 2 : U32)], base))
