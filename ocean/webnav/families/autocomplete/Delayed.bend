import Base
import ../../miniwob/autocomplete/Model.bend as M
import ../../miniwob/Text.bend as T
import ../../miniwob/Widgets.bend as W

# jQuery UI 1.12.1's 300 ms debounce. A pending search keeps the old menu
# visible and reads the current value when the callback actually executes.
type State is Data:
  D{s: M.State, due: U32, last: +List<U32>, modifier: Bool}

def fired(needed: Bool, s: M.State, last: +List<U32>) -> State:
  match needed s:
    case True{} M.S{task, +field, focus, menu, outcome}:
      D{M.S{task, field, focus, M.menu_for(field), outcome},
        0, T.value(field), False{}}
    case False{} _:
      D{s, 0, last, False{}}

def fire(+s: M.State, +last: +List<U32>, modifier: Bool) -> State:
  fired(Bool.not(T.equal(M.value(s), last)) ||
    (Bool.not(M.menu_open(s)) && Bool.not(modifier)), s, last)

def advance_if(ready: Bool, s: State) -> State:
  match ready s:
    case True{} D{state, due, last, modifier}: fire(state, last, modifier)
    case False{} _: s

def advance(s: State, +elapsed: U32) -> State:
  D{state, +due, last, modifier} = s
  advance_if(U32.is_gt(due, 0) && U32.is_le(due, elapsed) &&
    U32.is_lt(due, 10000), D{state, due, last, modifier})

def modifier(edit: T.Edit) -> Bool:
  match edit:
    case T.SelectAll{}: True{}
    case _: False{}

def editing(s: State, +edit: T.Edit, elapsed: U32) -> State:
  match s:
    case D{M.S{task, field, 1n, menu, W.Running{}}, due, last, old_modifier}:
      D{M.S{task, T.edit(edit, field), 1n, menu, W.Running{}},
        (elapsed + 300 : U32), last, modifier(edit)}
    case _: s

def remember(new_last: Bool, +next: M.State, due: U32,
    last: +List<U32>, pending_modifier: Bool) -> State:
  D{next, due, Bool.pick(+List<U32>, new_last, M.value(next), last), pending_modifier}

def accept_last(s: M.State) -> Bool:
  Bool.not(Nat.is_eq(M.menu_index(s), 0n))

def ordinary(action: M.Action, +s: M.State, due: U32,
    last: +List<U32>, pending_modifier: Bool, elapsed: U32) -> State:
  match action:
    case M.Submit{}:
      D{M.step_at(s, M.Submit{}, elapsed), 0, last, False{}}
    case M.MenuDown{}:
      remember(Bool.not(M.menu_open(s)), M.step_at(s, M.MenuDown{}, elapsed),
        due, last, pending_modifier)
    case M.MenuUp{}:
      remember(Bool.not(M.menu_open(s)), M.step_at(s, M.MenuUp{}, elapsed),
        due, last, pending_modifier)
    case M.MenuSelect{+index}:
      remember(M.menu_open(s), M.step_at(s, M.MenuSelect{index}, elapsed),
        due, last, pending_modifier)
    case M.MenuAccept{}:
      remember(accept_last(s), M.step_at(s, M.MenuAccept{}, elapsed),
        due, last, pending_modifier)
    case _:
      D{M.step_at(s, action, elapsed), due, last, pending_modifier}

def active(action: M.Action, s: State, elapsed: U32) -> State:
  match action:
    case M.Editing{edit}: editing(s, edit, elapsed)
    case _:
      D{state, due, last, pending_modifier} = s
      ordinary(action, state, due, last, pending_modifier, elapsed)

def deadline(expired: Bool, s: State, action: M.Action, elapsed: U32) -> State:
  match expired s:
    case True{} D{state, due, last, pending_modifier}:
      D{M.step_at(state, M.Timeout{}, elapsed), 0, last, False{}}
    case False{} _: active(action, s, elapsed)

def step(s: State, action: M.Action, +elapsed: U32) -> State:
  match s:
    case D{M.S{task, field, focus, menu, W.Running{}}, due, last, pending_modifier}:
      deadline(U32.is_ge(elapsed, 10000),
        advance(D{M.S{task, field, focus, menu, W.Running{}},
          due, last, pending_modifier}, elapsed), action, elapsed)
    case _: s
