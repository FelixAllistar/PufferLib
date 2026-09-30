import Base
import ./Generate.bend as G
import ../../miniwob/training/TextGenerate.bend as TG

def name_at(xs: +List<String>, index: Nat) -> String:
  match xs index:
    case Nil{} _: ""
    case Con{value, rest} 0n: value
    case Con{value, rest} 1n+p: name_at(rest, p)

def genre(index: U32) -> String:
  String.to_lower(name_at(["Action", "Adventure", "Comedy", "Crime", "Drama",
    "Fantasy", "Historical", "Horror", "Mystery", "Paranoid", "Political",
    "Romance", "Saga", "Satire", "Surreal", "Thriller", "Urban", "Western"],
    U32.to_nat(index)))

# A bounded training subset of the original LAST_NAMES table; original browser
# instances import any surname from that table through the same state model.
def surname(index: U32) -> String:
  name_at(["Abbott", "Anderson", "Brown", "Davis", "Garcia", "Harris",
    "Jackson", "Johnson", "Jones", "Lee", "Martinez", "Miller", "Moore",
    "Robinson", "Smith", "Taylor", "Thomas", "Thompson", "White", "Wilson"],
    U32.to_nat(index))

def option(index: U32) -> String:
  name_at(["", "5ft 9in", "5ft 10in", "5ft 11in", "6 ft", "6ft 1in",
    "6ft 2in"], U32.to_nat(index))

def button(index: U32) -> String:
  name_at(["", "Yes", "No", "Maybe"], U32.to_nat(index))

def ordinal(index: U32) -> String:
  name_at(["", "1st", "2nd", "3rd"], U32.to_nat(index))

def set_goal(+index: U32, text_value: String, +base: U32, a: Array<U32>) -> Array<U32>:
  +units = TG.units(text_value)
  a[(base + 44 + index : U32)] <- U32.from_nat(List.length(&2, U32, units))
  TG.write_units(units, (base + 1024 + index * 128 : U32), a)

def header(task: U32, seed: U32, deadline: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = G.clear_blocks(64n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 2 : U32)] <- 0
  a[(base + 3 : U32)] <- seed
  a[(base + 8 : U32)] <- 0
  a[(base + 9 : U32)] <- 0
  a[(base + 10 : U32)] <- F32.bits(0.0)
  a[(base + 11 : U32)] <- F32.bits(0.0)
  a[(base + 12 : U32)] <- deadline
  a[(base + 32 : U32)] <- 0
  a[(base + 33 : U32)] <- 0
  a[(base + 34 : U32)] <- 0
  a[(base + 35 : U32)] <- 0
  a[(base + 36 : U32)] <- 0
  a[(base + 40 : U32)] <- 0
  a[(base + 41 : U32)] <- 0
  a[(base + 42 : U32)] <- 0
  a[(base + 44 : U32)] <- 0
  a[(base + 45 : U32)] <- 0
  a[(base + 46 : U32)] <- 0

def radio_case(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +radio = (1 + G.mix((seed + 101 : U32)) % 3 : U32)
  +number = (G.mix((seed + 109 : U32)) % 61 : U32)
  +box = (1 + G.mix((seed + 113 : U32)) % 3 : U32)
  +number_text = G.signed_offset(number)
  +instruction = "Check the " ++ ordinal(radio) ++ " radio button and enter the number \"" ++
    number_text ++ "\" into the " ++ ordinal(box) ++ " textbox."
  a = header(1, seed, 10000, base, a)
  a[(base + 34 : U32)] <- radio
  a[(base + 35 : U32)] <- box
  a = set_goal((box - 1 : U32), number_text, base, a)
  G.write_text(instruction, (base + 128 : U32), a)

def dropdown_case(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +wanted = (1 + G.mix((seed + 127 : U32)) % 5 : U32)
  +press = (1 + G.mix((seed + 131 : U32)) % 3 : U32)
  +instruction = "Choose " ++ option(wanted) ++
    " from the dropdown, then click the button labeled \"" ++ button(press) ++ "\"."
  a = header(2, seed, 10000, base, a)
  a[(base + 34 : U32)] <- wanted
  a[(base + 35 : U32)] <- press
  G.write_text(instruction, (base + 128 : U32), a)

def movie_case(+task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +g = genre((G.mix((seed + 137 : U32)) % 18 : U32))
  +d = surname((G.mix((seed + 139 : U32)) % 20 : U32))
  +year = U32.show((1970 + G.mix((seed + 149 : U32)) % 48 : U32))
  +layout = Bool.pick(U32, U32.is_eq(task, 4), 2,
    (G.mix((seed + 151 : U32)) % 5 : U32))
  +order = (G.mix((seed + 157 : U32)) % 6 : U32)
  +instruction = "Search for " ++ g ++ " movies directed by " ++ d ++
    " from year " ++ year ++ "."
  a = header(task, seed, 20000, base, a)
  a[(base + 36 : U32)] <- layout
  a[(base + 37 : U32)] <- order
  a = set_goal(0, g, base, a)
  a = set_goal(1, d, base, a)
  a = set_goal(2, year, base, a)
  G.write_text(instruction, (base + 128 : U32), a)

def generate_kind(kind: Nat, seed: U32, base: U32, a: Array<U32>) -> Array<U32>:
  match kind:
    case 1n: radio_case(seed, base, a)
    case 2n: dropdown_case(seed, base, a)
    case 3n: movie_case(3, seed, base, a)
    case _: movie_case(4, seed, base, a)

def generate(task: U32, seed: U32, base: U32, a: Array<U32>) -> Array<U32>:
  generate_kind(U32.to_nat(task), seed, base, a)
