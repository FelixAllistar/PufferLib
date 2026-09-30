import Base

# The original handler compares button HTML text; duplicate labels all win.
type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{goal_label: U32, outcome: Outcome}

type Action is Data:
  Wait{}
  Click{label: U32}

def act(s: State, action: Action) -> State:
  match s action:
    case S{goal, outcome} Wait{}: S{goal, outcome}
    case S{+goal, outcome} Click{label}:
      S{goal, Finished{U32.is_eq(goal, label)}}

def guarded(expired: Bool, s: State, action: Action) -> State:
  match expired:
    case True{}:
      S{goal, outcome} = s
      S{goal, TimedOut{}}
    case False{}: act(s, action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{goal, Running{}}:
      guarded(U32.is_ge(elapsed, deadline), S{goal, Running{}}, action)
    case S{goal, Finished{success}}: S{goal, Finished{success}}
    case S{goal, TimedOut{}}: S{goal, TimedOut{}}

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
