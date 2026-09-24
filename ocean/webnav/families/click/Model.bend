import Base
import ../../miniwob/Widgets.bend as W

# Share checkbox involution, radio exclusivity, indexed goal preservation and
# original fractional rewards with the previously checked widget core.
type Mode is Data:
  Direct{}
  Focus{}
  Form{}

type State is Data:
  S{mode: Mode, nodes: +List<W.Node>, focus: U32, outcome: W.Outcome}

def result(mode: Mode, n: W.Node, nodes: +List<W.Node>) -> W.Outcome:
  match mode:
    case Form{}: W.clicked(W.Checkboxes{}, n, nodes)
    case _: W.binary(W.goal(n))

def focus(mode: Mode, index: U32) -> U32:
  match mode:
    case Focus{}: 0
    case _: (index + 1 : U32)

def active(valid: Bool, +mode: Mode, +nodes: +List<W.Node>, old_focus: U32, +index: U32) -> State:
  match valid:
    case False{}: S{mode, nodes, old_focus, W.Running{}}
    case True{}:
      +i = U32.to_nat(index)
      +n = W.at(nodes, i)
      S{mode, W.change(n, nodes, i), focus(mode, index), result(mode, n, nodes)}

def click(s: State, +index: U32) -> State:
  match s:
    case S{mode, +nodes, old_focus, W.Running{}}:
      active(W.contains(nodes, U32.to_nat(index)), mode, nodes, old_focus, index)
    case S{mode, nodes, old_focus, outcome}: S{mode, nodes, old_focus, outcome}

def timeout(s: State) -> State:
  match s:
    case S{mode, nodes, old_focus, W.Running{}}: S{mode, nodes, old_focus, W.TimedOut{}}
    case S{mode, nodes, old_focus, outcome}: S{mode, nodes, old_focus, outcome}

def guarded(expired: Bool, clicked: Bool, s: State, index: U32) -> State:
  match expired clicked:
    case True{} _: timeout(s)
    case False{} True{}: click(s, index)
    case False{} False{}: s

def step(s: State, clicked: Bool, index: U32, elapsed: U32, deadline: U32) -> State:
  guarded(U32.is_ge(elapsed, deadline), clicked, s, index)

def timed(o: W.Outcome, elapsed: U32, deadline: U32) -> F32:
  match o:
    case W.Running{}: 0.0
    case W.TimedOut{}: (0.0 - 1.0 : F32)
    case W.Finished{correct, +count, scale}:
      +raw = ((U32.to_f32(correct) * 2.0 - U32.to_f32(count)) / U32.to_f32(count) : F32)
      Bool.pick(F32, scale, (raw * F32.max(0.0, (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32)) : F32), raw)
