import Base

# A typed matched-instance model of MiniWoB++ navigate-tree.  The host imports
# the pre-order topology and the private text-equality target bit.  Labels are
# deliberately not inferred from the target: repeated labels therefore retain
# the upstream "any matching node" success rule.

type TreeKind is Data:
  File{}
  Folder{}

type Node is Data:
  N{kind: TreeKind, target: Bool, expanded: Bool, parent: U32, subtree_end: U32}

type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{nodes: +List<Node>, last: U32, outcome: Outcome}

type Action is Data:
  Wait{}
  Click{index: U32}
  Timeout{}

def is_folder(kind: TreeKind) -> Bool:
  match kind:
    case Folder{}: True{}
    case File{}: False{}

def kind_of(node: Node) -> TreeKind:
  N{kind, target, expanded, parent, subtree_end} = node
  kind

def target_of(node: Node) -> Bool:
  N{kind, target, expanded, parent, subtree_end} = node
  target

def expanded_of(node: Node) -> Bool:
  N{kind, target, expanded, parent, subtree_end} = node
  expanded

def parent_of(node: Node) -> U32:
  N{kind, target, expanded, parent, subtree_end} = node
  parent

def subtree_end_of(node: Node) -> U32:
  N{kind, target, expanded, parent, subtree_end} = node
  subtree_end

def toggle_node(node: Node) -> Node:
  match node:
    case N{Folder{}, target, expanded, parent, subtree_end}:
      N{Folder{}, target, Bool.not(expanded), parent, subtree_end}
    case N{kind, target, expanded, parent, subtree_end}:
      N{kind, target, expanded, parent, subtree_end}

def at(nodes: +List<Node>, index: Nat) -> Node:
  match nodes index:
    case Nil{} _: N{File{}, False{}, False{}, 0, 0}
    case Con{node, rest} 0n: node
    case Con{node, rest} 1n+p: at(rest, p)

def contains(nodes: +List<Node>, index: Nat) -> Bool:
  match nodes index:
    case Nil{} _: False{}
    case Con{node, rest} 0n: True{}
    case Con{node, rest} 1n+p: contains(rest, p)

def parent_visible(fuel: Nat, +nodes: +List<Node>, parent: U32, root: Bool) -> Bool:
  match fuel:
    case 0n: False{}
    case 1n+p:
      match root:
        case True{}: True{}
        case False{}:
          +parent_node = at(nodes, U32.to_nat((parent - 1 : U32)))
          expanded_of(parent_node) && parent_visible(p, nodes, parent_of(parent_node), U32.is_eq(parent_of(parent_node), 0))

def visible_node(+nodes: +List<Node>, +node: Node) -> Bool:
  parent_visible(List.length(&2, Node, nodes), nodes, parent_of(node), U32.is_eq(parent_of(node), 0))

def visible_at(+nodes: +List<Node>, index: Nat) -> Bool:
  visible_node(nodes, at(nodes, index))

def toggle_at(nodes: +List<Node>, index: Nat) -> +List<Node>:
  match nodes index:
    case Nil{} _: Nil{}
    case Con{node, rest} 0n: toggle_node(node) <> rest
    case Con{node, rest} 1n+p: node <> toggle_at(rest, p)

def bubble_result(fuel: Nat, +nodes: +List<Node>, +node: Node, root: Bool) -> Outcome:
  match fuel:
    case 0n: Running{}
    case 1n+p:
      match node:
        case N{kind, True{}, expanded, parent, subtree_end}: Finished{True{}}
        case N{File{}, False{}, expanded, parent, subtree_end}: Finished{False{}}
        case N{Folder{}, False{}, expanded, parent, subtree_end}:
          match root:
            case True{}: Running{}
            case False{}:
              +ancestor = at(nodes, U32.to_nat((parent - 1 : U32)))
              bubble_result(p, nodes, ancestor, U32.is_eq(parent_of(ancestor), 0))

def outcome_for_in(+node: Node, +nodes: +List<Node>) -> Outcome:
  bubble_result(List.length(&2, Node, nodes), nodes, node, U32.is_eq(parent_of(node), 0))

