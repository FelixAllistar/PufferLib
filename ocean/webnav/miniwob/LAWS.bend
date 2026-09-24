import Base
import ./Generate.bend as G
import ./Widgets.bend as W

law toggle_preserves_goal:
  for node: W.Node
  {W.goal(W.toggle(node)) == W.goal(node) : Bool}

law toggle_twice:
  for node: W.Node
  {W.toggle(W.toggle(node)) == node : W.Node}

law finished_absorbs_click:
  for task: W.Task
  for nodes: +List<W.Node>
  for focus: U32
  for correct: U32
  for count: U32
  for scaled: Bool
  for index: U32
  {W.click(W.S{task, nodes, focus, W.Finished{correct, count, scaled}}, index) == W.S{task, nodes, focus, W.Finished{correct, count, scaled}} : W.State}

law timeout_absorbs_click:
  for task: W.Task
  for nodes: +List<W.Node>
  for focus: U32
  for index: U32
  {W.click(W.S{task, nodes, focus, W.TimedOut{}}, index) == W.S{task, nodes, focus, W.TimedOut{}} : W.State}

law timeout_preserves_finished:
  for task: W.Task
  for nodes: +List<W.Node>
  for focus: U32
  for correct: U32
  for count: U32
  for scaled: Bool
  {W.timeout(W.S{task, nodes, focus, W.Finished{correct, count, scaled}}) == W.S{task, nodes, focus, W.Finished{correct, count, scaled}} : W.State}

# Clicking any position cannot change any widget's task goal.
law toggle_at_preserves_goals:
  for nodes: +List<W.Node>
  for index: Nat
  {W.goals(W.toggle_at(nodes, index)) == W.goals(nodes) : +List<Bool>}

law empty_click_is_identity:
  for task: W.Task
  for focus: U32
  for index: U32
  {W.click(W.S{task, Nil{}, focus, W.Running{}}, index) == W.S{task, Nil{}, focus, W.Running{}} : W.State}

law guarded_preserves_task:
  for valid: Bool
  for task: W.Task
  for nodes: +List<W.Node>
  for focus: U32
  for index: U32
  {W.task_of(W.guarded(valid, task, nodes, focus, index)) == task : W.Task}

law click_preserves_task:
  for s: W.State
  for +index: U32
  {W.task_of(W.click(s, index)) == W.task_of(s) : W.Task}

law running_reward_is_zero:
  for elapsed: U32
  {W.reward_at(W.Running{}, elapsed) == 0.0 : F32}

law timeout_reward_is_negative_one:
  for elapsed: U32
  {W.reward_at(W.TimedOut{}, elapsed) == (0.0 - 1.0 : F32) : F32}

law untimed_reward_preserves_raw:
  for correct: U32
  for count: U32
  for elapsed: U32
  {W.reward_at(W.Finished{correct, count, False{}}, elapsed) == W.raw_reward(W.Finished{correct, count, False{}}) : F32}

law deadline_preempts_action:
  for s: W.State
  for action: W.Action
  {W.step_at(s, action, 10000) == W.timeout(s) : W.State}

law radio_value_preserves_goal:
  for node: W.Node
  for selected: Bool
  {W.goal(W.radio_value(node, selected)) == W.goal(node) : Bool}

law radio_value_overwrites:
  for node: W.Node
  for first: Bool
  for last: Bool
  {W.radio_value(W.radio_value(node, first), last) == W.radio_value(node, last) : W.Node}

law radio_set_preserves_goals:
  for nodes: +List<W.Node>
  for +index: Nat
  for +at: Nat
  {W.goals(W.radio_set(nodes, index, at)) == W.goals(nodes) : +List<Bool>}

law radio_set_overwrites:
  for nodes: +List<W.Node>
  for +first: Nat
  for +last: Nat
  for +at: Nat
  {W.radio_set(W.radio_set(nodes, first, at), last, at) == W.radio_set(nodes, last, at) : +List<W.Node>}


law generated_nodes_start_clear:
  for es: +List<G.Entry>
  for +radio: Bool
  {G.all_clear(G.nodes(es, radio)) == True{} : Bool}

law generated_form_starts_initial:
  for f: G.Form
  {G.is_initial(G.state(f)) == True{} : Bool}

law radio_value_sets_radio:
  for old: Bool
  for target: Bool
  for selected: Bool
  {W.checked(W.radio_value(W.N{W.Radio{}, old, target}, selected)) == selected : Bool}

law radio_selection_preserves_checkboxes:
  for old: Bool
  for target: Bool
  for selected: Bool
  {W.radio_value(W.N{W.Checkbox{}, old, target}, selected) == W.N{W.Checkbox{}, old, target} : W.Node}

law generated_node_count:
  for es: +List<G.Entry>
  for +radio: Bool
  {List.length(&2, W.Node, G.nodes(es, radio)) == 1n+List.length(&2, G.Entry, es) : Nat}
