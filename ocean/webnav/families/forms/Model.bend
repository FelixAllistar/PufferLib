import Base
import ../../miniwob/Text.bend as T
import ../../miniwob/Widgets.bend as W

# The order matches the family-local tags documented in README.md. The tags are
# converted once at the wire boundary; task behavior stays typed in Bend.
type TaskKind is Data:
  EnterDynamic{}
  EnterCase{}
  EnterPassword{}
  Transform{}
  CopyOne{}
  CopyThree{}
  ReadTable{}
  PopupLogin{}

def next_kind(kind: TaskKind) -> TaskKind:
  match kind:
    case EnterDynamic{}: EnterCase{}
    case EnterCase{}: EnterPassword{}
    case EnterPassword{}: Transform{}
    case Transform{}: CopyOne{}
    case CopyOne{}: CopyThree{}
    case CopyThree{}: ReadTable{}
    case ReadTable{}: PopupLogin{}
    case PopupLogin{}: EnterDynamic{}

def kind_loop(n: Nat, +kind: TaskKind) -> TaskKind:
  match n:
    case 0n: kind
    case 1n+p: kind_loop(p, next_kind(kind))

def task_kind(tag: U32) -> TaskKind:
  kind_loop(U32.to_nat(tag), EnterDynamic{})

def deadline(kind: TaskKind) -> U32:
  match kind:
    case EnterPassword{}: 15000
    case Transform{}: 15000
    case ReadTable{}: 20000
    case PopupLogin{}: 15000
    case _: 10000

type Button is Data:
  Submit{}
  PopupOK{}
  PopupCancel{}
  Other{}

def button_nat(code: Nat) -> Button:
  match code:
    case 0n: Other{}
    case 1n+p:
      match p:
        case 0n: Submit{}
        case 1n+q:
          match q:
            case 0n: PopupOK{}
            case 1n+r:
              match r:
                case 0n: PopupCancel{}
                case _: Other{}

def button_from_code(code: U32) -> Button:
  button_nat(U32.to_nat(code))

type State is Data:
  S{kind: TaskKind, fields: +List<T.Field>, focus: Nat,
    clipboard: +List<U32>, popup: Bool, popup_mode: Nat, popup_seen: Bool,
    outcome: W.Outcome}

type Action is Data:
  Wait{}
  Focus{index: Nat}
  Editing{edit: T.Edit}
  SelectRange{start: Nat, end: Nat}
  Copy{}
  Paste{}
  Tab{}
  Press{button: Button}
  Timeout{}

def empty_field() -> T.Field:
  T.F{Nil{}, 0n, 0n, Nil{}}

def field_at(fields: +List<T.Field>, index: Nat) -> T.Field:
  match fields index:
    case Nil{} _: empty_field()
    case Con{field, rest} 0n: field
    case Con{field, rest} 1n+p: field_at(rest, p)

def selected(field: T.Field) -> +List<U32>:
  T.F{+xs, +start, +end, goal} = field
  List.take(&2, U32, List.drop(&2, U32, xs, start), Nat.sub(end, start))

def range_at(fields: +List<T.Field>, index: Nat, start: Nat, end: Nat) -> +List<T.Field>:
  match fields index:
    case Nil{} _: Nil{}
    case Con{field, rest} 0n: T.range(field, start, end) <> rest
    case Con{field, rest} 1n+p: field <> range_at(rest, p, start, end)

def field_correct(field: T.Field) -> Bool:
  T.F{+value, start, end, +goal} = field
  T.equal(value, goal)

def all_correct(fields: +List<T.Field>, +kind: TaskKind, index: Nat) -> Bool:
  match fields kind index:
    case Nil{} _ _: True{}
    case Con{field, rest} CopyOne{} 0n: all_correct(rest, kind, 1n)
    case Con{field, rest} CopyThree{} 0n: all_correct(rest, kind, 1n)
    case Con{field, rest} CopyThree{} 1n: all_correct(rest, kind, 2n)
    case Con{field, rest} CopyThree{} 2n: all_correct(rest, kind, 3n)
    case Con{field, rest} _ 0n: field_correct(field) && all_correct(rest, kind, 1n)
    case Con{field, rest} _ 1n+p: field_correct(field) && all_correct(rest, kind, 1n+p)

def correct_state(s: State) -> Bool:
  S{kind, fields, focus, clipboard, popup, popup_mode, popup_seen, outcome} = s
  all_correct(fields, kind, 0n)

