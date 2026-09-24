import Base
import ./Model.bend as M
import ./LAWS.bend as L

def L.terminal_absorbs(active, panels, accordion, target, medium, success, action, elapsed, deadline):
  {==}

def L.timeout_absorbs(active, panels, accordion, target, medium, action, elapsed, deadline):
  {==}

def L.expired_preempts_action(active, panels, accordion, target, medium, action):
  {==}

def L.invalid_header_preserves_state(s, panel):
  {==}

def L.hidden_link_preserves_state(s, correct):
  {==}

def L.visible_link_uses_goal(s, correct):
  {==}

def L.medium_absent_target_accepts_second_tab():
  {==}

def L.medium_present_target_rejects_second_tab():
  {==}

def L.accordion_closes_selected_panel():
  {==}

def L.tabs_keep_selected_panel():
  {==}

def L.closed_submit_fails():
  {==}

def L.open_submit_succeeds():
  {==}

def L.untimed_failure_reward(elapsed, deadline):
  {==}

def L.timeout_reward():
  {==}

