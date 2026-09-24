import Base
import ../autocomplete/Countries.bend as C

# Independent, deterministic CPU generator for the bounded text tasks and
# use-autocomplete-nodelay.  The stream is deliberately not seedrandom: it is
# a small reproducible Bend stream used by the native trainer.

def mix(+x: U32) -> U32:
  a = (x .^. (x >> 16n) : U32)
  +b = (a * 2146121005 : U32)
  +c = (b .^. (b >> 15n) : U32)
  +d = (c * 2221713035 : U32)
  (d .^. (d >> 16n) : U32)

def next_seed(x: U32) -> U32:
  mix((x + 2654435769 : U32))

def chars(xs: +List<U32>) -> +List<Char>:
  match xs:
    case Nil{}:
      Nil{}
    case Con{x, rest}:
      Char.from_u32(x) <> chars(rest)

def string(xs: +List<U32>) -> String:
  String.from_list(chars(xs))

def units(s: String) -> +List<U32>:
  match s:
    case SNil{}:
      Nil{}
    case SCon{head, tail}:
      Char.to_u32(head) <> units(tail)

def string_at(xs: +List<String>, index: Nat) -> String:
  match xs index:
    case Nil{} _:
      ""
    case Con{item, rest} 0n:
      item
    case Con{item, rest} 1n+p:
      string_at(rest, p)

# The exact fifty-name pool used by the pinned enter-text and login-user
# pages.  Keeping the source pool here makes the public instruction useful to
# a browser harness while preserving the original <=32 character field bound.
def name(+seed: U32) -> String:
  +xs = {[
    "Kenda", "Karrie", "Joye", "Cheree", "Deneen", "Tula", "Briana",
    "Beaulah", "Vina", "Vanda", "Cierra", "Leonie", "Bernardine", "Keli",
    "Renda", "Marcella", "Kanesha", "Enola", "Nathalie", "Annis", "Nieves",
    "Lyda", "Livia", "Kasie", "Tora", "Macie", "Agustina", "Dolores",
    "Ashlea", "Cristin", "Jerald", "Teodoro", "Rex", "Riley", "Jess", "Emile",
    "Donovan", "Keneth", "Chas", "Thaddeus", "Sergio", "Michel", "Myron",
    "Olin", "Dannie", "Juan", "Alan", "Ignacio", "Truman", "Augus"
  ] : +List<String>}
  +n = List.length(&2, String, xs)
  +bound = U32.from_nat(n)
  +index = U32.to_nat((mix(seed) % bound : U32))
  string_at(xs, index)

def password_char(+x: U32) -> Char:
  +r = (x % 36 : U32)
  Char.from_u32(Bool.pick(U32, U32.is_lt(r, 26), (97 + r : U32), (48 + r - 26 : U32)))

def password(n: Nat, +seed: U32) -> String:
  match n:
    case 0n:
      SNil{}
    case 1n+p:
      SCon{password_char((seed % 36 : U32)), password(p, next_seed(seed))}

def login_password(+seed: U32) -> String:
  password(U32.to_nat((2 + mix(seed) % 4 : U32)), next_seed(seed))

def username(+seed: U32) -> String:
  String.to_lower(name(seed))

def table_key(+seed: U32, index: U32) -> String:
  +xs = {["Color", "Name", "Country", "Gender", "Year", "Language", "Role", "City"] : +List<String>}
  string_at(xs, U32.to_nat(((index + mix(seed) : U32) % 8 : U32)))

def table_value(+seed: U32, index: U32) -> String:
  +xs = {["red", "blue", "green", "amber", "Alice", "Canada", "Female", "English"] : +List<String>}
  string_at(xs, U32.to_nat(((index + mix(seed) : U32) % 8 : U32)))

def table_choice(+seed: U32) -> U32:
  (mix(next_seed(seed)) % 2 : U32)

def country(+seed: U32) -> String:
  +xs = C.countries()
  +n = List.length(&2, String, xs)
  +bound = U32.from_nat(n)
  +index = U32.to_nat((mix(seed) % bound : U32))
  string_at(xs, index)

def prefix_size(+seed: U32) -> U32:
  (2 + mix(seed) % 3 : U32)

def suffix_size(+seed: U32) -> U32:
  (2 + mix(next_seed(seed)) % 3 : U32)

def match_end(+seed: U32) -> Bool:
  U32.is_eq((mix(next_seed(next_seed(seed))) % 2 : U32), 1)

def country_prefix(+seed: U32) -> +List<U32>:
  List.take(&2, U32, units(country(seed)), U32.to_nat((prefix_size(seed) : U32)))

def country_suffix(+seed: U32) -> +List<U32>:
  +xs = units(country(seed))
  +n = List.length(&2, U32, xs)
  List.drop(&2, U32, xs, Nat.sub(n, U32.to_nat((suffix_size(seed) : U32))))

def enter_instruction(+seed: U32) -> String:
  "Enter \"" ++ name(seed) ++ "\" into the text field and press Submit."

def login_instruction(+seed: U32) -> String:
  "Enter username \"" ++ username(seed) ++ "\" and password \"" ++ login_password(seed) ++ "\" then press Login."

def table_instruction(+seed: U32) -> String:
  "Enter the value for \"" ++ table_key(seed, table_choice(seed)) ++ "\" and press Submit."

