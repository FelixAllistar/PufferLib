import Base

def mix(+x: U32) -> U32:
  +a = (x .^. (x >> 16n) : U32)
  +b = (a * 2146121005 : U32)
  +c = (b .^. (b >> 15n) : U32)
  +d = (c * 2221713035 : U32)
  (d .^. (d >> 16n) : U32)

def clear(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: clear(p, base, a[(base + U32.from_nat(p) : U32)] <- 0)

def clear_blocks(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: clear_blocks(p, base, clear(128n, (base + U32.from_nat(p) * 128 : U32), a))

def text(s: String, +at: U32, a: Array<U32>) -> Array<U32>:
  match s:
    case SNil{}: a
    case SCon{ch, rest}: text(rest, (at + 1 : U32), a[at] <- Char.to_u32(ch))

def names() -> List<String>:
  ["Aaren", "Aarika", "Abagael", "Abbe", "Abbey", "Abbi", "Abbie", "Abby",
   "Ada", "Adah", "Adaline", "Adan", "Adara", "Adda", "Addi", "Addie",
   "Adel", "Adela", "Adelaide", "Adele", "Adeline", "Adella", "Adena", "Adi",
   "Aimee", "Ainsley", "Alaine", "Alana", "Alberta", "Alice", "Alison", "Allie"]

def nth(xs: List<String>, index: Nat) -> String:
  match xs index:
    case Nil{} _: ""
    case Con{x, rest} 0n: x
    case Con{x, rest} 1n+p: nth(rest, p)

def name(index: U32, seed: U32) -> String:
  +start = (mix(seed) % 32 : U32)
  nth(names(), U32.to_nat(((start + index) % 32 : U32)))

def name_ref(ref: U32, seed: U32) -> String:
  name((ref - 1 : U32), seed)

def parent_ref(index: Nat) -> U32:
  match index:
    case 0n: 0
    case 1n: 0
    case 2n: 0
    case 3n: 0
    case 4n: 1
    case 5n: 1
    case 6n: 4
    case 7n: 4
    case 8n: 2
    case 9n: 2
    case _: 0

def path_state(depth: Nat, ref: U32, +seed: U32, +parent: U32) -> String:
  match depth:
    case 0n: name_ref(ref, seed)
    case 1n+p:
      match parent:
        case 0: name_ref(ref, seed)
        case _:
          path_state(p, parent, seed, parent_ref(U32.to_nat(parent))) ++ " > " ++ name_ref(ref, seed)

def path(depth: Nat, +ref: U32, seed: U32) -> String:
  path_state(depth, ref, seed, parent_ref(U32.to_nat(ref)))

def generated_target(choice: Nat) -> U32:
  match choice:
    case 0n: 3
    case 1n: 5
    case 2n: 6
    case 3n: 7
    case 4n: 8
    case _: 9

def write_node(+ref: U32, parent: U32, kind: U32, target: Bool, enabled: Bool, children: Bool, icon: U32, visible: Bool, label: String, +base: U32, a: Array<U32>) -> Array<U32>:
  +at = (base + 64 + (ref - 1) * 40 : U32)
  a[at] <- kind
  a[(at + 1 : U32)] <- parent
  a[(at + 2 : U32)] <- Bool.to_u32(target)
  a[(at + 3 : U32)] <- Bool.to_u32(enabled)
  a[(at + 4 : U32)] <- Bool.to_u32(children)
  a[(at + 5 : U32)] <- icon
  a[(at + 36 : U32)] <- Bool.to_u32(visible)
  text(label, (at + 6 : U32), a)

def icon_name_index(icon: Nat) -> String:
  match icon:
    case 1n: "disk"
    case 2n: "seek-start"
    case 3n: "stop"
    case 4n: "play"
    case 5n: "seek-end"
    case 6n: "zoomin"
    case 7n: "zoomout"
    case _: "print"

def icon_name(icon: U32) -> String:
  icon_name_index(U32.to_nat(icon))

def task2_target(choice: Nat) -> U32:
  match choice:
    case 0n: 2
    case 1n: 4
    case 2n: 5
    case 3n: 6
    case 4n: 7
    case 5n: 9
    case _: 10

def task2_icon_index(ref: Nat) -> U32:
  match ref:
    case 2n: 1
    case 4n: 2
    case 5n: 3
    case 6n: 4
    case 7n: 5
    case 9n: 6
    case _: 7

def task2_icon(ref: U32) -> U32:
  task2_icon_index(U32.to_nat(ref))

def task2_label_index(ref: Nat) -> String:
  match ref:
    case 2n: "Save"
    case 3n: "Playback"
    case 4n: "Prev"
    case 5n: "Stop"
    case 6n: "Play"
    case 7n: "Next"
    case 8n: "Print..."
    case 9n: "Zoom In"
    case _: "Zoom Out"

def task2_label(ref: U32) -> String:
  task2_label_index(U32.to_nat(ref))

def write_task0(+seed: U32, +target: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = write_node(1, 0, 0, False{}, True{}, True{}, 0, True{}, name(0, seed), base, a)
  a = write_node(2, 0, 0, False{}, True{}, True{}, 0, True{}, name(1, seed), base, a)
  a = write_node(3, 0, 0, U32.is_eq(target, 3), True{}, False{}, 0, True{}, name(2, seed), base, a)
  a = write_node(4, 1, 0, False{}, True{}, True{}, 0, False{}, name(3, seed), base, a)
  a = write_node(5, 1, 0, U32.is_eq(target, 5), True{}, False{}, 0, False{}, name(4, seed), base, a)
  a = write_node(6, 4, 0, U32.is_eq(target, 6), True{}, False{}, 0, False{}, name(5, seed), base, a)
  a = write_node(7, 4, 0, U32.is_eq(target, 7), True{}, False{}, 0, False{}, name(6, seed), base, a)
  a = write_node(8, 2, 0, U32.is_eq(target, 8), True{}, False{}, 0, False{}, name(7, seed), base, a)
  a = write_node(9, 2, 0, U32.is_eq(target, 9), True{}, False{}, 0, False{}, name(8, seed), base, a)
  +query = "Select " ++ path(3n, target, seed) ++ "."
  text(query, (base + 6400 : U32), a)

def write_task1(+seed: U32, +target: U32, variant: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = write_node(1, 0, 1, False{}, True{}, False{}, 0, True{}, "Menu", base, a)
  a = write_node(2, 0, 0, U32.is_eq(target, 2), True{}, False{}, 1, False{}, "Save", base, a)
  a = write_node(3, 0, 0, False{}, True{}, True{}, 0, False{}, "Playback", base, a)
  a = write_node(4, 3, 0, U32.is_eq(target, 4), True{}, False{}, 2, False{}, "Prev", base, a)
  a = write_node(5, 3, 0, U32.is_eq(target, 5), True{}, False{}, 3, False{}, "Stop", base, a)
  a = write_node(6, 3, 0, U32.is_eq(target, 6), True{}, False{}, 4, False{}, "Play", base, a)
  a = write_node(7, 3, 0, U32.is_eq(target, 7), True{}, False{}, 5, False{}, "Next", base, a)
  a = write_node(8, 0, 0, False{}, False{}, False{}, 8, False{}, "Print...", base, a)
  a = write_node(9, 0, 0, U32.is_eq(target, 9), True{}, False{}, 6, False{}, "Zoom In", base, a)
  a = write_node(10, 0, 0, U32.is_eq(target, 10), True{}, False{}, 7, False{}, "Zoom Out", base, a)
  +query = Bool.pick(String, U32.is_eq(variant, 0),
    "Click the \"Menu\" button, and then find and click on the item labeled \"" ++ task2_label(target) ++ "\".",
    "Click the \"Menu\" button, and then find and click on the item with the " ++ icon_name(task2_icon(target)) ++ " icon.")
  text(query, (base + 6400 : U32), a)

def initialize(task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = clear_blocks(64n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 10000
  a

def generate_index(task_index: Nat, task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match task_index:
    case 0n:
      a = initialize(task, seed, base, a)
      +target = generated_target(U32.to_nat((mix(seed) % 6 : U32)))
      a[(base + 32 : U32)] <- 9
      a[(base + 33 : U32)] <- 1
      a[(base + 35 : U32)] <- target
      write_task0(seed, target, base, a)
    case 1n:
      a = initialize(task, seed, base, a)
      +target = task2_target(U32.to_nat((mix((seed + 31 : U32)) % 7 : U32)))
      +variant = (mix((seed + 73 : U32)) % 2 : U32)
      a[(base + 32 : U32)] <- 10
      a[(base + 33 : U32)] <- 0
      a[(base + 35 : U32)] <- target
      a[(base + 36 : U32)] <- variant
      write_task1(seed, target, variant, base, a)
    case _: a

def generate(+task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  generate_index(U32.to_nat(task), task, seed, base, a)
