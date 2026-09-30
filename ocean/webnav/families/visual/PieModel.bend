import Base

type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{items: +List<U32>, expected: U32, open: Bool,
    ready_at: U32, no_delay: Bool, outcome: Outcome}

type Action is Data:
  Wait{}
  Toggle{}
  Choose{index: Nat}

def item_at(items: +List<U32>, index: Nat) -> U32:
  match items index:
    case Nil{} _: 0
    case Con{value, rest} 0n: value
    case Con{value, rest} 1n+p: item_at(rest, p)

def chosen(eligible: Bool, s: State, index: Nat) -> State:
  match eligible s:
    case False{} _: s
    case True{} S{+items, +expected, open, ready_at, no_delay, outcome}:
      S{items, expected, open, ready_at, no_delay,
        Finished{U32.is_eq(item_at(items, index), expected)}}

def toggled(s: State, +elapsed: U32) -> State:
  S{items, expected, open, ready_at, +no_delay, outcome} = s
  S{items, expected, Bool.not(open),
    Bool.pick(U32, no_delay, elapsed, (elapsed + 1500 : U32)),
    no_delay, outcome}

def active(action: Action, s: State, elapsed: U32) -> State:
  match action s:
    case Wait{} _: s
    case Toggle{} _: toggled(s, elapsed)
    case Choose{index} S{items, expected, +open, +ready_at, no_delay, outcome}:
      chosen(open && U32.is_ge(elapsed, ready_at),
        S{items, expected, open, ready_at, no_delay, outcome}, index)

def expire(s: State) -> State:
  S{items, expected, open, ready_at, no_delay, outcome} = s
  S{items, expected, open, ready_at, no_delay, TimedOut{}}

def guard(expired: Bool, s: State, action: Action, elapsed: U32) -> State:
  match expired:
    case True{}: expire(s)
    case False{}: active(action, s, elapsed)

def step(s: State, action: Action, +elapsed: U32, deadline: U32) -> State:
  match s:
    case S{items, expected, open, ready_at, no_delay, Running{}}:
      guard(U32.is_ge(elapsed, deadline),
        S{items, expected, open, ready_at, no_delay, Running{}},
        action, elapsed)
    case S{items, expected, open, ready_at, no_delay, Finished{success}}:
      S{items, expected, open, ready_at, no_delay, Finished{success}}
    case S{items, expected, open, ready_at, no_delay, TimedOut{}}:
      S{items, expected, open, ready_at, no_delay, TimedOut{}}

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
