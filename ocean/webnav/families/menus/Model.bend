import Base

type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type Node is Data:
  N{kind: U32, parent: U32, target: Bool, enabled: Bool, children: Bool, icon: U32}

type State is Data:
  S{task: U32, count: U32, nodes: +List<Node>, open: Bool, hover: U32, outcome: Outcome}

type Action is Data:
  Wait{}
  Hover{ref: U32}
  Choose{ref: U32}

def empty_node() -> Node:
  N{0, 0, False{}, False{}, False{}, 0}

def at(xs: +List<Node>, index: Nat) -> Node:
  match xs index:
    case Nil{} _: empty_node()
    case Con{n, rest} 0n: n
    case Con{n, rest} 1n+p: at(rest, p)

def node_positive(positive: Bool, ref: U32, nodes: +List<Node>) -> Node:
  match positive:
    case False{}: empty_node()
    case True{}: at(nodes, U32.to_nat((ref - 1 : U32)))

def node(+ref: U32, nodes: +List<Node>) -> Node:
  node_positive(U32.is_gt(ref, 0), ref, nodes)

def node_parent(item: Node) -> U32:
  match item:
    case N{kind, parent, target, enabled, children, icon}: parent

def parent_of(ref: U32, nodes: +List<Node>) -> U32:
  node_parent(node(ref, nodes))

def count_nodes(xs: +List<Node>) -> Nat:
  match xs:
    case Nil{}: 0n
    case Con{n, rest}: 1n + count_nodes(rest)

def on_path_state(steps: Nat, ref: U32, +wanted: U32, +nodes: +List<Node>, equal: Bool) -> Bool:
  match steps:
    case 0n: False{}
    case 1n+p:
      match equal:
        case True{}: U32.is_gt(ref, 0)
        case False{}:
          +parent = parent_of(ref, nodes)
          on_path_state(p, parent, wanted, nodes, U32.is_eq(parent, wanted))

def on_path(steps: Nat, +ref: U32, +wanted: U32, +nodes: +List<Node>) -> Bool:
  on_path_state(steps, ref, wanted, nodes, U32.is_eq(ref, wanted))

def parent_open(task: U32, open: Bool, hover: U32, parent: U32, nodes: +List<Node>) -> Bool:
  match parent:
    case 0: Bool.pick(Bool, U32.is_eq(task, 0), True{}, open)
    case _: open && on_path(4n, hover, parent, nodes)

def visible_node(item: Node, task: U32, open: Bool, hover: U32, nodes: +List<Node>) -> Bool:
  match item:
    case N{kind, parent, target, enabled, children, icon}:
      match kind:
        case 1: True{}
        case _: parent_open(task, open, hover, parent, nodes)

def visible_ref(ref: U32, task: U32, open: Bool, hover: U32, +nodes: +List<Node>) -> Bool:
  visible_node(node(ref, nodes), task, open, hover, nodes)

def expanded_node(item: Node, +ref: U32, open: Bool, hover: U32, +nodes: +List<Node>) -> Bool:
  match item:
    case N{kind, parent, target, enabled, children, icon}:
      children && open && on_path(4n, hover, ref, nodes)

def expanded_ref(+ref: U32, open: Bool, hover: U32, +nodes: +List<Node>) -> Bool:
  expanded_node(node(ref, nodes), ref, open, hover, nodes)

def valid_ref(+ref: U32, count: U32) -> Bool:
  U32.is_gt(ref, 0) && U32.is_le(ref, count)

def branch_choice(+task: U32, +count: U32, +nodes: +List<Node>, open: Bool, hover: U32, outcome: Outcome, +ref: U32) -> State:
  match task:
    case 0: S{task, count, nodes, open, hover, Finished{False{}}}
    case _: S{task, count, nodes, open, ref, outcome}

def choose_node(item: Node, +task: U32, +count: U32, +nodes: +List<Node>, open: Bool, hover: U32, outcome: Outcome, +ref: U32) -> State:
  match item:
    case N{kind, parent, target, enabled, children, icon}:
      match kind:
        case 1: S{task, count, nodes, Bool.not(open), 0, outcome}
        case _:
          match enabled:
            case False{}: S{task, count, nodes, open, hover, outcome}
            case True{}:
              match children:
                case True{}: branch_choice(task, count, nodes, open, hover, outcome, ref)
                case False{}: S{task, count, nodes, open, hover, Finished{target}}

def choose_visible(visible: Bool, +task: U32, +count: U32, +nodes: +List<Node>, open: Bool, hover: U32, outcome: Outcome, +ref: U32) -> State:
  match visible:
    case False{}: S{task, count, nodes, open, hover, outcome}
    case True{}: choose_node(node(ref, nodes), task, count, nodes, open, hover, outcome, ref)

def choose(s: State, +ref: U32) -> State:
  S{+task, +count, +nodes, +open, +hover, outcome} = s
  choose_visible(valid_ref(ref, count) && visible_ref(ref, task, open, hover, nodes), task, count, nodes, open, hover, outcome, ref)

def hover_node(item: Node, +task: U32, +count: U32, +nodes: +List<Node>, open: Bool, outcome: Outcome, +ref: U32) -> State:
  match item:
    case N{kind, parent, target, enabled, children, icon}:
      match kind:
        case 1: S{task, count, nodes, open, 0, outcome}
        case _: S{task, count, nodes, open, ref, outcome}

def hover_visible(visible: Bool, +task: U32, +count: U32, +nodes: +List<Node>, open: Bool, outcome: Outcome, +ref: U32) -> State:
  match visible:
    case False{}: S{task, count, nodes, open, 0, outcome}
    case True{}: hover_node(node(ref, nodes), task, count, nodes, open, outcome, ref)

def move_pointer(s: State, +ref: U32) -> State:
  S{+task, +count, +nodes, +open, hover, outcome} = s
  hover_visible(valid_ref(ref, count) && visible_ref(ref, task, open, hover, nodes), task, count, nodes, open, outcome, ref)

def act(s: State, action: Action) -> State:
  match action:
    case Wait{}: s
    case Hover{ref}: move_pointer(s, ref)
    case Choose{ref}: choose(s, ref)

def timeout(s: State) -> State:
  S{task, count, nodes, open, hover, outcome} = s
  S{task, count, nodes, open, hover, TimedOut{}}

def running(expired: Bool, s: State, action: Action) -> State:
  match expired:
    case True{}: timeout(s)
    case False{}: act(s, action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{task, count, nodes, open, hover, Running{}}:
      running(U32.is_ge(elapsed, deadline), S{task, count, nodes, open, hover, Running{}}, action)
    case S{task, count, nodes, open, hover, outcome}:
      S{task, count, nodes, open, hover, outcome}

def raw(outcome: Outcome) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{True{}}: 1.0
    case Finished{False{}}: (0.0 - 1.0 : F32)
    case TimedOut{}: (0.0 - 1.0 : F32)

def timed(outcome: Outcome, elapsed: U32, deadline: U32) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{True{}}: (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32)
    case Finished{False{}}: (0.0 - 1.0 : F32)
    case TimedOut{}: (0.0 - 1.0 : F32)
