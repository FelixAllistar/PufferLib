import Base
import ./Tree.bend as T

# Matched-instance transport for navigate-tree.  See ABI.md.  This module
# intentionally exposes row(base, a), so the shared webnav dispatcher can route
# task tag 9 without changing the existing widget wire.

def kind(code: U32) -> T.TreeKind:
  match code:
    case 1: T.Folder{}
    case _: T.File{}

def outcome(code: U32, success: U32) -> T.Outcome:
  match code:
    case 0: T.Running{}
    case 1: T.Finished{U32.is_eq(success, 1)}
    case _: T.TimedOut{}

def action(command: U32, index: U32) -> T.Action:
  match command:
    case 1: T.Click{index}
    case 2: T.Timeout{}
    case _: T.Wait{}

def save_node(+node: T.Node, +all: +List<T.Node>, +at: U32, +index: Nat, a: Array<U32>) -> Array<U32>:
  a[(at + 0 : U32)] <- Bool.to_u32(T.is_folder(T.kind_of(node)))
  a[(at + 1 : U32)] <- Bool.to_u32(T.target_of(node))
  a[(at + 2 : U32)] <- Bool.to_u32(T.expanded_of(node))
  a[(at + 3 : U32)] <- T.parent_of(node)
  a[(at + 4 : U32)] <- T.subtree_end_of(node)
  a[(at + 5 : U32)] <- Bool.to_u32(T.visible_at(all, index))

def save_nodes(+all: +List<T.Node>, nodes: +List<T.Node>, +at: U32, +index: Nat, a: Array<U32>) -> Array<U32>:
  match nodes:
    case Nil{}: a
    case Con{+node, rest}:
      save_nodes(all, rest, (at + 6 : U32), 1n+index, save_node(node, all, at, index, a))

def save_outcome(o: T.Outcome, +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case T.Running{}:
      a[(base + 2 : U32)] <- 0
      a[(base + 4 : U32)] <- 0
      a[(base + 5 : U32)] <- 1
      a[(base + 6 : U32)] <- 0
    case T.Finished{+success}:
      a[(base + 2 : U32)] <- 1
      a[(base + 4 : U32)] <- Bool.to_u32(success)
      a[(base + 5 : U32)] <- 1
      a[(base + 6 : U32)] <- Bool.to_u32(success)
    case T.TimedOut{}:
      a[(base + 2 : U32)] <- 2
      a[(base + 4 : U32)] <- 0
      a[(base + 5 : U32)] <- 1
      a[(base + 6 : U32)] <- 0

def save_rewards(previous: U32, +result: T.Outcome, +base: U32, +elapsed: U32, a: Array<U32>) -> Array<U32>:
  match previous:
    case 0:
      a[(base + 10 : U32)] <- elapsed
      a[(base + 11 : U32)] <- F32.bits(T.raw_reward(result))
      a[(base + 12 : U32)] <- F32.bits(T.reward_at(result, elapsed))
    case _: a

def save(s: T.State, +base: U32, elapsed: U32, previous: U32, a: Array<U32>) -> Array<U32>:
  T.S{+nodes, last, +result} = s
  a[(base + 3 : U32)] <- last
  save_outcome(result, base, save_nodes(nodes, nodes, (base + 16 : U32), 0n, save_rewards(previous, result, base, elapsed, a)))

def save_reset(s: T.State, +base: U32, a: Array<U32>) -> Array<U32>:
  T.S{+nodes, last, result} = s
  a[(base + 2 : U32)] <- 0
  a[(base + 3 : U32)] <- 0
  a[(base + 4 : U32)] <- 0
  a[(base + 5 : U32)] <- 1
  a[(base + 6 : U32)] <- 0
  a[(base + 10 : U32)] <- 0
  a[(base + 11 : U32)] <- F32.bits(T.raw_reward(T.Running{}))
  a[(base + 12 : U32)] <- F32.bits(T.reward_at(T.Running{}, 0))
  save_nodes(nodes, nodes, (base + 16 : U32), 0n, a)

def add_node(nodes: +List<T.Node>, kind_code: U32, target: U32, expanded: U32, parent: U32, subtree_end: U32) -> +List<T.Node>:
  T.N{kind(kind_code), U32.is_eq(target, 1), U32.is_eq(expanded, 1), parent, subtree_end} <> nodes

def load_end(nodes: +List<T.Node>, +at: U32, kind_code: U32, target: U32, expanded: U32, parent: U32, pair: Array<U32> & U32) -> Array<U32> & +List<T.Node>:
  (a, subtree_end) = pair
  (a, add_node(nodes, kind_code, target, expanded, parent, subtree_end))

def load_parent(nodes: +List<T.Node>, +at: U32, kind_code: U32, target: U32, expanded: U32, pair: Array<U32> & U32) -> Array<U32> & +List<T.Node>:
  (a, parent) = pair
  load_end(nodes, at, kind_code, target, expanded, parent, a[(at + 4 : U32)])

def load_expanded(nodes: +List<T.Node>, +at: U32, kind_code: U32, target: U32, pair: Array<U32> & U32) -> Array<U32> & +List<T.Node>:
  (a, expanded) = pair
  load_parent(nodes, at, kind_code, target, expanded, a[(at + 3 : U32)])

def load_target(nodes: +List<T.Node>, +at: U32, kind_code: U32, pair: Array<U32> & U32) -> Array<U32> & +List<T.Node>:
  (a, target) = pair
  load_expanded(nodes, at, kind_code, target, a[(at + 2 : U32)])

def load_kind(nodes: +List<T.Node>, +at: U32, pair: Array<U32> & U32) -> Array<U32> & +List<T.Node>:
  (a, kind_code) = pair
  load_target(nodes, at, kind_code, a[(at + 1 : U32)])

def load(n: Nat, +base: U32, pair: Array<U32> & +List<T.Node>) -> Array<U32> & +List<T.Node>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, nodes) = pair
      +at = (base + 16 + U32.from_nat(p) * 6 : U32)
      load(p, base, load_kind(nodes, at, a[at]))

