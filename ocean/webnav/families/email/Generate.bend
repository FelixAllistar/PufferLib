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

def clear_blocks(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: clear_blocks(p, base, clear(128n, (base + U32.from_nat(p) * 128 : U32), a))

def length(s: String) -> U32:
  match s:
    case SNil{}: 0
    case SCon{ch, rest}: (1 + length(rest) : U32)

def text(s: String, +at: U32, a: Array<U32>) -> Array<U32>:
  match s:
    case SNil{}: a
    case SCon{ch, rest}: text(rest, (at + 1 : U32), a[at] <- Char.to_u32(ch))

def name_index(i: Nat) -> String:
  match i:
    case 0n: "Aaren"
    case 1n: "Aarika"
    case 2n: "Abagael"
    case 3n: "Abbe"
    case 4n: "Abbey"
    case 5n: "Abbi"
    case 6n: "Abbie"
    case 7n: "Abby"
    case 8n: "Ada"
    case 9n: "Adah"
    case 10n: "Adaline"
    case 11n: "Adan"
    case 12n: "Adara"
    case 13n: "Adda"
    case 14n: "Addi"
    case _: "Addie"

def sender(+seed: U32, +index: U32) -> String:
  name_index(U32.to_nat(((mix((seed + 11 : U32)) % 16 + index * 5) % 16 : U32)))

def word_index(i: Nat) -> String:
  match i:
    case 0n: "lorem"
    case 1n: "ipsum"
    case 2n: "dolor"
    case 3n: "sit"
    case 4n: "amet"
    case 5n: "consectetur"
    case 6n: "adipiscing"
    case 7n: "elit"
    case 8n: "aliquam"
    case 9n: "purus"
    case 10n: "luctus"
    case 11n: "venenatis"
    case 12n: "lectus"
    case 13n: "magna"
    case 14n: "fringilla"
    case _: "urna"

def word(+seed: U32, +salt: U32) -> String:
  word_index(U32.to_nat((mix((seed + salt : U32)) % 16 : U32)))

def subject(+seed: U32, +index: U32) -> String:
  +k = (index * 171 + 201 : U32)
  word(seed, k) ++ " " ++ word(seed, (k + 1 : U32)) ++ "."

def body(+seed: U32, +index: U32) -> String:
  +k = (index * 263 + 401 : U32)
  word(seed, k) ++ " " ++ word(seed, (k + 1 : U32)) ++ " " ++
  word(seed, (k + 2 : U32)) ++ " " ++ word(seed, (k + 3 : U32)) ++ " " ++
  word(seed, (k + 4 : U32)) ++ " " ++ word(seed, (k + 5 : U32)) ++ "."

def reply_text(+seed: U32) -> String:
  word(seed, 707) ++ " " ++ word(seed, 708) ++ "."

def write_email(+seed: U32, +index: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +at = (base + 64 + index * 256 : U32)
  +who = sender(seed, index)
  +subj = subject(seed, index)
  +message = body(seed, index)
  a = a[at] <- length(who)
  a = a[(at + 1 : U32)] <- length(subj)
  a = a[(at + 2 : U32)] <- length(message)
  a = text(who, (at + 8 : U32), a)
  a = text(subj, (at + 40 : U32), a)
  text(message, (at + 80 : U32), a)

def emails(n: Nat, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: emails(p, seed, base, write_email(seed, U32.from_nat(p), base, a))

def is_long(+task: U32) -> Bool:
  U32.is_eq(task, 1) || U32.is_eq(task, 5) || U32.is_eq(task, 9)

def count_for(task: U32, seed: U32) -> U32:
  Bool.pick(U32, is_long(task), (4 + mix((seed + 19 : U32)) % 8 : U32), 3)

def goal_index(task: Nat, +seed: U32) -> U32:
  match task:
    case 0n: 2
    case 1n: 1
    case 2n: 1
    case 3n: 1
    case 4n: 3
    case 5n: (mix((seed + 1701 : U32)) % 4 : U32)
    case 6n: (mix((seed + 1701 : U32)) % 4 : U32)
    case 7n: 0
    case 8n: (mix((seed + 1701 : U32)) % 2 * 3 : U32)
    case _: (mix((seed + 1701 : U32)) % 4 : U32)

def goal_for(task: U32, seed: U32) -> U32:
  goal_index(U32.to_nat(task), seed)

def recipient_index(+task: U32, +seed: U32, +target: U32, count: U32) -> U32:
  +j = (mix((seed + 2311 : U32)) % (count - 1) : U32)
  +other = Bool.pick(U32, U32.is_ge(j, target), (j + 1 : U32), j)
  Bool.pick(U32, U32.is_eq(task, 1) || U32.is_eq(task, 2) || U32.is_eq(task, 5), other,
    (mix((seed + 449 : U32)) % 16 : U32))

def standard_query_index(goal: Nat, +who: String, +to: String, +reply: String) -> String:
  match goal:
    case 0n: "Find the email by " ++ who ++ " and reply to them with the text \"" ++ reply ++ "\"."
    case 1n: "Find the email by " ++ who ++ " and forward that email to " ++ to ++ "."
    case 2n: "Find the email by " ++ who ++ " and click the trash icon to delete it."
    case _: "Find the email by " ++ who ++ " and click the star icon to mark it as important."

def standard_query(goal: U32, who: String, to: String, reply: String) -> String:
  standard_query_index(U32.to_nat(goal), who, to, reply)

def nl_query_index(goal: Nat, +who: String, +to: String, +reply: String) -> String:
  match goal:
    case 0n: "Reply to " ++ who ++ "'s email with \"" ++ reply ++ "\"."
    case 1n: "Forward the email from " ++ who ++ " to " ++ to ++ "."
    case 2n: "Delete the email from " ++ who ++ "."
    case _: "Mark the email from " ++ who ++ " as important."

def nl_query(goal: U32, who: String, to: String, reply: String) -> String:
  nl_query_index(U32.to_nat(goal), who, to, reply)

def query(+task: U32, +goal: U32, +who: String, +to: String, +reply: String) -> String:
  Bool.pick(String, U32.is_eq(task, 1) || U32.is_eq(task, 2) || U32.is_eq(task, 5),
    nl_query(goal, who, to, reply), standard_query(goal, who, to, reply))

def initialize(+task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = clear_blocks(64n, base, a)
  a = a[base] <- 2
  a = a[(base + 1 : U32)] <- task
  a = a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 30000

def generate(+task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +count = count_for(task, seed)
  +target = (mix((seed + 997 : U32)) % count : U32)
  +goal = goal_for(task, seed)
  +recipient = recipient_index(task, seed, target, count)
  +who = sender(seed, target)
  +to = Bool.pick(String, U32.is_eq(task, 1) || U32.is_eq(task, 2) || U32.is_eq(task, 5),
    sender(seed, recipient), name_index(U32.to_nat(recipient)))
  +reply = reply_text(seed)
  +instruction = query(task, goal, who, to, reply)
  a = initialize(task, seed, base, a)
  a = a[(base + 32 : U32)] <- count
  a = a[(base + 40 : U32)] <- goal
  a = a[(base + 41 : U32)] <- (target + 1 : U32)
  a = emails(U32.to_nat(count), seed, base, a)
  a = text(instruction, (base + 3200 : U32), a)
  a = text(reply, (base + 5100 : U32), a)
  text(to, (base + 5300 : U32), a)
