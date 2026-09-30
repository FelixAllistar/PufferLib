import Base

# Prices are integer cents. Tick zero has no displayed price; tick one draws
# the first generated point at elapsed 100 ms.
type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{prices: +List<U32>, threshold: U32, tick: U32,
    visible: U32, outcome: Outcome}

type Action is Data:
  Wait{}
  Buy{}

def price_at(xs: +List<U32>, index: Nat) -> U32:
  match xs index:
    case Nil{} _: 0
    case Con{value, rest} 0n: value
    case Con{value, rest} 1n+p: price_at(rest, p)

def visible_price(has_tick: Bool, prices: +List<U32>, tick: U32) -> U32:
  match has_tick:
    case False{}: 0
    case True{}: price_at(prices, U32.to_nat((tick - 1 : U32)))

def advance(s: State, elapsed: U32) -> State:
  S{+prices, threshold, tick, visible, outcome} = s
  +next_tick = (elapsed / 100 : U32)
  S{prices, threshold, next_tick,
    visible_price(U32.is_gt(next_tick, 0), prices, next_tick), outcome}

def active(action: Action, s: State) -> State:
  match action s:
    case Wait{} _: s
    case Buy{} S{prices, +threshold, tick, +visible, outcome}:
      S{prices, threshold, tick, visible,
        Finished{U32.is_gt(visible, 0) && U32.is_le(visible, threshold)}}

def timeout(s: State) -> State:
  S{prices, threshold, tick, visible, outcome} = s
  S{prices, threshold, tick, visible, TimedOut{}}

def guard(expired: Bool, action: Action, s: State,
    elapsed: U32, deadline: U32) -> State:
  match expired:
    case True{}: timeout(advance(s, (deadline - 1 : U32)))
    case False{}: active(action, advance(s, elapsed))

def step(s: State, action: Action, +elapsed: U32, +deadline: U32) -> State:
  match s:
    case S{prices, threshold, tick, visible, Running{}}:
      guard(U32.is_ge(elapsed, deadline), action,
        S{prices, threshold, tick, visible, Running{}}, elapsed, deadline)
    case S{prices, threshold, tick, visible, Finished{success}}:
      S{prices, threshold, tick, visible, Finished{success}}
    case S{prices, threshold, tick, visible, TimedOut{}}:
      S{prices, threshold, tick, visible, TimedOut{}}

def raw(o: Outcome) -> F32:
  match o:
    case Running{}: 0.0
    case Finished{True{}}: 1.0
    case _: (0.0 - 1.0 : F32)

def timed(o: Outcome, elapsed: U32, deadline: U32) -> F32:
  match o:
    case Running{}: 0.0
    case Finished{True{}}:
      F32.max(0.0, (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32))
    case _: (0.0 - 1.0 : F32)
