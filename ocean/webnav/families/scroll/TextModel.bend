import Base
import ../../miniwob/Text.bend as T

type State is Data:
  S{position: U32, height: U32, client: U32, field: T.Field, status: U32}

type Action is Data:
  Wait{}
  Scroll{position: U32}
  Edit{edit: T.Edit}
  Submit{}

def alnum(+ch: U32) -> Bool:
  (U32.is_ge(ch, 48) && U32.is_le(ch, 57)) ||
  (U32.is_ge(ch, 65) && U32.is_le(ch, 90)) ||
  (U32.is_ge(ch, 97) && U32.is_le(ch, 122))

def keep(valid: Bool, ch: U32, rest: +List<U32>) -> +List<U32>:
  match valid:
    case True{}: ch <> rest
    case False{}: rest

def clean(xs: +List<U32>) -> +List<U32>:
  match xs:
    case Nil{}: Nil{}
    case Con{+ch, rest}:
      keep(alnum(ch), ch, clean(rest))

def correct(+field: T.Field) -> Bool:
  T.equal(clean(T.value(field)), T.goal(field))

def max_scroll(+height: U32, +client: U32) -> U32:
  Bool.pick(U32, U32.is_gt(height, client), (height - client : U32), 0)

def active(s: State, action: Action) -> State:
  match s action:
    case S{position, height, client, field, status} Wait{}:
      S{position, height, client, field, status}
    case S{position, +height, +client, field, status} Scroll{next}:
      S{U32.min(next, max_scroll(height, client)), height, client, field, status}
    case S{position, height, client, field, status} Edit{edit}:
      S{position, height, client, T.edit(edit, field), status}
    case S{position, height, client, +field, status} Submit{}:
      S{position, height, client, field,
        Bool.pick(U32, correct(field), 1, 2)}

def guarded(expired: Bool, s: State, action: Action) -> State:
  match expired s:
    case True{} S{position, height, client, field, status}:
      S{position, height, client, field, 3}
    case False{} _: active(s, action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{position, height, client, field, 0}:
      guarded(U32.is_ge(elapsed, deadline),
        S{position, height, client, field, 0}, action)
    case _: s
