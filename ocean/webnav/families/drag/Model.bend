import Base

# Box positions are CSS left/top plus 256. The offset represents signed
# positions without losing source's strict integer comparisons.
type Outcome is Data:
  Running{}
  Win{}
  Loss{}
  TimedOut{}

type State is Data:
  S{small_x: U32, small_y: U32, large_x: U32, large_y: U32,
    held: U32, outcome: Outcome}

type Action is Data:
  Wait{}
  Down{target: U32}
  Move{x: U32, y: U32}
  Up{}
  Submit{}

def between(+inner: U32, +outer: U32) -> Bool:
  U32.is_gt(inner, outer) && U32.is_lt(inner, (outer + 30 : U32))

def contained(+s: State) -> Bool:
  S{small_x, small_y, large_x, large_y, held, outcome} = s
  between(small_x, large_x) && between(small_y, large_y)

def submit_result(ok: Bool, s: State) -> State:
  match ok:
    case True{}:
      S{small_x, small_y, large_x, large_y, held, outcome} = s
      S{small_x, small_y, large_x, large_y, held, Win{}}
    case False{}:
      S{small_x, small_y, large_x, large_y, held, outcome} = s
      S{small_x, small_y, large_x, large_y, held, Loss{}}

def down(s: State, target: U32) -> State:
  S{small_x, small_y, large_x, large_y, held, outcome} = s
  S{small_x, small_y, large_x, large_y, target, Running{}}

def move_held(holding: U32, s: State, x: U32, y: U32) -> State:
  match holding:
    case 1:
      S{small_x, small_y, large_x, large_y, held, outcome} = s
      S{x, y, large_x, large_y, held, Running{}}
    case 2:
      S{small_x, small_y, large_x, large_y, held, outcome} = s
      S{small_x, small_y, x, y, held, Running{}}
    case _: s

def move(s: State, x: U32, y: U32) -> State:
  S{+small_x, +small_y, +large_x, +large_y, +held, outcome} = s
  move_held(held, S{small_x, small_y, large_x, large_y, held, Running{}}, x, y)

def up(s: State) -> State:
  S{small_x, small_y, large_x, large_y, held, outcome} = s
  S{small_x, small_y, large_x, large_y, 0, Running{}}

def active(+s: State, action: Action) -> State:
  match action:
    case Wait{}: s
    case Down{target}: down(s, target)
    case Move{x, y}: move(s, x, y)
    case Up{}: up(s)
    case Submit{}: submit_result(contained(s), s)

def expired(now: U32, deadline: U32) -> Bool:
  U32.is_ge(now, deadline)

def running_expired(dead: Bool, s: State, action: Action) -> State:
  match dead:
    case True{}:
      S{small_x, small_y, large_x, large_y, held, outcome} = s
      S{small_x, small_y, large_x, large_y, held, TimedOut{}}
    case False{}: active(s, action)

def step(s: State, action: Action, now: U32, deadline: U32) -> State:
  match s:
    case S{small_x, small_y, large_x, large_y, held, Running{}}:
      running_expired(expired(now, deadline),
        S{small_x, small_y, large_x, large_y, held, Running{}}, action)
    case S{small_x, small_y, large_x, large_y, held, Win{}}:
      S{small_x, small_y, large_x, large_y, held, Win{}}
    case S{small_x, small_y, large_x, large_y, held, Loss{}}:
      S{small_x, small_y, large_x, large_y, held, Loss{}}
    case S{small_x, small_y, large_x, large_y, held, TimedOut{}}:
      S{small_x, small_y, large_x, large_y, held, TimedOut{}}

def raw(outcome: Outcome) -> F32:
  match outcome:
    case Running{}: 0.0
    case Win{}: 1.0
    case Loss{}: (0.0 - 1.0 : F32)
    case TimedOut{}: (0.0 - 1.0 : F32)

def timed(outcome: Outcome, elapsed: U32, deadline: U32) -> F32:
  match outcome:
    case Running{}: 0.0
    case Win{}: (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32)
    case Loss{}: (0.0 - 1.0 : F32)
    case TimedOut{}: (0.0 - 1.0 : F32)
