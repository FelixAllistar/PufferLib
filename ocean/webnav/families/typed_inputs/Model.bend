import Base

# Atomic HTML date-input values are ASCII YYYY-MM-DD. Unsupported or invalid
# values normalize to empty, matching the native control's value property.
type Value is Data:
  Empty{}
  Date{year: U32, month: U32, day: U32}

type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{goal_year: U32, goal_month: U32, goal_day: U32,
    value: Value, outcome: Outcome}

type Action is Data:
  Wait{}
  SetValue{units: +List<U32>}
  Submit{}

def digit(+ch: U32) -> Bool:
  U32.is_ge(ch, 48) && U32.is_le(ch, 57)

def decimal(ch: U32) -> U32:
  (ch - 48 : U32)

def leap(+year: U32) -> Bool:
  U32.is_eq((year % 400 : U32), 0) ||
    (U32.is_eq((year % 4 : U32), 0) && Bool.not(U32.is_eq((year % 100 : U32), 0)))

def month_days(month: U32, year: U32) -> U32:
  match month:
    case 2: Bool.pick(U32, leap(year), 29, 28)
    case 4: 30
    case 6: 30
    case 9: 30
    case 11: 30
    case _: 31

def valid_date(+year: U32, +month: U32, +day: U32) -> Bool:
  U32.is_gt(year, 0) && U32.is_ge(month, 1) && U32.is_le(month, 12) &&
    U32.is_ge(day, 1) && U32.is_le(day, month_days(month, year))

def parsed(+a: U32, +b: U32, +c: U32, +d: U32, sep1: U32,
           +e: U32, +f: U32, sep2: U32, +g: U32, +h: U32) -> Value:
  +year = (decimal(a) * 1000 + decimal(b) * 100 + decimal(c) * 10 + decimal(d) : U32)
  +month = (decimal(e) * 10 + decimal(f) : U32)
  +day = (decimal(g) * 10 + decimal(h) : U32)
  +shape = digit(a) && digit(b) && digit(c) && digit(d) &&
    U32.is_eq(sep1, 45) && digit(e) && digit(f) && U32.is_eq(sep2, 45) &&
    digit(g) && digit(h)
  Bool.pick(Value, shape && valid_date(year, month, day),
    Date{year, month, day}, Empty{})

def parse(units: +List<U32>) -> Value:
  match units:
    case Con{+a, Con{+b, Con{+c, Con{+d, Con{sep1,
         Con{+e, Con{+f, Con{sep2, Con{+g, Con{+h, Nil{}}}}}}}}}}}:
      parsed(a, b, c, d, sep1, e, f, sep2, g, h)
    case _: Empty{}

def equal(goal_year: U32, goal_month: U32, goal_day: U32, value: Value) -> Bool:
  match value:
    case Empty{}: False{}
    case Date{year, month, day}:
      U32.is_eq(goal_year, year) && U32.is_eq(goal_month, month) && U32.is_eq(goal_day, day)

def act(s: State, action: Action) -> State:
  match s action:
    case S{year, month, day, value, outcome} Wait{}:
      S{year, month, day, value, outcome}
    case S{year, month, day, value, outcome} SetValue{units}:
      S{year, month, day, parse(units), outcome}
    case S{+year, +month, +day, +value, outcome} Submit{}:
      S{year, month, day, value, Finished{equal(year, month, day, value)}}

def guarded(expired: Bool, s: State, action: Action) -> State:
  match expired:
    case True{}:
      S{year, month, day, value, outcome} = s
      S{year, month, day, value, TimedOut{}}
    case False{}: act(s, action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{year, month, day, value, Running{}}:
      guarded(U32.is_ge(elapsed, deadline), S{year, month, day, value, Running{}}, action)
    case S{year, month, day, value, Finished{success}}:
      S{year, month, day, value, Finished{success}}
    case S{year, month, day, value, TimedOut{}}:
      S{year, month, day, value, TimedOut{}}

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
