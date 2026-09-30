import Base

# The source pages sample each target independently of its starting control
# value. Separate mix domains preserve that property for deterministic seeds.
def mix(+x: U32) -> U32:
  +a = (x .^. (x >> 16n) : U32)
  +b = (a * 2146121005 : U32)
  +c = (b .^. (b >> 15n) : U32)
  +d = (c * 2221713035 : U32)
  (d .^. (d >> 16n) : U32)

def draw(seed: U32, stream: U32, count: U32) -> U32:
  (mix((seed + stream * 2654435769 : U32)) % count : U32)

def clear(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: clear(p, base, a[(base + U32.from_nat(p) : U32)] <- 0)

def write_text(s: String, +at: U32, a: Array<U32>) -> Array<U32>:
  match s:
    case SNil{}: a
    case SCon{ch, rest}: write_text(rest, (at + 1 : U32), a[at] <- Char.to_u32(ch))

def pick(xs: List<String>, index: Nat) -> String:
  match xs index:
    case Nil{} _: ""
    case Con{x, rest} 0n: x
    case Con{x, rest} 1n+p: pick(rest, p)

def names() -> List<String>:
  ["Aaren", "Aarika", "Abagael", "Abagail", "Abbe", "Abbey", "Abbi", "Abbie",
   "Abby", "Abbye", "Abigael", "Abigail", "Abigale", "Abra", "Ada", "Adah"]

def countries() -> List<String>:
  ["Afghanistan", "Albania", "Algeria", "Andorra", "Angola", "Argentina", "Armenia", "Australia",
   "Austria", "Azerbaijan", "Bahamas", "Bahrain", "Bangladesh", "Barbados", "Belarus", "Belgium"]

def option(+seed: U32, +index: U32, category: U32) -> String:
  +start = draw(seed, 11, 16)
  +stride = (1 + 2 * draw(seed, 12, 8) : U32)
  +at = ((start + index * stride) % 16 : U32)
  Bool.pick(String, U32.is_eq(category, 0), pick(names(), U32.to_nat(at)), pick(countries(), U32.to_nat(at)))

def options(n: Nat, +seed: U32, +category: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +i = U32.from_nat(p)
      options(p, seed, category, base, write_text(option(seed, i, category), (base + 1024 + i * 64 : U32), a))

def named_color(index: Nat) -> String & U32:
  match index:
    case 0n: ("black", 0)
    case 1n: ("white", 16777215)
    case 2n: ("aqua", 65535)
    case 3n: ("blue", 255)
    case 4n: ("gray", 8421504)
    case 5n: ("green", 32768)
    case 6n: ("lime", 65280)
    case 7n: ("maroon", 8388608)
    case 8n: ("navy", 128)
    case 9n: ("olive", 8421376)
    case 10n: ("purple", 8388736)
    case 11n: ("red", 16711680)
    case 12n: ("silver", 12632256)
    case 13n: ("teal", 32896)
    case 14n: ("yellow", 16776960)
    case 15n: ("pink", 16761035)
    case 16n: ("magenta", 16711935)
    case 17n: ("gold", 16766720)
    case _: ("orange", 16753920)

def signed_number(+value: U32) -> String:
  Bool.pick(String, U32.is_lt(value, 100), "-" ++ U32.show((100 - value : U32)), U32.show((value - 100 : U32)))

def slider_min(index: Nat) -> U32:
  match index:
    case 0n: 0
    case 1n: 100
    case 2n: 110
    case _: 200

def slider_span(index: Nat) -> U32:
  match index:
    case 0n: 5
    case 1n: 10
    case 2n: 50
    case _: 100

def header(task: U32, seed: U32, deadline: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = clear(2048n, base, a)
  a = a[base] <- 2
  a = a[(base + 1 : U32)] <- task
  a = a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- deadline

def generate_named(pair: String & U32, task: U32, seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  (name, desired) = pair
  a = header(task, seed, 7000, base, a)
  a = a[(base + 46 : U32)] <- desired
  a = a[(base + 47 : U32)] <- 6
  a = a[(base + 48 : U32)] <- 6
  a = a[(base + 49 : U32)] <- 6
  a = write_text("AB2567", (base + 768 : U32), a)
  write_text("Select " ++ name ++ " with the color picker and hit Submit.", (base + 512 : U32), a)

def generate_task(index: Nat, task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match index:
    case 0n:
      +count = (3 + draw(seed, 1, 7) : U32)
      +category = draw(seed, 2, 2)
      +goal = draw(seed, 3, count)
      a = header(task, seed, 10000, base, a)
      a = a[(base + 32 : U32)] <- count
      a = a[(base + 42 : U32)] <- 0
      a = a[(base + 43 : U32)] <- goal
      a = options(U32.to_nat(count), seed, category, base, a)
      write_text("Select " ++ option(seed, goal, category) ++ " from the list and click Submit.", (base + 512 : U32), a)
    case 1n:
      +low = slider_min(U32.to_nat(draw(seed, 1, 4)))
      +high = (low + slider_span(U32.to_nat(draw(seed, 2, 4))) : U32)
      +initial = (low + draw(seed, 3, (high - low + 1 : U32)) : U32)
      +goal = (low + draw(seed, 4, (high - low + 1 : U32)) : U32)
      a = header(task, seed, 10000, base, a)
      a = a[(base + 33 : U32)] <- low
      a = a[(base + 34 : U32)] <- high
      a = a[(base + 35 : U32)] <- draw(seed, 5, 2)
      a = a[(base + 36 : U32)] <- initial
      a = a[(base + 37 : U32)] <- goal
      write_text("Select " ++ signed_number(goal) ++ " with the slider and hit Submit.", (base + 512 : U32), a)
    case 2n:
      +v1 = draw(seed, 1, 21)
      +v2 = draw(seed, 2, 21)
      +v3 = draw(seed, 3, 21)
      +g1 = draw(seed, 4, 21)
      +g2 = draw(seed, 5, 21)
      +g3 = draw(seed, 6, 21)
      a = header(task, seed, 20000, base, a)
      a = a[(base + 33 : U32)] <- 0
      a = a[(base + 34 : U32)] <- 20
      a = a[(base + 36 : U32)] <- v1
      a = a[(base + 37 : U32)] <- g1
      a = a[(base + 38 : U32)] <- v2
      a = a[(base + 39 : U32)] <- g2
      a = a[(base + 40 : U32)] <- v3
      a = a[(base + 41 : U32)] <- g3
      write_text("Set the sliders to the combination [" ++ U32.show(g1) ++ "," ++ U32.show(g2) ++ "," ++ U32.show(g3) ++ "] and submit.", (base + 512 : U32), a)
    case 3n:
      +goal = (90 + draw(seed, 1, 20) : U32)
      a = header(task, seed, 10000, base, a)
      a = a[(base + 33 : U32)] <- 0
      a = a[(base + 34 : U32)] <- 0
      a = a[(base + 36 : U32)] <- 0
      a = a[(base + 37 : U32)] <- (goal - 100 : U32)
      write_text("Select " ++ signed_number(goal) ++ " with the spinner and hit Submit.", (base + 512 : U32), a)
    case 4n:
      generate_named(named_color(U32.to_nat(draw(seed, 1, 19))), task, seed, base, a)
    case 5n:
      +desired = (draw(seed, 1, 256) * 65536 + draw(seed, 2, 256) * 256 + draw(seed, 3, 256) : U32)
      a = header(task, seed, 7000, base, a)
      a = a[(base + 46 : U32)] <- desired
      a = a[(base + 47 : U32)] <- 6
      a = a[(base + 48 : U32)] <- 6
      a = a[(base + 49 : U32)] <- 6
      a = write_text("AB2567", (base + 768 : U32), a)
      write_text("Select the following color with the color picker and hit Submit.", (base + 512 : U32), a)
    case _: a

def generate(+task: U32, seed: U32, base: U32, a: Array<U32>) -> Array<U32>:
  generate_task(U32.to_nat(task), task, seed, base, a)
