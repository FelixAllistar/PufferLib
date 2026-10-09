import Base
import ./Model.bend as M

# Private transport, 64 words per lane, distinct from WFView/WFFamily v2.
# 0 route, 1 count, 2 latency_ms, 3 failures_left, 4 phase, 5 remaining_ms,
# 6 back length, 7 forward length, 8 command, 9 target, 10 elapsed delta,
# 11 preset version=1, 12 operation, 13 reset seed;
# back at 16..23, forward at 24..31.
# Valid state: count 1..32, route/history entries < count, phase 0..2.
# Caller validates before entry; public observations never contain this row.

def word(xs: +List<U32>, index: Nat) -> U32:
  match xs index:
    case Nil{} _: 0
    case Con{x, rest} 0n: x
    case Con{x, rest} 1n+p: word(rest, p)

def prepend(pair: Array<U32> & U32, xs: +List<U32>) -> Array<U32> & +List<U32>:
  (a, x) = pair
  (a, x <> xs)

def read_words(n: Nat, +base: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, xs) = pair
      read_words(p, base, prepend(a[(base + U32.from_nat(p) : U32)], xs))

def phase(code: Nat, remaining: U32) -> M.Phase:
  match code:
    case 0n: M.Ready{}
    case 1n: M.Loading{remaining}
    case _: M.Failed{}

def command(code: Nat, target: U32) -> M.Action:
  match code:
    case 1n: M.Navigate{target}
    case 2n: M.Back{}
    case 3n: M.Forward{}
    case 4n: M.Retry{}
    case 5n: M.Reload{}
    case _: M.Wait{}

def load(+xs: +List<U32>) -> M.State:
  +back = List.take(&2, U32, List.drop(&2, U32, xs, 16n), Nat.min(U32.to_nat(word(xs, 6n)), 8n))
  +forward = List.take(&2, U32, List.drop(&2, U32, xs, 24n), Nat.min(U32.to_nat(word(xs, 7n)), 8n))
  M.S{word(xs, 0n), word(xs, 1n), word(xs, 2n), word(xs, 3n), back, forward,
    phase(U32.to_nat(word(xs, 4n)), word(xs, 5n))}

def phase_code(phase: M.Phase) -> U32:
  match phase:
    case M.Ready{}: 0
    case M.Loading{remaining}: 1
    case M.Failed{}: 2

def remaining_ms(phase: M.Phase) -> U32:
  match phase:
    case M.Loading{remaining}: remaining
    case _: 0

def clear(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: clear(p, base, a[(base + U32.from_nat(p) : U32)] <- 0)

def write_words(xs: +List<U32>, +at: U32, a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{x, rest}: write_words(rest, (at + 1 : U32), a[at] <- x)

def save(s: M.State, +base: U32, a: Array<U32>) -> Array<U32>:
  M.S{route, count, latency, failures, +back, +forward, +phase_} = s
  a = a[base] <- route
  a = a[(base + 1 : U32)] <- count
  a = a[(base + 2 : U32)] <- latency
  a = a[(base + 3 : U32)] <- failures
  a = a[(base + 4 : U32)] <- phase_code(phase_)
  a = a[(base + 5 : U32)] <- remaining_ms(phase_)
  a = a[(base + 6 : U32)] <- U32.from_nat(List.length(&2, U32, back))
  a = a[(base + 7 : U32)] <- U32.from_nat(List.length(&2, U32, forward))
  a = clear(3n, (base + 8 : U32), a)
  a = a[(base + 11 : U32)] <- 1
  a = clear(2n, (base + 12 : U32), a)
  a = clear(16n, (base + 16 : U32), a)
  write_words(forward, (base + 24 : U32), write_words(back, (base + 16 : U32), a))

def apply_row(+base: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, +xs) = pair
  save(M.step(load(xs), command(U32.to_nat(word(xs, 8n)), word(xs, 9n)), word(xs, 10n)), base, a)

def step_lane(+base: U32, a: Array<U32>) -> Array<U32>:
  apply_row(base, read_words(32n, base, (a, Nil{})))

# No goal-dependent masks. Readiness gates page interactions outside this unit.
# Only public navigation facts; failure schedule/latency/countdown stay private.
def public_state(s: M.State) -> +List<U32>:
  M.S{route, count, latency, failures, back, forward, phase_} = s
  [route, phase_code(phase_), Bool.to_u32(Nat.is_gt(List.length(&2, U32, back), 0n)),
    Bool.to_u32(Nat.is_gt(List.length(&2, U32, forward), 0n))]

# Independently salted infrastructure variation. No benchmark goals or data.
def reset(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +count = (2 + seed % 7 : U32)
  +latency = (25 + (seed .^. 2654435769 : U32) % 7 * 25 : U32)
  +failures = ((seed .^. 2246822507 : U32) % 3 : U32)
  save(M.S{0, count, latency, failures, Nil{}, Nil{}, M.Ready{}}, base, clear(64n, base, a))

def dispatch(operation: Nat, seed: U32, base: U32, a: Array<U32>) -> Array<U32>:
  match operation:
    case 1n: reset(seed, base, a)
    case 2n: step_lane(base, a)
    case _: a

def with_seed(operation: U32, base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  dispatch(U32.to_nat(operation), seed, base, a)

def with_operation(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, operation) = pair
  with_seed(operation, base, a[(base + 13 : U32)])

def row(+base: U32, a: Array<U32>) -> Array<U32>:
  with_operation(base, a[(base + 12 : U32)])
