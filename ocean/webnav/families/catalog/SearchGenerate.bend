import Base
import ./Generate.bend as G

def label(i: Nat) -> String:
  match i:
    case 0n: "Kenda"
    case 1n: "Karrie"
    case 2n: "Joye"
    case 3n: "Cheree"
    case 4n: "Deneen"
    case 5n: "Tula"
    case 6n: "Briana"
    case 7n: "Beaulah"
    case 8n: "Vina"
    case 9n: "Vanda"
    case 10n: "Cierra"
    case 11n: "Leonie"
    case 12n: "Bernardine"
    case 13n: "Keli"
    case 14n: "Renda"
    case _: "Marcella"

def suffix(i: Nat) -> String:
  match i:
    case 0n: "st"
    case 1n: "nd"
    case 2n: "rd"
    case _: "th"

def ordinal(+index: U32) -> String:
  U32.show((index + 1 : U32)) ++ suffix(U32.to_nat(index))

def title(+seed: U32, +index: U32) -> String:
  label(U32.to_nat((G.mix((seed + index * 137 + 17 : U32)) % 16 : U32)))

def target_name(seed: U32) -> String:
  label(U32.to_nat((G.mix((seed + 1733 : U32)) % 16 : U32)))

def write_result(+seed: U32, +index: U32, +target: U32,
    +wanted: String, +base: U32, a: Array<U32>) -> Array<U32>:
  +at = (base + 64 + index * 256 : U32)
  a = G.text(Bool.pick(String, U32.is_eq(index, target), wanted,
    title(seed, index)), at, a)
  a = G.text("https://www." ++ U32.show((G.mix((seed + index * 139 + 19 : U32)) % 1000 : U32)) ++
    ".example.org", (at + 64 : U32), a)
  G.text("Lorem ipsum search result " ++ U32.show(index) ++ ".",
    (at + 160 : U32), a)

def results(n: Nat, +seed: U32, +target: U32,
    +wanted: String, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      results(p, seed, target, wanted, base,
        write_result(seed, U32.from_nat(p), target, wanted, base, a))

def fake(+index: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +at = (base + 4000 + index * 256 : U32)
  a = G.text(label(U32.to_nat((index + 5 : U32))), at, a)
  a = G.text("https://www.example.com", (at + 64 : U32), a)
  G.text("Unrelated search result.", (at + 160 : U32), a)

def fakes(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: fakes(p, base, fake(U32.from_nat(p), base, a))

def generate(task: U32, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +target = (G.mix((seed + 701 : U32)) % 9 : U32)
  +wanted = target_name(seed)
  a = G.blocks(64n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 20000
  a[(base + 33 : U32)] <- target
  a[(base + 34 : U32)] <- G.length(wanted)
  a = G.text(wanted, (base + 2700 : U32), a)
  a = results(9n, seed, target, wanted, base, a)
  a = fakes(3n, base, a)
  G.text("Use the textbox to enter \"" ++ wanted ++
    "\" and press \"Search\", then find and click the " ++
    ordinal(target) ++ " search result.",
    (base + 3000 : U32), a)
