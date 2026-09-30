import Base

type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{document: +List<U32>, expected: +List<U32>, start: Nat, end_: Nat,
    outcome: Outcome}

type Action is Data:
  Wait{}
  Select{start: Nat, end_: Nat}
  Submit{}

def keep(+ch: U32) -> Bool:
  U32.is_ne(ch, 32) && U32.is_ne(ch, 9) &&
  U32.is_ne(ch, 10) && U32.is_ne(ch, 13)

def filtered(yes: Bool, ch: U32, rest: +List<U32>) -> +List<U32>:
  match yes:
    case True{}: ch <> rest
    case False{}: rest

def compact(xs: +List<U32>) -> +List<U32>:
  match xs:
    case Nil{}: Nil{}
    case Con{+ch, tail}: filtered(keep(ch), ch, compact(tail))

def equal(xs: +List<U32>, ys: +List<U32>) -> Bool:
  match xs ys:
    case Nil{} Nil{}: True{}
    case Con{x, xt} Con{y, yt}:
      U32.is_eq(x, y) && equal(xt, yt)
    case _ _: False{}

def selected(document: +List<U32>, start: Nat, end_: Nat) -> +List<U32>:
  List.drop(&2, U32, List.take(&2, U32, document, end_), start)

def act(s: State, action: Action) -> State:
  match s action:
    case S{document, expected, start, end_, outcome} Wait{}:
      S{document, expected, start, end_, outcome}
    case S{document, expected, start, end_, outcome} Select{begin, finish}:
      S{document, expected, begin, finish, outcome}
    case S{+document, +expected, +start, +end_, outcome} Submit{}:
      S{document, expected, start, end_,
        Finished{equal(compact(selected(document, start, end_)),
          compact(expected))}}

def guarded(expired: Bool, s: State, action: Action) -> State:
  match expired s:
    case True{} S{document, expected, start, end_, outcome}:
      S{document, expected, start, end_, TimedOut{}}
    case False{} _: act(s, action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{document, expected, start, end_, Running{}}:
      guarded(U32.is_ge(elapsed, deadline),
        S{document, expected, start, end_, Running{}}, action)
    case S{document, expected, start, end_, Finished{success}}:
      S{document, expected, start, end_, Finished{success}}
    case S{document, expected, start, end_, TimedOut{}}:
      S{document, expected, start, end_, TimedOut{}}

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
