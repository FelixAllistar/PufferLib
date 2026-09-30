import Base

# social-media: menu state belongs to the clicked post; action controls finish
# immediately. The source scores username equality, not post index equality.
type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{menu: U32, expected: U32, outcome: Outcome}

type Action is Data:
  Wait{}
  Menu{post: U32}
  Press{kind: U32, same_user: Bool}
  Submit{correct: Bool}

def toggle(mask: U32, kind: U32) -> U32:
  (mask .^. (1 << U32.to_nat(kind) : U32) : U32)

def mark_ok(task: U32, +mask: U32, +same_user: Bool, +expected: U32) -> Bool:
  +bit = (1 << U32.to_nat(expected) : U32)
  +allowed = Bool.pick(U32, same_user, bit, 0)
  Bool.pick(Bool, U32.is_eq(task, 1), U32.is_eq(mask, allowed),
    U32.is_eq(mask, 0) || (same_user && U32.is_eq(mask, bit)))

def marked(mask: U32, same_user: Bool, expected: U32) -> U32:
  Bool.to_u32(same_user && U32.is_eq(mask, (1 << U32.to_nat(expected) : U32)))

def accepted(task: U32, ok: Bool, selected: U32, required: U32) -> Bool:
  ok && (U32.is_eq(task, 1) || U32.is_eq(selected, required))

def act(s: State, action: Action) -> State:
  match s action:
    case S{menu, expected, outcome} Wait{}:
      S{menu, expected, outcome}
    case S{+menu, expected, outcome} Menu{+post}:
      S{Bool.pick(U32, U32.is_eq(menu, post), 0, post), expected, outcome}
    case S{menu, +expected, outcome} Press{+kind, same_user}:
      S{menu, expected, Finished{same_user && U32.is_eq(kind, expected)}}
    case S{menu, expected, outcome} Submit{correct}:
      S{menu, expected, Finished{correct}}

def guarded(expired: Bool, s: State, action: Action) -> State:
  match expired s:
    case True{} S{menu, expected, outcome}:
      S{menu, expected, TimedOut{}}
    case False{} _: act(s, action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{menu, expected, Running{}}:
      guarded(U32.is_ge(elapsed, deadline), S{menu, expected, Running{}}, action)
    case S{menu, expected, Finished{success}}:
      S{menu, expected, Finished{success}}
    case S{menu, expected, TimedOut{}}:
      S{menu, expected, TimedOut{}}

def raw(outcome: Outcome) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{True{}}: 1.0
    case _: (0.0 - 1.0 : F32)

def timed(outcome: Outcome, elapsed: U32, deadline: U32) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{True{}}: (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32)
    case _: (0.0 - 1.0 : F32)
