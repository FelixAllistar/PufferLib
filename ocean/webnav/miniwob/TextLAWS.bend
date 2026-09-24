import Base
import ./Text.bend as T
import ./Widgets.bend as W

law edit_preserves_goal:
  for f: T.Field
  for action: T.Edit
  {T.goal(T.edit(action, f)) == T.goal(f) : +List<U32>}

law modify_preserves_goals:
  for fs: +List<T.Field>
  for index: Nat
  for action: T.Edit
  {T.targets(T.modify(fs, index, action)) == T.targets(fs) : +List<+List<U32>>}

law end_preserves_value:
  for f: T.Field
  {T.value(T.end(f)) == T.value(f) : +List<U32>}

law select_all_preserves_value:
  for f: T.Field
  {T.value(T.select_all(f)) == T.value(f) : +List<U32>}

law unfocused_edit_is_identity:
  for fs: +List<T.Field>
  for action: T.Edit
  {T.step(T.S{fs, 0n, W.Running{}}, T.Editing{action}) == T.S{fs, 0n, W.Running{}} : T.State}

law finished_absorbs_action:
  for fs: +List<T.Field>
  for focus: Nat
  for correct: U32
  for count: U32
  for scaled: Bool
  for action: T.Action
  {T.step(T.S{fs, focus, W.Finished{correct, count, scaled}}, action) == T.S{fs, focus, W.Finished{correct, count, scaled}} : T.State}

law timeout_absorbs_action:
  for fs: +List<T.Field>
  for focus: Nat
  for action: T.Action
  {T.step(T.S{fs, focus, W.TimedOut{}}, action) == T.S{fs, focus, W.TimedOut{}} : T.State}

law deadline_preempts_action:
  for s: T.State
  for action: T.Action
  {T.step_at(s, action, 10000) == T.step(s, T.Timeout{}) : T.State}

law first_edit_preserves_tail:
  for f: T.Field
  for fs: +List<T.Field>
  for action: T.Edit
  {T.modify(Con{f, fs}, 0n, action) == Con{T.edit(action, f), fs} : +List<T.Field>}

law drop_whole_text:
  for xs: +List<U32>
  {List.drop(&2, U32, xs, T.length(xs)) == Nil{} : +List<U32>}

law append_empty:
  for xs: +List<U32>
  {List.append(&2, U32, xs, Nil{}) == xs : +List<U32>}

law selected_all_replacement:
  for f: T.Field
  for +inserted: +List<U32>
  {T.value(T.edit(T.Insert{inserted}, T.select_all(f))) == inserted : +List<U32>}
