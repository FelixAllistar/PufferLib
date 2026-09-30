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

def signed_text(+n: U32) -> String:
  +negative = U32.is_eq((n >> 31n : U32), 1)
  +magnitude = Bool.pick(U32, negative, (0 - n : U32), n)
  Bool.pick(String, negative, "-" ++ U32.show(magnitude), U32.show(magnitude))

def query(+goal: U32) -> String:
  +x = ((goal / 5 : U32) - 2 : U32)
  +y = (2 - (goal % 5 : U32) : U32)
  "Click on the grid coordinate (" ++ signed_text(x) ++ "," ++
    signed_text(y) ++ ")."

def grid(+seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +goal = (mix((seed + 7193 : U32)) % 25 : U32)
  a[(base + 32 : U32)] <- goal
  a[(base + 33 : U32)] <- 25
  text(query(goal), (base + 512 : U32), a)

def geometry_fields(+ax: U32, ay: U32, bx: U32, by: U32,
    cx: U32, cy: U32, radius: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  a[(base + 40 : U32)] <- ax
  a[(base + 41 : U32)] <- ay
  a[(base + 42 : U32)] <- bx
  a[(base + 43 : U32)] <- by
  a[(base + 44 : U32)] <- cx
  a[(base + 45 : U32)] <- cy
  a[(base + 46 : U32)] <- radius

def center(+seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +radius = (25 + (mix((seed + 1001 : U32)) % 15 : U32) : U32)
  +span = (125 - radius * 2 : U32)
  +cx = (5 + radius + (mix((seed + 1003 : U32)) % span : U32) : U32)
  +cy = (5 + radius + (mix((seed + 1007 : U32)) % span : U32) : U32)
  a = geometry_fields(cx, cy, 0, 0, 0, 0, radius, base, a)
  text("Find and click on the center of the circle, then press submit.",
    (base + 512 : U32), a)

def midpoint(+seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +ax = (5 + (mix((seed + 2001 : U32)) % 130 : U32) : U32)
  +ay = (10 + (mix((seed + 2003 : U32)) % 110 : U32) : U32)
  +bx = Bool.pick(U32, U32.is_lt(ax, 70),
    (ax + 40 : U32), (ax - 40 : U32))
  +by = (10 + (mix((seed + 2007 : U32)) % 115 : U32) : U32)
  a = geometry_fields(ax, ay, bx, by, 0, 0, 0, base, a)
  text("Find and click on the shortest mid-point between the two points, then press submit.",
    (base + 512 : U32), a)

def right(+seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +ax = (20 + (mix((seed + 3001 : U32)) % 20 : U32) : U32)
  +ay = (25 + (mix((seed + 3003 : U32)) % 30 : U32) : U32)
  +bx = (80 + (mix((seed + 3007 : U32)) % 20 : U32) : U32)
  +by = (50 + (mix((seed + 3011 : U32)) % 30 : U32) : U32)
  a = geometry_fields(ax, ay, bx, by, 0, 0, 0, base, a)
  text("Add a third point to create a right angle, then press submit.",
    (base + 512 : U32), a)

def bisect(+seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +vx = (65 + (mix((seed + 4001 : U32)) % 10 : U32) : U32)
  +vy = (70 + (mix((seed + 4003 : U32)) % 20 : U32) : U32)
  +ax = (vx - 45 - (mix((seed + 4007 : U32)) % 10 : U32) : U32)
  +ay = (vy - 15 - (mix((seed + 4011 : U32)) % 10 : U32) : U32)
  +bx = (vx + 45 + (mix((seed + 4013 : U32)) % 10 : U32) : U32)
  +by = (vy - 15 - (mix((seed + 4019 : U32)) % 10 : U32) : U32)
  a = geometry_fields(ax, ay, bx, by, vx, vy, 0, base, a)
  text("Create a line that bisects the angle evenly in two, then press submit.",
    (base + 512 : U32), a)

def task_index(task: Nat, seed: U32, base: U32,
    a: Array<U32>) -> Array<U32>:
  match task:
    case 0n: bisect(seed, base, a)
    case 1n: center(seed, base, a)
    case 2n: midpoint(seed, base, a)
    case 3n: grid(seed, base, a)
    case _: right(seed, base, a)

def generate(+task: U32, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  a = blocks(16n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 10000
  task_index(U32.to_nat(task), seed, base, a)
