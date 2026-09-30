import Base

def mix(+x: U32) -> U32:
  +a = (x .^. (x >> 16n) : U32)
  +b = (a * 2146121005 : U32)
  +c = (b .^. (b >> 15n) : U32)
  +d = (c * 2221713035 : U32)
  (d .^. (d >> 16n) : U32)

def clear(n: Nat, +at: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: clear(p, at, a[(at + U32.from_nat(p) : U32)] <- 0)

def blocks(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: blocks(p, base, clear(128n, (base + U32.from_nat(p) * 128 : U32), a))

def text(s: String, +at: U32, a: Array<U32>) -> Array<U32>:
  match s:
    case SNil{}: a
    case SCon{ch, rest}: text(rest, (at + 1 : U32), a[at] <- Char.to_u32(ch))

def length(s: String) -> U32:
  match s:
    case SNil{}: 0
    case SCon{ch, rest}: (1 + length(rest) : U32)

def person(i: Nat) -> String:
  match i:
    case 0n: "Aaren"
    case 1n: "Aarika"
    case 2n: "Abagael"
    case 3n: "Abagail"
    case 4n: "Abbe"
    case 5n: "Abbey"
    case 6n: "Abbi"
    case _: "Abbie"

def handle(i: Nat) -> String:
  match i:
    case 0n: "aaren"
    case 1n: "aarika"
    case 2n: "abagael"
    case 3n: "abagail"
    case 4n: "abbe"
    case 5n: "abbey"
    case 6n: "abbi"
    case _: "abbie"

def street(i: Nat) -> String:
  match i:
    case 0n: "Smith Street"
    case 1n: "Jones Road"
    case 2n: "Brown Avenue"
    case 3n: "Davis Lane"
    case 4n: "Miller Court"
    case 5n: "Wilson Drive"
    case 6n: "Moore Place"
    case _: "Taylor Terrace"

def pad3(+n: U32) -> String:
  Bool.pick(String, U32.is_lt(n, 10), "00" ++ U32.show(n),
    Bool.pick(String, U32.is_lt(n, 100), "0" ++ U32.show(n), U32.show(n)))

def pad4(+n: U32) -> String:
  Bool.pick(String, U32.is_lt(n, 10), "000" ++ U32.show(n),
    Bool.pick(String, U32.is_lt(n, 100), "00" ++ U32.show(n),
      Bool.pick(String, U32.is_lt(n, 1000), "0" ++ U32.show(n), U32.show(n))))

def name_id(+seed: U32, index: U32) -> U32:
  ((mix((seed + 19 : U32)) % 8 + index) % 8 : U32)

def phone(+seed: U32, +index: U32) -> String:
  U32.show((110 + mix((seed + index * 101 + 137 : U32)) % 880 : U32)) ++ "-" ++
  pad3((mix((seed + index * 103 + 139 : U32)) % 1000 : U32)) ++ "-" ++
  pad4((mix((seed + index * 107 + 149 : U32)) % 10000 : U32))

def email(+seed: U32, +index: U32, id: U32) -> String:
  handle(U32.to_nat(id)) ++ U32.show((mix((seed + index * 109 + 157 : U32)) % 100 : U32)) ++
  Bool.pick(String, U32.is_eq((index % 2 : U32), 0), "@gmail.com", "@yahoo.net")

def address(+seed: U32, +index: U32, id: U32) -> String:
  U32.show((1 + mix((seed + index * 113 + 163 : U32)) % 9999 : U32)) ++
  " " ++ street(U32.to_nat(id))

def write_contact(+seed: U32, +index: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +at = (base + 64 + index * 512 : U32)
  +id = name_id(seed, index)
  a = text(person(U32.to_nat(id)), at, a)
  a = text(phone(seed, index), (at + 64 : U32), a)
  a = text(email(seed, index, id), (at + 128 : U32), a)
  text(address(seed, index, id), (at + 256 : U32), a)

def contacts(n: Nat, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      contacts(p, seed, base, write_contact(seed, U32.from_nat(p), base, a))

def instruction_index(property: Nat, +who: String) -> String:
  match property:
    case 0n: "Find " ++ who ++ " in the contact book and click on their phone number."
    case 1n: "Find " ++ who ++ " in the contact book and click on their email."
    case _: "Find " ++ who ++ " in the contact book and click on their address."

def instruction(property: U32, who: String) -> String:
  instruction_index(U32.to_nat(property), who)

def generate(task: U32, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +target = (1 + mix((seed + 701 : U32)) % 5 : U32)
  +property = (mix((seed + 1301 : U32)) % 3 : U32)
  +id = name_id(seed, (target - 1 : U32))
  a = blocks(64n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 15000
  a[(base + 32 : U32)] <- 1
  a[(base + 33 : U32)] <- target
  a[(base + 34 : U32)] <- property
  a[(base + 35 : U32)] <- 5
  a = contacts(5n, seed, base, a)
  text(instruction(property, person(U32.to_nat(id))), (base + 3000 : U32), a)
