import Base
import ../panels/Generate.bend as G
import ./Synonyms.bend as S

def at(xs: +List<String>, index: Nat) -> String:
  match xs index:
    case Nil{} _: ""
    case Con{x, rest} 0n: x
    case Con{x, rest} 1n+p: at(rest, p)

def synonym(index: U32, seed: U32) -> String:
  S.word(index, G.mix(seed))

def required_cases(dispatch: Nat, task: U32, mode: U32, +seed: U32) -> U32:
  match dispatch:
    case 7n: Bool.pick(U32, U32.is_eq(mode, 1), (4 + G.mix(seed) % 3 : U32), (G.mix(seed) % 4 : U32))
    case 8n: (5 + G.mix(seed) % 8 : U32)
    case 9n: (1 + G.mix(seed) % 5 : U32)
    case _: 0


def required(+task: U32, mode: U32, +seed: U32) -> U32:
  required_cases(U32.to_nat(task), task, mode, seed)

def count_cases(dispatch: Nat, task: U32, +needed: U32, seed: U32) -> U32:
  match dispatch:
    case 0n: 1
    case 1n: 2
    case 2n: 2
    case 3n: 1
    case 4n: 3
    case 5n: 5
    case 6n: 3
    case 7n:
      +low = U32.max(3, needed)
      (low + G.mix(seed) % (7 - low) : U32)
    case 8n:
      +low = U32.max(7, needed)
      (low + G.mix(seed) % (13 - low) : U32)
    case _:
      +low = U32.max(3, (needed + 1 : U32))
      (low + G.mix(seed) % (7 - low) : U32)


def count(+task: U32, +needed: U32, seed: U32) -> U32:
  count_cases(U32.to_nat(task), task, needed, seed)

def at_role_cases(dispatch: Nat, k: U32) -> U32:
  match dispatch:
    case 0n: 5
    case 1n: 2
    case 2n: 3
    case 3n: 6
    case _: 1


def at_role(+k: U32) -> U32:
  at_role_cases(U32.to_nat(k), k)

def role_cases(dispatch: Nat, task: U32, +index: U32, +seed: U32) -> U32:
  match dispatch:
    case 5n:
      +k = (G.mix((seed + index : U32)) % 5 : U32)
      at_role(k)
    case 6n: 3
    case 7n: 2
    case 8n: 2
    case 9n: 2
    case _: 1


def role(+task: U32, +index: U32, +seed: U32) -> U32:
  role_cases(U32.to_nat(task), task, index, seed)

def role_name_cases(dispatch: Nat, k: U32) -> String:
  match dispatch:
    case 5n: "radio"
    case 2n: "checkbox"
    case 3n: "text"
    case 6n: "textarea"
    case _: "button"


def role_name(+k: U32) -> String:
  role_name_cases(U32.to_nat(k), k)

def label_cases(dispatch: Nat, task: U32, +index: U32, +seed: U32) -> String:
  match dispatch:
    case 0n: "Click Me!"
    case 1n: Bool.pick(String, U32.is_eq(index, 0), "ONE", "TWO")
    case 2n: Bool.pick(String, U32.is_eq(index, 0), "ONE", "TWO")
    case 3n: "Close"
    case 4n: at(["Close", "Cancel", "OK"], U32.to_nat(index))
    case 6n: ""
    case 9n: synonym(((G.mix(seed) + index) % S.count() : U32), (seed + index : U32))
    case _: "x" ++ U32.show((G.mix(seed) % 1000 : U32)) ++ "n" ++ U32.show(index)


def label(+task: U32, +index: U32, +seed: U32) -> String:
  label_cases(U32.to_nat(task), task, index, seed)

def wanted_cases(dispatch: Nat, task: U32, +index: U32, +n: U32, +need: U32, +seed: U32, target: U32) -> Bool:
  match dispatch:
    case 5n: U32.is_eq(role(5, index, seed), role(5, target, seed))
    case 7n: U32.is_lt(((index + G.mix(seed)) % n : U32), need)
    case 8n: U32.is_lt(((index + G.mix(seed)) % n : U32), need)
    case 9n: U32.is_lt(((index + G.mix(seed)) % n : U32), need)
    case _: U32.is_eq(index, target)


def wanted(+task: U32, +index: U32, +n: U32, +need: U32, +seed: U32, target: U32) -> Bool:
  wanted_cases(U32.to_nat(task), task, index, n, need, seed, target)

