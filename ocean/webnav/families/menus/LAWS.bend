import Base
import ./Model.bend as M

law hovering_root_does_not_reveal_grandchild:
  {M.visible_ref(3, 0, True{}, 1, [M.N{0, 0, False{}, True{}, True{}, 0}, M.N{0, 1, False{}, True{}, True{}, 0}, M.N{0, 2, True{}, True{}, False{}, 0}]) == False{} : Bool}

law hovering_child_keeps_ancestor_expanded:
  {M.expanded_ref(1, True{}, 2, [M.N{0, 0, False{}, True{}, True{}, 0}, M.N{0, 1, False{}, True{}, True{}, 0}, M.N{0, 2, True{}, True{}, False{}, 0}]) == True{} : Bool}

law wait_is_identity:
  for s: M.State
  {M.act(s, M.Wait{}) == s : M.State}

law invalid_zero_ref_is_ignored:
  for task: U32
  for count: U32
  for nodes: +List<M.Node>
  for open: Bool
  for hover: U32
  for outcome: M.Outcome
  {M.choose(M.S{task, count, nodes, open, hover, outcome}, 0) == M.S{task, count, nodes, open, hover, outcome} : M.State}

law expired_action_is_timeout:
  for s: M.State
  for ref: U32
  {M.running(True{}, s, M.Choose{ref}) == M.timeout(s) : M.State}

law terminal_action_is_absorbed:
  for task: U32
  for count: U32
  for nodes: +List<M.Node>
  for open: Bool
  for hover: U32
  for success: Bool
  for elapsed: U32
  for deadline: U32
  {M.step(M.S{task, count, nodes, open, hover, M.Finished{success}}, M.Choose{1}, elapsed, deadline) == M.S{task, count, nodes, open, hover, M.Finished{success}} : M.State}

law visible_root_menu_item:
  for open: Bool
  for hover: U32
  {M.visible_ref(1, 0, open, hover, [M.N{0, 0, False{}, True{}, False{}, 0}]) == True{} : Bool}

law closed_second_menu_hides_items:
  {M.visible_ref(2, 1, False{}, 0, [M.N{1, 0, False{}, True{}, False{}, 0}, M.N{0, 0, False{}, True{}, False{}, 0}]) == False{} : Bool}

law hovering_branch_reveals_child:
  {M.visible_ref(2, 0, True{}, 1, [M.N{0, 0, False{}, True{}, True{}, 0}, M.N{0, 1, False{}, True{}, False{}, 0}]) == True{} : Bool}

law correct_leaf_scores_success:
  {M.choose(M.S{0, 1, [M.N{0, 0, True{}, True{}, False{}, 0}], True{}, 0, M.Running{}}, 1) == M.S{0, 1, [M.N{0, 0, True{}, True{}, False{}, 0}], True{}, 0, M.Finished{True{}}} : M.State}

law disabled_leaf_scores_failure:
  {M.choose(M.S{1, 1, [M.N{0, 0, False{}, False{}, False{}, 8}], True{}, 0, M.Running{}}, 1) == M.S{1, 1, [M.N{0, 0, False{}, False{}, False{}, 8}], True{}, 0, M.Running{}} : M.State}

law click_menu_branch_selection_fails:
  {M.choose(M.S{0, 1, [M.N{0, 0, False{}, True{}, True{}, 0}], True{}, 0, M.Running{}}, 1) == M.S{0, 1, [M.N{0, 0, False{}, True{}, True{}, 0}], True{}, 0, M.Finished{False{}}} : M.State}

law opening_button_shows_menu:
  {M.choose(M.S{1, 1, [M.N{1, 0, False{}, True{}, False{}, 0}], False{}, 0, M.Running{}}, 1) == M.S{1, 1, [M.N{1, 0, False{}, True{}, False{}, 0}], True{}, 0, M.Running{}} : M.State}
