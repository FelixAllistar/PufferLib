import Base
import ./Tree.bend as T

# Laws for the bounded matched-instance navigate-tree model.  The target bit is
# intentionally treated as private imported data; the laws constrain the
# browser transition semantics around it rather than deriving it from labels.

law toggle_preserves_target:
  for node: T.Node
  {T.target_of(T.toggle_node(node)) == T.target_of(node) : Bool}

law toggle_preserves_parent:
  for node: T.Node
  {T.parent_of(T.toggle_node(node)) == T.parent_of(node) : U32}

law toggle_twice:
  for node: T.Node
  {T.toggle_node(T.toggle_node(node)) == node : T.Node}

law toggle_at_preserves_targets:
  for nodes: +List<T.Node>
  for index: Nat
  {T.node_targets(T.toggle_at(nodes, index)) == T.node_targets(nodes) : +List<Bool>}

law toggle_at_preserves_parents:
  for nodes: +List<T.Node>
  for index: Nat
  {T.node_parents(T.toggle_at(nodes, index)) == T.node_parents(nodes) : +List<U32>}

law toggle_at_head_preserves_tail:
  for node: T.Node
  for rest: +List<T.Node>
  {T.toggle_at(node <> rest, 0n) == T.toggle_node(node) <> rest : +List<T.Node>}

law root_is_visible:
  for kind: T.TreeKind
  for target: Bool
  for expanded: Bool
  for subtree_end: U32
  {T.visible_node([T.N{kind, target, expanded, 0, subtree_end}], T.N{kind, target, expanded, 0, subtree_end}) == True{} : Bool}

law collapsed_parent_hides_child:
  for target: Bool
  {T.visible_at([T.N{T.Folder{}, False{}, False{}, 0, 2}, T.N{T.File{}, target, False{}, 1, 2}], 1n) == False{} : Bool}

law expanded_parent_shows_child:
  for target: Bool
  {T.visible_at([T.N{T.Folder{}, False{}, True{}, 0, 2}, T.N{T.File{}, target, False{}, 1, 2}], 1n) == True{} : Bool}

law wrong_file_finishes_failure:
  {T.click([T.N{T.File{}, False{}, False{}, 0, 1}], 0, 0) == T.S{[T.N{T.File{}, False{}, False{}, 0, 1}], 1, T.Finished{False{}}} : T.State}

law wrong_folder_expands_and_runs:
  {T.click([T.N{T.Folder{}, False{}, False{}, 0, 1}], 0, 0) == T.S{[T.N{T.Folder{}, False{}, True{}, 0, 1}], 1, T.Running{}} : T.State}

law target_folder_expands_and_succeeds:
  {T.click([T.N{T.Folder{}, True{}, False{}, 0, 1}], 0, 0) == T.S{[T.N{T.Folder{}, True{}, True{}, 0, 1}], 1, T.Finished{True{}}} : T.State}

# A wrong descendant folder's handler returns undefined, so the click bubbles
# through ancestor li handlers.  The ancestor target can therefore finish the
# episode; only the clicked folder toggles its child ul.
law target_ancestor_bubbles:
  {T.click([T.N{T.Folder{}, True{}, True{}, 0, 2}, T.N{T.Folder{}, False{}, False{}, 1, 2}], 0, 1) == T.S{[T.N{T.Folder{}, True{}, True{}, 0, 2}, T.N{T.Folder{}, False{}, True{}, 1, 2}], 2, T.Finished{True{}}} : T.State}

law hidden_click_is_identity:
  {T.click([T.N{T.Folder{}, False{}, False{}, 0, 2}, T.N{T.File{}, True{}, False{}, 1, 2}], 0, 1) == T.S{[T.N{T.Folder{}, False{}, False{}, 0, 2}, T.N{T.File{}, True{}, False{}, 1, 2}], 0, T.Running{}} : T.State}

law finished_absorbs_click:
  for nodes: +List<T.Node>
  for last: U32
  for success: Bool
  for index: U32
  {T.act(T.Click{index}, nodes, last, T.Finished{success}) == T.S{nodes, last, T.Finished{success}} : T.State}

law timeout_absorbs_click:
  for nodes: +List<T.Node>
  for last: U32
  for index: U32
  {T.act(T.Click{index}, nodes, last, T.TimedOut{}) == T.S{nodes, last, T.TimedOut{}} : T.State}

law timeout_preserves_finished:
  for nodes: +List<T.Node>
  for last: U32
  for success: Bool
  {T.timeout(nodes, last, T.Finished{success}) == T.S{nodes, last, T.Finished{success}} : T.State}

law deadline_preempts_click:
  for nodes: +List<T.Node>
  for last: U32
  for outcome: T.Outcome
  for index: U32
  {T.step_at(T.S{nodes, last, outcome}, T.Click{index}, 10000) == T.timeout(nodes, last, outcome) : T.State}

law running_reward_is_zero:
  for elapsed: U32
  {T.reward_at(T.Running{}, elapsed) == 0.0 : F32}

law timeout_reward_is_negative_one:
  for elapsed: U32
  {T.reward_at(T.TimedOut{}, elapsed) == (0.0 - 1.0 : F32) : F32}

law failed_leaf_reward_is_untimed:
  for elapsed: U32
  {T.reward_at(T.Finished{False{}}, elapsed) == (0.0 - 1.0 : F32) : F32}

law success_reward_scales:
  for elapsed: U32
  {T.reward_at(T.Finished{True{}}, elapsed) == F32.max(0.0, (1.0 - U32.to_f32(elapsed) / 10000.0 : F32)) : F32}
