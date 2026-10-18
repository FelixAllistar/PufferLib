import Base

# Executed in the browser through stock Bend 2.0.6's JavaScript target.
# DOM, Web Workers and WASM are host transport; controls and session accounting
# remain pure Bend. The native engine independently validates every action.
def choose(flag: Bool, yes: U32, no: U32) -> U32:
  match flag:
    case True{}: yes
    case False{}: no

def minimum(+a: U32, +b: U32) -> U32:
  choose(U32.is_le(a, b), a, b)

def maximum(+a: U32, +b: U32) -> U32:
  choose(U32.is_ge(a, b), a, b)

def raise_target(requested: U32, lower: U32, +upper: U32) -> U32:
  minimum(maximum(requested, minimum(lower, upper)), upper)

# All amounts are integer chips and positive actions are street totals.
# Pot raises include the call before applying the pot fraction.
def opening_size(+multiple: U32, +high: U32, +blind: U32) -> U32:
  choose(U32.is_le(high, blind), (multiple * blind : U32),
    (multiple * high : U32))

def preset_request(kind: Nat, +street: U32, +high: U32, +pot: U32,
                   +call: U32, +blind: U32, lower: U32, upper: U32) -> U32:
  match kind:
    case 0n: lower
    case 1n:
      choose(U32.is_eq(street, 0), opening_size(3, high, blind),
        (high + (pot + call) / 2 : U32))
    case 2n:
      choose(U32.is_eq(street, 0), opening_size(5, high, blind),
        (high + 3 * (pot + call) / 4 : U32))
    case 3n: (high + pot + call : U32)
    case _: upper

def preset(kind: Nat, street: U32, high: U32, pot: U32, call: U32,
           blind: U32, +lower: U32, +upper: U32) -> U32:
  raise_target(preset_request(kind, street, high, pot, call, blind, lower, upper), lower, upper)

def phase(done: Bool, actor: U32, viewer: U32) -> U32:
  choose(done, 2, choose(U32.is_eq(actor, viewer), 1, 0))

def action_kind(kind: Nat, call: U32, raising: U32, high: U32,
                lower: U32, +upper: U32, +target: U32) -> U32:
  match kind:
    case 0n: choose(U32.is_gt(call, 0), 1, 0)
    case 1n: 1
    case 2n:
      (raising * choose(U32.is_gt(target, high), 1, 0)
        * choose(U32.is_le(target, upper), 1, 0)
        * choose(U32.is_eq(target, upper), 1,
            choose(U32.is_ge(target, lower), 1, 0)) : U32)
    case _: 0

def allowed(done: Bool, actor: U32, viewer: U32, kind: Nat,
            call: U32, raising: U32, high: U32, lower: U32,
            upper: U32, target: U32) -> U32:
  match done:
    case True{}: 0
    case False{}:
      (choose(U32.is_eq(actor, viewer), 1, 0)
        * action_kind(kind, call, raising, high, lower, upper, target) : U32)

type Session is Data:
  S{hands: U32, wins: U32, won: U32, lost: U32}

def empty() -> Session:
  S{0, 0, 0, 0}

def record(+won: U32, lost: U32, session: Session) -> Session:
  S{hands, wins, earned, spent} = session
  S{(hands + 1 : U32), (wins + choose(U32.is_gt(won, 0), 1, 0) : U32),
    (earned + won : U32), (spent + lost : U32)}
