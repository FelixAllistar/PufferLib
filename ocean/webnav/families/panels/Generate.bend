import Base

# Generator distributions are explicit subsets; imported browser instances use
# the same transition model but their original public names and target flags.
def mix(+x: U32) -> U32:
  +a = (x .^. (x >> 16n) : U32)
  +b = (a * 2146121005 : U32)
  +c = (b .^. (b >> 15n) : U32)
  +d = (c * 2221713035 : U32)
  (d .^. (d >> 16n) : U32)

def panels(task: U32, seed: U32) -> U32:
  match task:
    case 2: 1
    case 3: 2
    case 4: (2 + mix(seed) % 5 : U32)
    case 5: 2
    case 6: 2
    case _: 3

def deadline(task: U32) -> U32:
  match task:
    case 4: 20000
    case _: 10000

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

def write_node(+index: U32, kind: U32, panel: U32, goal: Bool, label: String, +base: U32, a: Array<U32>) -> Array<U32>:
  +at = (base + 64 + index * 48 : U32)
  a[at] <- kind
  a[(at + 1 : U32)] <- panel
  a[(at + 2 : U32)] <- Bool.to_u32(goal)
  text(label, (at + 4 : U32), a)

def headers(n: Nat, +accordion: Bool, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +panel = (U32.from_nat(p) + 1 : U32)
      label = (Bool.pick(String, accordion, "Section #", "Tab #") ++ U32.show(panel))
      headers(p, accordion, base, write_node(U32.from_nat(p), 0, panel, False{}, label, base, a))

def label(+index: U32, seed: U32) -> String:
  "Word" ++ U32.show((mix(seed) % 1000 : U32)) ++ "-" ++ U32.show(index)

def links(n: Nat, +count: U32, +target: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +i = U32.from_nat(p)
      links(p, count, target, seed, base, write_node((count + i : U32), 1, (1 + i / 2 : U32), U32.is_eq(i, target), label(i, seed), base, a))

def content(task: U32, +count: U32, +target: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match task:
    case 0:
      a[(base + 38 : U32)] <- count
      text("Click on Tab #" ++ U32.show((target + 1 : U32)) ++ ".", (base + 6208 : U32), a)
    case 5:
      a[(base + 38 : U32)] <- 2
      a = write_node(1, 2, 0, False{}, "Submit", base, a)
      text("Expand the section below and click submit.", (base + 6208 : U32), a)
    case 6:
      a[(base + 38 : U32)] <- 2
      a = write_node(1, 2, 0, False{}, "Submit", base, a)
      text("Expand the section below and click submit.", (base + 6208 : U32), a)
    case 3:
      a[(base + 38 : U32)] <- (count + 2 : U32)
      a[(base + 36 : U32)] <- Bool.pick(U32, U32.is_ge(target, 2), 1, 2)
      a = links(2n, count, target, seed, base, a)
      text("Switch between the tabs to find and click on the link \"" ++ label(target, seed) ++ "\".", (base + 6208 : U32), a)
    case _:
      a[(base + 38 : U32)] <- (count * 3 : U32)
      a = links(U32.to_nat((count * 2 : U32)), count, target, seed, base, a)
      text(Bool.pick(String, U32.is_ge(task, 5), "Expand the sections below, to find and click on the link \"", "Switch between the tabs to find and click on the link \"") ++ label(target, seed) ++ "\".", (base + 6208 : U32), a)

def generate(+task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +count = panels(task, seed)
  +accordion = U32.is_ge(task, 5)
  +target = (mix((seed + 17 : U32)) % Bool.pick(U32, U32.is_eq(task, 0), count, (count * 2 : U32)) : U32)
  a = clear_blocks(64n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- deadline(task)
  a[(base + 32 : U32)] <- count
  a[(base + 33 : U32)] <- Bool.pick(U32, accordion, 0, Bool.pick(U32, U32.is_eq(task, 0), (1 + (target + 1) % count : U32), 1))
  a[(base + 34 : U32)] <- Bool.to_u32(accordion)
  a[(base + 35 : U32)] <- Bool.pick(U32, U32.is_eq(task, 0), (target + 1 : U32), 0)
  a[(base + 37 : U32)] <- Bool.to_u32(U32.is_eq(task, 5) || U32.is_eq(task, 7))
  content(task, count, target, seed, base, headers(U32.to_nat(Bool.pick(U32, U32.is_eq(task, 5) || U32.is_eq(task, 6), 1, count)), accordion, base, a))
