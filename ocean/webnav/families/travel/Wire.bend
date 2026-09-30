import Base
import ./Model.bend as M
import ./Generate.bend as G
import ./TicketGenerate.bend as TG
import ./TicketWire.bend as TW

def outcome(status: U32, raw: U32) -> M.Outcome:
  match status:
    case 0: M.Running{}
    case 1: M.Finished{U32.is_eq(raw, 1065353216)}
    case _: M.TimedOut{}

def read_duration(price: U32, pair: Array<U32> & U32) -> Array<U32> & M.Flight:
  (a, duration) = pair
  (a, M.F{price, duration})

def read_price(+at: U32, pair: Array<U32> & U32) -> Array<U32> & M.Flight:
  (a, price) = pair
  read_duration(price, a[(at + 1 : U32)])

def read_flight(+at: U32, a: Array<U32>) -> Array<U32> & M.Flight:
  read_price(at, a[at])

def prepend(pair: Array<U32> & M.Flight,
    rest: +List<M.Flight>) -> Array<U32> & +List<M.Flight>:
  (a, flight) = pair
  (a, flight <> rest)

def read_flights(n: Nat, +base: U32,
    pair: Array<U32> & +List<M.Flight>) -> Array<U32> & +List<M.Flight>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, flights) = pair
      +at = (base + 64 + U32.from_nat(p) * 4 : U32)
      read_flights(p, base, prepend(read_flight(at, a), flights))

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