def autocomplete_instruction_if(ending: Bool, prefix: String, suffix: String) -> String:
  match ending:
    case True{}:
      "Starts \"" ++ prefix ++ "\" ends \"" ++ suffix ++ "\""
    case False{}:
      "Starts \"" ++ prefix ++ "\""

def autocomplete_instruction(+seed: U32) -> String:
  autocomplete_instruction_if(match_end(seed), string(country_prefix(seed)), string(country_suffix(seed)))

# Clear the complete 256-word row before writing.  All public metadata is
# therefore NUL-terminated and stale private payload words cannot survive a
# reset.
def clear(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n:
      a
    case 1n+ +p:
      clear(p, base, a[(base + U32.from_nat(p) : U32)] <- 0)

def initial(kind: U32, count: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = clear(256n, base, a)
  a = a[base] <- kind
  a = a[(base + 1 : U32)] <- count
  a = a[(base + 2 : U32)] <- 0
  a = a[(base + 3 : U32)] <- 0
  a = a[(base + 4 : U32)] <- 0
  a = a[(base + 5 : U32)] <- 0
  a = a[(base + 6 : U32)] <- 0
  a = a[(base + 7 : U32)] <- 0
  a = a[(base + 8 : U32)] <- 0
  a = a[(base + 9 : U32)] <- 0
  a = a[(base + 10 : U32)] <- 0
  a[(base + 11 : U32)] <- 0

def write_units(xs: +List<U32>, +at: U32, a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}:
      a
    case Con{x, rest}:
      write_units(rest, (at + 1 : U32), a[at] <- x)

# Pack public ASCII text four bytes per little-endian U32.  `limit` is the
# fixed byte capacity of the destination region; the full row was cleared by
# initial, so the first unwritten word supplies the NUL terminator.
def packed_word(n: Nat, s: String, +shift: Nat, word: U32) -> String & U32:
  match n:
    case 0n: (s, word)
    case 1n+p:
      match s:
        case SNil{}: (SNil{}, word)
        case SCon{ch, rest}:
          packed_word(p, rest, Nat.add(shift, 8n), U32.or(word, (Char.to_u32(ch) << shift : U32)))

def write_word(offset: U32, a: Array<U32>, pair: String & U32) -> Array<U32> & String:
  (rest, word) = pair
  a[offset] <- word
  (a, rest)

def write_packed_impl(words: Nat, +offset: U32, pair: Array<U32> & String) -> Array<U32>:
  match words:
    case 0n:
      (a, s) = pair
      a
    case 1n+p:
      (a, s) = pair
      write_packed_impl(p, (offset + 1 : U32), write_word(offset, a, packed_word(4n, s, 0n, 0)))

def write_packed(words: Nat, s: String, offset: U32, a: Array<U32>) -> Array<U32>:
  write_packed_impl(words, offset, (a, s))

def write_goal(goal: String, +base: U32, +index: U32, a: Array<U32>) -> Array<U32>:
  +xs = units(goal)
  +at = (base + 96 + index * 32 : U32)
  +n = U32.from_nat(List.length(&2, U32, xs))
  a = a[(base + 16 + index * 4 : U32)] <- 0
  a = a[(base + 17 + index * 4 : U32)] <- 0
  a = a[(base + 18 + index * 4 : U32)] <- 0
  a = a[(base + 19 + index * 4 : U32)] <- n
  write_units(xs, at, a)

def enter(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = initial(4, 1, base, a)
  a = write_goal(name(seed), base, 0, a)
  write_packed(32n, enter_instruction(seed), (base + 224 : U32), a)

def login(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = initial(5, 2, base, a)
  a = write_goal(username(seed), base, 0, a)
  a = write_goal(login_password(seed), base, 1, a)
  write_packed(32n, login_instruction(seed), (base + 224 : U32), a)

def write_table(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = write_packed(2n, table_key(seed, 0), (base + 240 : U32), a)
  a = write_packed(2n, table_value(seed, 0), (base + 242 : U32), a)
  a = write_packed(2n, table_key(seed, 1), (base + 244 : U32), a)
  a = write_packed(2n, table_value(seed, 1), (base + 246 : U32), a)
  a = write_packed(2n, "KEY", (base + 248 : U32), a)
  write_packed(2n, "VALUE", (base + 250 : U32), a)

def table(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = initial(6, 1, base, a)
  a = write_goal(table_value(seed, table_choice(seed)), base, 0, a)
  a = write_table(seed, base, a)
  write_packed(16n, table_instruction(seed), (base + 224 : U32), a)

def autocomplete(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = initial(10, 1, base, a)
  a = a[(base + 12 : U32)] <- Bool.to_u32(match_end(seed))
  a = a[(base + 13 : U32)] <- prefix_size(seed)
  a = a[(base + 14 : U32)] <- suffix_size(seed)
  a = write_units(country_prefix(seed), (base + 160 : U32), a)
  a = write_units(country_suffix(seed), (base + 192 : U32), a)
  write_packed(10n, autocomplete_instruction(seed), (base + 22 : U32), a)

# Unsupported kinds intentionally use the valid enter-text preset.  This
# keeps every emitted row in the original ABI while the trainer's dispatcher
# still passes only 4, 5, 6 and 10.
def generate(kind: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match kind:
    case 4:
      enter(seed, base, a)
    case 5:
      login(seed, base, a)
    case 6:
      table(seed, base, a)
    case 10:
      autocomplete(seed, base, a)
    case _:
      enter(seed, base, a)