def nodes(left: Nat, +task: U32, +n: U32, +need: U32, +seed: U32, +target: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match left:
    case 0n: a
    case 1n+ +p:
      +i = U32.from_nat(p)
      +pos = (base + 64 + i * 40 : U32)
      a[pos] <- role(task, i, seed)
      a[(pos + 2 : U32)] <- Bool.to_u32(wanted(task, i, n, need, seed, target))
      a[(pos + 32 : U32)] <- (10 + i % 2 * 80 : U32)
      a[(pos + 33 : U32)] <- (50 + i / 2 * 20 : U32)
      a[(pos + 34 : U32)] <- 60
      a[(pos + 35 : U32)] <- 18
      nodes(p, task, n, need, seed, target, base, G.text(label(task, i, seed), (pos + 4 : U32), a))

def requested_word_cases(dispatch: Nat, task: U32, +index: U32, +seed: U32) -> String:
  match dispatch:
    case 9n: synonym(((G.mix(seed) + index) % S.count() : U32), (seed + index + 7919 : U32))
    case _: label(task, index, seed)


def requested_word(+task: U32, +index: U32, +seed: U32) -> String:
  requested_word_cases(U32.to_nat(task), task, index, seed)

def query_words(left: Nat, +task: U32, +n: U32, +need: U32, +seed: U32, acc: String) -> String:
  match left:
    case 0n: acc
    case 1n+ +p:
      +i = U32.from_nat(p)
      query_words(p, task, n, need, seed, acc ++ Bool.pick(String, wanted(task, i, n, need, seed, 0), requested_word(task, i, seed) ++ ", ", ""))

def query_cases(dispatch: Nat, +task: U32, +n: U32, +need: U32, +seed: U32, +target: U32) -> String:
  match dispatch:
    case 0n: "Click the button."
    case 1n: "Click button ONE."
    case 2n: "Click button " ++ Bool.pick(String, U32.is_eq(target, 0), "ONE", "TWO") ++ "."
    case 3n: "Close the dialog box by clicking the \"x\"."
    case 4n: "Click the button in the dialog box labeled \"" ++ at(["x", "Cancel", "OK"], U32.to_nat(target)) ++ "\"."
    case 5n: "Click on a \"" ++ role_name(role(task, target, seed)) ++ "\" widget."
    case 6n: "Focus into the " ++ at(["1st", "2nd", "3rd"], U32.to_nat(target)) ++ " input textbox."
    case _:
      Bool.pick(String, U32.is_eq(task, 9), "Select words similar to ", "Select ") ++ Bool.pick(String, U32.is_eq(need, 0), "nothing", query_words(U32.to_nat(n), task, n, need, seed, "")) ++ " and click Submit."


def query(+task: U32, +n: U32, +need: U32, +seed: U32, +target: U32) -> String:
  query_cases(U32.to_nat(task), task, n, need, seed, target)

def submit(form: Bool, n: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match form:
    case False{}: a
    case True{}:
      +pos = (base + 64 + n * 40 : U32)
      a[pos] <- 1
      a[(pos + 32 : U32)] <- 10
      a[(pos + 33 : U32)] <- 195
      a[(pos + 34 : U32)] <- 80
      a[(pos + 35 : U32)] <- 20
      G.text("Submit", (pos + 4 : U32), a)

def generate(+task: U32, +seed: U32, +mode: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +need = required(task, mode, seed)
  +n = count(task, need, (seed + 31 : U32))
  +form = U32.is_ge(task, 7)
  +target = Bool.pick(U32, U32.is_eq(task, 2), Bool.to_u32(U32.is_eq(mode, 1)), Bool.pick(U32, U32.is_eq(task, 1), 0, (G.mix((seed + 19 : U32)) % n : U32)))
  a = G.clear_blocks(16n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 6 : U32)] <- mode
  a[(base + 12 : U32)] <- Bool.pick(U32, U32.is_eq(task, 8), 20000, 10000)
  a[(base + 32 : U32)] <- Bool.pick(U32, form, 2, Bool.pick(U32, U32.is_eq(task, 6), 1, 0))
  a[(base + 33 : U32)] <- (n + Bool.to_u32(form) : U32)
  a = nodes(U32.to_nat(n), task, n, need, seed, target, base, a)
  a = submit(form, n, base, a)
  G.text(query(task, n, need, seed, target), (base + 768 : U32), a)
