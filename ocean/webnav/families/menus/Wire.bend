import Base
import ./Model.bend as M
import ./Generate.bend as G

def outcome(status: U32, raw: U32) -> M.Outcome:
  match status:
    case 0: M.Running{}
    case 1: M.Finished{U32.is_eq(raw, 1065353216)}
    case _: M.TimedOut{}

def read_icon(kind: U32, parent: U32, target: U32, enabled: U32, children: U32, rest: +List<M.Node>, pair: Array<U32> & U32) -> Array<U32> & +List<M.Node>:
  (a, icon) = pair
  +node = {M.N{kind, parent, U32.is_eq(target, 1), U32.is_eq(enabled, 1), U32.is_eq(children, 1), icon} : M.Node}
  (a, node <> rest)

def read_children(+at: U32, kind: U32, parent: U32, target: U32, enabled: U32, rest: +List<M.Node>, pair: Array<U32> & U32) -> Array<U32> & +List<M.Node>:
  (a, children) = pair
  read_icon(kind, parent, target, enabled, children, rest, a[(at + 5 : U32)])

def read_enabled(+at: U32, kind: U32, parent: U32, target: U32, rest: +List<M.Node>, pair: Array<U32> & U32) -> Array<U32> & +List<M.Node>:
  (a, enabled) = pair
  read_children(at, kind, parent, target, enabled, rest, a[(at + 4 : U32)])

def read_target(+at: U32, kind: U32, parent: U32, rest: +List<M.Node>, pair: Array<U32> & U32) -> Array<U32> & +List<M.Node>:
  (a, target) = pair
  read_enabled(at, kind, parent, target, rest, a[(at + 3 : U32)])

def read_parent(+at: U32, kind: U32, rest: +List<M.Node>, pair: Array<U32> & U32) -> Array<U32> & +List<M.Node>:
  (a, parent) = pair
  read_target(at, kind, parent, rest, a[(at + 2 : U32)])

def read_kind(+at: U32, rest: +List<M.Node>, pair: Array<U32> & U32) -> Array<U32> & +List<M.Node>:
  (a, kind) = pair
  read_parent(at, kind, rest, a[(at + 1 : U32)])

def load_nodes(n: Nat, +base: U32, pair: Array<U32> & +List<M.Node>) -> Array<U32> & +List<M.Node>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, rest) = pair
      index = U32.from_nat(p)
      +at = (base + 64 + index * 40 : U32)
      load_nodes(p, base, read_kind(at, rest, a[at]))

def save_nodes(xs: +List<M.Node>, +nodes: +List<M.Node>, +index: U32, +task: U32, +open: Bool, +hover: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{n, rest}:
      +ref = (index + 1 : U32)
      +at = (base + 64 + index * 40 : U32)
      a = a[(at + 36 : U32)] <- Bool.to_u32(M.visible_ref(ref, task, open, hover, nodes))
      a = a[(at + 37 : U32)] <- Bool.to_u32(M.expanded_ref(ref, open, hover, nodes))
      save_nodes(rest, nodes, ref, task, open, hover, base, a)

def save_result(o: M.Outcome, +elapsed: U32, +deadline: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case M.Running{}:
      a[(base + 9 : U32)] <- 0
      a[(base + 10 : U32)] <- 0
      a[(base + 11 : U32)] <- 0
    case M.Finished{+success}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(M.raw(M.Finished{success}))
      a[(base + 11 : U32)] <- F32.bits(M.timed(M.Finished{success}, elapsed, deadline))
    case M.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(s: M.State, elapsed: U32, deadline: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  M.S{task, count, +nodes, +open, +hover, o} = s
  a[(base + 2 : U32)] <- 0
  a[(base + 33 : U32)] <- Bool.to_u32(open)
  a[(base + 34 : U32)] <- hover
  a = save_result(o, elapsed, deadline, base, a)
  save_nodes(nodes, nodes, 0, task, open, hover, base, a)

def decoded_index(action: Nat, target: U32) -> M.Action:
  match action:
    case 14n: M.Hover{target}
    case 1n: M.Choose{target}
    case _: M.Wait{}

def decoded(action: U32, target: U32) -> M.Action:
  decoded_index(U32.to_nat(action), target)

def ready_loaded(+base: U32, task: U32, count: U32, opened: U32, hover: U32, action: U32, target: U32, +elapsed: U32, +deadline: U32, status: U32, raw: U32, pair: Array<U32> & +List<M.Node>) -> Array<U32>:
  (a, nodes) = pair
  +s = {M.S{task, count, nodes, U32.is_eq(opened, 1), hover, outcome(status, raw)} : M.State}
  save(M.step(s, decoded(action, target), elapsed, deadline), elapsed, deadline, base, a)

def ready_raw(+base: U32, task: U32, +count: U32, opened: U32, hover: U32, action: U32, target: U32, +elapsed: U32, +deadline: U32, status: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, raw) = pair
  ready_loaded(base, task, count, opened, hover, action, target, elapsed, deadline, status, raw, load_nodes(U32.to_nat(count), base, (a, Nil{})))

def ready_status(+base: U32, task: U32, count: U32, opened: U32, hover: U32, action: U32, target: U32, +elapsed: U32, +deadline: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  ready_raw(base, task, count, opened, hover, action, target, elapsed, deadline, status, a[(base + 10 : U32)])

def ready_deadline(+base: U32, task: U32, count: U32, opened: U32, hover: U32, action: U32, target: U32, +elapsed: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, deadline) = pair
  ready_status(base, task, count, opened, hover, action, target, elapsed, deadline, a[(base + 9 : U32)])

def ready_elapsed(+base: U32, task: U32, count: U32, opened: U32, hover: U32, action: U32, target: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, elapsed) = pair
  ready_deadline(base, task, count, opened, hover, action, target, elapsed, a[(base + 12 : U32)])

def ready_target(+base: U32, task: U32, count: U32, opened: U32, hover: U32, action: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, target) = pair
  ready_elapsed(base, task, count, opened, hover, action, target, a[(base + 8 : U32)])

def ready_action(+base: U32, task: U32, count: U32, opened: U32, hover: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, action) = pair
  ready_target(base, task, count, opened, hover, action, a[(base + 5 : U32)])

def ready_hover(+base: U32, task: U32, count: U32, opened: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, hover) = pair
  ready_action(base, task, count, opened, hover, a[(base + 4 : U32)])

def ready_open(+base: U32, task: U32, count: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, opened) = pair
  ready_hover(base, task, count, opened, a[(base + 34 : U32)])

def ready_count(+base: U32, task: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, count) = pair
  ready_open(base, task, count, a[(base + 33 : U32)])

def ready(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, task) = pair
  ready_count(base, task, a[(base + 32 : U32)])

def reset_task_seed(+base: U32, task: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  G.generate(task, seed, base, a)

def reset_task(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, task) = pair
  reset_task_seed(base, task, a[(base + 3 : U32)])

def operation(op: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match op:
    case 0: a
    case 1: reset_task(base, a[(base + 1 : U32)])
    case _: ready(base, a[(base + 1 : U32)])

def row(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, op) = pair
  operation(op, base, a)

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +base = (U32.from_nat(p) * 8192 : U32)
      batch(p, row(base, a[(base + 2 : U32)]))
