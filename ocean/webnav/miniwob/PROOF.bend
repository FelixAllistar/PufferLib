import Base
import ./training/TreeGeneratePROOF.bend as TreeGenerateProof
import ./training/SequenceGeneratePROOF.bend as SequenceGenerateProof
import ./TextPROOF.bend as TextProof
import ./sequence_select/PROOF.bend as SequenceSelectProof
import ./tree/PROOF.bend as TreeProof
import ./autocomplete/PROOF.bend as AutocompleteProof
import ./Generate.bend as G
import ./Widgets.bend as W
import ./LAWS.bend as L

def L.toggle_preserves_goal(node):
  match node:
    case W.N{W.Checkbox{}, selected, goal}: {==}
    case W.N{W.Button{}, selected, goal}: {==}
    case W.N{W.Textbox{}, selected, goal}: {==}
    case W.N{W.Other{}, selected, goal}: {==}
    case W.N{W.PointerTarget{}, selected, goal}: {==}
    case W.N{W.Radio{}, selected, goal}: {==}

def L.toggle_twice(node):
  match node:
    case W.N{W.Checkbox{}, False{}, goal}: {==}
    case W.N{W.Checkbox{}, True{}, goal}: {==}
    case W.N{W.Button{}, selected, goal}: {==}
    case W.N{W.Textbox{}, selected, goal}: {==}
    case W.N{W.Other{}, selected, goal}: {==}
    case W.N{W.PointerTarget{}, selected, goal}: {==}
    case W.N{W.Radio{}, selected, goal}: {==}

def L.finished_absorbs_click(task, nodes, focus, correct, count, scaled, index):
  {==}

def L.timeout_absorbs_click(task, nodes, focus, index):
  {==}

def L.timeout_preserves_finished(task, nodes, focus, correct, count, scaled):
  {==}

def L.toggle_at_preserves_goals(nodes, index):
  match nodes index:
    case Nil{} _: {==}
    case Con{n, rest} 0n:
      %L.toggle_preserves_goal(n) : {Con{W.goal(W.toggle(n)), W.goals(rest)} == Con{_, W.goals(rest)} : +List<Bool>}
      {==}
    case Con{n, rest} 1n+p:
      %L.toggle_at_preserves_goals(rest, p) : {Con{W.goal(n), W.goals(W.toggle_at(rest, p))} == Con{W.goal(n), _} : +List<Bool>}
      {==}

def L.empty_click_is_identity(task, focus, index):
  {==}

def L.guarded_preserves_task(valid, task, nodes, focus, index):
  match valid:
    case True{}: {==}
    case False{}: {==}

def L.click_preserves_task(s, index):
  match s:
    case W.S{task, +nodes, focus, W.Running{}}:
      L.guarded_preserves_task(W.contains(nodes, U32.to_nat(index)), task, nodes, focus, index)
    case W.S{task, nodes, focus, W.Finished{correct, count, scale}}: {==}
    case W.S{task, nodes, focus, W.TimedOut{}}: {==}

def L.running_reward_is_zero(elapsed):
  {==}

def L.timeout_reward_is_negative_one(elapsed):
  {==}

def L.untimed_reward_preserves_raw(correct, count, elapsed):
  {==}

def L.deadline_preempts_action(s, action):
  {==}

def L.radio_value_preserves_goal(node, selected):
  match node:
    case W.N{W.Radio{}, old, target}: {==}
    case W.N{W.Button{}, old, target}: {==}
    case W.N{W.Checkbox{}, old, target}: {==}
    case W.N{W.Textbox{}, old, target}: {==}
    case W.N{W.Other{}, old, target}: {==}
    case W.N{W.PointerTarget{}, old, target}: {==}

def L.radio_value_overwrites(node, first, last):
  match node:
    case W.N{W.Radio{}, old, target}: {==}
    case W.N{W.Button{}, old, target}: {==}
    case W.N{W.Checkbox{}, old, target}: {==}
    case W.N{W.Textbox{}, old, target}: {==}
    case W.N{W.Other{}, old, target}: {==}
    case W.N{W.PointerTarget{}, old, target}: {==}

def L.radio_set_preserves_goals(nodes, index, at):
  match nodes:
    case Nil{}: {==}
    case Con{node, rest}:
      %L.radio_value_preserves_goal(node, Nat.is_eq(index, at)) : {Con{W.goal(W.radio_value(node, Nat.is_eq(index, at))), W.goals(W.radio_set(rest, index, 1n+at))} == Con{_, W.goals(rest)} : +List<Bool>}
      %L.radio_set_preserves_goals(rest, index, 1n+at) : {Con{W.goal(W.radio_value(node, Nat.is_eq(index, at))), W.goals(W.radio_set(rest, index, 1n+at))} == Con{W.goal(W.radio_value(node, Nat.is_eq(index, at))), _} : +List<Bool>}
      {==}

def L.radio_set_overwrites(nodes, first, last, at):
  match nodes:
    case Nil{}: {==}
    case Con{node, rest}:
      %L.radio_value_overwrites(node, Nat.is_eq(first, at), Nat.is_eq(last, at)) : {Con{W.radio_value(W.radio_value(node, Nat.is_eq(first, at)), Nat.is_eq(last, at)), W.radio_set(W.radio_set(rest, first, 1n+at), last, 1n+at)} == Con{_, W.radio_set(rest, last, 1n+at)} : +List<W.Node>}
      %L.radio_set_overwrites(rest, first, last, 1n+at) : {Con{W.radio_value(W.radio_value(node, Nat.is_eq(first, at)), Nat.is_eq(last, at)), W.radio_set(W.radio_set(rest, first, 1n+at), last, 1n+at)} == Con{W.radio_value(W.radio_value(node, Nat.is_eq(first, at)), Nat.is_eq(last, at)), _} : +List<W.Node>}
      {==}


def L.generated_nodes_start_clear(es, radio):
  match es:
    case Nil{}: {==}
    case Con{G.E{label, target}, rest}: L.generated_nodes_start_clear(rest, radio)

def L.generated_form_starts_initial(f):
  match f:
    case G.F{radio, es}: L.generated_nodes_start_clear(es, radio)

def L.radio_value_sets_radio(old, target, selected):
  {==}

def L.radio_selection_preserves_checkboxes(old, target, selected):
  {==}

def L.generated_node_count(es, radio):
  match es:
    case Nil{}: {==}
    case Con{G.E{label, target}, rest}:
      %L.generated_node_count(rest, radio) : {1n+List.length(&2, W.Node, G.nodes(rest, radio)) == 1n+_ : Nat}
      {==}
