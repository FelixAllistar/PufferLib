import Base
import ../../miniwob/PROOF.bend as WidgetProof
import ./Model.bend as M
import ./LAWS.bend as L

def L.direct_uses_target(role, selected, target, nodes):
  {==}

def L.focus_uses_target(role, selected, target, nodes):
  {==}

def L.focus_blurs(index):
  {==}

def L.invalid_preserves_state(mode, nodes, focus, index):
  {==}

def L.checkbox_submit_uses_fractional_score(selected, target, nodes):
  {==}

def L.terminal_click_absorbs(mode, nodes, focus, correct, count, scale, index):
  {==}

def L.terminal_timeout_absorbs(mode, nodes, focus, correct, count, scale):
  {==}

def L.wait_preserves_state(s, index):
  {==}

def L.deadline_preempts_click(s, index):
  {==}

def L.sequence_first_click_never_finishes(node, nodes, index):
  {==}

def L.sequence_one_then_two_succeeds(node, nodes):
  {==}

def L.sequence_wrong_first_fails(node, nodes, index):
  {==}

def L.sequence_repeated_one_fails(node, nodes):
  {==}

def L.option_submit_uses_selected_target(selected, target, nodes):
  {==}
