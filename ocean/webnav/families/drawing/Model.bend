import Base
import ./Points.bend as P
import ./Scoring.bend as Q

type Outcome is Data:
  Running{}
  Finished{reward: F32}
  TimedOut{}

type State is Data:
  S{points: +List<P.Point>, active: Bool, outcome: Outcome}

type Action is Data:
  Wait{}
  Down{point: P.Point}
  Move{point: P.Point}
  Up{}
  Submit{}

def act(action: Action, s: State) -> State:
  match action s:
    case Wait{} _: s
    case Submit{} _: s
    case Down{point} S{points, active, outcome}:
      S{[point], True{}, outcome}
    case Move{point} S{points, True{}, outcome}:
      S{point <> points, True{}, outcome}
    case Move{point} _: s
    case Up{} S{points, active, outcome}:
      S{points, False{}, outcome}

def guard(expired: Bool, s: State, action: Action) -> State:
  match expired s:
    case True{} S{points, active, outcome}: S{points, active, TimedOut{}}
    case False{} _: act(action, s)

def step(s: State, action: Action, elapsed: U32) -> State:
  match s:
    case S{points, active, Running{}}:
      guard(U32.is_ge(elapsed, 10000), S{points, active, Running{}}, action)
    case _: s


def submit(expired: Bool, +points: +List<P.Point>, active: Bool,
    task: U32, center: P.Point, horizontal: Bool) -> State:
  match expired:
    case True{}: S{points, active, TimedOut{}}
    case False{}: S{points, active, Finished{Q.score(points, task, center, horizontal)}}

def step_task(s: State, action: Action, +elapsed: U32,
    task: U32, center: P.Point, horizontal: Bool) -> State:
  match s action:
    case S{points, active, Running{}} Submit{}:
      submit(U32.is_ge(elapsed, 10000), points, active, task, center, horizontal)
    case _ _: step(s, action, elapsed)
