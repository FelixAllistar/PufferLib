import Base

# A small independent CPU generator for the tag-9 navigate-tree row.  The
# topology is deliberately bounded to eight pre-order nodes and depth two.
# It is a matched training preset: its integer stream is deterministic, but it
# is not intended to reproduce JavaScript seedrandom's exact distribution.

type ShapeNode is Data:
  SN{kind: U32, parent: U32, subtree_end: U32}

type GeneratedNode is Data:
  GN{kind: U32, target: Bool, expanded: Bool, parent: U32, subtree_end: U32, visible: Bool, label: String}

type TreeInstance is Data:
  TI{nodes: +List<GeneratedNode>, target: String, instruction: String}

def mix(+x: U32) -> U32:
  +a = (x .^. (x >> 16n) : U32)
  +b = (a * 2146121005 : U32)
  +c = (b .^. (b >> 15n) : U32)
  +d = (c * 2221713035 : U32)
  (d .^. (d >> 16n) : U32)

def next_seed(x: U32) -> U32:
  mix((x + 2654435769 : U32))

def digit(+x: U32) -> Char:
  Char.from_u32((48 + x % 10 : U32))

def node_label(+index: U32, +seed: U32) -> String:
  "Node-" ++ Char.show(digit(index)) ++ Char.show(digit((mix((seed + index * 17 : U32)) % 10 : U32)))

# Six fixed shapes cover a root file, roots with siblings, nested folders and
# an eight-node two-root tree.  Every end is the exclusive pre-order endpoint
# of that node's subtree, and every parent is one-based.
def shape(code: U32) -> +List<ShapeNode>:
  match code:
    case 0:
      [SN{0, 0, 1}]
    case 1:
      [SN{1, 0, 4}, SN{0, 1, 2}, SN{1, 1, 4}, SN{0, 3, 4}]
    case 2:
      [SN{1, 0, 3}, SN{0, 1, 2}, SN{0, 1, 3}, SN{0, 0, 4}, SN{0, 0, 5}]
    case 3:
      [SN{0, 0, 1}, SN{1, 0, 4}, SN{1, 2, 4}, SN{0, 3, 4}, SN{0, 0, 5}]
    case 4:
      [SN{1, 0, 5}, SN{0, 1, 2}, SN{1, 1, 4}, SN{0, 3, 4}, SN{0, 1, 5}, SN{0, 0, 6}]
    case _:
      [SN{1, 0, 4}, SN{1, 1, 4}, SN{0, 2, 3}, SN{0, 2, 4},
       SN{1, 0, 7}, SN{0, 5, 6}, SN{0, 5, 7}, SN{0, 0, 8}]

type ShapeKind is Data:
  Single{}
  Nested{}
  Siblings{}
  Mixed{}
  Six{}
  Eight{}

def shape_kind(code: U32) -> ShapeKind:
  match code:
    case 0: Single{}
    case 1: Nested{}
    case 2: Siblings{}
    case 3: Mixed{}
    case 4: Six{}
    case _: Eight{}

def size_for_kind(kind: ShapeKind) -> U32:
  match kind:
    case Single{}: 1
    case Nested{}: 4
    case Siblings{}: 5
    case Mixed{}: 5
    case Six{}: 6
    case Eight{}: 8

def shape_size(code: U32) -> U32:
  size_for_kind(shape_kind(code))

def make_node(spec: ShapeNode, +index: U32, target: U32, +seed: U32) -> GeneratedNode:
  SN{kind, +parent, subtree_end} = spec
  GN{kind, U32.is_eq(index, target), False{}, parent, subtree_end,
     U32.is_eq(parent, 0), node_label(index, seed)}

def make_nodes(specs: +List<ShapeNode>, +index: U32, +target: U32, +seed: U32) -> +List<GeneratedNode>:
  match specs:
    case Nil{}:
      Nil{}
    case Con{spec, rest}:
      make_node(spec, index, target, seed) <> make_nodes(rest, (index + 1 : U32), target, seed)

def bool_eq(a: Bool, b: Bool) -> Bool:
  match a b:
    case False{} False{}: True{}
    case True{} True{}: True{}
    case _ _: False{}

