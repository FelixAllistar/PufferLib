import Base

# An atomic native time-input value is empty or exact ASCII HH:MM.
type Value is Data:
  Empty{}
  Time{hour: U32, minute: U32}

type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{goal_hour: U32, goal_minute: U32, value: Value, outcome: Outcome}

type Action is Data:
  Wait{}
  SetValue{units: +List<U32>}
  Submit{}

def digit(+ch: U32) -> Bool:
  U32.is_ge(ch, 48) && U32.is_le(ch, 57)

def decimal(ch: U32) -> U32:
  (ch - 48 : U32)

def parsed(+a: U32, +b: U32, sep: U32, +c: U32, +d: U32) -> Value:
  +hour = (decimal(a) * 10 + decimal(b) : U32)
  +minute = (decimal(c) * 10 + decimal(d) : U32)
  +shape = digit(a) && digit(b) && U32.is_eq(sep, 58) && digit(c) && digit(d)
  Bool.pick(Value, shape && U32.is_lt(hour, 24) && U32.is_lt(minute, 60),
    Time{hour, minute}, Empty{})

def parse(units: +List<U32>) -> Value:
  match units:
    case Con{+a, Con{+b, Con{sep, Con{+c, Con{+d, Nil{}}}}}}:
      parsed(a, b, sep, c, d)
    case _: Empty{}

def equal(goal_hour: U32, goal_minute: U32, value: Value) -> Bool:
  match value:
    case Empty{}: False{}
    case Time{hour, minute}:
      U32.is_eq(goal_hour, hour) && U32.is_eq(goal_minute, minute)

def act(s: State, action: Action) -> State:
  match s action:
    case S{hour, minute, value, outcome} Wait{}:
      S{hour, minute, value, outcome}
    case S{hour, minute, value, outcome} SetValue{units}:
      S{hour, minute, parse(units), outcome}
    case S{+hour, +minute, +value, outcome} Submit{}:
      S{hour, minute, value, Finished{equal(hour, minute, value)}}

def guarded(expired: Bool, s: State, action: Action) -> State:
  match expired:
    case True{}:
      S{hour, minute, value, outcome} = s
      S{hour, minute, value, TimedOut{}}
    case False{}: act(s, action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{hour, minute, value, Running{}}:
      guarded(U32.is_ge(elapsed, deadline), S{hour, minute, value, Running{}}, action)
    case S{hour, minute, value, Finished{success}}:
      S{hour, minute, value, Finished{success}}
    case S{hour, minute, value, TimedOut{}}:
      S{hour, minute, value, TimedOut{}}

def raw(outcome: Outcome) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{True{}}: 1.0
    case Finished{False{}}: (0.0 - 1.0 : F32)
    case TimedOut{}: (0.0 - 1.0 : F32)

def timed(outcome: Outcome, elapsed: U32, deadline: U32) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{True{}}: (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32)
    case Finished{False{}}: (0.0 - 1.0 : F32)
    case TimedOut{}: (0.0 - 1.0 : F32)
