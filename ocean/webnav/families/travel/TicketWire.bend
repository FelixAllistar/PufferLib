import Base
import ./Model.bend as F
import ./TicketModel.bend as M

def outcome(status: U32, +raw: U32) -> M.Outcome:
  match status:
    case 0: M.Running{}
    case 1:
      M.Finished{Bool.pick(U32, U32.is_eq(raw, 1065353216), 0,
        Bool.pick(U32, U32.is_eq(raw, 3204448256), 1, 2))}
    case _: M.TimedOut{}

def duration(price: U32, pair: Array<U32> & U32) -> Array<U32> & F.Flight:
  (a, ms) = pair
  (a, F.F{price, ms})

def price(+at: U32, pair: Array<U32> & U32) -> Array<U32> & F.Flight:
  (a, dollars) = pair
  duration(dollars, a[(at + 1 : U32)])

def flight(+at: U32, a: Array<U32>) -> Array<U32> & F.Flight:
  price(at, a[at])

def prepend(pair: Array<U32> & F.Flight,
    rest: +List<F.Flight>) -> Array<U32> & +List<F.Flight>:
  (a, value) = pair
  (a, value <> rest)

def flights(n: Nat, +base: U32,
    pair: Array<U32> & +List<F.Flight>) -> Array<U32> & +List<F.Flight>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, rest) = pair
      +at = (base + 64 + U32.from_nat(p) * 4 : U32)
      flights(p, base, prepend(flight(at, a), rest))

def save_outcome(outcome: M.Outcome, +elapsed: U32,
    +deadline: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match outcome:
    case M.Running{}: a
    case M.Finished{+grade}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(M.raw(M.Finished{grade}))
      a[(base + 11 : U32)] <- F32.bits(M.timed(M.Finished{grade}, elapsed, deadline))
    case M.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(s: M.State, elapsed: U32, deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  M.S{criterion, items, result} = s
  a[(base + 2 : U32)] <- 0
  save_outcome(result, elapsed, deadline, base, a)

def action(command: U32, target: U32) -> M.Action:
  match command:
    case 1: M.Buy{U32.to_nat((target - 1 : U32))}
    case _: M.Wait{}

def ready(+base: U32, command: U32, target: U32,
    +elapsed: U32, status: U32, raw: U32, +deadline: U32,
    criterion: U32, values: +List<F.Flight>, a: Array<U32>) -> Array<U32>:
  s = {M.S{U32.to_nat(criterion), values, outcome(status, raw)} : M.State}
  save(M.step(s, action(command, target), elapsed, deadline),
    elapsed, deadline, base, a)

def loaded(base: U32, command: U32, target: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    criterion: U32, pair: Array<U32> & +List<F.Flight>) -> Array<U32>:
  (a, values) = pair
  ready(base, command, target, elapsed, status, raw, deadline,
    criterion, values, a)

def load_criterion(+base: U32, command: U32, target: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, criterion) = pair
  loaded(base, command, target, elapsed, status, raw, deadline,
    criterion, flights(4n, base, (a, Nil{})))

def load_deadline(+base: U32, command: U32, target: U32,
    elapsed: U32, status: U32, raw: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, deadline) = pair
  load_criterion(base, command, target, elapsed, status, raw, deadline,
    a[(base + 32 : U32)])

def load_raw(+base: U32, command: U32, target: U32,
    elapsed: U32, status: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, raw) = pair
  load_deadline(base, command, target, elapsed, status, raw,
    a[(base + 12 : U32)])

def load_status(+base: U32, command: U32, target: U32,
    elapsed: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  load_raw(base, command, target, elapsed, status,
    a[(base + 10 : U32)])

def load_elapsed(+base: U32, command: U32, target: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, elapsed) = pair
  load_status(base, command, target, elapsed,
    a[(base + 9 : U32)])

def load_target(+base: U32, command: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, target) = pair
  load_elapsed(base, command, target, a[(base + 8 : U32)])

def load_command(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, command) = pair
  load_target(base, command, a[(base + 5 : U32)])

def run(+base: U32, a: Array<U32>) -> Array<U32>:
  load_command(base, a[(base + 4 : U32)])
