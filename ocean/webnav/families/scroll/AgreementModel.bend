import Base
import ../../miniwob/Text.bend as T

type State is Data:
  S{position: U32, height: U32, client: U32, mode: U32,
    enabled: Bool, field: T.Field, status: U32}

type Action is Data:
  Wait{}
  Scroll{position: U32}
  Edit{edit: T.Edit}
  Cancel{}
  Agree{}

def max_scroll(+height: U32, +client: U32) -> U32:
  Bool.pick(U32, U32.is_gt(height, client), (height - client : U32), 0)

def scrolled(s: State, next: U32) -> State:
  S{position, +height, +client, mode, +enabled, field, status} = s
  +at = U32.min(next, max_scroll(height, client))
  S{at, height, client, mode, enabled || U32.is_le(height, (at + 95 : U32)), field, status}

def edited(enabled: Bool, s: State, edit: T.Edit) -> State:
  match enabled s:
    case False{} _: s
    case True{} S{position, height, client, mode, enabled_, field, status}:
      S{position, height, client, mode, enabled_, T.edit(edit, field), status}

def clicked(s: State, +agree: Bool) -> State:
  S{position, height, client, +mode, enabled, +field, status} = s
  +right_button = Bool.pick(Bool, U32.is_eq(mode, 1), agree, Bool.not(agree))
  +right_name = Bool.pick(Bool, U32.is_eq(mode, 0), True{},
    T.equal(T.value(field), T.goal(field)))
  S{position, height, client, mode, enabled, field,
    Bool.pick(U32, right_button && right_name, 1, 2)}

def agree(enabled: Bool, s: State) -> State:
  match enabled:
    case False{}: s
    case True{}: clicked(s, True{})

def edit_from_state(s: State, edit: T.Edit) -> State:
  S{position, height, client, mode, +enabled, field, status} = s
  edited(enabled, S{position, height, client, mode, enabled, field, status}, edit)

def agree_from_state(s: State) -> State:
  S{position, height, client, mode, +enabled, field, status} = s
  agree(enabled, S{position, height, client, mode, enabled, field, status})

def active(s: State, action: Action) -> State:
  match action:
    case Wait{}: s
    case Scroll{next}: scrolled(s, next)
    case Edit{edit}: edit_from_state(s, edit)
    case Cancel{}: clicked(s, False{})
    case Agree{}: agree_from_state(s)

def guarded(expired: Bool, s: State, action: Action) -> State:
  match expired s:
    case True{} S{position, height, client, mode, enabled, field, status}:
      S{position, height, client, mode, enabled, field, 3}
    case False{} _: active(s, action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{position, height, client, mode, enabled, field, 0}:
      guarded(U32.is_ge(elapsed, deadline),
        S{position, height, client, mode, enabled, field, 0}, action)
    case _: s
