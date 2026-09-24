import Base
import ../../miniwob/Widgets.bend as W
import ./Model.bend as M
import ./Generate.bend as G

def decode_role_cases(dispatch: Nat, k: U32) -> W.Role:
  match dispatch:
    case 1n: W.Button{}
    case 2n: W.Checkbox{}
    case 3n: W.Textbox{}
    case 4n: W.PointerTarget{}
    case 5n: W.Radio{}
    case _: W.Other{}


def decode_role(+k: U32) -> W.Role:
  decode_role_cases(U32.to_nat(k), k)

def decode_mode_cases(dispatch: Nat, k: U32) -> M.Mode:
  match dispatch:
    case 1n: M.Focus{}
    case 2n: M.Form{}
    case _: M.Direct{}


def decode_mode(+k: U32) -> M.Mode:
  decode_mode_cases(U32.to_nat(k), k)

def node_goal(kind: U32, selected: U32, rest: +List<W.Node>, pair: Array<U32> & U32) -> Array<U32> & +List<W.Node>:
  (a, goal) = pair
  (a, W.N{decode_role(kind), U32.is_eq(selected, 1), U32.is_eq(goal, 1)} <> rest)

def node_selected(+at: U32, kind: U32, rest: +List<W.Node>, pair: Array<U32> & U32) -> Array<U32> & +List<W.Node>:
  (a, selected) = pair
  node_goal(kind, selected, rest, a[(at + 2 : U32)])

def node_kind(+at: U32, rest: +List<W.Node>, pair: Array<U32> & U32) -> Array<U32> & +List<W.Node>:
  (a, kind) = pair
  node_selected(at, kind, rest, a[(at + 1 : U32)])

def load_nodes(n: Nat, +base: U32, pair: Array<U32> & +List<W.Node>) -> Array<U32> & +List<W.Node>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, rest) = pair
      +at = (base + 64 + U32.from_nat(p) * 40 : U32)
      load_nodes(p, base, node_kind(at, rest, a[at]))

def save_nodes(xs: +List<W.Node>, +index: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{n, rest}:
      save_nodes(rest, (index + 1 : U32), base, a[(base + 65 + index * 40 : U32)] <- Bool.to_u32(W.checked(n)))

def save_result(o: W.Outcome, +elapsed: U32, +deadline: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case W.Running{}: a
    case W.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864
      a[(base + 35 : U32)] <- elapsed
    case W.Finished{+correct, +count, +scale}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(W.raw_reward(W.Finished{correct, count, scale}))
      a[(base + 11 : U32)] <- F32.bits(M.timed(W.Finished{correct, count, scale}, elapsed, deadline))
      a[(base + 35 : U32)] <- elapsed
      a[(base + 36 : U32)] <- correct
      a[(base + 37 : U32)] <- count
      a[(base + 38 : U32)] <- Bool.to_u32(scale)

def save(s: M.State, elapsed: U32, deadline: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  M.S{mode, nodes, focus, outcome} = s
  a[(base + 34 : U32)] <- focus
  a[(base + 2 : U32)] <- 0
  save_result(outcome, elapsed, deadline, base, save_nodes(nodes, 0, base, a))

def ready(+base: U32, kind: U32, action: U32, +target: U32, +elapsed: U32, +deadline: U32, focus: U32, pair: Array<U32> & +List<W.Node>) -> Array<U32>:
  (a, nodes) = pair
  s = {M.S{decode_mode(kind), nodes, focus, W.Running{}} : M.State}
  save(M.step(s, U32.is_eq(action, 1) && U32.is_gt(target, 0), (target - 1 : U32), elapsed, deadline), elapsed, deadline, base, a)

def got_count(+base: U32, kind: U32, action: U32, target: U32, elapsed: U32, deadline: U32, focus: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, count) = pair
  ready(base, kind, action, target, elapsed, deadline, focus, load_nodes(U32.to_nat(count), base, (a, Nil{})))

def load5(+base: U32, kind: U32, action: U32, target: U32, elapsed: U32, deadline: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, focus) = pair
  got_count(base, kind, action, target, elapsed, deadline, focus, a[(base + 33 : U32)])

def load4(+base: U32, kind: U32, action: U32, target: U32, elapsed: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, deadline) = pair
  load5(base, kind, action, target, elapsed, deadline, a[(base + 34 : U32)])

def load3(+base: U32, kind: U32, action: U32, target: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, elapsed) = pair
  load4(base, kind, action, target, elapsed, a[(base + 12 : U32)])

def load2(+base: U32, kind: U32, action: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, target) = pair
  load3(base, kind, action, target, a[(base + 8 : U32)])

def load1(+base: U32, kind: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, action) = pair
  load2(base, kind, action, a[(base + 5 : U32)])

def load0(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, kind) = pair
  load1(base, kind, a[(base + 4 : U32)])

def reset_mode(task: U32, seed: U32, base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, mode) = pair
  G.generate(task, seed, mode, base, a)

def reset_seed(task: U32, +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  reset_mode(task, seed, base, a[(base + 6 : U32)])

def reset_task(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, task) = pair
  reset_seed(task, base, a[(base + 3 : U32)])

def status_cases(dispatch: Nat, code: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match dispatch:
    case 0n: load0(base, a[(base + 32 : U32)])
    case _: a[(base + 2 : U32)] <- 0


def status(+code: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  status_cases(U32.to_nat(code), code, base, a)

def got_status(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, code) = pair
  status(code, base, a)

def operation_cases(dispatch: Nat, op: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match dispatch:
    case 0n: a
    case 1n: reset_task(base, a[(base + 1 : U32)])
    case _: got_status(base, a[(base + 9 : U32)])


def operation(+op: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  operation_cases(U32.to_nat(op), op, base, a)

def row(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, op) = pair
  operation(op, base, a)

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +base = (U32.from_nat(p) * 2048 : U32)
      batch(p, row(base, a[(base + 2 : U32)]))
