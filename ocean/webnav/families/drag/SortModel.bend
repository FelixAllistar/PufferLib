import Base
import ./Model.bend as B

# Item IDs are stable across reordering. The values list is used only by
# drag-sort-numbers (encoded signed number +100). `ix` and `goal` are the
# source's one-indexed queried item and required final position.
type State is Data:
  S{order: +List<U32>, values: +List<U32>, held: U32, old: U32,
    ix: U32, goal: U32, outcome: B.Outcome}

type Action is Data:
  Wait{}
  Down{item: U32}
  Move{destination: U32}
  Up{}
  Submit{}

def position(xs: +List<U32>, +item: U32, +at: U32) -> U32:
  match xs:
    case Nil{}: 0
    case Con{+head, tail}:
      Bool.pick(U32, U32.is_eq(head, item), at,
        position(tail, item, (at + 1 : U32)))

def remove_choice(found: Bool, head: U32, tail: +List<U32>) -> +List<U32>:
  match found:
    case True{}: tail
    case False{}: head <> tail

def remove(xs: +List<U32>, +item: U32) -> +List<U32>:
  match xs:
    case Nil{}: Nil{}
    case Con{+head, tail}:
      remove_choice(U32.is_eq(head, item), head, remove(tail, item))

def insert(n: Nat, item: U32, xs: +List<U32>) -> +List<U32>:
  match n:
    case 0n: item <> xs
    case 1n+ p:
      match xs:
        case Nil{}: item <> Nil{}
        case Con{head, tail}: head <> insert(p, item, tail)

def move_to(xs: +List<U32>, +item: U32, destination: U32) -> +List<U32>:
  insert(U32.to_nat((destination - 1 : U32)), item, remove(xs, item))

def lookup(values: +List<U32>, n: Nat) -> U32:
  match values:
    case Nil{}: 0
    case Con{head, tail}:
      match n:
        case 0n: 0
        case 1n+ +p:
          Bool.pick(U32, Nat.is_eq(p, 0n), head, lookup(tail, p))

def ascending(xs: +List<U32>, +values: +List<U32>, +previous: U32) -> Bool:
  match xs:
    case Nil{}: True{}
    case Con{head, tail}:
      +current = lookup(values, U32.to_nat(head))
      U32.is_ge(current, previous) && ascending(tail, values, current)

def result(ok: Bool, s: State) -> State:
  match ok:
    case True{}:
      S{order, values, held, old, ix, goal, outcome} = s
      S{order, values, held, old, ix, goal, B.Win{}}
    case False{}:
      S{order, values, held, old, ix, goal, outcome} = s
      S{order, values, held, old, ix, goal, B.Loss{}}

def drop_result(number_sort: Bool, +s: State) -> State:
  match number_sort:
    case True{}:
      S{order, values, held, old, ix, goal, outcome} = s
      S{order, values, 0, 0, ix, goal, B.Running{}}
    case False{}:
      S{+order, +values, +held, +old, +ix, +goal, outcome} = s
      result(U32.is_eq(old, ix) &&
        U32.is_eq(position(order, held, 1), goal),
        S{order, values, 0, old, ix, goal, B.Running{}})

def move_held(held: U32, s: State, destination: U32) -> State:
  match held:
    case 0: s
    case _:
      S{+order, values, +holding, +old, ix, goal, outcome} = s
      start = Bool.pick(U32, U32.is_eq(old, 0),
        position(order, holding, 1), old)
      S{move_to(order, holding, destination), values, holding,
        start, ix, goal, B.Running{}}

def up_started(started: Bool, mode: U32, s: State) -> State:
  match started:
    case True{}: drop_result(U32.is_eq(mode, 8), s)
    case False{}:
      S{order, values, held, old, ix, goal, outcome} = s
      S{order, values, 0, 0, ix, goal, B.Running{}}

def up_held(held: U32, mode: U32, s: State) -> State:
  match held:
    case 0: s
    case _:
      S{+order, +values, +holding, +old, +ix, +goal, outcome} = s
      up_started(U32.is_gt(old, 0), mode,
        S{order, values, holding, old, ix, goal, B.Running{}})

def active(mode: U32, s: State, action: Action) -> State:
  S{+order, +values, +held, +old, +ix, +goal, outcome} = s
  match action:
    case Wait{}: S{order, values, held, old, ix, goal, B.Running{}}
    case Down{+item}:
      S{order, values, item, 0, ix, goal, B.Running{}}
    case Move{destination}:
      move_held(held, S{order, values, held, old, ix, goal, B.Running{}}, destination)
    case Up{}:
      up_held(held, mode,
        S{order, values, held, old, ix, goal, B.Running{}})
    case Submit{}:
      result(ascending(order, values, 0),
        S{order, values, held, old, ix, goal, B.Running{}})

def active_or_timeout(dead: Bool, mode: U32, s: State, action: Action) -> State:
  match dead:
    case False{}: active(mode, s, action)
    case True{}:
      S{order, values, held, old, ix, goal, outcome} = s
      S{order, values, held, old, ix, goal, B.TimedOut{}}

def step(mode: U32, s: State, action: Action, now: U32, deadline: U32) -> State:
  match s:
    case S{order, values, held, old, ix, goal, B.Running{}}:
      active_or_timeout(U32.is_ge(now, deadline), mode,
        S{order, values, held, old, ix, goal, B.Running{}}, action)
    case S{order, values, held, old, ix, goal, B.Win{}}:
      S{order, values, held, old, ix, goal, B.Win{}}
    case S{order, values, held, old, ix, goal, B.Loss{}}:
      S{order, values, held, old, ix, goal, B.Loss{}}
    case S{order, values, held, old, ix, goal, B.TimedOut{}}:
      S{order, values, held, old, ix, goal, B.TimedOut{}}
