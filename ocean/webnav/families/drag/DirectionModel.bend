import Base
import ./Model.bend as B

# Coordinates are SVG getBBox/gridCoords values in tenths, offset by 2560.
# Direction codes follow the page's left/right/up/down array.
type State is Data:
  S{initial_x: U32, initial_y: U32, x: U32, y: U32,
    direction: U32, held: U32, outcome: B.Outcome}

type Action is Data:
  Wait{}
  Down{}
  Move{x: U32, y: U32}
  Up{}
  Submit{}

def direction_match(direction: U32, +initial_x: U32, +initial_y: U32,
                    +x: U32, +y: U32) -> Bool:
  match direction:
    case 0: U32.is_lt(x, initial_x)
    case 1: U32.is_gt(x, initial_x)
    case 2: U32.is_lt(y, initial_y)
    case _: U32.is_gt(y, initial_y)

def success(s: State) -> Bool:
  S{initial_x, initial_y, x, y, direction, held, outcome} = s
  direction_match(direction, initial_x, initial_y, x, y)

def submit_result(ok: Bool, s: State) -> State:
  match ok:
    case True{}:
      S{initial_x, initial_y, x, y, direction, held, outcome} = s
      S{initial_x, initial_y, x, y, direction, held, B.Win{}}
    case False{}:
      S{initial_x, initial_y, x, y, direction, held, outcome} = s
      S{initial_x, initial_y, x, y, direction, held, B.Loss{}}

def move_held(held: U32, s: State, x: U32, y: U32) -> State:
  match held:
    case 0: s
    case _:
      S{initial_x, initial_y, old_x, old_y, direction, holding, outcome} = s
      S{initial_x, initial_y, x, y, direction, holding, B.Running{}}

def active(action: Action, +s: State) -> State:
  match action:
    case Wait{}: s
    case Down{}:
      S{initial_x, initial_y, x, y, direction, held, outcome} = s
      S{initial_x, initial_y, x, y, direction, 1, B.Running{}}
    case Move{x, y}:
      S{+initial_x, +initial_y, +old_x, +old_y, +direction, +held, outcome} = s
      move_held(held, S{initial_x, initial_y, old_x, old_y, direction, held,
        B.Running{}}, x, y)
    case Up{}:
      S{initial_x, initial_y, x, y, direction, held, outcome} = s
      S{initial_x, initial_y, x, y, direction, 0, B.Running{}}
    case Submit{}: submit_result(success(s), s)

def active_or_timeout(dead: Bool, s: State, action: Action) -> State:
  match dead:
    case False{}: active(action, s)
    case True{}:
      S{initial_x, initial_y, x, y, direction, held, outcome} = s
      S{initial_x, initial_y, x, y, direction, held, B.TimedOut{}}

def step(s: State, action: Action, now: U32, deadline: U32) -> State:
  match s:
    case S{initial_x, initial_y, x, y, direction, held, B.Running{}}:
      active_or_timeout(U32.is_ge(now, deadline),
        S{initial_x, initial_y, x, y, direction, held, B.Running{}}, action)
    case S{initial_x, initial_y, x, y, direction, held, B.Win{}}:
      S{initial_x, initial_y, x, y, direction, held, B.Win{}}
    case S{initial_x, initial_y, x, y, direction, held, B.Loss{}}:
      S{initial_x, initial_y, x, y, direction, held, B.Loss{}}
    case S{initial_x, initial_y, x, y, direction, held, B.TimedOut{}}:
      S{initial_x, initial_y, x, y, direction, held, B.TimedOut{}}
