import Base
import ./Model.bend as M
import ../Text.bend as T
import ../Widgets.bend as W

# The autocomplete lane has its own laws in addition to the shared text laws.
# They intentionally keep the browser's case-sensitive goal predicate separate
# from the case-insensitive suggestion source.
law edit_preserves_task:
  for task: M.Task
  for field: T.Field
  for menu: M.Menu
  for edit: T.Edit
  {M.task_of(M.step(M.S{task, field, 1n, menu, W.Running{}}, M.Editing{edit})) == task : M.Task}

law terminal_absorbs_action:
  for task: M.Task
  for field: T.Field
  for menu: M.Menu
  for correct: U32
  for count: U32
  for scaled: Bool
  for action: M.Action
  {M.step(M.S{task, field, 2n, menu, W.Finished{correct, count, scaled}}, action) == M.S{task, field, 2n, menu, W.Finished{correct, count, scaled}} : M.State}

law timeout_absorbs_action:
  for task: M.Task
  for field: T.Field
  for menu: M.Menu
  for action: M.Action
  {M.step(M.S{task, field, 1n, menu, W.TimedOut{}}, action) == M.S{task, field, 1n, menu, W.TimedOut{}} : M.State}

law deadline_preempts_action:
  for task: M.Task
  for field: T.Field
  for menu: M.Menu
  for action: M.Action
  {M.step_at(M.S{task, field, 1n, menu, W.Running{}}, action, 10000) == M.step(M.S{task, field, 1n, menu, W.Running{}}, M.Timeout{}) : M.State}

law closed_menu_select_is_identity:
  for task: M.Task
  for field: T.Field
  {M.step(M.S{task, field, 1n, M.Closed{}, W.Running{}}, M.MenuSelect{0n}) == M.S{task, field, 1n, M.Closed{}, W.Running{}} : M.State}

law unfocused_down_is_identity:
  for task: M.Task
  for field: T.Field
  for menu: M.Menu
  {M.step(M.S{task, field, 0n, menu, W.Running{}}, M.MenuDown{}) == M.S{task, field, 0n, menu, W.Running{}} : M.State}

law unfocused_up_is_identity:
  for task: M.Task
  for field: T.Field
  for menu: M.Menu
  {M.step(M.S{task, field, 0n, menu, W.Running{}}, M.MenuUp{}) == M.S{task, field, 0n, menu, W.Running{}} : M.State}

law unfocused_accept_is_identity:
  for task: M.Task
  for field: T.Field
  for menu: M.Menu
  {M.step(M.S{task, field, 0n, menu, W.Running{}}, M.MenuAccept{}) == M.S{task, field, 0n, menu, W.Running{}} : M.State}

law submit_closes_popup:
  for task: M.Task
  for field: T.Field
  for menu: M.Menu
  {M.step(M.S{task, field, 1n, menu, W.Running{}}, M.Submit{}) == M.S{task, field, 2n, M.Closed{}, W.binary(M.satisfies(task, field))} : M.State}

law prefix_accepts_non_country:
  {M.satisfies(M.Q{M.units("Ca"), M.units("zz"), False{}}, T.F{M.units("Cactus"), 0n, 0n, Nil{}}) == True{} : Bool}

law suffix_is_case_sensitive:
  {M.satisfies(M.Q{M.units("Ca"), M.units("DA"), True{}}, T.F{M.units("Canada"), 0n, 0n, Nil{}}) == False{} : Bool}

law filter_preserves_source_order:
  {M.filtered(["Canada", "Cambodia", "Cameroon"], "ca") == ["Canada", "Cambodia", "Cameroon"] : +List<String>}

law menu_filter_is_case_insensitive:
  {M.menu_count_term(M.units("CA")) == M.menu_count_term(M.units("ca")) : Nat}

law menu_item_is_source_ordered:
  {M.item_at(M.menu_items_term(M.units("ca")), 0n) == Some{"Cambodia"} : Maybe<&2, String>}
