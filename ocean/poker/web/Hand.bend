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

# Public-card evaluator for result presentation. Unknown cards (52) are removed;
# this never reads the worker's private deal or opponents' folded holdings.
def keep_card(yes: Bool, head: U32, rest: +List<U32>) -> +List<U32>:
  match yes:
    case True{}: head <> rest
    case False{}: rest

def visible(cards: +List<U32>) -> +List<U32>:
  match cards:
    case Nil{}: Nil{}
    case Con{+head, rest}: keep_card(U32.is_lt(head, 52), head, visible(rest))

def count_rank(cards: +List<U32>, +rank: U32) -> U32:
  match cards:
    case Nil{}: 0
    case Con{head, rest}:
      (C.choose(U32.is_eq((head / 4 + 2 : U32), rank), 1, 0) + count_rank(rest, rank) : U32)

def count_suit(cards: +List<U32>, +suit: U32) -> U32:
  match cards:
    case Nil{}: 0
    case Con{head, rest}:
      (C.choose(U32.is_eq((head % 4 : U32), suit), 1, 0) + count_suit(rest, suit) : U32)

def without_rank(cards: +List<U32>, +rank: U32) -> +List<U32>:
  match cards:
    case Nil{}: Nil{}
    case Con{+head, rest}:
      keep_card(U32.is_ne((head / 4 + 2 : U32), rank), head, without_rank(rest, rank))

def only_suit(cards: +List<U32>, +suit: U32) -> +List<U32>:
  match cards:
    case Nil{}: Nil{}
    case Con{+head, rest}:
      keep_card(U32.is_eq((head % 4 : U32), suit), head, only_suit(rest, suit))

def highest(n: Nat, +cards: +List<U32>, +copies: U32) -> U32:
  match n:
    case 0n: 0
    case 1n+ +p:
      +rank = (U32.from_nat(p) + 2 : U32)
      C.choose(U32.is_ge(count_rank(cards, rank), copies), rank, highest(p, cards, copies))

def top(n: Nat, +cards: +List<U32>, out: U32) -> U32:
  match n:
    case 0n: out
    case 1n+p:
      +rank = highest(13n, cards, 1)
      top(p, without_rank(cards, rank), (out * 16 + rank : U32))

def flush_code(n: Nat, +cards: +List<U32>) -> U32:
  match n:
    case 0n: 0
    case 1n+ +p:
      +suit = U32.from_nat(p)
      C.choose(U32.is_ge(count_suit(cards, suit), 5), (suit + 1 : U32), flush_code(p, cards))

def has_rank(cards: +List<U32>, rank: U32) -> U32:
  C.choose(U32.is_gt(count_rank(cards, rank), 0), 1, 0)

def run_high(n: Nat, +cards: +List<U32>) -> U32:
  match n:
    case 0n: 0
    case 1n+ +p:
      +rank = (U32.from_nat(p) + 5 : U32)
      hit = (has_rank(cards, rank) * has_rank(cards, (rank - 1 : U32))
        * has_rank(cards, (rank - 2 : U32)) * has_rank(cards, (rank - 3 : U32))
        * has_rank(cards, C.choose(U32.is_eq(rank, 5), 14, (rank - 4 : U32))) : U32)
      C.choose(U32.is_gt(hit, 0), rank, run_high(p, cards))

def public_rank(+cards: +List<U32>) -> U32:
  +four = highest(13n, cards, 4)
  +trip = highest(13n, cards, 3)
  +pair = highest(13n, cards, 2)
  +second = highest(13n, without_rank(cards, pair), 2)
  +full_pair = highest(13n, without_rank(cards, trip), 2)
  +flush = flush_code(4n, cards)
  +flush_cards = only_suit(cards, (flush - 1 : U32))
  +sf = run_high(10n, flush_cards)
  +straight = run_high(10n, cards)
  C.choose(U32.is_gt(sf, 0), (134217728 + sf * 65536 : U32),
    C.choose(U32.is_gt(four, 0), (117440512 + four * 65536 + highest(13n, without_rank(cards, four), 1) * 4096 : U32),
    C.choose(U32.is_gt((trip * full_pair : U32), 0), (100663296 + trip * 65536 + full_pair * 4096 : U32),
    C.choose(U32.is_gt(flush, 0), (83886080 + top(5n, flush_cards, 0) : U32),
    C.choose(U32.is_gt(straight, 0), (67108864 + straight * 65536 : U32),
    C.choose(U32.is_gt(trip, 0), (50331648 + trip * 65536 + top(2n, without_rank(cards, trip), 0) * 256 : U32),
    C.choose(U32.is_gt(second, 0), (33554432 + pair * 65536 + second * 4096 + highest(13n, without_rank(without_rank(cards, pair), second), 1) * 256 : U32),
    C.choose(U32.is_gt(pair, 0), (16777216 + pair * 65536 + top(3n, without_rank(cards, pair), 0) * 16 : U32),
      top(5n, cards, 0)))))))))

def rank7(a: U32, b: U32, c: U32, d: U32, e: U32, f: U32, g: U32) -> U32:
  public_rank(visible([a, b, c, d, e, f, g]))

def kicker_label(kind: Nat, +rank: U32) -> String:
  match kind:
    case 1n: " · " ++ singular(U32.to_nat((rank / 4096 % 16 : U32))) ++ " kicker"
    case 2n: " · " ++ singular(U32.to_nat((rank / 256 % 16 : U32))) ++ " kicker"
    case 3n: " · " ++ singular(U32.to_nat((rank / 4096 % 16 : U32))) ++ " kicker"
    case 7n: " · " ++ singular(U32.to_nat((rank / 4096 % 16 : U32))) ++ " kicker"
    case _: ""

def showdown_name(+rank: U32) -> String:
  name(rank) ++ kicker_label(U32.to_nat((rank / 16777216 : U32)), rank)
