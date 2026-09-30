import Base
import ./Generate.bend as G

def word(+seed: U32, +index: U32) -> String:
  +id = ((seed % 8 + index) % 8 : U32)
  Bool.pick(String, U32.is_eq(index, 0),
    G.upper(U32.to_nat(id)), G.lower(U32.to_nat(id)))

def words(n: Nat, +seed: U32, +count: U32) -> String:
  match n:
    case 0n: ""
    case 1n+ +p:
      +index = U32.from_nat(p)
      words(p, seed, count) ++ word(seed, index) ++
      Bool.pick(String, U32.is_eq((index + 1 : U32), count), ".", " ")

def offset(n: Nat, +seed: U32) -> U32:
  match n:
    case 0n: 0
    case 1n+ +p:
      (offset(p, seed) + G.length(word(seed, U32.from_nat(p))) + 1 : U32)

def chosen_word(+seed: U32, +index: U32, count: U32) -> String:
  word(seed, index) ++
  Bool.pick(String, U32.is_eq((index + 1 : U32), count), ".", "")

def style_name(i: Nat) -> String:
  match i:
    case 0n: "bold"
    case 1n: "italics"
    case _: "underlined"

def color_name(i: Nat) -> String:
  match i:
    case 0n: "red"
    case 1n: "orange"
    case 2n: "yellow"
    case 3n: "green"
    case 4n: "blue"
    case _: "purple"

def query_kind(+style: Nat, single: Nat,
    wanted: String, +color: U32) -> String:
  match style single:
    case 3n 1n:
      "Using the text editor, give the text " ++ wanted ++
      " the color " ++ color_name(U32.to_nat(color)) ++ "."
    case 3n _:
      "Using the text editor, make everything the color " ++
      color_name(U32.to_nat(color)) ++ " and press Submit."
    case _ 1n:
      "Using the text editor, give the text " ++ wanted ++
      " the style " ++ style_name(style) ++ " and press Submit."
    case _ _:
      "Using the text editor, give everything the style " ++
      style_name(style) ++ " and press Submit."

def generate(task: U32, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +count = (3 + G.mix((seed + 101 : U32)) % 2 : U32)
  +choice = (G.mix((seed + 701 : U32)) % count : U32)
  +style = (G.mix((seed + 307 : U32)) % 4 : U32)
  +color = (G.mix((seed + 401 : U32)) % 6 : U32)
  +single = (G.mix((seed + 509 : U32)) % 2 : U32)
  +body = words(U32.to_nat(count), seed, count)
  +wanted = chosen_word(seed, choice, count)
  +start = offset(U32.to_nat(choice), seed)
  +end_ = (start + G.length(wanted) : U32)
  +size = G.length(body)
  a = G.blocks(64n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 15000
  a[(base + 32 : U32)] <- Bool.pick(U32, U32.is_eq(single, 1), start, 0)
  a[(base + 33 : U32)] <- Bool.pick(U32, U32.is_eq(single, 1), end_, size)
  a[(base + 34 : U32)] <- style
  a[(base + 35 : U32)] <- color
  a[(base + 36 : U32)] <- single
  a[(base + 37 : U32)] <- size
  a[(base + 40 : U32)] <- count
  a[(base + 41 : U32)] <- choice
  a = G.text(body, (base + 512 : U32), a)
  G.text(query_kind(U32.to_nat(style), U32.to_nat(single),
    wanted, color), (base + 256 : U32), a)
