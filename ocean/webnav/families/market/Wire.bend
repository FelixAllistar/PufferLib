import Base
import ./Model.bend as M
import ./Generate.bend as G

def outcome_index(status: Nat, raw: U32) -> M.Outcome:
  match status:
    case 0n: M.Running{}
    case 1n: M.Finished{U32.is_eq(raw, 1065353216)}
    case _: M.TimedOut{}

def outcome(status: U32, raw: U32) -> M.Outcome:
  outcome_index(U32.to_nat(status), raw)

def prepend(pair: Array<U32> & U32,
    rest: +List<U32>) -> Array<U32> & +List<U32>:
  (a, value) = pair
  (a, value <> rest)

def prices(n: Nat, +base: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, rest) = pair
      prices(p, base,
        prepend(a[(base + 64 + U32.from_nat(p) : U32)], rest))

def save_outcome(result: M.Outcome, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match result:
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
  M.S{items, threshold, tick, visible, result} = s
  a[(base + 2 : U32)] <- 0
  a[(base + 32 : U32)] <- tick
  a[(base + 33 : U32)] <- visible
  save_outcome(result, elapsed, deadline, base, a)

def action_index(kind: Nat) -> M.Action:
  match kind:
    case 1n: M.Buy{}
    case _: M.Wait{}

def loaded(+base: U32, status: U32, raw: U32,
    +elapsed: U32, +deadline: U32, kind: U32,
    tick: U32, visible: U32, threshold: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, items) = pair
  s = {M.S{items, threshold, tick, visible, outcome(status, raw)} : M.State}
  save(M.step(s, action_index(U32.to_nat(kind)), elapsed, deadline),
    elapsed, deadline, base, a)

def load_count(+base: U32, status: U32, raw: U32,
    elapsed: U32, deadline: U32, kind: U32,
    tick: U32, visible: U32, threshold: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, count) = pair
  loaded(base, status, raw, elapsed, deadline, kind,
    tick, visible, threshold,
    prices(U32.to_nat(count), base, (a, Nil{})))

def load_threshold(+base: U32, status: U32, raw: U32,
    elapsed: U32, deadline: U32, kind: U32,
    tick: U32, visible: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, threshold) = pair
  load_count(base, status, raw, elapsed, deadline, kind,
    tick, visible, threshold, a[(base + 35 : U32)])

def load_visible(+base: U32, status: U32, raw: U32,
    elapsed: U32, deadline: U32, kind: U32, tick: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, visible) = pair
  load_threshold(base, status, raw, elapsed, deadline, kind,
    tick, visible, a[(base + 34 : U32)])

def load_tick(+base: U32, status: U32, raw: U32,
    elapsed: U32, deadline: U32, kind: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, tick) = pair
  load_visible(base, status, raw, elapsed, deadline,
    kind, tick, a[(base + 33 : U32)])

def load_kind(+base: U32, status: U32, raw: U32,
    elapsed: U32, deadline: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, kind) = pair
  load_tick(base, status, raw, elapsed, deadline,
    kind, a[(base + 32 : U32)])

def load_deadline(+base: U32, status: U32, raw: U32,
    elapsed: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, deadline) = pair
  load_kind(base, status, raw, elapsed, deadline,
    a[(base + 4 : U32)])

def load_elapsed(+base: U32, status: U32, raw: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, elapsed) = pair
  load_deadline(base, status, raw, elapsed,
    a[(base + 12 : U32)])

def load_raw(+base: U32, status: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, raw) = pair
  load_elapsed(base, status, raw, a[(base + 8 : U32)])

def active_status(status: Nat, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match status:
    case 0n: load_raw(base, 0, a[(base + 10 : U32)])
    case _: a[(base + 2 : U32)] <- 0

def load_status(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  active_status(U32.to_nat(status), base, a)

def step(+base: U32, a: Array<U32>) -> Array<U32>:
  load_status(base, a[(base + 9 : U32)])

def reset_seed(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  G.generate(0, seed, base, a)

def reset(+base: U32, a: Array<U32>) -> Array<U32>:
  reset_seed(base, a[(base + 3 : U32)])

def operation_index(op: Nat, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match op:
    case 0n: a
    case 1n: reset(base, a)
    case _: step(base, a)

def operation(op: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  operation_index(U32.to_nat(op), base, a)

def row(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, op) = pair
  operation(op, base, a)

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +base = (U32.from_nat(p) * 8192 : U32)
      batch(p, row(base, a[(base + 2 : U32)]))
