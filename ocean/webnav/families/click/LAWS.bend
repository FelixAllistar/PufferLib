import Base
import ../../miniwob/Widgets.bend as W
import ./Model.bend as M

law direct_uses_target:
  for role: W.Role
  for selected: Bool
  for target: Bool
  for nodes: +List<W.Node>
  {M.result(M.Direct{}, W.N{role, selected, target}, nodes) == W.binary(target) : W.Outcome}

law focus_uses_target:
  for role: W.Role
  for selected: Bool
  for target: Bool
  for nodes: +List<W.Node>
  {M.result(M.Focus{}, W.N{role, selected, target}, nodes) == W.binary(target) : W.Outcome}

law focus_blurs:
  for index: U32
  {M.focus(M.Focus{}, index) == 0 : U32}

law invalid_preserves_state:
  for mode: M.Mode
  for nodes: +List<W.Node>
  for focus: U32
  for index: U32
  {M.active(False{}, mode, nodes, focus, index) == M.S{mode, nodes, focus, W.Running{}} : M.State}

law checkbox_submit_uses_fractional_score:
  for selected: Bool
  for target: Bool
  for nodes: +List<W.Node>
  {M.result(M.Form{}, W.N{W.Button{}, selected, target}, nodes) == W.scored(W.score(nodes, 0, 0)) : W.Outcome}

law terminal_click_absorbs:
  for mode: M.Mode
  for nodes: +List<W.Node>
  for focus: U32
  for correct: U32
  for count: U32
  for scale: Bool
  for index: U32
  {M.click(M.S{mode, nodes, focus, W.Finished{correct, count, scale}}, index) == M.S{mode, nodes, focus, W.Finished{correct, count, scale}} : M.State}

law terminal_timeout_absorbs:
  for mode: M.Mode
  for nodes: +List<W.Node>
  for focus: U32
  for correct: U32
  for count: U32
  for scale: Bool
  {M.timeout(M.S{mode, nodes, focus, W.Finished{correct, count, scale}}) == M.S{mode, nodes, focus, W.Finished{correct, count, scale}} : M.State}

law wait_preserves_state:
  for s: M.State
  for index: U32
  {M.guarded(False{}, False{}, s, index) == s : M.State}

law deadline_preempts_click:
  for s: M.State
  for index: U32
  {M.guarded(True{}, True{}, s, index) == M.timeout(s) : M.State}
