import Base

type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{page: U32, target: U32, matched: Bool, focus: Bool, outcome: Outcome}

type Action is Data:
  Wait{}
  Focus{}
  Search{equal: Bool}
  Page{index: U32, equal: Bool}
  Result{index: U32, fake: Bool}

def act(s: State, action: Action) -> State:
  match s action:
    case S{page, target, matched, focus, outcome} Wait{}:
      S{page, target, matched, focus, outcome}
    case S{page, target, matched, focus, outcome} Focus{}:
      S{page, target, matched, True{}, outcome}
    case S{page, target, matched, focus, outcome} Search{equal}:
      S{1, target, equal, False{}, outcome}
    case S{page, target, matched, focus, outcome} Page{index, equal}:
      S{index, target, equal, False{}, outcome}
    case S{page, +target, +matched, focus, outcome} Result{index, fake}:
      S{page, target, matched, False{},
        Finished{matched && Bool.not(fake) && U32.is_eq(index, target)}}

def guarded(expired: Bool, s: State, action: Action) -> State:
  match expired s:
    case True{} S{page, target, matched, focus, outcome}:
      S{page, target, matched, focus, TimedOut{}}
    case False{} _: act(s, action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{page, target, matched, focus, Running{}}:
      guarded(U32.is_ge(elapsed, deadline),
        S{page, target, matched, focus, Running{}}, action)
    case S{page, target, matched, focus, Finished{success}}:
      S{page, target, matched, focus, Finished{success}}
    case S{page, target, matched, focus, TimedOut{}}:
      S{page, target, matched, focus, TimedOut{}}

def raw(o: Outcome) -> F32:
  match o:
    case Running{}: 0.0
    case Finished{True{}}: 1.0
    case _: (0.0 - 1.0 : F32)

def timed(o: Outcome, elapsed: U32, deadline: U32) -> F32:
  match o:
    case Running{}: 0.0
    case Finished{True{}}: (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32)
    case _: (0.0 - 1.0 : F32)