def node_is_initial(node: GeneratedNode) -> Bool:
  GN{_, _, expanded, parent, _, visible, _} = node
  Bool.and(Bool.not(expanded), bool_eq(visible, U32.is_eq(parent, 0)))

def all_initial(nodes: +List<GeneratedNode>) -> Bool:
  match nodes:
    case Nil{}:
      True{}
    case Con{node, rest}:
      Bool.and(node_is_initial(node), all_initial(rest))

def target_count(nodes: +List<GeneratedNode>) -> U32:
  match nodes:
    case Nil{}:
      0
    case Con{GN{_, target, _, _, _, _, _}, rest}:
      (Bool.to_u32(target) + target_count(rest) : U32)

def instruction_mentions(instance: TreeInstance) -> Bool:
  TI{nodes, target, instruction} = instance
  String.contains(instruction, target)

def tree_for(+seed: U32) -> TreeInstance:
  +shape_seed = next_seed(seed)
  +code = (shape_seed % 6 : U32)
  +size = shape_size(code)
  +target = (mix((shape_seed + 101 : U32)) % size : U32)
  +nodes = make_nodes(shape(code), 0, target, seed)
  +goal = node_label(target, seed)
  TI{nodes, goal, "Navigate through the file tree. Find and click on the folder or file named \"" ++ goal ++ "\"."}

def clear(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n:
      a
    case 1n+ +p:
      a[(base + U32.from_nat(p) : U32)] <- 0
      clear(p, base, a)

def read_word(index: U32, a: Array<U32>) -> Array<U32> & U32:
  a[index]

def merge_word(pair: Array<U32> & U32, index: U32, value: U32) -> Array<U32>:
  (a, old) = pair
  a[index] <- U32.or(old, value)

def write_packed_at(s: String, +offset: U32, +position: U32, a: Array<U32>) -> Array<U32>:
  match s:
    case SNil{}:
      a
    case SCon{ch, rest}:
      +word = (offset + position / 4 : U32)
      +shift = U32.to_nat(((position % 4) * 8 : U32))
      +value = (Char.to_u32(ch) << shift : U32)
      write_packed_at(rest, offset, (position + 1 : U32), merge_word(read_word(word, a), word, value))

def write_packed(s: String, +offset: U32, a: Array<U32>) -> Array<U32>:
  write_packed_at(s, offset, 0, a)

def write_node(node: GeneratedNode, +base: U32, +index: U32, a: Array<U32>) -> Array<U32>:
  GN{kind, target, expanded, +parent, subtree_end, visible, label} = node
  +at = (base + 16 + index * 6 : U32)
  +label_at = (base + 64 + index * 8 : U32)
  a = a[at] <- kind
  a = a[(at + 1 : U32)] <- Bool.to_u32(target)
  a = a[(at + 2 : U32)] <- Bool.to_u32(expanded)
  a = a[(at + 3 : U32)] <- parent
  a = a[(at + 4 : U32)] <- subtree_end
  a = a[(at + 5 : U32)] <- Bool.to_u32(visible)
  write_packed(label, label_at, a)

def write_nodes(nodes: +List<GeneratedNode>, +base: U32, +index: U32, a: Array<U32>) -> Array<U32>:
  match nodes:
    case Nil{}:
      a
    case Con{node, rest}:
      write_nodes(rest, base, (index + 1 : U32), write_node(node, base, index, a))

def write_tree(instance: TreeInstance, +base: U32, size: U32, a: Array<U32>) -> Array<U32>:
  TI{nodes, target, instruction} = instance
  a = clear(256n, base, a)
  a = a[base] <- 9
  a = a[(base + 1 : U32)] <- size
  a = a[(base + 2 : U32)] <- 0
  a = a[(base + 3 : U32)] <- 0
  a = a[(base + 4 : U32)] <- 0
  a = a[(base + 5 : U32)] <- 1
  a = a[(base + 6 : U32)] <- 0
  a = write_nodes(nodes, base, 0, a)
  write_packed(instruction, (base + 224 : U32), a)

def generate(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +instance = tree_for(seed)
  write_tree(instance, base, shape_size((next_seed(seed) % 6 : U32)), a)
