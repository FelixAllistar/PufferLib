import Base
import ./Model.bend as M
import ./LAWS.bend as L
import ../Text.bend as T
import ../Widgets.bend as W

def L.edit_preserves_task(task, field, menu, edit):
  match edit:
    case T.Insert{units}: {==}
    case T.Backspace{}: {==}
    case T.Delete{}: {==}
    case T.Left{}: {==}
    case T.Right{}: {==}
    case T.Home{}: {==}
    case T.End{}: {==}
    case T.SelectAll{}: {==}

def L.terminal_absorbs_action(task, field, menu, correct, count, scaled, action):
  {==}

def L.timeout_absorbs_action(task, field, menu, action):
  {==}

def L.deadline_preempts_action(task, field, menu, action):
  {==}

def L.closed_menu_select_is_identity(task, field):
  {==}

def L.unfocused_down_is_identity(task, field, menu):
  {==}

def L.unfocused_up_is_identity(task, field, menu):
  {==}

def L.unfocused_accept_is_identity(task, field, menu):
  {==}

def L.submit_closes_popup(task, field, menu):
  {==}

def L.prefix_accepts_non_country():
  {==}

def L.suffix_is_case_sensitive():
  {==}

def L.filter_preserves_source_order():
  {==}

def L.menu_filter_is_case_insensitive():
  {==}

def L.menu_item_is_source_ordered():
  {==}
