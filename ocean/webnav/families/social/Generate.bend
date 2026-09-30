import Base

# Bounded source-shaped generator. Original randi upper bounds are exclusive.
def mix(+x: U32) -> U32:
  +a = (x .^. (x >> 16n) : U32)
  +b = (a * 2146121005 : U32)
  +c = (b .^. (b >> 15n) : U32)
  +d = (c * 2221713035 : U32)
  (d .^. (d >> 16n) : U32)

def count(task: U32, +seed: U32) -> U32:
  Bool.pick(U32, U32.is_eq(task, 0),
    (5 + mix((seed + 13 : U32)) % 5 : U32),
    (6 + mix((seed + 13 : U32)) % 6 : U32))

def target(+seed: U32, count_posts: U32) -> U32:
  (1 + mix((seed + 113 : U32)) % count_posts : U32)

def expected(task: U32, seed: U32) -> U32:
  (mix((seed + 811 : U32)) % Bool.pick(U32, U32.is_eq(task, 0), 9, 4) : U32)

def deadline(task: U32) -> U32:
  Bool.pick(U32, U32.is_eq(task, 0), 15000, 20000)

# Deterministic interleaving of duplicate-name pairs; browser imports use the
# original shuffle, while this CPU generator stays a bounded subset.
def identity(task: U32, +index: U32, +count_posts: U32) -> U32:
  Bool.pick(U32, U32.is_eq(task, 0), index,
    (((index % 2) * ((count_posts + 1 : U32) / 2) + index / 2) / 2 : U32))

def count_matches(n: Nat, +task: U32, +count_posts: U32,
    +wanted: U32, acc: U32) -> U32:
  match n:
    case 0n: acc
    case 1n+ +p:
      count_matches(p, task, count_posts, wanted,
        (acc + Bool.to_u32(U32.is_eq(identity(task, U32.from_nat(p), count_posts), wanted)) : U32))

def amount(task: U32, +seed: U32, +same_count: U32) -> U32:
  Bool.pick(U32, U32.is_eq(task, 2),
    (1 + mix((seed + 541 : U32)) % same_count : U32), same_count)

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

def person(i: Nat) -> String:
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
    case _: "Vanda"

def username(i: Nat) -> String:
  match i:
    case 0n: "@kenda"
    case 1n: "@karrie"
    case 2n: "@joye"
    case 3n: "@cheree"
    case 4n: "@deneen"
    case 5n: "@tula"
    case 6n: "@briana"
    case 7n: "@beaulah"
    case 8n: "@vina"
    case _: "@vanda"

def action_name(i: Nat) -> String:
  match i:
    case 0n: "Reply"
    case 1n: "Retweet"
    case 2n: "Like"
    case 3n: "Share via DM"
    case 4n: "Copy link to Tweet"
    case 5n: "Embed Tweet"
    case 6n: "Mute"
    case 7n: "Block"
    case _: "Report"

def action_label(task: U32, +code: U32) -> String:
  Bool.pick(String, U32.is_ne(task, 0) && U32.is_eq(code, 3),
    "Share", action_name(U32.to_nat(code)))

def post(+task: U32, +count_posts: U32, +index: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  +at = (base + 64 + index * 256 : U32)
  +id = identity(task, index, count_posts)
  a = text(person(U32.to_nat(id)), at, a)
  a = text(username(U32.to_nat(id)), (at + 64 : U32), a)
  a = text("Lorem ipsum dolor " ++ U32.show(index) ++ ".", (at + 128 : U32), a)
  text(Bool.pick(String, U32.is_eq(task, 0),
    U32.show((index + 1 : U32)) ++ "h ago", ""), (at + 224 : U32), a)

def posts(n: Nat, +task: U32, +count_posts: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      posts(p, task, count_posts, base,
        post(task, count_posts, U32.from_nat(p), base, a))

def query_index(task: Nat, +who: String, +action: String,
    +amount_posts: U32) -> String:
  match task:
    case 0n: "For the user " ++ who ++ ", click on the \"" ++ action ++ "\" button."
    case 1n: "Click the \"" ++ action ++ "\" button on all posts by " ++ who ++ " and then click Submit."
    case _: "Click the \"" ++ action ++ "\" button on " ++ U32.show(amount_posts) ++
      Bool.pick(String, U32.is_eq(amount_posts, 1), " post by ", " posts by ") ++
      who ++ " and then click Submit."

def query(task: U32, who: String, action: String, amount_posts: U32) -> String:
  query_index(U32.to_nat(task), who, action, amount_posts)

def generate(+task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +total = count(task, seed)
  +chosen = target(seed, total)
  +goal = expected(task, seed)
  +wanted = identity(task, (chosen - 1 : U32), total)
  +matches = count_matches(U32.to_nat(total), task, total, wanted, 0)
  +required = amount(task, seed, matches)
  a = blocks(64n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- deadline(task)
  a[(base + 32 : U32)] <- total
  a[(base + 34 : U32)] <- chosen
  a[(base + 35 : U32)] <- goal
  a[(base + 36 : U32)] <- required
  a = posts(U32.to_nat(total), task, total, base, a)
  text(query(task, username(U32.to_nat(wanted)),
    action_label(task, goal), required), (base + 3000 : U32), a)