def field_count(fields: +List<T.Field>) -> Nat:
  List.length(&2, T.Field, fields)

def trigger_login_popup(seen: Bool, mode: Nat, index: Nat) -> Bool:
  match seen:
    case True{}: False{}
    case False{}: Nat.is_eq(mode, 1n+index)

def trigger_popup(kind: TaskKind, mode: Nat, index: Nat, seen: Bool) -> Bool:
  match kind:
    case PopupLogin{}: trigger_login_popup(seen, mode, index)
    case _: False{}

def focus_result(show: Bool, kind: TaskKind, fields: +List<T.Field>, focus: Nat,
    clipboard: +List<U32>, popup: Bool, mode: Nat, seen: Bool,
    outcome: W.Outcome) -> State:
  match show:
    case True{}: S{kind, fields, focus, clipboard, True{}, mode, True{}, outcome}
    case False{}: S{kind, fields, focus, clipboard, popup, mode, seen, outcome}

def focus_update(valid: Bool, s: State, +index: Nat) -> State:
  match valid s:
    case False{} S{kind, fields, old_focus, clipboard, popup, mode, seen, outcome}:
      S{kind, fields, old_focus, clipboard, popup, mode, seen, outcome}
    case True{} S{+kind, +fields, old_focus, +clipboard, popup, +mode, +seen, outcome}:
      +updated = T.modify(fields, index, T.End{})
      +show = trigger_popup(kind, mode, index, seen)
      focus_result(show, kind, updated, 1n+index, clipboard, popup, mode, seen, outcome)

def focus_field(+s: State, +index: Nat) -> State:
  S{kind, fields, old_focus, clipboard, popup, mode, seen, outcome} = s
  focus_update(Nat.is_lt(index, field_count(fields)) && Bool.not(popup), s, index)

def edit_field(valid: Bool, s: State, action: T.Edit) -> State:
  match valid s:
    case False{} S{kind, fields, focus, clipboard, popup, mode, seen, outcome}:
      S{kind, fields, focus, clipboard, popup, mode, seen, outcome}
    case True{} S{kind, fields, 0n, clipboard, popup, mode, seen, outcome}:
      S{kind, fields, 0n, clipboard, popup, mode, seen, outcome}
    case True{} S{kind, fields, 1n+ +index, clipboard, popup, mode, seen, outcome}:
      S{kind, T.modify(fields, index, action), 1n+index, clipboard, popup, mode, seen, outcome}

def selection(valid: Bool, s: State, +start: Nat, +end: Nat) -> State:
  match valid s:
    case False{} S{kind, fields, focus, clipboard, popup, mode, seen, outcome}:
      S{kind, fields, focus, clipboard, popup, mode, seen, outcome}
    case True{} S{kind, fields, 0n, clipboard, popup, mode, seen, outcome}:
      S{kind, fields, 0n, clipboard, popup, mode, seen, outcome}
    case True{} S{kind, fields, 1n+ +index, clipboard, popup, mode, seen, outcome}:
      S{kind, range_at(fields, index, start, end), 1n+index, clipboard, popup, mode, seen, outcome}

def copy(valid: Bool, s: State) -> State:
  match valid s:
    case False{} S{kind, fields, focus, clipboard, popup, mode, seen, outcome}:
      S{kind, fields, focus, clipboard, popup, mode, seen, outcome}
    case True{} S{kind, +fields, 0n, clipboard, popup, mode, seen, outcome}:
      S{kind, fields, 0n, clipboard, popup, mode, seen, outcome}
    case True{} S{kind, +fields, 1n+ +index, clipboard, popup, mode, seen, outcome}:
      S{kind, fields, 1n+index, selected(field_at(fields, index)), popup, mode, seen, outcome}

def paste(valid: Bool, s: State) -> State:
  match valid s:
    case False{} S{kind, fields, focus, clipboard, popup, mode, seen, outcome}:
      S{kind, fields, focus, clipboard, popup, mode, seen, outcome}
    case True{} S{kind, fields, 0n, clipboard, popup, mode, seen, outcome}:
      S{kind, fields, 0n, clipboard, popup, mode, seen, outcome}
    case True{} S{kind, fields, 1n+ +index, +clipboard, popup, mode, seen, outcome}:
      S{kind, T.modify(fields, index, T.Insert{clipboard}), 1n+index, Nil{}, popup, mode, seen, outcome}

