import Base

# Phone book scores page identity and property independently: both right +1,
# right page/wrong property +0.4, wrong page/right property -0.4, both wrong -1.
type Outcome is Data:
  Running{}
  Finished{grade: U32}
  TimedOut{}

type State is Data:
  S{page: U32, target: U32, property: U32, outcome: Outcome}

type Action is Data:
  Wait{}
  Page{index: U32}
  Select{property: U32}

def grade(+page: U32, target: U32, +clicked: U32, property: U32) -> U32:
  (Bool.to_u32(U32.is_eq(page, target)) * 2 +
    Bool.to_u32(U32.is_eq(clicked, property)) : U32)

def act(s: State, action: Action) -> State:
  match s action:
    case S{page, target, property, outcome} Wait{}:
      S{page, target, property, outcome}
    case S{page, target, property, outcome} Page{index}:
      S{index, target, property, outcome}
    case S{+page, +target, +property, outcome} Select{+clicked}:
      S{page, target, property, Finished{grade(page, target, clicked, property)}}

def guarded(expired: Bool, s: State, action: Action) -> State:
  match expired s:
    case True{} S{page, target, property, outcome}:
      S{page, target, property, TimedOut{}}
    case False{} _: act(s, action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{page, target, property, Running{}}:
      guarded(U32.is_ge(elapsed, deadline),
        S{page, target, property, Running{}}, action)
    case S{page, target, property, Finished{grade_code}}:
      S{page, target, property, Finished{grade_code}}
    case S{page, target, property, TimedOut{}}:
      S{page, target, property, TimedOut{}}

def raw(outcome: Outcome) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{3}: 1.0
    case Finished{2}: 0.4
    case Finished{1}: (0.0 - 0.4 : F32)
    case _: (0.0 - 1.0 : F32)

def timed(outcome: Outcome, elapsed: U32, deadline: U32) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{3}: (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32)
    case Finished{2}: (0.4 * (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline)) : F32)
    case Finished{1}: (0.0 - 0.4 : F32)
    case _: (0.0 - 1.0 : F32)
