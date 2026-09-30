import Base
import ./ListModel.bend as L

# Bounded deterministic source-shaped instances. Browser comparisons import
# the exact original text, option values, goal and measured geometry once.
def clear(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: clear(p, base, a[(base + U32.from_nat(p) : U32)] <- 0)

def clear_blocks(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      clear_blocks(p, base,
        clear(128n, (base + U32.from_nat(p) * 128 : U32), a))

def text(s: String, +at: U32, a: Array<U32>) -> Array<U32>:
  match s:
    case SNil{}: a
    case SCon{ch, rest}: text(rest, (at + 1 : U32), a[at] <- Char.to_u32(ch))

def length(s: String) -> U32:
  match s:
    case SNil{}: 0
    case SCon{ch, rest}: (1 + length(rest) : U32)

def word_index(i: Nat) -> String:
  match i:
    case 0n: "lorem"
    case 1n: "ipsum"
    case 2n: "dolor"
    case 3n: "sit"
    case 4n: "amet"
    case 5n: "elit"
    case 6n: "ut"
    case _: "purus"

def word(i: U32) -> String:
  word_index(U32.to_nat((i % 8 : U32)))

def words(n: Nat, +seed: U32) -> String:
  match n:
    case 0n: word(seed) ++ "."
    case 1n+ +p:
      word((seed + U32.from_nat(p) * 3 : U32)) ++ " " ++ words(p, seed)

def label_index(i: Nat) -> String:
  match i:
    case 0n: "Ada"
    case 1n: "Alice"
    case 2n: "Ava"
    case 3n: "Bea"
    case 4n: "Bella"
    case 5n: "Cara"
    case 6n: "Clara"
    case 7n: "Dora"
    case 8n: "Ella"
    case 9n: "Emma"
    case 10n: "Erin"
    case _: "Eva"

def label(i: U32) -> String:
  label_index(U32.to_nat(i))

def person_index(i: Nat) -> String:
  match i:
    case 0n: "Kenda"
    case 1n: "Karrie"
    case 2n: "Joye"
    case 3n: "Cheree"
    case 4n: "Deneen"
    case _: "Tula"

def person(i: U32) -> String:
  person_index(U32.to_nat(i))

def options(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      options(p, base, text(label(U32.from_nat(p)),
        (base + 512 + U32.from_nat(p) * 64 : U32), a))

def list_query(+first: U32, +second: U32, two: Bool) -> String:
  "Select " ++ label(first) ++ Bool.pick(String, two,
    ", " ++ label(second), "") ++ " from the scroll list and click Submit."

def list_task(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +count = (8 + seed % 4 : U32)
  +first = (seed % count : U32)
  +second = ((first + 3 : U32) % count : U32)
  +two = U32.is_eq(((seed / 4 : U32) % 2 : U32), 1)
  +required = Bool.pick(U32, two, 2, 1)
  +allowed = (L.bit(U32.to_nat(first)) + Bool.pick(U32, two, L.bit(U32.to_nat(second)), 0) : U32)
  a[(base + 12 : U32)] <- 15000
  a[(base + 32 : U32)] <- 0
  a[(base + 33 : U32)] <- (count * 18 : U32)
  a[(base + 34 : U32)] <- 90
  a[(base + 47 : U32)] <- 90
  a[(base + 36 : U32)] <- count
  a[(base + 48 : U32)] <- 18
  a[(base + 38 : U32)] <- allowed
  a[(base + 39 : U32)] <- required
  a = text(list_query(first, second, two), (base + 256 : U32), a)
  options(U32.to_nat(count), base, a)

def word_task(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +count = (20 + seed % 130 : U32)
  +last = word(seed)
  a[(base + 12 : U32)] <- 10000
  a[(base + 32 : U32)] <- 0
  a[(base + 33 : U32)] <- 300
  a[(base + 34 : U32)] <- 95
  a[(base + 43 : U32)] <- length(last)
  a = text("Find the last word in the text area, enter it into the text field and hit Submit.",
    (base + 256 : U32), a)
  a = text(words(U32.to_nat((count - 1 : U32)), seed), (base + 1536 : U32), a)
  text(last, (base + 4736 : U32), a)

def direction_task(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +bottom = U32.is_eq((seed % 2 : U32), 0)
  +count = (50 + (seed / 2 : U32) % 100 : U32)
  a[(base + 12 : U32)] <- 10000
  a[(base + 32 : U32)] <- 125
  a[(base + 33 : U32)] <- 300
  a[(base + 34 : U32)] <- 100
  a[(base + 47 : U32)] <- 100
  a[(base + 35 : U32)] <- Bool.to_u32(bottom)
  a = text(Bool.pick(String, bottom,
    "Scroll the textarea to the bottom of the text hit submit.",
    "Scroll the textarea to the top of the text hit submit."),
    (base + 256 : U32), a)
  text(words(U32.to_nat((count - 1 : U32)), seed), (base + 1536 : U32), a)

def agreement_query(mode: Nat, name: String) -> String:
  match mode:
    case 0n: "Click the cancel button."
    case 1n: "Scroll to the bottom of the textarea, enter the name \"" ++ name ++ "\" then press \"Agree\""
    case _: "Scroll to the bottom of the textarea, enter the name \"" ++ name ++ "\" then press \"Cancel\""

def agreement_task(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +mode = (seed % 3 : U32)
  +name = person_index(U32.to_nat(((seed / 3 : U32) % 6 : U32)))
  +count = (150 + (seed / 3 : U32) % 150 : U32)
  a[(base + 12 : U32)] <- 10000
  a[(base + 32 : U32)] <- 0
  a[(base + 33 : U32)] <- 1000
  a[(base + 34 : U32)] <- 85
  a[(base + 40 : U32)] <- mode
  a[(base + 43 : U32)] <- length(name)
  a = text(agreement_query(U32.to_nat(mode), name), (base + 256 : U32), a)
  a = text(words(U32.to_nat((count - 1 : U32)), seed), (base + 1536 : U32), a)
  text(name, (base + 4736 : U32), a)

def generated(task: U32, seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match task:
    case 0: list_task(seed, base, a)
    case 1: word_task(seed, base, a)
    case 2: direction_task(seed, base, a)
    case _: agreement_task(seed, base, a)

def generate(+task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = clear_blocks(64n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  generated(task, seed, base, a)
