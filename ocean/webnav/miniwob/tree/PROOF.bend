import Base
import ./Tree.bend as T
import ./LAWS.bend as L

def L.toggle_preserves_target(node):
  match node:
    case T.N{T.File{}, target, expanded, parent, subtree_end}: {==}
    case T.N{T.Folder{}, target, expanded, parent, subtree_end}: {==}

def L.toggle_preserves_parent(node):
  match node:
    case T.N{T.File{}, target, expanded, parent, subtree_end}: {==}
    case T.N{T.Folder{}, target, expanded, parent, subtree_end}: {==}

def L.toggle_twice(node):
  match node:
    case T.N{T.File{}, target, expanded, parent, subtree_end}: {==}
    case T.N{T.Folder{}, target, False{}, parent, subtree_end}: {==}
    case T.N{T.Folder{}, target, True{}, parent, subtree_end}: {==}

def L.toggle_at_preserves_targets(nodes, index):
  match nodes index:
    case Nil{} _: {==}
    case Con{node, rest} 0n:
      %L.toggle_preserves_target(node) : {Con{T.target_of(T.toggle_node(node)), T.node_targets(rest)} == Con{_, T.node_targets(rest)} : +List<Bool>}
      {==}
    case Con{node, rest} 1n+p:
      %L.toggle_at_preserves_targets(rest, p) : {Con{T.target_of(node), T.node_targets(T.toggle_at(rest, p))} == Con{T.target_of(node), _} : +List<Bool>}
      {==}

def L.toggle_at_preserves_parents(nodes, index):
  match nodes index:
    case Nil{} _: {==}
    case Con{node, rest} 0n:
      %L.toggle_preserves_parent(node) : {Con{T.parent_of(T.toggle_node(node)), T.node_parents(rest)} == Con{_, T.node_parents(rest)} : +List<U32>}
      {==}
    case Con{node, rest} 1n+p:
      %L.toggle_at_preserves_parents(rest, p) : {Con{T.parent_of(node), T.node_parents(T.toggle_at(rest, p))} == Con{T.parent_of(node), _} : +List<U32>}
      {==}

def L.toggle_at_head_preserves_tail(node, rest):
  {==}

def L.root_is_visible(kind, target, expanded, subtree_end):
  {==}

def L.collapsed_parent_hides_child(target):
  {==}

def L.expanded_parent_shows_child(target):
  {==}

def L.wrong_file_finishes_failure():
  {==}

def L.wrong_folder_expands_and_runs():
  {==}

def L.target_folder_expands_and_succeeds():
  {==}

def L.target_ancestor_bubbles():
  {==}

def L.hidden_click_is_identity():
  {==}

def L.finished_absorbs_click(nodes, last, success, index):
  match success:
    case False{}: {==}
    case True{}: {==}

def L.timeout_absorbs_click(nodes, last, index):
  {==}

def L.timeout_preserves_finished(nodes, last, success):
  {==}

def L.deadline_preempts_click(nodes, last, outcome, index):
  match outcome:
    case T.Running{}: {==}
    case T.Finished{success}: {==}
    case T.TimedOut{}: {==}

def L.running_reward_is_zero(elapsed):
  {==}

def L.timeout_reward_is_negative_one(elapsed):
  {==}

def L.failed_leaf_reward_is_untimed(elapsed):
  {==}

def L.success_reward_scales(elapsed):
  {==}
