import Base

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

def blocks(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      blocks(p, base, clear(128n,
        (base + U32.from_nat(p) * 128 : U32), a))

def text(s: String, +at: U32, a: Array<U32>) -> Array<U32>:
  match s:
    case SNil{}: a
    case SCon{ch, rest}: text(rest, (at + 1 : U32), a[at] <- Char.to_u32(ch))

def two(+number: U32) -> String:
  Bool.pick(String, U32.is_lt(number, 10),
    "0" ++ U32.show(number), U32.show(number))

def money(+cents: U32) -> String:
  "$" ++ U32.show((cents / 100 : U32)) ++ "." ++
    two((cents % 100 : U32))

def name_at(xs: +List<String>, index: Nat) -> String:
  match xs index:
    case Nil{} _: "ABC"
    case Con{value, rest} 0n: value
    case Con{value, rest} 1n+p: name_at(rest, p)

def symbol(seed: U32) -> String:
  name_at(["ABC", "XYZ", "QRS", "LMN", "PQR", "TUV",
    "HJK", "BDF", "RMC", "WSP", "NKT", "GVA"],
    U32.to_nat((mix((seed + 9029 : U32)) % 12 : U32)))

def next_price(+current: U32, +seed: U32, index: U32) -> U32:
  +draw = (mix((seed + index * 157 + 4201 : U32)) % 31 : U32)
  +candidate = (current + draw * 10 : U32)
  Bool.pick(U32, U32.is_le(candidate, 150), 1,
    (candidate - 150 : U32))

def nth(n: Nat, current: U32, +seed: U32, +index: U32) -> U32:
  match n:
    case 0n: current
    case 1n+p:
      nth(p, next_price(current, seed, index), seed,
        (index + 1 : U32))

def write_points(n: Nat, +current: U32, +seed: U32,
    +index: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+p:
      a = a[(base + 64 + index : U32)] <- current
      write_points(p, next_price(current, seed, index), seed,
        (index + 1 : U32), base, a)

def generate(task: U32, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +start = ((40 + (mix((seed + 19 : U32)) % 20 : U32)) * 100 : U32)
  +threshold = nth(75n, start, seed, 0)
  +stock_name = symbol(seed)
  a = blocks(64n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 10000
  a[(base + 34 : U32)] <- threshold
  a[(base + 35 : U32)] <- 100
  a = write_points(100n, start, seed, 0, base, a)
  a = text(stock_name, (base + 1000 : U32), a)
  text("Buy " ++ stock_name ++ " stock when the price is less than " ++
    money(threshold) ++ ".", (base + 512 : U32), a)
