import Base
import ./Food.bend as F
import ./FoodGenerate.bend as G

type Check is Data:
  C{valid: Bool, total: U32}

def outcome_index(status: Nat, raw: U32) -> F.Outcome:
  match status:
    case 0n: F.Running{}
    case 1n: F.Finished{U32.is_eq(raw, 1065353216)}
    case _: F.TimedOut{}

def outcome(status: U32, raw: U32) -> F.Outcome:
  outcome_index(U32.to_nat(status), raw)

def save_outcome(o: F.Outcome, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case F.Running{}: a
    case F.Finished{+success}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(F.raw(F.Finished{success}))
      a[(base + 11 : U32)] <- F32.bits(F.timed(F.Finished{success}, elapsed, deadline))
    case F.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(s: F.State, elapsed: U32, deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  F.S{o} = s
  a = a[(base + 2 : U32)] <- 0
  save_outcome(o, elapsed, deadline, base, a)

def scan_acc(acc: Check, +quantity: U32, mask: U32,
    index: U32, mode: U32, first: U32,
    second_item: U32, type_code: U32) -> Check:
  match acc:
    case C{valid, total}:
      C{valid && F.item_ok(mode, quantity, index, first,
        second_item, type_code, mask), (total + quantity : U32)}

def scan_mask(pair: Array<U32> & U32, quantity: U32,
    index: U32, mode: U32, first: U32, second_item: U32,
    type_code: U32, acc: Check) -> Array<U32> & Check:
  (a, mask) = pair
  (a, scan_acc(acc, quantity, mask, index, mode, first, second_item, type_code))

def scan_qty(pair: Array<U32> & U32, +at: U32,
    index: U32, mode: U32, first: U32, second_item: U32,
    type_code: U32, acc: Check) -> Array<U32> & Check:
  (a, quantity) = pair
  scan_mask(a[(at + 1 : U32)], quantity, index, mode, first,
    second_item, type_code, acc)

def scan(n: Nat, +mode: U32, +first: U32,
    +second_item: U32, +type_code: U32,
    +base: U32, pair: Array<U32> & Check) -> Array<U32> & Check:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, acc) = pair
      +index = U32.from_nat(p)
      +at = (base + 64 + index * 128 : U32)
      scan(p, mode, first, second_item, type_code, base,
        scan_qty(a[at], at, index, mode, first, second_item, type_code, acc))

def finish(acc: Check, a: Array<U32>, mode: U32,
    required: U32, s: F.State, +elapsed: U32,
    +deadline: U32, +base: U32) -> Array<U32>:
  match acc:
    case C{valid, total}:
      save(F.step(s, F.Submit{F.accepted(mode, valid, total, required)},
        elapsed, deadline), elapsed, deadline, base, a)

def finish_pair(pair: Array<U32> & Check, mode: U32,
    required: U32, s: F.State, elapsed: U32,
    deadline: U32, +base: U32) -> Array<U32>:
  (a, acc) = pair
  finish(acc, a, mode, required, s, elapsed, deadline, base)

def submit(+mode: U32, +first: U32, +second_item: U32,
    +type_code: U32, required: U32, s: F.State,
    elapsed: U32, deadline: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  finish_pair(scan(12n, mode, first, second_item, type_code,
    base, (a, C{True{}, 0})), mode, required, s, elapsed, deadline, base)

def changed(pair: Array<U32> & U32, remove: Bool, +at: U32,
    s: F.State, +elapsed: U32, +deadline: U32,
    +base: U32) -> Array<U32>:
  (a, quantity) = pair
  save(F.step(s, F.Wait{}, elapsed, deadline), elapsed, deadline,
    base, a[at] <- F.adjust(remove, quantity))

def change_allowed(allowed: Bool, +ref: U32,
    s: F.State, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match allowed:
    case True{}:
      +at = (base + 64 + ((ref / 16 : U32) - 1) * 128 : U32)
      changed(a[at], U32.is_eq((ref % 16 : U32), 0), at,
        s, elapsed, deadline, base)
    case False{}:
      save(F.step(s, F.Wait{}, elapsed, deadline), elapsed, deadline, base, a)

def dispatch_submit(is_submit: Bool, ref: U32,
    mode: U32, first: U32, second_item: U32,
    type_code: U32, required: U32, allowed: Bool,
    s: F.State, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match is_submit:
    case True{}:
      submit(mode, first, second_item, type_code, required,
        s, elapsed, deadline, base, a)
    case False{}:
      change_allowed(allowed, ref, s, elapsed, deadline, base, a)

def dispatch_zero(zero: Bool, +ref: U32,
    mode: U32, first: U32, second_item: U32,
    type_code: U32, required: U32, allowed: Bool,
    s: F.State, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match zero:
    case True{}: save(F.step(s, F.Wait{}, elapsed, deadline), elapsed, deadline, base, a)
    case False{}:
      dispatch_submit(U32.is_eq(ref, 1), ref, mode, first, second_item,
        type_code, required, allowed, s, elapsed, deadline, base, a)

def ready(a: Array<U32>, +status: U32, raw: U32,
    +elapsed: U32, +ref: U32, mode: U32,
    first: U32, second_item: U32, type_code: U32,
    required: U32, +deadline: U32, +base: U32) -> Array<U32>:
  s = {F.S{outcome(status, raw)} : F.State}
  dispatch_zero(U32.is_eq(ref, 0), ref, mode, first, second_item,
    type_code, required, U32.is_eq(status, 0) &&
    U32.is_lt(elapsed, deadline), s, elapsed, deadline, base, a)

def load_required(pair: Array<U32> & U32, status: U32, raw: U32,
    elapsed: U32, ref: U32, mode: U32, first: U32,
    second_item: U32, type_code: U32, deadline: U32,
    +base: U32) -> Array<U32>:
  (a, required) = pair
  ready(a, status, raw, elapsed, ref, mode, first, second_item,
    type_code, required, deadline, base)

def load_type(pair: Array<U32> & U32, status: U32, raw: U32,
    elapsed: U32, ref: U32, mode: U32, first: U32,
    second_item: U32, deadline: U32, +base: U32) -> Array<U32>:
  (a, type_code) = pair
  load_required(a[(base + 36 : U32)], status, raw, elapsed, ref,
    mode, first, second_item, type_code, deadline, base)

def load_second(pair: Array<U32> & U32, status: U32, raw: U32,
    elapsed: U32, ref: U32, mode: U32, first: U32,
    deadline: U32, +base: U32) -> Array<U32>:
  (a, second_item) = pair
  load_type(a[(base + 35 : U32)], status, raw, elapsed, ref,
    mode, first, second_item, deadline, base)

def load_first(pair: Array<U32> & U32, status: U32, raw: U32,
    elapsed: U32, ref: U32, mode: U32,
    deadline: U32, +base: U32) -> Array<U32>:
  (a, first) = pair
  load_second(a[(base + 34 : U32)], status, raw, elapsed,
    ref, mode, first, deadline, base)

def load_mode(pair: Array<U32> & U32, status: U32, raw: U32,
    elapsed: U32, ref: U32, deadline: U32,
    +base: U32) -> Array<U32>:
  (a, mode) = pair
  load_first(a[(base + 33 : U32)], status, raw, elapsed,
    ref, mode, deadline, base)

def load_ref(pair: Array<U32> & U32, status: U32, raw: U32,
    elapsed: U32, deadline: U32, +base: U32) -> Array<U32>:
  (a, ref) = pair
  load_mode(a[(base + 32 : U32)], status, raw, elapsed, ref, deadline, base)

def load_elapsed(pair: Array<U32> & U32, status: U32, raw: U32,
    deadline: U32, +base: U32) -> Array<U32>:
  (a, elapsed) = pair
  load_ref(a[(base + 5 : U32)], status, raw, elapsed, deadline, base)

def load_deadline(pair: Array<U32> & U32, status: U32,
    raw: U32, +base: U32) -> Array<U32>:
  (a, deadline) = pair
  load_elapsed(a[(base + 8 : U32)], status, raw, deadline, base)

def load_raw(pair: Array<U32> & U32, status: U32,
    +base: U32) -> Array<U32>:
  (a, raw) = pair
  load_deadline(a[(base + 12 : U32)], status, raw, base)

def load_status(pair: Array<U32> & U32, +base: U32) -> Array<U32>:
  (a, status) = pair
  load_raw(a[(base + 10 : U32)], status, base)

def run(+base: U32, a: Array<U32>) -> Array<U32>:
  load_status(a[(base + 9 : U32)], base)

def reset_seed(pair: Array<U32> & U32, +base: U32) -> Array<U32>:
  (a, seed) = pair
  G.generate(1, seed, base, a)

def reset(+base: U32, a: Array<U32>) -> Array<U32>:
  reset_seed(a[(base + 3 : U32)], base)
