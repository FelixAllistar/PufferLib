import Base
import ./Text.bend as T
import ./Widgets.bend as W
import ./TextLAWS.bend as L

def L.edit_preserves_goal(f, action):
  match f action:
    case T.F{xs, start, end, goal} T.Insert{units}: {==}
    case T.F{xs, start, end, goal} T.Backspace{}: {==}
    case T.F{xs, start, end, goal} T.Delete{}: {==}
    case T.F{xs, start, end, goal} T.Left{}: {==}
    case T.F{xs, start, end, goal} T.Right{}: {==}
    case T.F{xs, start, end, goal} T.Home{}: {==}
    case T.F{xs, start, end, goal} T.End{}: {==}
    case T.F{xs, start, end, goal} T.SelectAll{}: {==}

def L.modify_preserves_goals(fs, index, action):
  match fs index:
    case Nil{} _: {==}
    case Con{f, rest} 0n:
      %L.edit_preserves_goal(f, action) : {Con{T.goal(T.edit(action, f)), T.targets(rest)} == Con{_, T.targets(rest)} : +List<+List<U32>>}
      {==}
    case Con{f, rest} 1n+p:
      %L.modify_preserves_goals(rest, p, action) : {Con{T.goal(f), T.targets(T.modify(rest, p, action))} == Con{T.goal(f), _} : +List<+List<U32>>}
      {==}

def L.end_preserves_value(f):
  match f:
    case T.F{xs, start, end, goal}: {==}

def L.select_all_preserves_value(f):
  match f:
    case T.F{xs, start, end, goal}: {==}

def L.unfocused_edit_is_identity(fs, action):
  {==}

def L.finished_absorbs_action(fs, focus, correct, count, scaled, action):
  {==}

def L.timeout_absorbs_action(fs, focus, action):
  {==}

def L.deadline_preempts_action(s, action):
  {==}

def L.first_edit_preserves_tail(f, fs, action):
  {==}

def L.drop_whole_text(xs):
  match xs:
    case Nil{}: {==}
    case Con{x, rest}: L.drop_whole_text(rest)

def L.append_empty(xs):
  match xs:
    case Nil{}: {==}
    case Con{x, rest}:
      %L.append_empty(rest) : {Con{x, List.append(&2, U32, rest, Nil{})} == Con{x, _} : +List<U32>}
      {==}

def L.selected_all_replacement(f, inserted):
  match f:
    case T.F{Nil{}, start, end, target}: L.append_empty(inserted)
    case T.F{Con{x, rest}, start, end, target}:
      Equal.trans(+List<U32>, List.append(&2, U32, inserted, List.drop(&2, U32, rest, T.length(rest))), List.append(&2, U32, inserted, Nil{}), inserted,
        Equal.cong(+List<U32>, +List<U32>, tail => List.append(&2, U32, inserted, tail), List.drop(&2, U32, rest, T.length(rest)), Nil{}, L.drop_whole_text(rest)),
        L.append_empty(inserted))
