import Base
import ./Model.bend as B

type State is Data:
  S{initial_width: U32, initial_height: U32, width: U32, height: U32,
    dimension: U32, direction: U32, held: U32, outcome: B.Outcome}

type Action is Data:
  Wait{}
  Down{}
  Resize{width: U32, height: U32}
  Up{}
  Submit{}

def comparison(direction: U32, current: U32, initial: U32) -> Bool:
  match direction:
    case 0: U32.is_lt(current, initial)
    case _: U32.is_gt(current, initial)

def check_dimension(dimension: U32, +initial_width: U32, +initial_height: U32,
                    +width: U32, +height: U32, direction: U32) -> Bool:
  match dimension:
    case 0: comparison(direction, width, initial_width)
    case _: comparison(direction, height, initial_height)

def success(s: State) -> Bool:
  S{initial_width, initial_height, width, height, dimension, direction,
    held, outcome} = s
  check_dimension(dimension, initial_width, initial_height, width, height,
    direction)

def submit_result(ok: Bool, s: State) -> State:
  match ok:
    case True{}:
      S{iw, ih, w, h, dim, dir, held, outcome} = s
      S{iw, ih, w, h, dim, dir, held, B.Win{}}
    case False{}:
      S{iw, ih, w, h, dim, dir, held, outcome} = s
      S{iw, ih, w, h, dim, dir, held, B.Loss{}}

def resize_held(held: U32, s: State, width: U32, height: U32) -> State:
  match held:
    case 0: s
    case _:
      S{iw, ih, old_w, old_h, dim, dir, holding, outcome} = s
      S{iw, ih, width, height, dim, dir, holding, B.Running{}}

def active(action: Action, +s: State) -> State:
  match action:
    case Wait{}: s
    case Down{}:
      S{iw, ih, w, h, dim, dir, held, outcome} = s
      S{iw, ih, w, h, dim, dir, 1, B.Running{}}
    case Resize{width, height}:
      S{+iw, +ih, +w, +h, +dim, +dir, +held, outcome} = s
      resize_held(held, S{iw, ih, w, h, dim, dir, held, B.Running{}},
        width, height)
    case Up{}:
      S{iw, ih, w, h, dim, dir, held, outcome} = s
      S{iw, ih, w, h, dim, dir, 0, B.Running{}}
    case Submit{}: submit_result(success(s), s)

def active_or_timeout(dead: Bool, s: State, action: Action) -> State:
  match dead:
    case False{}: active(action, s)
    case True{}:
      S{iw, ih, w, h, dim, dir, held, outcome} = s
      S{iw, ih, w, h, dim, dir, held, B.TimedOut{}}

def step(s: State, action: Action, now: U32, deadline: U32) -> State:
  match s:
    case S{iw, ih, w, h, dim, dir, held, B.Running{}}:
      active_or_timeout(U32.is_ge(now, deadline),
        S{iw, ih, w, h, dim, dir, held, B.Running{}}, action)
    case S{iw, ih, w, h, dim, dir, held, B.Win{}}:
      S{iw, ih, w, h, dim, dir, held, B.Win{}}
    case S{iw, ih, w, h, dim, dir, held, B.Loss{}}:
      S{iw, ih, w, h, dim, dir, held, B.Loss{}}
    case S{iw, ih, w, h, dim, dir, held, B.TimedOut{}}:
      S{iw, ih, w, h, dim, dir, held, B.TimedOut{}}
