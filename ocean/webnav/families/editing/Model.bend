import Base

type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{expected: +List<U32>, input: +List<U32>, focused: Bool, outcome: Outcome}

type Action is Data:
  Wait{}
  Focus{}
  Insert{units: +List<U32>}
  Submit{}

def acceptable(+ch: U32) -> Bool:
  (U32.is_ge(ch, 65) && U32.is_le(ch, 90)) ||
  (U32.is_ge(ch, 97) && U32.is_le(ch, 122)) ||
  (U32.is_ge(ch, 48) && U32.is_le(ch, 57))

def kept(keep: Bool, ch: U32, tail: +List<U32>) -> +List<U32>:
  match keep:
    case True{}: ch <> tail
    case False{}: tail

def clean(xs: +List<U32>) -> +List<U32>:
  match xs:
    case Nil{}: Nil{}
    case Con{+ch, rest}: kept(acceptable(ch), ch, clean(rest))

def equal(xs: +List<U32>, ys: +List<U32>) -> Bool:
  match xs ys:
    case Nil{} Nil{}: True{}
    case Con{x, xt} Con{y, yt}:
      U32.is_eq(x, y) && equal(xt, yt)
    case _ _: False{}

def act(s: State, action: Action) -> State:
  match s action:
    case S{expected, input, focused, outcome} Wait{}:
      S{expected, input, focused, outcome}
    case S{expected, input, focused, outcome} Focus{}:
      S{expected, input, True{}, outcome}
    case S{expected, input, focused, outcome} Insert{units}:
      S{expected, List.append(&2, U32, input, units), focused, outcome}
    case S{+expected, +input, focused, outcome} Submit{}:
      S{expected, input, False{}, Finished{equal(clean(input), expected)}}

def guarded(expired: Bool, s: State, action: Action) -> State:
  match expired s:
    case True{} S{expected, input, focused, outcome}:
      S{expected, input, focused, TimedOut{}}
    case False{} _: act(s, action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{expected, input, focused, Running{}}:
      guarded(U32.is_ge(elapsed, deadline),
        S{expected, input, focused, Running{}}, action)
    case S{expected, input, focused, Finished{success}}:
      S{expected, input, focused, Finished{success}}
    case S{expected, input, focused, TimedOut{}}:
      S{expected, input, focused, TimedOut{}}

def raw(o: Outcome) -> F32:
  match o:
    case Running{}: 0.0
    case Finished{True{}}: 1.0
    case _: (0.0 - 1.0 : F32)

def timed(o: Outcome, elapsed: U32, deadline: U32) -> F32:
  match o:
    case Running{}: 0.0
    case Finished{True{}}:
      (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32)
    case _: (0.0 - 1.0 : F32)
