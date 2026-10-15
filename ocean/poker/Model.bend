import Base

# Independent specification of betting rights and layered pot settlement.
# Integer chips; no rake; odd chips go clockwise from the button.
type Phase is Data:
  Playing{}
  Finished{}

type Chips is Data:
  C{stack: U32, committed: U32}

def choose(flag: Bool, yes: U32, no: U32) -> U32:
  match flag:
    case True{}: yes
    case False{}: no

def minimum(+a: U32, +b: U32) -> U32:
  choose(U32.is_le(a, b), a, b)

def payment(stack: U32, price: U32) -> U32:
  minimum(stack, price)

def move(+amount: U32, chips: Chips) -> Chips:
  C{stack, committed} = chips
  C{(stack - amount : U32), (committed + amount : U32)}

def step(phase: Phase, amount: U32, chips: Chips) -> Chips:
  match phase:
    case Playing{}: move(amount, chips)
    case Finished{}: chips

def raise_right(current: U32, reopen_at: U32) -> Bool:
  U32.is_ge(current, reopen_at)

def next_minimum(full_raise: Bool, increment: U32, previous: U32) -> U32:
  choose(full_raise, increment, previous)

def layer(+contribution: U32, +lower: U32, upper: U32) -> U32:
  choose(U32.is_le(contribution, lower), 0,
         (minimum(contribution, upper) - lower : U32))

def pot(contributions: +List<U32>, +lower: U32, +upper: U32) -> U32:
  match contributions:
    case Nil{}: 0
    case Con{c, rest}: (layer(c, lower, upper) + pot(rest, lower, upper) : U32)

def share(+amount: U32, +winners: U32, clockwise_index: U32) -> U32:
  ((amount / winners) + choose(U32.is_lt(clockwise_index,
      (amount % winners : U32)), 1, 0) : U32)

def visible(revealed: Bool, card: U32) -> U32:
  choose(revealed, card, 52)

# Natural-number model makes chip conservation independent of machine overflow.
type Wallet is Data:
  W{stack: Nat, committed: Nat}

def balance(wallet: Wallet) -> Nat:
  W{stack, committed} = wallet
  Nat.add(stack, committed)

def transfer(amount: Nat, stack: Nat, committed: Nat) -> Wallet:
  match amount stack:
    case 0n s: W{s, committed}
    case 1n+a 0n: W{0n, committed}
    case 1n+a 1n+s: transfer(a, s, 1n+committed)