def changed_kind(kind: TreeKind, nodes: +List<Node>, index: Nat) -> +List<Node>:
  match kind:
    case Folder{}: toggle_at(nodes, index)
    case File{}: nodes

def changed_nodes(node: Node, nodes: +List<Node>, index: Nat) -> +List<Node>:
  changed_kind(kind_of(node), nodes, index)

def active(+node: Node, +nodes: +List<Node>, +index: Nat) -> State:
  S{changed_nodes(node, nodes, index), (1 + U32.from_nat(index) : U32), outcome_for_in(node, nodes)}

def guarded(valid: Bool, node: Node, nodes: +List<Node>, index: Nat, last: U32) -> State:
  match valid:
    case True{}: active(node, nodes, index)
    case False{}: S{nodes, last, Running{}}

def click(+nodes: +List<Node>, +last: U32, +index: U32) -> State:
  +i = U32.to_nat(index)
  +node = at(nodes, i)
  guarded(contains(nodes, i) && visible_at(nodes, i), node, nodes, i, last)

def timeout(+nodes: +List<Node>, +last: U32, outcome: Outcome) -> State:
  match outcome:
    case Running{}: S{nodes, last, TimedOut{}}
    case Finished{success}: S{nodes, last, Finished{success}}
    case TimedOut{}: S{nodes, last, TimedOut{}}

def act(action: Action, nodes: +List<Node>, last: U32, outcome: Outcome) -> State:
  match action:
    case Wait{}: S{nodes, last, outcome}
    case Click{index}:
      match outcome:
        case Running{}: click(nodes, last, index)
        case Finished{success}: S{nodes, last, Finished{success}}
        case TimedOut{}: S{nodes, last, TimedOut{}}
    case Timeout{}: timeout(nodes, last, outcome)

def deadline(expired: Bool, nodes: +List<Node>, last: U32, outcome: Outcome, action: Action) -> State:
  match expired:
    case True{}: timeout(nodes, last, outcome)
    case False{}: act(action, nodes, last, outcome)

def step_at(s: State, action: Action, elapsed: U32) -> State:
  match s:
    case S{+nodes, last, outcome}:
      deadline(U32.is_ge(elapsed, 10000), nodes, last, outcome, action)

def raw_reward(outcome: Outcome) -> F32:
  match outcome:
    case Running{}: 0.0
    case TimedOut{}: (0.0 - 1.0 : F32)
    case Finished{success}:
      Bool.pick(F32, success, 1.0, (0.0 - 1.0 : F32))

def reward_at(outcome: Outcome, elapsed: U32) -> F32:
  match outcome:
    case Running{}: 0.0
    case TimedOut{}: (0.0 - 1.0 : F32)
    case Finished{success}:
      match success:
        case False{}: (0.0 - 1.0 : F32)
        case True{}:
          F32.max(0.0, (1.0 - U32.to_f32(elapsed) / 10000.0 : F32))

def node_targets(nodes: +List<Node>) -> +List<Bool>:
  match nodes:
    case Nil{}: Nil{}
    case Con{node, rest}: target_of(node) <> node_targets(rest)

def node_kinds(nodes: +List<Node>) -> +List<Bool>:
  match nodes:
    case Nil{}: Nil{}
    case Con{node, rest}: is_folder(kind_of(node)) <> node_kinds(rest)

def node_expansions(nodes: +List<Node>) -> +List<Bool>:
  match nodes:
    case Nil{}: Nil{}
    case Con{node, rest}: expanded_of(node) <> node_expansions(rest)

def node_parents(nodes: +List<Node>) -> +List<U32>:
  match nodes:
    case Nil{}: Nil{}
    case Con{node, rest}: parent_of(node) <> node_parents(rest)

def node_ends(nodes: +List<Node>) -> +List<U32>:
  match nodes:
    case Nil{}: Nil{}
    case Con{node, rest}: subtree_end_of(node) <> node_ends(rest)
