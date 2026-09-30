import Base
import ./Model.bend as M

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

def padded(+value: U32) -> String:
  Bool.pick(String, U32.is_lt(value, 10), "0" ++ U32.show(value), U32.show(value))

def month_of(remaining: Nat, +ordinal: U32, +month: Nat) -> U32:
  match remaining:
    case 0n: 12
    case 1n+p:
      +m = U32.from_nat(month)
      Bool.pick(U32,
                U32.is_le(ordinal, (M.ordinal_before(month) + M.days(m) : U32)),
                m, month_of(p, ordinal, 1n+month))

def date_string(+ordinal: U32) -> String:
  +month = month_of(12n, ordinal, 1n)
  +day = (ordinal - M.ordinal_before(U32.to_nat(month)) : U32)
  padded(month) ++ "/" ++ padded(day) ++ "/2016"

def daily_name_kind(index: Nat) -> String:
  match index:
    case 0n: "Phonecall"
    case 1n: "Food"
    case 2n: "Party"
    case 3n: "Meeting"
    case _: "Gym"

def daily_name(index: U32) -> String:
  daily_name_kind(U32.to_nat(index))

def window_name_kind(index: Nat) -> String:
  match index:
    case 0n: "8AM and 12PM"
    case 1n: "12PM and 4PM"
    case _: "4PM and 8PM"

def window_name(index: U32) -> String:
  window_name_kind(U32.to_nat(index))

def duration_name_kind(duration: Nat, +hours: Bool) -> String:
  match duration:
    case 1n: Bool.pick(String, hours, "0.5 hours", "30 mins")
    case 2n: Bool.pick(String, hours, "1 hour", "60 mins")
    case 3n: Bool.pick(String, hours, "1.5 hours", "90 mins")
    case _: "undefined"

def duration_name(duration: U32, hours: Bool) -> String:
  duration_name_kind(U32.to_nat(duration), hours)

def event_start(seed: U32, +segment: U32) -> U32:
  (12 + segment * 12 + mix((seed + 201 + segment * 7 : U32)) % 7 : U32)

def event_end(+seed: U32, +segment: U32) -> U32:
  +offset = (event_start(seed, segment) - (12 + segment * 12 : U32) : U32)
  +delta = Bool.pick(U32, U32.is_eq(offset, 6), 1,
                     (1 + mix((seed + 205 + segment * 7 : U32)) % 2 : U32))
  (event_start(seed, segment) + delta + 1 : U32)

def daily(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +content = mix((seed + 101 : U32))
  +target = mix((seed + 301 : U32))
  +window = (target % 3 : U32)
  +duration = (1 + mix((seed + 302 : U32)) % 3 : U32)
  +name = (mix((seed + 303 : U32)) % 5 : U32)
  a[(base + 36 : U32)] <- window
  a[(base + 37 : U32)] <- duration
  a[(base + 38 : U32)] <- name
  a[(base + 39 : U32)] <- 48
  a[(base + 40 : U32)] <- 48
  a[(base + 48 : U32)] <- event_start(seed, 0)
  a[(base + 49 : U32)] <- event_start(seed, 1)
  a[(base + 50 : U32)] <- event_start(seed, 2)
  a[(base + 51 : U32)] <- event_end(seed, 0)
  a[(base + 52 : U32)] <- event_end(seed, 1)
  a[(base + 53 : U32)] <- event_end(seed, 2)
  a[(base + 54 : U32)] <- (content % 5 : U32)
  a[(base + 55 : U32)] <- (mix((seed + 102 : U32)) % 5 : U32)
  a[(base + 56 : U32)] <- (mix((seed + 103 : U32)) % 5 : U32)
  text("Create a " ++ duration_name(duration, U32.is_eq((content % 2 : U32), 0)) ++
       " event named \"" ++ daily_name(name) ++ "\", between " ++
       window_name(window) ++ ".", (base + 256 : U32), a)

def date_target_kind(task: Nat, draw: U32) -> U32:
  match task:
    case 1n: (336 + draw % 30 : U32)
    case 2n: (306 + draw % 60 : U32)
    case _: (1 + draw % 365 : U32)

def date_target(task: U32, seed: U32) -> U32:
  +draw = mix((seed + 301 : U32))
  date_target_kind(U32.to_nat(task), draw)

def date_task(task: U32, seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +target = date_target(task, seed)
  a[(base + 32 : U32)] <- 12
  a[(base + 35 : U32)] <- target
  text("Select " ++ date_string(target) ++ " as the date and hit submit.",
       (base + 256 : U32), a)

def initialize(task: U32, seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = clear_blocks(8n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 20000

def generate_kind(task_kind: Nat, task: U32, seed: U32,
                  base: U32, a: Array<U32>) -> Array<U32>:
  match task_kind:
    case 4n: daily(seed, base, a)
    case _: date_task(task, seed, base, a)

def generate(+task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  generate_kind(U32.to_nat(task), task, seed, base, initialize(task, seed, base, a))
