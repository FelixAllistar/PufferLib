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

law legacy_text_exact:
  {M.all_correct([T.F{[65, 98], 0n, 0n, [65, 98]}], M.EnterText{}, 0n) == True{} : Bool}

law legacy_text_is_case_sensitive:
  {M.all_correct([T.F{[97, 98], 0n, 0n, [65, 98]}], M.EnterText{}, 0n) == False{} : Bool}

law login_requires_both_credentials:
  {M.all_correct([T.F{[65], 0n, 0n, [65]}, T.F{[66], 0n, 0n, [67]}], M.Login{}, 0n) == False{} : Bool}

law table_value_exact:
  {M.all_correct([T.F{[49, 57], 0n, 0n, [49, 57]}], M.TableOne{}, 0n) == True{} : Bool}
