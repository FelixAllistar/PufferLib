import Base

type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

# Click-color's 13 palette entries are encoded 1..13. A target of zero is
# the query's colored swatch, whose source handler has no data-color and fails.
type State is Data:
  S{a: U32, b: U32, c: U32, d: U32, expected: U32,
    swatch: Bool, outcome: Outcome}

type Action is Data:
  Wait{}
  Click{target: U32}

def correct_index(target: Nat, expected: U32,
    a: U32, b: U32, c: U32, d: U32) -> Bool:
  match target:
    case 1n: U32.is_eq(expected, a)
    case 2n: U32.is_eq(expected, b)
    case 3n: U32.is_eq(expected, c)
    case 4n: U32.is_eq(expected, d)
    case _: False{}

def correct(target: U32, expected: U32,
    a: U32, b: U32, c: U32, d: U32) -> Bool:
  correct_index(U32.to_nat(target), expected, a, b, c, d)

def active(action: Action, s: State) -> State:
  match action s:
    case Wait{} _: s
    case Click{target} S{+a, +b, +c, +d, +expected, swatch, outcome}:
      S{a, b, c, d, expected, swatch,
        Finished{correct(target, expected, a, b, c, d)}}

def expire(s: State) -> State:
  S{a, b, c, d, expected, swatch, outcome} = s
  S{a, b, c, d, expected, swatch, TimedOut{}}

def guard(expired: Bool, s: State, action: Action) -> State:
  match expired:
    case True{}: expire(s)
    case False{}: active(action, s)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{a, b, c, d, expected, swatch, Running{}}:
      guard(U32.is_ge(elapsed, deadline),
        S{a, b, c, d, expected, swatch, Running{}}, action)
    case S{a, b, c, d, expected, swatch, Finished{success}}:
      S{a, b, c, d, expected, swatch, Finished{success}}
    case S{a, b, c, d, expected, swatch, TimedOut{}}:
      S{a, b, c, d, expected, swatch, TimedOut{}}

def raw(result: Outcome) -> F32:
  match result:
    case Running{}: 0.0
    case Finished{True{}}: 1.0
    case _: (0.0 - 1.0 : F32)

def timed(result: Outcome, elapsed: U32, deadline: U32) -> F32:
  match result:
    case Running{}: 0.0
    case Finished{True{}}:
      F32.max(0.0, (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32))
    case _: (0.0 - 1.0 : F32)
