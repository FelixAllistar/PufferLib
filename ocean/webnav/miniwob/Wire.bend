import Base
import ./Widgets.bend as W
import ./Generate.bend as G
import ./TextWire.bend as TextWire
import ./sequence_select/Wire.bend as SequenceSelect
import ./tree/Wire.bend as Tree
import ./autocomplete/Wire.bend as Autocomplete
import ./training/SequenceGenerate.bend as SG
import ./training/TreeGenerate.bend as TG
import ./training/TextGenerate.bend as XG

# Matched-instance oracle wire, NOT the policy observation schema.
# Header 0..12: task,count,outcome,focus,matches,total,scaled,command,index,
# elapsed_ms,saved_elapsed_ms,raw_reward_bits,timed_reward_bits.
# Nodes at 16: triples role,selected,goal. Max 16 nodes, 256 words/lane.

def role(code: U32) -> W.Role:
  match code:
    case 1: W.Button{}
    case 2: W.Checkbox{}
    case 3: W.Textbox{}
    case 4: W.PointerTarget{}
    case 5: W.Radio{}
    case _: W.Other{}

def task(code: U32) -> W.Task:
  match code:
    case 0: W.Buttons{}
    case 2: W.FocusText{}
    case 3: W.Options{}
    case _: W.Checkboxes{}

def outcome(code: U32, correct: U32, count: U32, scale: U32) -> W.Outcome:
  match code:
    case 0: W.Running{}
    case 1: W.Finished{correct, count, U32.is_eq(scale, 1)}
    case _: W.TimedOut{}

def action(command: U32, index: U32) -> W.Action:
  match command:
    case 1: W.Click{index}
    case 2: W.Timeout{}
    case _: W.Wait{}

def save_nodes(nodes: +List<W.Node>, +at: U32, a: Array<U32>) -> Array<U32>:
  match nodes:
    case Nil{}: a
    case Con{W.N{kind, selected, target}, rest}:
      a[(at + 1 : U32)] <- Bool.to_u32(selected)
      save_nodes(rest, (at + 3 : U32), a)

def save_outcome(o: W.Outcome, +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case W.Running{}:
      a[(base + 2 : U32)] <- 0
      a
    case W.Finished{correct, count, scale}:
      a[(base + 2 : U32)] <- 1
      a[(base + 4 : U32)] <- correct
      a[(base + 5 : U32)] <- count
      a[(base + 6 : U32)] <- Bool.to_u32(scale)
      a
    case W.TimedOut{}:
      a[(base + 2 : U32)] <- 2
      a[(base + 4 : U32)] <- 0
      a[(base + 5 : U32)] <- 1
      a[(base + 6 : U32)] <- 0
      a

def save_rewards(previous: U32, +result: W.Outcome, +base: U32, +elapsed: U32, a: Array<U32>) -> Array<U32>:
  match previous:
    case 0:
      a[(base + 10 : U32)] <- elapsed
      a[(base + 11 : U32)] <- F32.bits(W.raw_reward(result))
      a[(base + 12 : U32)] <- F32.bits(W.reward_at(result, elapsed))
      a
    case _: a

def save(s: W.State, +base: U32, elapsed: U32, previous: U32, a: Array<U32>) -> Array<U32>:
  W.S{kind, nodes, focus, +result} = s
  a[(base + 3 : U32)] <- focus
  save_outcome(result, base, save_nodes(nodes, (base + 16 : U32), save_rewards(previous, result, base, elapsed, a)))

def add_node(nodes: +List<W.Node>, kind: U32, selected: U32, pair: Array<U32> & U32) -> Array<U32> & +List<W.Node>:
  (a, target) = pair
  (a, W.N{role(kind), U32.is_eq(selected, 1), U32.is_eq(target, 1)} <> nodes)

def load_selected(nodes: +List<W.Node>, kind: U32, at: U32, pair: Array<U32> & U32) -> Array<U32> & +List<W.Node>:
  (a, selected) = pair
  add_node(nodes, kind, selected, a[(at + 2 : U32)])

def load_role(nodes: +List<W.Node>, +at: U32, pair: Array<U32> & U32) -> Array<U32> & +List<W.Node>:
  (a, kind) = pair
  load_selected(nodes, kind, at, a[(at + 1 : U32)])

def load(n: Nat, +base: U32, pair: Array<U32> & +List<W.Node>) -> Array<U32> & +List<W.Node>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, nodes) = pair
      +at = (base + 16 + U32.from_nat(p) * 3 : U32)
      load(p, base, load_role(nodes, at, a[at]))

def ready(base: U32, kind: U32, +status: U32, focus: U32, correct: U32, count: U32, scale: U32, command: U32, index: U32, +elapsed: U32, pair: Array<U32> & +List<W.Node>) -> Array<U32>:
  (a, nodes) = pair
  s = {W.S{task(kind), nodes, focus, outcome(status, correct, count, scale)} : W.State}
  save(W.step_at(s, action(command, index), elapsed), base, elapsed, status, a)

def read9(+base: U32, kind: U32, size: U32, status: U32, focus: U32, correct: U32, count: U32, scale: U32, command: U32, index: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, elapsed) = pair
  ready(base, kind, status, focus, correct, count, scale, command, index, elapsed, load(U32.to_nat(size), base, (a, Nil{})))

