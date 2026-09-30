import Base

# Grid point i is column i/5 and row i%5, in source loop order.
type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{goal: U32, selected: U32, outcome: Outcome}

type Action is Data:
  Wait{}
  Click{index: U32}

def active(action: Action, s: State) -> State:
  match action s:
    case Wait{} _: s
    case Click{+index} S{+goal, selected, outcome}:
      S{goal, index, Finished{U32.is_eq(index, goal)}}

def expire(s: State) -> State:
  S{goal, selected, outcome} = s
  S{goal, selected, TimedOut{}}

def guard(expired: Bool, s: State, action: Action) -> State:
  match expired:
    case True{}: expire(s)
    case False{}: active(action, s)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{goal, selected, Running{}}:
      guard(U32.is_ge(elapsed, deadline),
        S{goal, selected, Running{}}, action)
    case S{goal, selected, Finished{success}}:
      S{goal, selected, Finished{success}}
    case S{goal, selected, TimedOut{}}:
      S{goal, selected, TimedOut{}}

def raw(outcome: Outcome) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{True{}}: 1.0
    case _: (0.0 - 1.0 : F32)

def timed(outcome: Outcome, elapsed: U32, deadline: U32) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{True{}}:
      F32.max(0.0, (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32))
    case _: (0.0 - 1.0 : F32)
