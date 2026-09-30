import Base
import ./Model.bend as B

# A normalized settled rotation is one drag gesture that leaves the requested
# numbered face active. The original widget starts at face 6, despite its
# viewport.currentSide field starting at zero.
type State is Data:
  S{goal: U32, face: U32, held: U32, outcome: B.Outcome}

type Action is Data:
  Wait{}
  Down{}
  Rotate{face: U32}
  Up{}
  Submit{}

def finish(ok: Bool, s: State) -> State:
  match ok:
    case True{}:
      S{goal, face, held, outcome} = s
      S{goal, face, held, B.Win{}}
    case False{}:
      S{goal, face, held, outcome} = s
      S{goal, face, held, B.Loss{}}

def rotate(held: U32, s: State, face: U32) -> State:
  match held:
    case 0: s
    case _:
      S{goal, old_face, holding, outcome} = s
      S{goal, face, holding, B.Running{}}

def active(s: State, action: Action) -> State:
  S{+goal, +face, +held, outcome} = s
  match action:
    case Wait{}: S{goal, face, held, B.Running{}}
    case Down{}: S{goal, face, 1, B.Running{}}
    case Rotate{new_face}: rotate(held,
      S{goal, face, held, B.Running{}}, new_face)
    case Up{}: S{goal, face, 0, B.Running{}}
    case Submit{}: finish(U32.is_eq(goal, face),
      S{goal, face, held, B.Running{}})

def active_or_timeout(dead: Bool, s: State, action: Action) -> State:
  match dead:
    case False{}: active(s, action)
    case True{}:
      S{goal, face, held, outcome} = s
      S{goal, face, held, B.TimedOut{}}

def step(s: State, action: Action, now: U32, deadline: U32) -> State:
  match s:
    case S{goal, face, held, B.Running{}}:
      active_or_timeout(U32.is_ge(now, deadline),
        S{goal, face, held, B.Running{}}, action)
    case S{goal, face, held, B.Win{}}: S{goal, face, held, B.Win{}}
    case S{goal, face, held, B.Loss{}}: S{goal, face, held, B.Loss{}}
    case S{goal, face, held, B.TimedOut{}}:
      S{goal, face, held, B.TimedOut{}}
