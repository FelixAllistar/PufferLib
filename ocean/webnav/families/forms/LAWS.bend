import Base
import ./Model.bend as M
import ../../miniwob/Widgets.bend as W
import ../../miniwob/Text.bend as T

law finished_absorbs:
  for kind: M.TaskKind
  for fields: +List<T.Field>
  for focus: Nat
  for clipboard: +List<U32>
  for popup: Bool
  for mode: Nat
  for seen: Bool
  for matches: U32
  for total: U32
  for scaled: Bool
  for action: M.Action
  for elapsed: U32
  for limit: U32
  {M.step(action, M.S{kind, fields, focus, clipboard, popup, mode, seen, W.Finished{matches, total, scaled}}, elapsed, limit) == M.S{kind, fields, focus, clipboard, popup, mode, seen, W.Finished{matches, total, scaled}} : M.State}

law timeout_absorbs:
  for kind: M.TaskKind
  for fields: +List<T.Field>
  for focus: Nat
  for clipboard: +List<U32>
  for popup: Bool
  for mode: Nat
  for seen: Bool
  for action: M.Action
  for elapsed: U32
  for limit: U32
  {M.step(action, M.S{kind, fields, focus, clipboard, popup, mode, seen, W.TimedOut{}}, elapsed, limit) == M.S{kind, fields, focus, clipboard, popup, mode, seen, W.TimedOut{}} : M.State}

law deadline_preempts:
  for kind: M.TaskKind
  for fields: +List<T.Field>
  for focus: Nat
  for clipboard: +List<U32>
  for popup: Bool
  for mode: Nat
  for seen: Bool
  for action: M.Action
  {M.running_step(True{}, M.S{kind, fields, focus, clipboard, popup, mode, seen, W.Running{}}, action) == M.S{kind, fields, focus, clipboard, popup, mode, seen, W.TimedOut{}} : M.State}

law popup_cancel_restores_form:
  for kind: M.TaskKind
  for fields: +List<T.Field>
  for focus: Nat
  for clipboard: +List<U32>
  for mode: Nat
  for seen: Bool
  {M.press(M.PopupCancel{}, M.S{kind, fields, focus, clipboard, True{}, mode, seen, W.Running{}}) == M.S{kind, fields, focus, clipboard, False{}, mode, seen, W.Running{}} : M.State}

law popup_ok_fails:
  for kind: M.TaskKind
  for fields: +List<T.Field>
  for focus: Nat
  for clipboard: +List<U32>
  for mode: Nat
  for seen: Bool
  {M.press(M.PopupOK{}, M.S{kind, fields, focus, clipboard, True{}, mode, seen, W.Running{}}) == M.S{kind, fields, focus, clipboard, True{}, mode, seen, W.Finished{0, 1, False{}}} : M.State}
