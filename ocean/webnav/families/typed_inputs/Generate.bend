import Base

# Every generated field, clock, and answer is written by Bend.
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

def padded(+value: U32) -> String:
  Bool.pick(String, U32.is_lt(value, 10), "0" ++ U32.show(value), U32.show(value))

def mix(+x: U32) -> U32:
  +a = (x .^. (x >> 16n) : U32)
  +b = (a * 2146121005 : U32)
  +c = (b .^. (b >> 15n) : U32)
  +d = (c * 2221713035 : U32)
  (d .^. (d >> 16n) : U32)

def display_hour(hour: U32) -> U32:
  +shown = (hour % 12 : U32)
  Bool.pick(U32, U32.is_eq(shown, 0), 12, shown)

def time_query(+hour: U32, minute: U32) -> String:
  "Enter " ++ U32.show(display_hour(hour)) ++ ":" ++ padded(minute) ++
  Bool.pick(String, U32.is_lt(hour, 12), " AM", " PM") ++
  " as the time and press submit."

def time_task(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +hour = (mix((seed + 101 : U32)) % 24 : U32)
  +minute = (mix((seed + 301 : U32)) % 60 : U32)
  a[(base + 32 : U32)] <- hour
  a[(base + 33 : U32)] <- minute
  text(time_query(hour, minute), (base + 256 : U32), a)

# All six source button labels are represented as Unicode scalar values.
# The capitalization branch in the original page leaves these labels unchanged.
def label_chars(id: Nat) -> +List<U32>:
  match id:
    case 0n: [214, 75]
    case 1n: [67, 97, 110, 99, 233, 108]
    case 2n: [9829, 9829, 9829]
    case 3n: [30830, 23450]
    case 4n: [21462, 28040]
    case _: [12504, 12523, 12503]

def label_length(id: Nat) -> U32:
  match id:
    case 0n: 2
    case 1n: 6
    case 2n: 3
    case 3n: 2
    case 4n: 2
    case _: 3

def write_units(xs: +List<U32>, +at: U32, a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{x, rest}: write_units(rest, (at + 1 : U32), a[at] <- x)

def label_id(seed: U32, slot: U32) -> U32:
  (mix((seed + slot * 13 + 17 : U32)) % 6 : U32)

def slot_kind(+seed: U32, +slot: U32, chosen: U32) -> U32:
  Bool.pick(U32, U32.is_eq(slot, chosen), 2,
    (mix((seed + slot * 23 + 71 : U32)) % 3 : U32))

def slot_text(kind: U32, label: U32, at: U32, a: Array<U32>) -> Array<U32>:
  match kind:
    case 0: text(" lorem ipsum dolor", at, a)
    case 1: text(" lorem ipsum dolor: ", at, a)
    case _: write_units(label_chars(U32.to_nat(label)), at, a)

def slots(n: Nat, +seed: U32, +chosen: U32, +base: U32,
          a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +slot = U32.from_nat(p)
      +kind = slot_kind(seed, slot, chosen)
      +label = label_id(seed, slot)
      a[(base + 40 + slot : U32)] <- kind
      a[(base + 48 + slot : U32)] <- label
      slots(p, seed, chosen, base,
        slot_text(kind, label, (base + 512 + slot * 64 : U32), a))

def unicode_query(+goal: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = text("Click on the \"", (base + 256 : U32), a)
  a = write_units(label_chars(U32.to_nat(goal)), (base + 270 : U32), a)
  text("\" button.", (base + 270 + label_length(U32.to_nat(goal)) : U32), a)

def unicode_task(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +chosen = (seed % 6 : U32)
  +goal = label_id(seed, chosen)
  a[(base + 12 : U32)] <- 10000
  a[(base + 32 : U32)] <- goal
  unicode_query(goal, base, slots(6n, seed, chosen, base, a))

def date_task(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +year = (2010 + seed % 10 : U32)
  +month = (1 + (seed / 10 : U32) % 12 : U32)
  +day = (1 + (seed / 120 : U32) % 28 : U32)
  a[(base + 32 : U32)] <- year
  a[(base + 33 : U32)] <- month
  a[(base + 34 : U32)] <- day
  text("Enter " ++ padded(month) ++ "/" ++ padded(day) ++ "/" ++
       U32.show(year) ++ " as the date and hit submit.",
       (base + 256 : U32), a)

def generated(task: U32, seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match task:
    case 0: date_task(seed, base, a)
    case 1: time_task(seed, base, a)
    case _: unicode_task(seed, base, a)

def generate(+task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = clear_blocks(8n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 20000
  generated(task, seed, base, a)