def ready(+base: U32, size: U32, +status: U32, last: U32, success: U32, total: U32, scaled: U32, command: U32, index: U32, +elapsed: U32, pair: Array<U32> & +List<T.Node>) -> Array<U32>:
  (a, nodes) = pair
  +s = {T.S{nodes, last, outcome(status, success)} : T.State}
  save(T.step_at(s, action(command, index), elapsed), base, elapsed, status, a)

def collapse_nodes(nodes: +List<T.Node>) -> +List<T.Node>:
  match nodes:
    case Nil{}: Nil{}
    case Con{node, rest}:
      T.N{tree_kind, target, expanded, parent, subtree_end} = node
      T.N{tree_kind, target, False{}, parent, subtree_end} <> collapse_nodes(rest)

def collapse_state(s: T.State) -> T.State:
  T.S{nodes, last, old_outcome} = s
  T.S{collapse_nodes(nodes), 0, T.Running{}}

def reset_state(+base: U32, +size: U32, pair: Array<U32> & +List<T.Node>) -> Array<U32>:
  (a, nodes) = pair
  +s = {T.S{nodes, 0, T.Running{}} : T.State}
  save_reset(collapse_state(s), base, a)

def read9_done(+base: U32, +size: U32, status: U32, last: U32, success: U32, total: U32, scaled: U32, command: U32, index: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, elapsed) = pair
  ready(base, size, status, last, success, total, scaled, command, index, elapsed, load(U32.to_nat(size), base, (a, Nil{})))

def reset_command(+base: U32, +size: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, elapsed) = pair
  reset_state(base, size, load(U32.to_nat(size), base, (a, Nil{})))

def read9(+base: U32, size: U32, status: U32, last: U32, success: U32, total: U32, scaled: U32, command: U32, index: U32, pair: Array<U32> & U32) -> Array<U32>:
  match command:
    case 3: reset_command(base, size, pair)
    case _: read9_done(base, size, status, last, success, total, scaled, command, index, pair)

def read8(+base: U32, size: U32, status: U32, last: U32, success: U32, total: U32, scaled: U32, command: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, index) = pair
  read9(base, size, status, last, success, total, scaled, command, index, a[(base + 9 : U32)])

def read7(+base: U32, size: U32, status: U32, last: U32, success: U32, total: U32, scaled: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, command) = pair
  read8(base, size, status, last, success, total, scaled, command, a[(base + 8 : U32)])

def read6(+base: U32, size: U32, status: U32, last: U32, success: U32, total: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, scaled) = pair
  read7(base, size, status, last, success, total, scaled, a[(base + 7 : U32)])

def read5(+base: U32, size: U32, status: U32, last: U32, success: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, total) = pair
  read6(base, size, status, last, success, total, a[(base + 6 : U32)])

def read4(+base: U32, size: U32, status: U32, last: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, success) = pair
  read5(base, size, status, last, success, a[(base + 5 : U32)])

def read3(+base: U32, size: U32, status: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, last) = pair
  read4(base, size, status, last, a[(base + 4 : U32)])

def read2(+base: U32, size: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  read3(base, size, status, a[(base + 3 : U32)])

def read1(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, size) = pair
  read2(base, size, a[(base + 2 : U32)])

def read0(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, tag) = pair
  read1(base, a[(base + 1 : U32)])

def route(tag: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match tag:
    case 9: read0(base, a[base])
    case _: a

def row_kind(base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, tag) = pair
  route(tag, base, a)

def row(+base: U32, a: Array<U32>) -> Array<U32>:
  row_kind(base, a[base])

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: batch(p, row((U32.from_nat(p) * 256 : U32), a))
