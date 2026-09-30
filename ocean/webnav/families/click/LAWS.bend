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

law sequence_first_click_never_finishes:
  for node: W.Node
  for nodes: +List<W.Node>
  for index: U32
  {M.click_result(M.Sequence{}, node, nodes, 0, index) == W.Running{} : W.Outcome}

law sequence_one_then_two_succeeds:
  for node: W.Node
  for nodes: +List<W.Node>
  {M.click_result(M.Sequence{}, node, nodes, 1, 1) == W.binary(True{}) : W.Outcome}

law sequence_wrong_first_fails:
  for node: W.Node
  for nodes: +List<W.Node>
  for index: U32
  {M.click_result(M.Sequence{}, node, nodes, 2, index) == W.binary(False{}) : W.Outcome}

law sequence_repeated_one_fails:
  for node: W.Node
  for nodes: +List<W.Node>
  {M.click_result(M.Sequence{}, node, nodes, 1, 0) == W.binary(False{}) : W.Outcome}

law option_submit_uses_selected_target:
  for selected: Bool
  for target: Bool
  for nodes: +List<W.Node>
  {M.result(M.Options{}, W.N{W.Button{}, selected, target}, nodes) == W.binary(W.radio_correct(nodes)) : W.Outcome}