def read8(+base: U32, kind: U32, size: U32, status: U32, focus: U32, correct: U32, count: U32, scale: U32, command: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, index) = pair
  read9(base, kind, size, status, focus, correct, count, scale, command, index, a[(base + 9 : U32)])

def read7(+base: U32, kind: U32, size: U32, status: U32, focus: U32, correct: U32, count: U32, scale: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, command) = pair
  read8(base, kind, size, status, focus, correct, count, scale, command, a[(base + 8 : U32)])

def read6(+base: U32, kind: U32, size: U32, status: U32, focus: U32, correct: U32, count: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, scale) = pair
  read7(base, kind, size, status, focus, correct, count, scale, a[(base + 7 : U32)])

def read5(+base: U32, kind: U32, size: U32, status: U32, focus: U32, correct: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, count) = pair
  read6(base, kind, size, status, focus, correct, count, a[(base + 6 : U32)])

def read4(+base: U32, kind: U32, size: U32, status: U32, focus: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, correct) = pair
  read5(base, kind, size, status, focus, correct, a[(base + 5 : U32)])

def read3(+base: U32, kind: U32, size: U32, status: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, focus) = pair
  read4(base, kind, size, status, focus, a[(base + 4 : U32)])

def read2(+base: U32, kind: U32, size: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  read3(base, kind, size, status, a[(base + 3 : U32)])

def read1(+base: U32, kind: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, size) = pair
  read2(base, kind, size, a[(base + 2 : U32)])

def read0(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, kind) = pair
  read1(base, kind, a[(base + 1 : U32)])

def clear(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      a[(base + U32.from_nat(p) : U32)] <- 0
      clear(p, base, a)

def write_text(s: String, +at: U32, a: Array<U32>) -> Array<U32>:
  match s:
    case SNil{}: a
    case SCon{ch, rest}:
      a[at] <- Char.to_u32(ch)
      write_text(rest, (at + 1 : U32), a)

def write_entries(es: +List<G.Entry>, +radio: Bool, +base: U32, +index: U32, a: Array<U32>) -> Array<U32>:
  match es:
    case Nil{}:
      a[(base + 16 + 3 * index : U32)] <- 1
      write_text("Submit", (base + 64 + 8 * index : U32), a)
    case Con{G.E{label, target}, rest}:
      a[(base + 16 + 3 * index : U32)] <- Bool.pick(U32, radio, 5, 2)
      a[(base + 18 + 3 * index : U32)] <- Bool.to_u32(target)
      write_entries(rest, radio, base, (index + 1 : U32), write_text(label, (base + 64 + 8 * index : U32), a))

def write_form(+form: G.Form, +base: U32, +kind: U32, seed: U32, a: Array<U32>) -> Array<U32>:
  G.F{radio, es} = form
  a = clear(256n, base, a)
  a[base] <- kind
  a[(base + 1 : U32)] <- G.count(form)
  a[(base + 13 : U32)] <- seed
  a[(base + 15 : U32)] <- 1
  write_entries(es, radio, base, 0, write_text(G.instruction(form), (base + 128 : U32), a))

def reset_form(base: U32, +kind: U32, +seed: U32, a: Array<U32>) -> Array<U32>:
  write_form(G.generate(U32.is_eq(kind, 3), seed), base, kind, seed, a)

def reset_seed(base: U32, kind: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  reset_form(base, kind, seed, a)

def reset_kind(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, kind) = pair
  reset_seed(base, kind, a[(base + 13 : U32)])

def dispatch_command(command: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match command:
    case 3: reset_kind(base, a[base])
    case _: read0(base, a[base])

def dispatch(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, command) = pair
  dispatch_command(command, base, a)

# Tag11 is a reset request: task index at1 and seed at2. It is never a policy observation.
def training_generate(requested_kind: U32, seed: U32, base: U32, a: Array<U32>) -> Array<U32>:
  match requested_kind:
    case 5: reset_form(base, 1, seed, a)
    case 6: reset_form(base, 3, seed, a)
    case 7: XG.generate(4, seed, base, a)
    case 8: XG.generate(5, seed, base, a)
    case 9: XG.generate(6, seed, base, a)
    case 10: TG.generate(seed, base, a)
    case 11: XG.generate(10, seed, base, a)
    case _: SG.generate(requested_kind, seed, base, a)

def training_seed(base: U32, requested_kind: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  training_generate(requested_kind, seed, base, a)

def training_task(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, requested_kind) = pair
  training_seed(base, requested_kind, a[(base + 2 : U32)])

def route(kind: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match kind:
    case 4: TextWire.row(base, a)
    case 5: TextWire.row(base, a)
    case 6: TextWire.row(base, a)
    case 7: SequenceSelect.row(base, a)
    case 8: SequenceSelect.row(base, a)
    case 9: Tree.row(base, a)
    case 10: Autocomplete.row(base, a)
    case 11: training_task(base, a[(base + 1 : U32)])
    case _: dispatch(base, a[(base + 7 : U32)])

def row_kind(base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, kind) = pair
  route(kind, base, a)

def row(+base: U32, a: Array<U32>) -> Array<U32>:
  row_kind(base, a[base])

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: batch(p, row((U32.from_nat(p) * 256 : U32), a))
