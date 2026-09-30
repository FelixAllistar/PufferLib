import Base
import ./Generate.bend as G

def person(index: Nat) -> String:
  match index:
    case 0n: "Aaren"
    case 1n: "Aarika"
    case 2n: "Abagael"
    case 3n: "Abagail"
    case 4n: "Abbe"
    case 5n: "Abbey"
    case 6n: "Abbi"
    case 7n: "Abbie"
    case 8n: "Abby"
    case 9n: "Abbye"
    case 10n: "Abigael"
    case _: "Abigail"

def label(seed: U32, index: U32) -> String:
  person(U32.to_nat(((seed + index * 7 : U32) % 12 : U32)))

def ordinal(+index: Nat) -> String:
  match index:
    case 1n: "1st"
    case 2n: "2nd"
    case 3n: "3rd"
    case _: U32.show(U32.from_nat(index)) ++ "th"

def grid_position(index: Nat) -> String:
  match index:
    case 1n: "top left"
    case 2n: "top center"
    case 3n: "top right"
    case 4n: "center left"
    case 5n: "center"
    case 6n: "center right"
    case 7n: "bottom left"
    case 8n: "bottom center"
    case _: "bottom right"

def query(task: U32, +item: String, +goal: U32) -> String:
  Bool.pick(String, U32.is_eq(task, 3),
    "Drag " ++ item ++ " to the " ++ ordinal(U32.to_nat(goal)) ++
      " position.",
    "Drag " ++ item ++ " to the " ++
      grid_position(U32.to_nat(goal)) ++ ".")

def write_items(n: Nat, +seed: U32, +base: U32,
                a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +i = U32.from_nat(p)
      a[(base + 37 + i : U32)] <- (i + 1 : U32)
      a = G.text(label(seed, (i + 1 : U32)),
        (base + 384 + i * 64 : U32), a)
      write_items(p, seed, base, a)

def write_numbers(n: Nat, +seed: U32, +base: U32,
                  a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +i = U32.from_nat(p)
      a[(base + 37 + i : U32)] <- (i + 1 : U32)
      a[(base + 64 + i : U32)] <-
        (G.next((seed + i * 1009 + 31 : U32)) % 200 : U32)
      write_numbers(p, seed, base, a)

def count(task: U32) -> U32:
  Bool.pick(U32, U32.is_eq(task, 3), 5, 9)

def items(+task: U32, +seed: U32, +base: U32,
          a: Array<U32>) -> Array<U32>:
  a = G.clear_blocks(8n, base, a)
  +n = count(task)
  +ix = ((G.next(seed) % n : U32) + 1 : U32)
  +goal = ((((ix - 1 : U32) + 1 + (G.next((seed + 19 : U32)) % (n - 1 : U32) : U32)) % n : U32) + 1 : U32)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 10000
  a[(base + 32 : U32)] <- n
  a[(base + 35 : U32)] <- ix
  a[(base + 36 : U32)] <- goal
  a = write_items(U32.to_nat(n), seed, base, a)
  G.text(query(task, label(seed, ix), goal), (base + 256 : U32), a)

def numbers(+task: U32, +seed: U32, +base: U32,
            a: Array<U32>) -> Array<U32>:
  a = G.clear_blocks(8n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 10000
  a[(base + 32 : U32)] <- 4
  a = write_numbers(4n, seed, base, a)
  G.text("Sort the numbers in increasing order, starting with the lowest number at the top of the list.",
    (base + 256 : U32), a)
