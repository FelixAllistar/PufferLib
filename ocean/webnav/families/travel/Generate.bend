import Base
import ../../miniwob/training/TextGenerate.bend as TG

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
    case 1n+ +p:
      clear_blocks(p, base,
        clear(128n, (base + U32.from_nat(p) * 128 : U32), a))

def write_text(s: String, +at: U32, a: Array<U32>) -> Array<U32>:
  match s:
    case SNil{}: a
    case SCon{ch, rest}: write_text(rest, (at + 1 : U32), a[at] <- Char.to_u32(ch))

def name_at(xs: +List<String>, index: Nat) -> String:
  match xs index:
    case Nil{} _: ""
    case Con{value, rest} 0n: value
    case Con{value, rest} 1n+p: name_at(rest, p)

def airport(index: U32) -> String:
  name_at(["", "Boston, MA (BOS)", "Chicago, IL - O'Hare (ORD)",
    "Denver, CO - International (DEN)", "Los Angeles, CA (LAX)",
    "New York, NY - Kennedy (JFK)", "San Francisco, CA (SFO)"],
    U32.to_nat(index))

def city(index: U32) -> String:
  name_at(["", "Boston, MA", "Chicago, IL - O'Hare",
    "Denver, CO - International", "Los Angeles, CA",
    "New York, NY - Kennedy", "San Francisco, CA"], U32.to_nat(index))

def code(index: U32) -> String:
  name_at(["", "BOS", "ORD", "DEN", "LAX", "JFK", "SFO"], U32.to_nat(index))

def public_airport(+index: U32, abbreviated: Bool) -> String:
  Bool.pick(String, abbreviated, code(index), city(index))

def two_digits(+value: U32) -> String:
  Bool.pick(String, U32.is_lt(value, 10), "0" ++ U32.show(value), U32.show(value))

def date_later(second: Bool, day: U32) -> String:
  match second:
    case True{}: "11/" ++ two_digits((day - 31 : U32)) ++ "/2016"
    case False{}: "12/" ++ two_digits((day - 61 : U32)) ++ "/2016"

def date_piece(first: Bool, +day: U32) -> String:
  match first:
    case True{}: "10/" ++ two_digits(day) ++ "/2016"
    case False{}: date_later(U32.is_le(day, 61), day)

def date_text(+day: U32) -> String:
  date_piece(U32.is_le(day, 31), day)

def flight_price(seed: U32, index: U32) -> U32:
  (50 + mix((seed + index * 101 + 3001 : U32)) % 1150 : U32)

def flight_duration(seed: U32, index: U32) -> U32:
  (60000 + mix((seed + index * 131 + 7001 : U32)) % 129540000 : U32)

def write_flights(n: Nat, +seed: U32, +offset: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +index = U32.from_nat(p)
      +at = (base + offset + index * 4 : U32)
      a[at] <- flight_price(seed, index)
      a[(at + 1 : U32)] <- flight_duration(seed, index)
      write_flights(p, seed, offset, base, a)

def write_airport(+index: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +units = TG.units(airport(index))
  +at = (base + 1024 + index * 128 : U32)
  a[at] <- U32.from_nat(List.length(&2, U32, units))
  TG.write_units(units, (at + 1 : U32), a)

def write_airports(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +index = (U32.from_nat(p) + 1 : U32)
      write_airports(p, base, write_airport(index, base, a))

def query(origin: U32, destination: U32, day: U32,
    shortest: Bool, abbrev_origin: Bool, abbrev_destination: Bool) -> String:
  "Book the " ++ Bool.pick(String, shortest, "shortest", "cheapest") ++
    " one-way flight from: " ++ public_airport(origin, abbrev_origin) ++
    " to: " ++ public_airport(destination, abbrev_destination) ++
    " on " ++ date_text(day) ++ "."

def generate(task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +origin = (1 + mix((seed + 11 : U32)) % 6 : U32)
  +destination = (1 + mix((seed + 17 : U32)) % 6 : U32)
  +day = (1 + mix((seed + 23 : U32)) % 91 : U32)
  +shortest = U32.is_eq((mix((seed + 29 : U32)) % 2 : U32), 1)
  +count = (3 + mix((seed + 31 : U32)) % 2 : U32)
  +fake_count = (3 + mix((seed + 2187954 : U32)) % 2 : U32)
  +abbrev_origin = U32.is_eq((mix((seed + 37 : U32)) % 2 : U32), 1)
  +abbrev_destination = U32.is_eq((mix((seed + 41 : U32)) % 2 : U32), 1)
  a = clear_blocks(64n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 2 : U32)] <- 0
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 30000
  a[(base + 35 : U32)] <- origin
  a[(base + 36 : U32)] <- destination
  a[(base + 37 : U32)] <- day
  a[(base + 38 : U32)] <- Bool.to_u32(shortest)
  a[(base + 40 : U32)] <- count
  a[(base + 43 : U32)] <- 6
  a[(base + 44 : U32)] <- fake_count
  a = write_flights(U32.to_nat(count), seed, 64, base, a)
  a = write_flights(U32.to_nat(fake_count), (seed + 2187923 : U32), 128, base, a)
  a = write_airports(6n, base, a)
  write_text(query(origin, destination, day, shortest,
    abbrev_origin, abbrev_destination), (base + 512 : U32), a)
