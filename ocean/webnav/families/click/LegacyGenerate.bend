import Base
import ../panels/Generate.bend as G

def pick(xs: +List<String>, index: Nat) -> String:
  match xs index:
    case Nil{} _: ""
    case Con{x, rest} 0n: x
    case Con{x, rest} 1n+p: pick(rest, p)

def key(+seed: U32, index: U32) -> U32:
  (G.mix((seed + index * 2654435761 + 7919 : U32)) % 8 : U32)

def letter(+x: U32) -> Char:
  +r = (x % 62 : U32)
  Char.from_u32(Bool.pick(U32, U32.is_lt(r, 26), (65 + r : U32),
    Bool.pick(U32, U32.is_lt(r, 52), (97 + r - 26 : U32), (48 + r - 52 : U32))))

def random_text(n: Nat, +seed: U32) -> String:
  match n:
    case 0n: SNil{}
    case 1n+p: SCon{letter(seed), random_text(p, G.mix((seed + 2654435769 : U32)))}

def label_kind(code: Nat, +seed: U32, +index: U32) -> String:
  match code:
    case 10n: pick(["submit", "okay", "ok", "yes", "no", "cancel", "next", "previous"], U32.to_nat(key(seed, index)))
    case 11n: pick(["lorem", "ipsum", "dolor", "sit", "consectetur", "adipiscing", "elit", "integer"], U32.to_nat(key(seed, index)))
    case 12n: ""
    case 15n: Bool.pick(String, U32.is_eq(index, 0), "ONE", "TWO")
    case _:
      +r = G.mix((seed + index * 2246822519 : U32))
      random_text(U32.to_nat((2 + r % 7 : U32)), r)

def label(task: U32, seed: U32, index: U32) -> String:
  label_kind(U32.to_nat(task), seed, index)

def count_kind(code: Nat, +seed: U32) -> U32:
  match code:
    case 10n: (1 + G.mix(seed) % 6 : U32)
    case 11n: (1 + G.mix(seed) % 8 : U32)
    case 12n: 1
    case 15n: 2
    case _: (2 + G.mix(seed) % 5 : U32)

def role_kind(code: Nat) -> U32:
  match code:
    case 11n: 4
    case 12n: 3
    case 13n: 2
    case 14n: 5
    case _: 1

def mode_kind(code: Nat) -> U32:
  match code:
    case 11n: 1
    case 12n: 1
    case 13n: 2
    case 14n: 3
    case 15n: 4
    case _: 0

def desired_kind(code: Nat, +seed: U32, +index: U32, +target: U32) -> Bool:
  match code:
    case 10n: U32.is_eq(key(seed, index), key(seed, target))
    case 11n: U32.is_eq(key(seed, index), key(seed, target))
    case 13n: U32.is_eq((G.mix((seed + index * 3266489917 + 12347 : U32)) % 2 : U32), 1)
    case _: U32.is_eq(index, target)

def desired(task: U32, seed: U32, index: U32, target: U32) -> Bool:
  desired_kind(U32.to_nat(task), seed, index, target)

def target_count(n: Nat, +task: U32, +seed: U32, +target: U32) -> U32:
  match n:
    case 0n: 0
    case 1n+ +p:
      (Bool.to_u32(desired(task, seed, U32.from_nat(p), target)) + target_count(p, task, seed, target) : U32)

def query_labels(n: Nat, +task: U32, +seed: U32, +target: U32) -> String:
  match n:
    case 0n: ""
    case 1n+ +p:
      +index = U32.from_nat(p)
      query_labels(p, task, seed, target) ++ Bool.pick(String,
        desired(task, seed, index, target), label(task, seed, index) ++ ", ", "")

def query_kind(code: Nat, +task: U32, +seed: U32, n: U32, target: U32, needed: U32) -> String:
  match code:
    case 10n: "Click on the \"" ++ label(task, seed, target) ++ "\" button."
    case 11n: "Click on the link \"" ++ label(task, seed, target) ++ "\"."
    case 12n: "Focus into the textbox."
    case 15n: "Click button ONE, then click button TWO."
    case 14n: "Select " ++ label(task, seed, target) ++ " and click Submit."
    case _: "Select " ++ Bool.pick(String, U32.is_eq(needed, 0), "nothing", query_labels(U32.to_nat(n), task, seed, target)) ++ " and click Submit."

def node_rows(n: Nat, +task: U32, +seed: U32, +target: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +index = U32.from_nat(p)
      +pos = (base + 64 + index * 40 : U32)
      a = a[pos] <- role_kind(U32.to_nat(task))
      a = a[(pos + 2 : U32)] <- Bool.to_u32(desired(task, seed, index, target))
      a = a[(pos + 32 : U32)] <- (10 + index % 2 * 80 : U32)
      a = a[(pos + 33 : U32)] <- (50 + index / 2 * 25 : U32)
      a = a[(pos + 34 : U32)] <- 60
      a = a[(pos + 35 : U32)] <- 20
      node_rows(p, task, seed, target, base, G.text(label(task, seed, index), (pos + 4 : U32), a))

def append_submit(form: Bool, count: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match form:
    case False{}: a
    case True{}:
      +pos = (base + 64 + count * 40 : U32)
      a = a[pos] <- 1
      a = a[(pos + 32 : U32)] <- 10
      a = a[(pos + 33 : U32)] <- 195
      a = a[(pos + 34 : U32)] <- 80
      a = a[(pos + 35 : U32)] <- 20
      G.text("Submit", (pos + 4 : U32), a)

def generate(+task: U32, +seed: U32, mode: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +n = count_kind(U32.to_nat(task), seed)
  +target = (G.mix((seed + 9347 : U32)) % n : U32)
  +form = U32.is_eq(task, 13) || U32.is_eq(task, 14)
  +needed = target_count(U32.to_nat(n), task, seed, target)
  a = G.clear_blocks(16n, base, a)
  a = a[base] <- 2
  a = a[(base + 1 : U32)] <- task
  a = a[(base + 3 : U32)] <- seed
  a = a[(base + 6 : U32)] <- mode
  a = a[(base + 12 : U32)] <- 10000
  a = a[(base + 32 : U32)] <- mode_kind(U32.to_nat(task))
  a = a[(base + 33 : U32)] <- (n + Bool.to_u32(form) : U32)
  a = node_rows(U32.to_nat(n), task, seed, target, base, a)
  a = append_submit(form, n, base, a)
  G.text(query_kind(U32.to_nat(task), task, seed, n, target, needed), (base + 768 : U32), a)
