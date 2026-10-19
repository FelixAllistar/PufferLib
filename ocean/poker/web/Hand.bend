import Base
import ./Controller.bend as C

def singular(rank: Nat) -> String:
  match rank:
    case 2n: "two"
    case 3n: "three"
    case 4n: "four"
    case 5n: "five"
    case 6n: "six"
    case 7n: "seven"
    case 8n: "eight"
    case 9n: "nine"
    case 10n: "ten"
    case 11n: "jack"
    case 12n: "queen"
    case 13n: "king"
    case 14n: "ace"
    case _: "unknown"

def plural(rank: Nat) -> String:
  match rank:
    case 2n: "twos"
    case 3n: "threes"
    case 4n: "fours"
    case 5n: "fives"
    case 6n: "sixes"
    case 7n: "sevens"
    case 8n: "eights"
    case 9n: "nines"
    case 10n: "tens"
    case 11n: "jacks"
    case 12n: "queens"
    case 13n: "kings"
    case 14n: "aces"
    case _: "unknown"

def describe(kind: Nat, first: Nat, second: Nat) -> String:
  match kind:
    case 0n: "High card, " ++ singular(first)
    case 1n: "One pair, " ++ plural(first)
    case 2n: "Two pair, " ++ plural(first) ++ " and " ++ plural(second)
    case 3n: "Three of a kind, " ++ plural(first)
    case 4n: "Straight, " ++ singular(first) ++ " high"
    case 5n: "Flush, " ++ singular(first) ++ " high"
    case 6n: "Full house, " ++ plural(first) ++ " full of " ++ plural(second)
    case 7n: "Four of a kind, " ++ plural(first)
    case _: "Straight flush, " ++ singular(first) ++ " high"

def name(+rank: U32) -> String:
  describe(U32.to_nat((rank / 16777216 : U32)),
    U32.to_nat((rank / 65536 % 16 : U32)), U32.to_nat((rank / 4096 % 16 : U32)))

def texture(suited: Bool) -> String:
  match suited:
    case True{}: " suited"
    case False{}: " offsuit"

def starting(pair: Bool, first: Nat, second: Nat, suited: Bool) -> String:
  match pair:
    case True{}: "Pocket " ++ plural(first)
    case False{}: singular(first) ++ "–" ++ singular(second) ++ texture(suited)

def holding_ranks(+first: U32, +second: U32, suited: Bool) -> String:
  starting(U32.is_eq(first, second), U32.to_nat(C.maximum(first, second)),
    U32.to_nat(C.minimum(first, second)), suited)

def holding(+first: U32, +second: U32) -> String:
  holding_ranks((first / 4 + 2 : U32), (second / 4 + 2 : U32),
    U32.is_eq((first % 4 : U32), (second % 4 : U32)))
