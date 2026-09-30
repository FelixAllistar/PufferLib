import Base

# The first implemented task is scroll-text-2 (registered local task 2).
# Browser scrollTop is clamped before the source's strict 10-pixel check.
type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{position: U32, scroll_height: U32, client_height: U32,
    offset_height: U32, bottom: Bool, outcome: Outcome}

type Action is Data:
  Wait{}
  Scroll{position: U32}
  Submit{}

def max_scroll(+height: U32, +client: U32) -> U32:
  Bool.pick(U32, U32.is_gt(height, client), (height - client : U32), 0)

def clamp(position: U32, height: U32, client: U32) -> U32:
  U32.min(position, max_scroll(height, client))

def difference(+a: U32, +b: U32) -> U32:
  Bool.pick(U32, U32.is_ge(a, b), (a - b : U32), (b - a : U32))

def near_end(+position: U32, +height: U32, offset: U32, bottom: Bool) -> Bool:
  Bool.pick(Bool, bottom,
    U32.is_lt(difference((offset + position : U32), height), 10),
    U32.is_lt(position, 10))

def act(s: State, action: Action) -> State:
  match s action:
    case S{position, height, client, offset, bottom, outcome} Wait{}:
      S{position, height, client, offset, bottom, outcome}
    case S{position, +height, +client, offset, bottom, outcome} Scroll{next}:
      S{clamp(next, height, client), height, client, offset, bottom, outcome}
    case S{+position, +height, client, +offset, +bottom, outcome} Submit{}:
      S{position, height, client, offset, bottom, Finished{near_end(position, height, offset, bottom)}}

def guarded(expired: Bool, s: State, action: Action) -> State:
  match expired s:
    case True{} S{position, height, client, offset, bottom, outcome}:
      S{position, height, client, offset, bottom, TimedOut{}}
    case False{} _: act(s, action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{position, height, client, offset, bottom, Running{}}:
      guarded(U32.is_ge(elapsed, deadline), S{position, height, client, offset, bottom, Running{}}, action)
    case S{position, height, client, offset, bottom, Finished{success}}:
      S{position, height, client, offset, bottom, Finished{success}}
    case S{position, height, client, offset, bottom, TimedOut{}}:
      S{position, height, client, offset, bottom, TimedOut{}}

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