def save(s: M.State, elapsed: U32, deadline: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  M.S{origin, destination, date, go, gd, gt, shortest, flights,
    phase, errors, fake, o} = s
  a[(base + 32 : U32)] <- origin
  a[(base + 33 : U32)] <- destination
  a[(base + 34 : U32)] <- date
  a[(base + 39 : U32)] <- U32.from_nat(phase)
  a[(base + 41 : U32)] <- errors
  a[(base + 42 : U32)] <- Bool.to_u32(fake)
  a[(base + 2 : U32)] <- 0
  save_outcome(o, elapsed, deadline, base, a)

def click(target: U32) -> M.Action:
  match target:
    case 4: M.Search{}
    case 5: M.Back{}
    case _: M.Book{U32.to_nat((target - 6 : U32))}

def select(target: U32, value: U32) -> M.Action:
  match target:
    case 1: M.SetOrigin{value}
    case 2: M.SetDestination{value}
    case _: M.SetDate{value}

def action(kind: U32, target: U32, value: U32) -> M.Action:
  match kind:
    case 21: select(target, value)
    case 1: click(target)
    case _: M.Wait{}

def ready(+base: U32, command: U32, target: U32, value: U32,
    +elapsed: U32, status: U32, raw: U32, +deadline: U32,
    origin: U32, destination: U32, date: U32, go: U32, gd: U32, gt: U32,
    shortest: U32, phase: U32, errors: U32, fake: U32,
    flights: +List<M.Flight>, a: Array<U32>) -> Array<U32>:
  s = {M.S{origin, destination, date, go, gd, gt, U32.is_eq(shortest, 1),
    flights, U32.to_nat(phase), errors, U32.is_eq(fake, 1),
    outcome(status, raw)} : M.State}
  save(M.step(s, action(command, target, value), elapsed, deadline),
    elapsed, deadline, base, a)

def flights_loaded(base: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    origin: U32, destination: U32, date: U32, go: U32, gd: U32, gt: U32,
    shortest: U32, phase: U32, errors: U32, fake: U32,
    pair: Array<U32> & +List<M.Flight>) -> Array<U32>:
  (a, flights) = pair
  ready(base, command, target, value, elapsed, status, raw, deadline,
    origin, destination, date, go, gd, gt, shortest, phase, errors, fake,
    flights, a)

def load_count(+base: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    origin: U32, destination: U32, date: U32, go: U32, gd: U32, gt: U32,
    shortest: U32, phase: U32, errors: U32, fake: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, count) = pair
  flights_loaded(base, command, target, value, elapsed, status, raw, deadline,
    origin, destination, date, go, gd, gt, shortest, phase, errors, fake,
    read_flights(U32.to_nat(count), base, (a, Nil{})))

def load_fake(+base: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    origin: U32, destination: U32, date: U32, go: U32, gd: U32, gt: U32,
    shortest: U32, phase: U32, errors: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, fake) = pair
  load_count(base, command, target, value, elapsed, status, raw, deadline,
    origin, destination, date, go, gd, gt, shortest, phase, errors, fake,
    a[(base + 40 : U32)])

def load_errors(+base: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    origin: U32, destination: U32, date: U32, go: U32, gd: U32, gt: U32,
    shortest: U32, phase: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, errors) = pair
  load_fake(base, command, target, value, elapsed, status, raw, deadline,
    origin, destination, date, go, gd, gt, shortest, phase, errors,
    a[(base + 42 : U32)])

def load_phase(+base: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    origin: U32, destination: U32, date: U32, go: U32, gd: U32, gt: U32,
    shortest: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, phase) = pair
  load_errors(base, command, target, value, elapsed, status, raw, deadline,
    origin, destination, date, go, gd, gt, shortest, phase,
    a[(base + 41 : U32)])

def load_shortest(+base: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    origin: U32, destination: U32, date: U32, go: U32, gd: U32, gt: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, shortest) = pair
  load_phase(base, command, target, value, elapsed, status, raw, deadline,
    origin, destination, date, go, gd, gt, shortest,
    a[(base + 39 : U32)])

def load_gt(+base: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    origin: U32, destination: U32, date: U32, go: U32, gd: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, gt) = pair
  load_shortest(base, command, target, value, elapsed, status, raw, deadline,
    origin, destination, date, go, gd, gt,
    a[(base + 38 : U32)])

def load_gd(+base: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    origin: U32, destination: U32, date: U32, go: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, gd) = pair
  load_gt(base, command, target, value, elapsed, status, raw, deadline,
    origin, destination, date, go, gd, a[(base + 37 : U32)])

def load_go(+base: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    origin: U32, destination: U32, date: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, go) = pair
  load_gd(base, command, target, value, elapsed, status, raw, deadline,
    origin, destination, date, go, a[(base + 36 : U32)])

def load_date(+base: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    origin: U32, destination: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, date) = pair
  load_go(base, command, target, value, elapsed, status, raw, deadline,
    origin, destination, date, a[(base + 35 : U32)])

def load_destination(+base: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32, origin: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, destination) = pair
  load_date(base, command, target, value, elapsed, status, raw, deadline,
    origin, destination, a[(base + 34 : U32)])

def load_origin(+base: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32, deadline: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, origin) = pair
  load_destination(base, command, target, value, elapsed, status, raw, deadline,
    origin, a[(base + 33 : U32)])

def load_deadline(+base: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, raw: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, deadline) = pair
  load_origin(base, command, target, value, elapsed, status, raw, deadline,
    a[(base + 32 : U32)])

def load_raw(+base: U32, command: U32, target: U32, value: U32,
    elapsed: U32, status: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, raw) = pair
  load_deadline(base, command, target, value, elapsed, status, raw,
    a[(base + 12 : U32)])

def load_status(+base: U32, command: U32, target: U32, value: U32,
    elapsed: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  load_raw(base, command, target, value, elapsed, status,
    a[(base + 10 : U32)])

def load_elapsed(+base: U32, command: U32, target: U32, value: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, elapsed) = pair
  load_status(base, command, target, value, elapsed,
    a[(base + 9 : U32)])

def load_value(+base: U32, command: U32, target: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, value) = pair
  load_elapsed(base, command, target, value, a[(base + 8 : U32)])

def load_target(+base: U32, command: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, target) = pair
  load_value(base, command, target, a[(base + 6 : U32)])

def load_command(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, command) = pair
  load_target(base, command, a[(base + 5 : U32)])

def reset_choice(kind: Nat, task: U32, seed: U32,
    base: U32, a: Array<U32>) -> Array<U32>:
  match kind:
    case 2n: TG.generate(task, seed, base, a)
    case _: G.generate(task, seed, base, a)

def reset_seed(+task: U32, +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  reset_choice(U32.to_nat(task), task, seed, base, a)

def reset_task(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, task) = pair
  reset_seed(task, base, a[(base + 3 : U32)])

def step_choice(kind: Nat, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match kind:
    case 2n: TW.run(base, a)
    case _: load_command(base, a[(base + 4 : U32)])

def step_task(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, task) = pair
  step_choice(U32.to_nat(task), base, a)

def operation(op: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match op:
    case 0: a
    case 1: reset_task(base, a[(base + 1 : U32)])
    case _: step_task(base, a[(base + 1 : U32)])

def row(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, op) = pair
  operation(op, base, a)

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +base = (U32.from_nat(p) * 8192 : U32)
      batch(p, row(base, a[(base + 2 : U32)]))
