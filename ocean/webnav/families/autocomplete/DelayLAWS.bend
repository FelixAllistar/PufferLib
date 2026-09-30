import Base
import ./Delayed.bend as D
import ../../miniwob/autocomplete/Model.bend as M
import ../../miniwob/Text.bend as T
import ../../miniwob/Widgets.bend as W

law edit_keeps_old_menu_until_debounce:
  {D.step(D.D{M.S{M.Q{[65, 108], [97], True{}},
    T.F{Nil{}, 0n, 0n, Nil{}}, 1n, M.Closed{}, W.Running{}},
    0, Nil{}, False{}}, M.Editing{T.Insert{[65, 108]}}, 100) ==
   D.D{M.S{M.Q{[65, 108], [97], True{}},
    T.F{[65, 108], 2n, 2n, Nil{}}, 1n, M.Closed{}, W.Running{}},
    400, Nil{}, False{}} : D.State}

law before_due_does_not_publish_menu:
  for s: M.State
  for last: +List<U32>
  {D.advance(D.D{s, 400, last, False{}}, 399) ==
   D.D{s, 400, last, False{}} : D.State}

law exact_deadline_preempts_new_edit:
  {D.step(D.D{M.S{M.Q{[65, 108], [97], True{}},
    T.F{Nil{}, 0n, 0n, Nil{}}, 1n, M.Closed{}, W.Running{}},
    0, Nil{}, False{}}, M.Editing{T.Insert{[65, 108]}}, 10000) ==
   D.D{M.S{M.Q{[65, 108], [97], True{}},
    T.F{Nil{}, 0n, 0n, Nil{}}, 1n, M.Closed{}, W.TimedOut{}},
    0, Nil{}, False{}} : D.State}

law terminal_absorbs_pending_callback:
  for task: M.Task
  for field: T.Field
  for focus: Nat
  for menu: M.Menu
  for due: U32
  for last: +List<U32>
  for action: M.Action
  for elapsed: U32
  {D.step(D.D{M.S{task, field, focus, menu, W.TimedOut{}},
    due, last, False{}}, action, elapsed) ==
   D.D{M.S{task, field, focus, menu, W.TimedOut{}},
    due, last, False{}} : D.State}
