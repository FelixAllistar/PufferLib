import Base
import ./Model.bend as M
import ./LAWS.bend as L

def L.sequence_miss_is_identity(one, two, clicks, first):
  {==}

def L.sequence_first_one_waits(one, two):
  {==}

def L.sequence_first_two_waits(one, two):
  {==}

def L.sequence_correct_second_click_succeeds(one, two):
  {==}

def L.sequence_wrong_second_click_fails(one, two):
  {==}

def L.sequence_overlap_uses_topmost_button():
  {==}

def L.sequence_left_edge_is_inside():
  {==}

def L.sequence_right_edge_is_outside():
  {==}

def L.sequence_reset_clears_history(one, two, clicks, first, outcome):
  {==}

def L.sequence_terminal_absorbs_action(one, two, success, action):
  {==}

def L.sequence_timeout_absorbs_action(one, two, action):
  {==}

def L.sequence_deadline_preempts_action(s, action):
  match s:
    case M.S{one, two, clicks, first, M.Running{}}: {==}
    case M.S{one, two, clicks, first, M.Finished{success}}: {==}
    case M.S{one, two, clicks, first, M.TimedOut{}}: {==}

def L.sequence_running_reward_is_zero(elapsed):
  {==}

def L.sequence_success_reward_is_positive():
  {==}

def L.sequence_failure_reward_is_negative():
  {==}

def L.choose_select_preserves_options(valid, options, target, selected, index):
  match valid:
    case True{}: {==}
    case False{}: {==}

def L.choose_select_preserves_target(valid, options, target, selected, index):
  match valid:
    case True{}: {==}
    case False{}: {==}

def L.choose_invalid_select_is_identity():
  {==}

def L.choose_select_last_write_wins():
  {==}

def L.choose_submit_compares_visible_text():
  {==}

def L.choose_submit_rejects_different_text():
  {==}

def L.choose_reset_clears_selection(options, target, selected, outcome):
  {==}

def L.choose_terminal_absorbs_action(options, target, selected, success, action):
  {==}

def L.choose_deadline_preempts_action(s, action):
  match s:
    case M.C{options, target, selected, M.Running{}}: {==}
    case M.C{options, target, selected, M.Finished{success}}: {==}
    case M.C{options, target, selected, M.TimedOut{}}: {==}

def L.choose_timeout_reward_is_negative(elapsed):
  {==}
