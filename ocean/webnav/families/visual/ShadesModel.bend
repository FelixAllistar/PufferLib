import Base

type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{hues: +List<U32>, selected: U32, expected: U32, outcome: Outcome}

type Action is Data:
  Wait{}
  Toggle{index: U32}
  Submit{}

def target_mask(xs: +List<U32>, +expected: U32, +index: U32) -> U32:
  match xs:
    case Nil{}: 0
    case Con{hue, rest}:
      +bit = Bool.pick(U32, U32.is_eq(hue, expected),
        (1 << U32.to_nat(index) : U32), 0)
      (bit .|. target_mask(rest, expected, (index + 1 : U32)) : U32)

def active(action: Action, s: State) -> State:
  match action s:
    case Wait{} _: s
    case Toggle{index} S{hues, selected, expected, outcome}:
      S{hues, (selected .^. (1 << U32.to_nat(index) : U32) : U32),
        expected, outcome}
    case Submit{} S{+hues, +selected, +expected, outcome}:
      S{hues, selected, expected,
        Finished{U32.is_eq(selected, target_mask(hues, expected, 0))}}

def expire(s: State) -> State:
  S{hues, selected, expected, outcome} = s
  S{hues, selected, expected, TimedOut{}}

def guard(expired: Bool, s: State, action: Action) -> State:
  match expired:
    case True{}: expire(s)
    case False{}: active(action, s)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{hues, selected, expected, Running{}}:
      guard(U32.is_ge(elapsed, deadline),
        S{hues, selected, expected, Running{}}, action)
    case S{hues, selected, expected, Finished{success}}:
      S{hues, selected, expected, Finished{success}}
    case S{hues, selected, expected, TimedOut{}}:
      S{hues, selected, expected, TimedOut{}}

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
