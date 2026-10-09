import Base

# Infrastructure only: no task goals, rewards, DOM refs or success shortcuts.
# Routes are application-local page IDs, not a policy's node slot numbers.
# One pending load; navigating again cancels it. History retains eight entries.
type Phase is Data:
  Ready{}
  Loading{remaining_ms: U32}
  Failed{}

type State is Data:
  S{route: U32, route_count: U32, latency_ms: U32, failures_left: U32,
    back: +List<U32>, forward: +List<U32>, phase: Phase}

type Action is Data:
  Wait{}
  Navigate{route: U32}
  Back{}
  Forward{}
  Retry{}
  Reload{}

def push(route: U32, history: +List<U32>) -> +List<U32>:
  List.take(&2, U32, route <> history, 8n)

def reload(s: State) -> State:
  S{route, count, +latency, failures, back, forward, phase} = s
  S{route, count, latency, failures, back, forward, Loading{latency}}

def navigate_same(same: Bool, s: State, route: U32) -> State:
  match same s:
    case True{} _: reload(s)
    case False{} S{old, count, +latency, failures, back, forward, phase}:
      S{route, count, latency, failures, push(old, back), Nil{}, Loading{latency}}

def navigate_if(valid: Bool, +s: State, +route: U32) -> State:
  match valid:
    case False{}: s
    case True{}:
      S{old, count, latency, failures, back, forward, phase} = s
      navigate_same(U32.is_eq(old, route), s, route)

def navigate(+s: State, +route: U32) -> State:
  S{old, count, latency, failures, back, forward, phase} = s
  navigate_if(U32.is_lt(route, count), s, route)

def go_back(s: State) -> State:
  match s:
    case S{route, count, latency, failures, Nil{}, forward, phase}:
      S{route, count, latency, failures, Nil{}, forward, phase}
    case S{route, count, +latency, failures, Con{previous, rest}, forward, phase}:
      S{previous, count, latency, failures, rest, push(route, forward), Loading{latency}}

def go_forward(s: State) -> State:
  match s:
    case S{route, count, latency, failures, back, Nil{}, phase}:
      S{route, count, latency, failures, back, Nil{}, phase}
    case S{route, count, +latency, failures, back, Con{next, rest}, phase}:
      S{next, count, latency, failures, push(route, back), rest, Loading{latency}}

def retry(s: State) -> State:
  match s:
    case S{route, count, +latency, failures, back, forward, Failed{}}:
      S{route, count, latency, failures, back, forward, Loading{latency}}
    case _: s

def complete(failed: Bool, s: State) -> State:
  match failed s:
    case False{} S{route, count, latency, failures, back, forward, phase}:
      S{route, count, latency, failures, back, forward, Ready{}}
    case True{} S{route, count, latency, failures, back, forward, phase}:
      S{route, count, latency, (failures - 1 : U32), back, forward, Failed{}}

def advance_loading(done: Bool, s: State, elapsed_ms: U32) -> State:
  match done s:
    case True{} S{route, count, latency, +failures, back, forward, phase}:
      complete(U32.is_gt(failures, 0), S{route, count, latency, failures, back, forward, phase})
    case False{} S{route, count, latency, failures, back, forward, Loading{remaining}}:
      S{route, count, latency, failures, back, forward, Loading{(remaining - elapsed_ms : U32)}}
    case False{} _: s

def advance(s: State, +elapsed_ms: U32) -> State:
  match s:
    case S{route, count, latency, failures, back, forward, Loading{+remaining}}:
      advance_loading(U32.is_le(remaining, elapsed_ms),
        S{route, count, latency, failures, back, forward, Loading{remaining}}, elapsed_ms)
    case _: s

def act(action: Action, s: State) -> State:
  match action:
    case Wait{}: s
    case Navigate{route}: navigate(s, route)
    case Back{}: go_back(s)
    case Forward{}: go_forward(s)
    case Retry{}: retry(s)
    case Reload{}: reload(s)

# elapsed_ms advances the previous load, then dispatches the new command.
# The caller supplies elapsed time; the policy cannot declare load completion.
def step(s: State, action: Action, elapsed_ms: U32) -> State:
  act(action, advance(s, elapsed_ms))
