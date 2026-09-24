import Base

# Pure click-widget semantics. Wire.bend owns numeric tags and row packing.
type Role is Data:
  Button{}
  Checkbox{}
  Textbox{}
  Other{}
  PointerTarget{}
  Radio{}

type Task is Data:
  Buttons{}
  Checkboxes{}
  FocusText{}
  Options{}

type Node is Data:
  N{kind: Role, selected: Bool, target: Bool}

type Outcome is Data:
  Running{}
  Finished{matches: U32, total: U32, scaled: Bool}
  TimedOut{}

type State is Data:
  S{task: Task, nodes: +List<Node>, focus: U32, outcome: Outcome}

def toggle(n: Node) -> Node:
  match n:
    case N{Checkbox{}, selected, target}: N{Checkbox{}, Bool.not(selected), target}
    case N{kind, selected, target}: N{kind, selected, target}

def goal(n: Node) -> Bool:
  N{kind, selected, target} = n
  target

def checked(n: Node) -> Bool:
  N{kind, selected, target} = n
  selected

def kind(n: Node) -> Role:
  N{role, selected, target} = n
  role

def goals(nodes: +List<Node>) -> +List<Bool>:
  match nodes:
    case Nil{}: Nil{}
    case Con{n, rest}: goal(n) <> goals(rest)

def at(nodes: +List<Node>, index: Nat) -> Node:
  match nodes index:
    case Nil{} _: N{Other{}, False{}, False{}}
    case Con{n, rest} 0n: n
    case Con{n, rest} 1n+p: at(rest, p)

def toggle_at(nodes: +List<Node>, index: Nat) -> +List<Node>:
  match nodes index:
    case Nil{} _: Nil{}
    case Con{n, rest} 0n: toggle(n) <> rest
    case Con{n, rest} 1n+p: n <> toggle_at(rest, p)

def matches(a: Bool, b: Bool) -> U32:
  match a b:
    case False{} False{}: 1
    case True{} True{}: 1
    case _ _: 0

def score(nodes: +List<Node>, correct: U32, count: U32) -> U32 & U32:
  match nodes:
    case Nil{}: (correct, count)
    case Con{N{Checkbox{}, selected, target}, rest}:
      score(rest, (correct + matches(selected, target) : U32), (count + 1 : U32))
    case Con{_, rest}: score(rest, correct, count)

def scored(pair: U32 & U32) -> Outcome:
  (+correct, +count) = pair
  Finished{correct, count, U32.is_gt((correct * 2 : U32), count)}

# All radios in this first-wave task belong to one group. Other controls keep
# their state; a radio click is selection, never a toggle.
def radio_value(n: Node, selected: Bool) -> Node:
  match n:
    case N{Radio{}, old, target}: N{Radio{}, selected, target}
    case N{role_, old, target}: N{role_, old, target}

def radio_set(nodes: +List<Node>, +index: Nat, +at: Nat) -> +List<Node>:
  match nodes:
    case Nil{}: Nil{}
    case Con{node, rest}:
      radio_value(node, Nat.is_eq(index, at)) <> radio_set(rest, index, 1n+at)

def change(n: Node, nodes: +List<Node>, index: Nat) -> +List<Node>:
  match n:
    case N{Radio{}, selected, target}: radio_set(nodes, index, 0n)
    case _: toggle_at(nodes, index)

def radio_correct(nodes: +List<Node>) -> Bool:
  match nodes:
    case Nil{}: False{}
    case Con{N{Radio{}, selected, target}, rest}: (selected && target) || radio_correct(rest)
    case Con{_, rest}: radio_correct(rest)

def binary(+correct: Bool) -> Outcome:
  Finished{Bool.to_u32(correct), 1, correct}

def clicked(task: Task, n: Node, nodes: +List<Node>) -> Outcome:
  match task n:
    case Buttons{} N{Button{}, selected, +target}: Finished{Bool.to_u32(target), 1, target}
    case Buttons{} N{PointerTarget{}, selected, +target}: Finished{Bool.to_u32(target), 1, target}
    case Checkboxes{} N{Button{}, selected, target}: scored(score(nodes, 0, 0))
    case FocusText{} N{Textbox{}, selected, target}: Finished{1, 1, True{}}
    case Options{} N{Button{}, selected, target}: binary(radio_correct(nodes))
    case _ _: Running{}

def next_focus(task: Task, n: Node, index: U32) -> U32:
  match task n:
    case FocusText{} N{Textbox{}, selected, target}: 0
    case _ N{PointerTarget{}, selected, target}: 0
    case _ _: (index + 1 : U32)

def active(+task: Task, +nodes: +List<Node>, +index: U32) -> State:
  +i = U32.to_nat(index)
  +node = at(nodes, i)
  S{task, change(node, nodes, i), next_focus(task, node, index), clicked(task, node, nodes)}

def contains(nodes: +List<Node>, index: Nat) -> Bool:
  match nodes index:
    case Nil{} _: False{}
    case Con{n, rest} 0n: True{}
    case Con{n, rest} 1n+p: contains(rest, p)

def guarded(valid: Bool, task: Task, nodes: +List<Node>, focus: U32, index: U32) -> State:
  match valid:
    case True{}: active(task, nodes, index)
    case False{}: S{task, nodes, focus, Running{}}

def click(s: State, +index: U32) -> State:
  match s:
    case S{task, +nodes, focus, Running{}}: guarded(contains(nodes, U32.to_nat(index)), task, nodes, focus, index)
    case S{task, nodes, focus, outcome}: S{task, nodes, focus, outcome}

def timeout(s: State) -> State:
  match s:
    case S{task, nodes, focus, Running{}}: S{task, nodes, focus, TimedOut{}}
    case S{task, nodes, focus, outcome}: S{task, nodes, focus, outcome}

def task_of(s: State) -> Task:
  S{task, nodes, focus, outcome} = s
  task

# Reward arithmetic stays in the pure model; host supplies logical elapsed time.
def raw_reward(o: Outcome) -> F32:
  match o:
    case Running{}: 0.0
    case TimedOut{}: (0.0 - 1.0 : F32)
    case Finished{correct, +count, scale}:
      ((U32.to_f32(correct) * 2.0 - U32.to_f32(count)) / U32.to_f32(count) : F32)

def reward_at(o: Outcome, elapsed: U32) -> F32:
  match o:
    case Running{}: 0.0
    case TimedOut{}: (0.0 - 1.0 : F32)
    case Finished{correct, +count, scale}:
      +raw = ((U32.to_f32(correct) * 2.0 - U32.to_f32(count)) / U32.to_f32(count) : F32)
      Bool.pick(F32, scale, (raw * F32.max(0.0, (1.0 - U32.to_f32(elapsed) / 10000.0 : F32)) : F32), raw)

type Action is Data:
  Wait{}
  Click{index: U32}
  Timeout{}

def act(s: State, action: Action) -> State:
  match action:
    case Wait{}: s
    case Click{index}: click(s, index)
    case Timeout{}: timeout(s)

def deadline(expired: Bool, s: State, action: Action) -> State:
  match expired:
    case True{}: timeout(s)
    case False{}: act(s, action)

def step_at(s: State, action: Action, elapsed: U32) -> State:
  deadline(U32.is_ge(elapsed, 10000), s, action)
