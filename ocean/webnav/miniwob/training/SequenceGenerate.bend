import Base

# Independent CPU generator for the bounded training preset.
#
# This intentionally does not reproduce JavaScript seedrandom.  It produces
# deterministic, typed task instances with the same private field ranges and
# public text contracts as the existing MiniWoB rows.  `base` is the beginning
# of one 256-word row in the caller-owned batch array.

def mix(+x: U32) -> U32:
  +a = (x .^. (x >> 16n) : U32)
  +b = (a * 2146121005 : U32)
  +c = (b .^. (b >> 15n) : U32)
  +d = (c * 2221713035 : U32)
  (d .^. (d >> 16n) : U32)

def next(x: U32) -> U32:
  mix((x + 2654435769 : U32))

def task_tag(task: U32) -> U32:
  match task:
    case 0: 0
    case 1: 0
    case 2: 2
    case 3: 7
    case 4: 8
    case _: 255

def widget_role(task: U32) -> U32:
  Bool.pick(U32, U32.is_eq(task, 0), 1, 4)

def focus_label() -> String:
  "Textbox"

def focus_query() -> String:
  "Focus into the textbox."

def sequence_query() -> String:
  "Click button ONE, then click button TWO."

def choose_query(label: String) -> String:
  "Select " ++ label ++ " from the list and click Submit."

