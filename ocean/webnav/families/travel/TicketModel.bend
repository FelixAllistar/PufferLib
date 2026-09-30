import Base
import ./Model.bend as F

type Outcome is Data:
  Running{}
  Finished{grade: U32}
  TimedOut{}

type State is Data:
  S{criterion: Nat, flights: +List<F.Flight>, outcome: Outcome}

type Action is Data:
  Wait{}
  Buy{index: Nat}

def is_duration(+criterion: Nat) -> Bool:
  Nat.is_eq(criterion, 1n) || Nat.is_eq(criterion, 2n)

def want_max(+criterion: Nat) -> Bool:
  Nat.is_eq(criterion, 2n) || Nat.is_eq(criterion, 3n)

def maximum(+duration: Bool, flights: +List<F.Flight>, +best: U32) -> U32:
  match flights:
    case Nil{}: best
    case Con{flight, rest}:
      +score = F.metric(duration, flight)
      maximum(duration, rest, Bool.pick(U32, U32.is_gt(score, best), score, best))

def rank_values(maximum_wanted: Bool, +score: U32, +low: U32, +high: U32) -> U32:
  match maximum_wanted:
    case True{}:
      Bool.pick(U32, U32.is_eq(score, high), 0,
        Bool.pick(U32, U32.is_gt(score, low), 1, 2))
    case False{}:
      Bool.pick(U32, U32.is_eq(score, low), 0,
        Bool.pick(U32, U32.is_lt(score, high), 1, 2))

def rank_checked(valid: Bool, +criterion: Nat,
    +flights: +List<F.Flight>, index: Nat) -> U32:
  match valid:
    case False{}: 2
    case True{}:
      +duration = is_duration(criterion)
      +score = F.metric_at(duration, flights, index)
      +low = F.minimum(duration, flights, 4294967295)
      +high = maximum(duration, flights, 0)
      rank_values(want_max(criterion), score, low, high)

def rank(criterion: Nat, +flights: +List<F.Flight>, +index: Nat) -> U32:
  rank_checked(Nat.is_lt(index, List.length(&2, F.Flight, flights)),
    criterion, flights, index)

def buy(s: State, index: Nat) -> State:
  S{+criterion, +flights, outcome} = s
  S{criterion, flights, Finished{rank(criterion, flights, index)}}

def active(action: Action, s: State) -> State:
  match action:
    case Wait{}: s
    case Buy{index}: buy(s, index)

def guard(expired: Bool, s: State, action: Action) -> State:
  match expired s:
    case True{} S{criterion, flights, outcome}:
      S{criterion, flights, TimedOut{}}
    case False{} _: active(action, s)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{criterion, flights, Running{}}:
      guard(U32.is_ge(elapsed, deadline),
        S{criterion, flights, Running{}}, action)
    case S{criterion, flights, Finished{grade}}:
      S{criterion, flights, Finished{grade}}
    case S{criterion, flights, TimedOut{}}:
      S{criterion, flights, TimedOut{}}

def raw(outcome: Outcome) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{+grade}:
      Bool.pick(F32, U32.is_eq(grade, 0), 1.0,
        Bool.pick(F32, U32.is_eq(grade, 1), (0.0 - 0.5 : F32),
          (0.0 - 1.0 : F32)))
    case TimedOut{}: (0.0 - 1.0 : F32)

def timed(outcome: Outcome, elapsed: U32, deadline: U32) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{+grade}:
      Bool.pick(F32, U32.is_eq(grade, 0),
        F32.max(0.0, (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32)),
        Bool.pick(F32, U32.is_eq(grade, 1), (0.0 - 0.5 : F32),
          (0.0 - 1.0 : F32)))
    case TimedOut{}: (0.0 - 1.0 : F32)
