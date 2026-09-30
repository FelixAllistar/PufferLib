import Base

type Flight is Data:
  F{price: U32, duration: U32}

type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

# Airports are public catalog indices, 1..6; 0 is blank and 7 is invalid.
# Date 1..91 represents generated targets Oct 1 through Dec 30, 2016;
# 92 is the selectable Dec 31 boundary. Search only checks nonblank date.
type State is Data:
  S{origin: U32, destination: U32, date: U32,
    goal_origin: U32, goal_destination: U32, goal_date: U32,
    shortest: Bool, flights: +List<Flight>, phase: Nat,
    errors: U32, fake: Bool, outcome: Outcome}

type Action is Data:
  Wait{}
  SetOrigin{value: U32}
  SetDestination{value: U32}
  SetDate{value: U32}
  Search{}
  Back{}
  Book{index: Nat}

def valid_airport(+value: U32) -> Bool:
  U32.is_gt(value, 0) && U32.is_le(value, 6)

def valid_date(value: U32) -> Bool:
  U32.is_gt(value, 0)

def error_mask(origin: U32, destination: U32, date: U32) -> U32:
  (Bool.pick(U32, valid_airport(origin), 0, 1) +
    Bool.pick(U32, valid_airport(destination), 0, 2) +
    Bool.pick(U32, valid_date(date), 0, 4) : U32)

def metric(shortest: Bool, flight: Flight) -> U32:
  F{price, duration} = flight
  Bool.pick(U32, shortest, duration, price)

def minimum(+shortest: Bool, flights: +List<Flight>, +best: U32) -> U32:
  match flights:
    case Nil{}: best
    case Con{flight, rest}:
      +score = metric(shortest, flight)
      minimum(shortest, rest, Bool.pick(U32, U32.is_lt(score, best), score, best))

def metric_at(shortest: Bool, flights: +List<Flight>, index: Nat) -> U32:
  match flights index:
    case Nil{} _: 0
    case Con{flight, rest} 0n: metric(shortest, flight)
    case Con{flight, rest} 1n+p: metric_at(shortest, rest, p)

def book_success(fake: Bool, +shortest: Bool, +flights: +List<Flight>, +index: Nat) -> Bool:
  Bool.not(fake) && Nat.is_lt(index, List.length(&2, Flight, flights)) &&
    U32.is_eq(metric_at(shortest, flights, index),
      minimum(shortest, flights, 4294967295))

def set_origin(s: State, value: U32) -> State:
  S{origin, destination, date, go, gd, gt, shortest, flights, phase, errors, fake, outcome} = s
  S{value, destination, date, go, gd, gt, shortest, flights, phase, errors, fake, outcome}

def set_destination(s: State, value: U32) -> State:
  S{origin, destination, date, go, gd, gt, shortest, flights, phase, errors, fake, outcome} = s
  S{origin, value, date, go, gd, gt, shortest, flights, phase, errors, fake, outcome}

def set_date(s: State, value: U32) -> State:
  S{origin, destination, date, go, gd, gt, shortest, flights, phase, errors, fake, outcome} = s
  S{origin, destination, value, go, gd, gt, shortest, flights, phase, errors, fake, outcome}

def search(s: State) -> State:
  S{+origin, +destination, +date, +go, +gd, +gt,
    shortest, flights, phase, errors, fake, outcome} = s
  +bad = error_mask(origin, destination, date)
  +wrong = Bool.not(U32.is_eq(origin, go) && U32.is_eq(destination, gd) && U32.is_eq(date, gt))
  S{origin, destination, date, go, gd, gt, shortest, flights,
    Bool.pick(Nat, U32.is_eq(bad, 0), 1n, 0n), bad, wrong, outcome}

def back(s: State) -> State:
  S{origin, destination, date, go, gd, gt, shortest, flights, phase, errors, fake, outcome} = s
  S{origin, destination, date, go, gd, gt, shortest, flights, 0n, 0, fake, outcome}

def book(s: State, index: Nat) -> State:
  S{origin, destination, date, go, gd, gt, +shortest, +flights, phase, errors, +fake, outcome} = s
  S{origin, destination, date, go, gd, gt, shortest, flights, phase, errors, fake,
    Finished{book_success(fake, shortest, flights, index)}}

def active(action: Action, s: State) -> State:
  match action:
    case Wait{}: s
    case SetOrigin{value}: set_origin(s, value)
    case SetDestination{value}: set_destination(s, value)
    case SetDate{value}: set_date(s, value)
    case Search{}: search(s)
    case Back{}: back(s)
    case Book{index}: book(s, index)

def guard(expired: Bool, s: State, action: Action) -> State:
  match expired s:
    case True{} S{origin, destination, date, go, gd, gt, shortest, flights, phase, errors, fake, outcome}:
      S{origin, destination, date, go, gd, gt, shortest, flights, phase, errors, fake, TimedOut{}}
    case False{} _: active(action, s)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{origin, destination, date, go, gd, gt, shortest, flights, phase, errors, fake, Running{}}:
      guard(U32.is_ge(elapsed, deadline),
        S{origin, destination, date, go, gd, gt, shortest, flights, phase, errors, fake, Running{}}, action)
    case S{origin, destination, date, go, gd, gt, shortest, flights, phase, errors, fake, Finished{success}}:
      S{origin, destination, date, go, gd, gt, shortest, flights, phase, errors, fake, Finished{success}}
    case S{origin, destination, date, go, gd, gt, shortest, flights, phase, errors, fake, TimedOut{}}:
      S{origin, destination, date, go, gd, gt, shortest, flights, phase, errors, fake, TimedOut{}}

def raw(outcome: Outcome) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{True{}}: 1.0
    case Finished{False{}}: (0.0 - 1.0 : F32)
    case TimedOut{}: (0.0 - 1.0 : F32)

def timed(outcome: Outcome, elapsed: U32, deadline: U32) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{True{}}: F32.max(0.0, (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32))
    case Finished{False{}}: (0.0 - 1.0 : F32)
    case TimedOut{}: (0.0 - 1.0 : F32)
