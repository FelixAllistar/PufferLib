import Base

# Count-sides and identify-shape share only their button/clock semantics.
# Their generated public scenes are stored separately from this private goal.
type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{goal: U32, outcome: Outcome}

type Action is Data:
  Wait{}
  Choose{value: U32}

def active(s: State, action: Action) -> State:
  match s action:
    case S{goal, outcome} Wait{}: S{goal, outcome}
    case S{+goal, outcome} Choose{value}:
      S{goal, Finished{U32.is_eq(value, goal)}}

def expire(s: State) -> State:
  S{goal, outcome} = s
  S{goal, TimedOut{}}

def guard(expired: Bool, s: State, action: Action) -> State:
  match expired:
    case True{}: expire(s)
    case False{}: active(s, action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{goal, Running{}}:
      guard(U32.is_ge(elapsed, deadline), S{goal, Running{}}, action)
    case S{goal, Finished{success}}:
      S{goal, Finished{success}}
    case S{goal, TimedOut{}}: S{goal, TimedOut{}}

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
