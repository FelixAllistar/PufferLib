import Base
import ./Model.bend as M
import ./Generate.bend as G
import ./FoodWire.bend as FW
import ./SearchWire.bend as SW

def outcome_index(status: Nat, grade_code: U32) -> M.Outcome:
  match status:
    case 0n: M.Running{}
    case 1n: M.Finished{grade_code}
    case _: M.TimedOut{}

def outcome(status: U32, grade_code: U32) -> M.Outcome:
  outcome_index(U32.to_nat(status), grade_code)

def save_outcome(o: M.Outcome, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case M.Running{}: a
    case M.Finished{+grade_code}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(M.raw(M.Finished{grade_code}))
      a[(base + 11 : U32)] <- F32.bits(M.timed(M.Finished{grade_code}, elapsed, deadline))
      a[(base + 36 : U32)] <- grade_code
    case M.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(s: M.State, elapsed: U32, deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  M.S{page, target, property, o} = s
  a = a[(base + 32 : U32)] <- page
  a = a[(base + 2 : U32)] <- 0
  save_outcome(o, elapsed, deadline, base, a)

def page_or_select(is_page: Bool, ref: U32) -> M.Action:
  match is_page:
    case True{}: M.Page{ref}
    case False{}: M.Select{(ref - 100 : U32)}

def decode(zero: Bool, +ref: U32) -> M.Action:
  match zero:
    case True{}: M.Wait{}
    case False{}: page_or_select(U32.is_le(ref, 5), ref)

def ready(a: Array<U32>, status: U32, grade_code: U32,
    +elapsed: U32, +ref: U32, page: U32, target: U32,
    property: U32, +deadline: U32, +base: U32) -> Array<U32>:
  s = {M.S{page, target, property, outcome(status, grade_code)} : M.State}
  save(M.step(s, decode(U32.is_eq(ref, 0), ref), elapsed, deadline),
    elapsed, deadline, base, a)

def load_property(pair: Array<U32> & U32, status: U32, grade_code: U32,
    elapsed: U32, ref: U32, page: U32, target: U32,
    deadline: U32, +base: U32) -> Array<U32>:
  (a, property) = pair
  ready(a, status, grade_code, elapsed, ref, page, target,
    property, deadline, base)

def load_target(pair: Array<U32> & U32, status: U32, grade_code: U32,
    elapsed: U32, ref: U32, page: U32,
    deadline: U32, +base: U32) -> Array<U32>:
  (a, target) = pair
  load_property(a[(base + 34 : U32)], status, grade_code, elapsed,
    ref, page, target, deadline, base)

def load_page(pair: Array<U32> & U32, status: U32, grade_code: U32,
    elapsed: U32, ref: U32, deadline: U32,
    +base: U32) -> Array<U32>:
  (a, page) = pair
  load_target(a[(base + 33 : U32)], status, grade_code, elapsed,
    ref, page, deadline, base)

def load_ref(pair: Array<U32> & U32, status: U32, grade_code: U32,
    elapsed: U32, deadline: U32, +base: U32) -> Array<U32>:
  (a, ref) = pair
  load_page(a[(base + 32 : U32)], status, grade_code, elapsed, ref,
    deadline, base)

def load_elapsed(pair: Array<U32> & U32, status: U32,
    grade_code: U32, deadline: U32, +base: U32) -> Array<U32>:
  (a, elapsed) = pair
  load_ref(a[(base + 5 : U32)], status, grade_code, elapsed, deadline, base)

def load_deadline(pair: Array<U32> & U32, status: U32,
    grade_code: U32, +base: U32) -> Array<U32>:
  (a, deadline) = pair
  load_elapsed(a[(base + 8 : U32)], status, grade_code, deadline, base)

def load_grade(pair: Array<U32> & U32, status: U32,
    +base: U32) -> Array<U32>:
  (a, grade_code) = pair
  load_deadline(a[(base + 12 : U32)], status, grade_code, base)

def load_status(pair: Array<U32> & U32, +base: U32) -> Array<U32>:
  (a, status) = pair
  load_grade(a[(base + 36 : U32)], status, base)

def reset_seed(pair: Array<U32> & U32, task: U32, +base: U32) -> Array<U32>:
  (a, seed) = pair
  G.generate(task, seed, base, a)

def reset_task(pair: Array<U32> & U32, +base: U32) -> Array<U32>:
  (a, task) = pair
  reset_seed(a[(base + 3 : U32)], task, base)

def operation_index(op: Nat, task: Nat, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match op task:
    case 0n _: a
    case 1n 0n: reset_task(a[(base + 1 : U32)], base)
    case 1n 1n: FW.reset(base, a)
    case 1n _: SW.reset(base, a)
    case _ 0n: load_status(a[(base + 9 : U32)], base)
    case _ 1n: FW.run(base, a)
    case _ _: SW.run(base, a)

def operation(op: U32, task: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  operation_index(U32.to_nat(op), U32.to_nat(task), base, a)

def run_task(pair: Array<U32> & U32, op: U32,
    +base: U32) -> Array<U32>:
  (a, task) = pair
  operation(op, task, base, a)

def row(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, op) = pair
  run_task(a[(base + 1 : U32)], op, base)

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +base = (U32.from_nat(p) * 8192 : U32)
      batch(p, row(base, a[(base + 2 : U32)]))