def tab_more(more: Bool, +s: State, kind: TaskKind, fields: +List<T.Field>,
    clipboard: +List<U32>, popup: Bool, mode: Nat, seen: Bool,
    outcome: W.Outcome, +next: Nat) -> State:
  match more:
    case True{}: focus_field(s, next)
    case False{}: S{kind, fields, 0n, clipboard, popup, mode, seen, outcome}

def tab_next(+s: State) -> State:
  match s:
    case S{kind, fields, focus, clipboard, True{}, mode, seen, outcome}: s
    case S{kind, fields, 0n, clipboard, False{}, mode, seen, outcome}:
      focus_field(s, 0n)
    case S{kind, +fields, 1n+ +index, clipboard, False{}, mode, seen, outcome}:
      +next = {1n+index : Nat}
      tab_more(Nat.is_lt(next, field_count(fields)), s, kind,
        fields, clipboard, False{}, mode, seen, outcome, next)

def finish_ok(s: State) -> State:
  S{+kind, +fields, focus, clipboard, popup, mode, seen, outcome} = s
  S{kind, fields, focus, clipboard, popup, mode, seen, W.binary(all_correct(fields, kind, 0n))}

def finish_fail(s: State) -> State:
  S{kind, fields, focus, clipboard, popup, mode, seen, outcome} = s
  S{kind, fields, focus, clipboard, popup, mode, seen, W.binary(False{})}

def finish(ok: Bool, +s: State) -> State:
  match ok:
    case True{}: finish_ok(s)
    case False{}: finish_fail(s)

def press(button: Button, +s: State) -> State:
  match button s:
    case Submit{} S{kind, fields, focus, clipboard, False{}, mode, seen, outcome}:
      finish(True{}, s)
    case Submit{} _: s
    case PopupOK{} S{kind, fields, focus, clipboard, True{}, mode, seen, outcome}:
      finish(False{}, s)
    case PopupOK{} _: s
    case PopupCancel{} S{kind, fields, focus, clipboard, True{}, mode, seen, outcome}:
      S{kind, fields, focus, clipboard, False{}, mode, seen, outcome}
    case PopupCancel{} _: s
    case Other{} _: s

def timed_out(+s: State) -> State:
  match s:
    case S{kind, fields, focus, clipboard, popup, mode, seen, W.Running{}}:
      S{kind, fields, focus, clipboard, popup, mode, seen, W.TimedOut{}}
    case _: s

def s_popup(+s: State) -> Bool:
  S{kind, fields, focus, clipboard, popup, popup_mode, popup_seen, outcome} = s
  popup

def active(action: Action, +s: State) -> State:
  match action:
    case Wait{}: s
    case Focus{index}: focus_field(s, index)
    case Editing{edit}: edit_field(Bool.not(s_popup(s)), s, edit)
    case SelectRange{start, end}: selection(Bool.not(s_popup(s)), s, start, end)
    case Copy{}: copy(Bool.not(s_popup(s)), s)
    case Paste{}: paste(Bool.not(s_popup(s)), s)
    case Tab{}: tab_next(s)
    case Press{button}: press(button, s)
    case Timeout{}: timed_out(s)

def running_step(expired: Bool, +s: State, action: Action) -> State:
  match expired:
    case True{}: timed_out(s)
    case False{}: active(action, s)

def step(+action: Action, s: State, elapsed: U32, limit: U32) -> State:
  match action s:
    case _ S{kind, fields, focus, clipboard, popup, mode, seen, W.Running{}}:
      running_step(U32.is_ge(elapsed, limit),
        S{kind, fields, focus, clipboard, popup, mode, seen, W.Running{}}, action)
    case _ S{kind, fields, focus, clipboard, popup, mode, seen, outcome}:
      S{kind, fields, focus, clipboard, popup, mode, seen, outcome}

def reward_raw(outcome: W.Outcome) -> F32:
  W.raw_reward(outcome)

def reward_at(outcome: W.Outcome, elapsed: U32, limit: U32) -> F32:
  match outcome:
    case W.Running{}: 0.0
    case W.TimedOut{}: (0.0 - 1.0 : F32)
    case W.Finished{matches, total, +scaled}:
      +raw = W.raw_reward(W.Finished{matches, total, scaled})
      Bool.pick(F32, scaled, (raw * F32.max(0.0, (1.0 - U32.to_f32(elapsed) / U32.to_f32(limit) : F32)) : F32), raw)
