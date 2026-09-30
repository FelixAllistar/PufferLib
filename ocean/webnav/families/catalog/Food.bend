import Base

type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{outcome: Outcome}

type Action is Data:
  Wait{}
  Submit{correct: Bool}

def decrement(zero: Bool, quantity: U32) -> U32:
  match zero:
    case True{}: 0
    case False{}: (quantity - 1 : U32)

def adjust(remove: Bool, +quantity: U32) -> U32:
  match remove:
    case True{}: decrement(U32.is_eq(quantity, 0), quantity)
    case False{}: (quantity + 1 : U32)

def item_ok(mode: U32, +quantity: U32, +index: U32,
    +first: U32, +second: U32, type_code: U32, mask: U32) -> Bool:
  Bool.pick(Bool, U32.is_eq(mode, 0),
    U32.is_eq(quantity, Bool.pick(U32,
      U32.is_eq(index, first) || U32.is_eq(index, second), 1, 0)),
    U32.is_eq(quantity, 0) ||
      U32.is_ne(((1 << U32.to_nat(type_code) : U32) .&. mask : U32), 0))

def accepted(mode: U32, valid: Bool, total: U32, required: U32) -> Bool:
  valid && (U32.is_eq(mode, 0) || U32.is_eq(total, required))

def act(s: State, action: Action) -> State:
  match s action:
    case S{outcome} Wait{}: S{outcome}
    case S{outcome} Submit{correct}: S{Finished{correct}}

def guarded(expired: Bool, s: State, action: Action) -> State:
  match expired s:
    case True{} S{outcome}: S{TimedOut{}}
    case False{} _: act(s, action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{Running{}}:
      guarded(U32.is_ge(elapsed, deadline), S{Running{}}, action)
    case S{Finished{success}}: S{Finished{success}}
    case S{TimedOut{}}: S{TimedOut{}}

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