def clear(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n:
      a
    case 1n+ +p:
      a[(base + U32.from_nat(p) : U32)] <- 0
      clear(p, base, a)

def text_at(+xs: +List<String>, index: Nat) -> String:
  match xs index:
    case Nil{} _:
      ""
    case Con{+x, rest} 0n:
      x
    case Con{+x, rest} 1n+p:
      text_at(rest, p)

# Pack four ASCII characters little-endian into each U32 word.  The zero word
# written at the end is the terminator; rows are cleared before this function
# is called, so all remaining capacity is zero padding.
def pack_string(s: String, slot: Nat, +at: U32, +word: U32,
                a: Array<U32>) -> Array<U32>:
  match s slot:
    case SNil{} _:
      a[at] <- word
    case SCon{ch, rest} 0n:
      pack_string(rest, 1n, at, U32.or(word, Char.to_u32(ch)), a)
    case SCon{ch, rest} 1n:
      pack_string(rest, 2n, at,
                  U32.or(word, U32.shln(Char.to_u32(ch), 8n)), a)
    case SCon{ch, rest} 2n:
      pack_string(rest, 3n, at,
                  U32.or(word, U32.shln(Char.to_u32(ch), 16n)), a)
    case SCon{ch, rest} 3n:
      +next_word = U32.or(word, U32.shln(Char.to_u32(ch), 24n))
      pack_string(rest, 0n, (at + 1 : U32), 0, a[at] <- next_word)
    case SCon{ch, rest} 4n+ +p:
      # The caller keeps slot in the range 0..3.
      a[at] <- word

def button_words() -> +List<String>:
  ["submit", "okay", "cancel", "next", "previous", "confirm", "apply", "continue",
   "accept", "close", "open", "start", "stop", "save", "send", "finish"]

def link_words() -> +List<String>:
  ["alpha", "bravo", "charlie", "delta", "echo", "foxtrot", "golf", "hotel",
   "india", "juliet", "kilo", "lima", "mike", "november", "oscar", "papa"]

def option_words() -> +List<String>:
  ["Alice", "Bob", "Carol", "Diana", "Canada", "Japan", "Brazil", "Kenya",
   "Norway", "Mexico", "New Zealand", "South Africa", "Vatican City State",
   "Heard Island and McDonald Islands", "Singapore", "Uruguay"]

def widget_count(+seed: U32) -> U32:
  (2 + mix(seed) % 5 : U32)

def widget_start(+seed: U32) -> U32:
  (mix(next(seed)) % 16 : U32)

def widget_target(+seed: U32, count: U32) -> U32:
  (mix(next(next(seed))) % count : U32)

def widget_words_for(task: U32) -> +List<String>:
  match task:
    case 0:
      button_words()
    case _:
      link_words()

def write_widgets(n: Nat, +words: +List<String>, +start: U32, +target: U32,
                  +role: U32, +base: U32, +index: U32,
                  a: Array<U32>) -> Array<U32>:
  match n:
    case 0n:
      a
    case 1n+ +p:
      +word_index = ((start + index : U32) % 16 : U32)
      +label = text_at(words, U32.to_nat(word_index))
      +at = (base + 64 + index * 8 : U32)
      a = a[(base + 16 + index * 3 : U32)] <- role
      a = a[(base + 17 + index * 3 : U32)] <- 0
      a = a[(base + 18 + index * 3 : U32)] <- Bool.to_u32(U32.is_eq(index, target))
      write_widgets(p, words, start, target, role, base, (index + 1 : U32),
                    pack_string(label, 0n, at, 0, a))

def click_query(label: String) -> String:
  "Click " ++ SCon{Char.from_u32(34), SNil{}} ++ label ++ SCon{Char.from_u32(34), SNil{}} ++ "."

def write_common(+base: U32, +tag: U32, +count: U32, a: Array<U32>) -> Array<U32>:
  a = a[base] <- tag
  a = a[(base + 1 : U32)] <- count
  a = a[(base + 2 : U32)] <- 0
  a = a[(base + 3 : U32)] <- 0
  a = a[(base + 4 : U32)] <- 0
  a = a[(base + 5 : U32)] <- 0
  a = a[(base + 6 : U32)] <- 0
  a = a[(base + 7 : U32)] <- 0
  a = a[(base + 8 : U32)] <- 0
  a[(base + 9 : U32)] <- 0

def write_widgets_instance(+task: U32, +seed: U32, +base: U32,
                            a: Array<U32>) -> Array<U32>:
  +count = widget_count(seed)
  +start = widget_start(seed)
  +target = widget_target(seed, count)
  +words = widget_words_for(task)
  +role = widget_role(task)
  +target_label = text_at(words, U32.to_nat(((start + target : U32) % 16 : U32)))
  +query = click_query(target_label)
  a = write_common(base, task_tag(task), count, a)
  a = write_widgets(U32.to_nat(count), words, start, target, role, base, 0, a)
  pack_string(query, 0n, (base + 224 : U32), 0, a)

def write_focus_instance(+base: U32, a: Array<U32>) -> Array<U32>:
  a = write_common(base, task_tag(2), 1, a)
  a = a[(base + 16 : U32)] <- 3
  a = a[(base + 17 : U32)] <- 0
  a = a[(base + 18 : U32)] <- 1
  a = pack_string(focus_label(), 0n, (base + 64 : U32), 0, a)
  pack_string(focus_query(), 0n, (base + 224 : U32), 0, a)

def write_sequence_instance(+seed: U32, +base: U32,
                            a: Array<U32>) -> Array<U32>:
  +r0 = mix(seed)
  +r1 = next(r0)
  +r2 = next(r1)
  +r3 = next(r2)
  +low = (r0 % 38 : U32)
  +high = (78 + r2 % 40 : U32)
  +swap = U32.is_eq((r1 % 2 : U32), 1)
  a = write_common(base, task_tag(3), 0, a)
  a = a[(base + 13 : U32)] <- Bool.pick(U32, swap, high, low)
  a = a[(base + 14 : U32)] <- (50 + r1 % 118 : U32)
  a = a[(base + 15 : U32)] <- Bool.pick(U32, swap, low, high)
  a = a[(base + 16 : U32)] <- (50 + r3 % 118 : U32)
  a = a[(base + 17 : U32)] <- 1
  a = pack_string("ONE", 0n, (base + 64 : U32), 0, a)
  a = pack_string("TWO", 0n, (base + 72 : U32), 0, a)
  pack_string(sequence_query(), 0n, (base + 224 : U32), 0, a)

def option_start(+seed: U32) -> U32:
  (mix(next(seed)) % 16 : U32)

def option_target(+seed: U32, count: U32) -> U32:
  (mix(next(next(seed))) % count : U32)

def write_options(n: Nat, +words: +List<String>, +start: U32,
                  +base: U32, +index: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n:
      a
    case 1n+ +p:
      +word_index = ((start + index : U32) % 16 : U32)
      +label = text_at(words, U32.to_nat(word_index))
      +length = String.length(label)
      +at = (base + 32 + index * 16 : U32)
      a = a[(base + 17 + index : U32)] <- U32.from_nat(length)
      write_options(p, words, start, base, (index + 1 : U32),
                    pack_string(label, 0n, at, 0, a))

def write_choose_instance(+seed: U32, +base: U32,
                          a: Array<U32>) -> Array<U32>:
  +count = (3 + mix(seed) % 4 : U32)
  +start = option_start(seed)
  +target = option_target(seed, count)
  +words = option_words()
  +target_label = text_at(words, U32.to_nat(((start + target : U32) % 16 : U32)))
  +query = choose_query(target_label)
  a = write_common(base, task_tag(4), 0, a)
  a = a[(base + 10 : U32)] <- 0
  a = a[(base + 11 : U32)] <- target
  a = a[(base + 12 : U32)] <- 1
  a = a[(base + 13 : U32)] <- count
  a = write_options(U32.to_nat(count), words, start, base, 0, a)
  pack_string(query, 0n, (base + 224 : U32), 0, a)

def generate(task: U32, seed: U32, +base: U32,
             a: Array<U32>) -> Array<U32>:
  match task:
    case 0:
      write_widgets_instance(0, seed, base, clear(256n, base, a))
    case 1:
      write_widgets_instance(1, seed, base, clear(256n, base, a))
    case 2:
      write_focus_instance(base, clear(256n, base, a))
    case 3:
      write_sequence_instance(seed, base, clear(256n, base, a))
    case 4:
      write_choose_instance(seed, base, clear(256n, base, a))
    case _:
      clear(256n, base, a)
