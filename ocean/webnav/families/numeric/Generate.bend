import Base

# Row layout is numeric-family local: 32..44 are task state, 64 onwards are
# 48-word nodes, then the public instruction and action text buffers.
def mix(+x: U32) -> U32:
  +a = (x .^. (x >> 16n) : U32)
  +b = (a * 2146121005 : U32)
  +c = (b .^. (b >> 15n) : U32)
  +d = (c * 2221713035 : U32)
  (d .^. (d >> 16n) : U32)

def clear(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      clear(p, base, a[(base + U32.from_nat(p) : U32)] <- 0)

def put_string(s: String, +at: U32, a: Array<U32>) -> Array<U32>:
  match s:
    case SNil{}: a[at] <- 0
    case SCon{ch, rest}:
      put_string(rest, (at + 1 : U32), a[at] <- Char.to_u32(ch))

def write_node(index: U32, kind: U32, target: Bool, number: U32,
               name: String, x: U32, y: U32, width: U32, height: U32,
               +base: U32, a: Array<U32>) -> Array<U32>:
  +at = (base + 64 + index * 48 : U32)
  a[at] <- kind
  a[(at + 1 : U32)] <- 0
  a[(at + 2 : U32)] <- Bool.to_u32(target)
  a[(at + 3 : U32)] <- number
  a[(at + 20 : U32)] <- 0
  a[(at + 44 : U32)] <- x
  a[(at + 45 : U32)] <- y
  a[(at + 46 : U32)] <- width
  a[(at + 47 : U32)] <- height
  put_string(name, (at + 4 : U32), a)

def node_value(index: U32, value: String, +base: U32, a: Array<U32>) -> Array<U32>:
  +at = (base + 64 + index * 48 + 20 : U32)
  put_string(value, at, a)

def query(s: String, +base: U32, a: Array<U32>) -> Array<U32>:
  put_string(s, (base + 1600 : U32), a)

def signed_text(+n: U32) -> String:
  +negative = U32.is_eq((n >> 31n : U32), 1)
  +magnitude = Bool.pick(U32, negative, (0 - n : U32), n)
  Bool.pick(String, negative, "-" ++ U32.show(magnitude), U32.show(magnitude))

def pattern_mask_index(digit: Nat) -> U32:
  match digit:
    case 0n: 110729622
    case 1n: 71582820
    case 2n: 252856726
    case 3n: 110651542
    case 4n: 71628113
    case 5n: 110653727
    case 6n: 110719382
    case 7n: 35932319
    case 8n: 110717334
    case _: 110684566

def pattern_mask(digit: U32) -> U32:
  pattern_mask_index(U32.to_nat(digit))

def pattern_cell(mask: U32, cell: U32) -> Bool:
  U32.is_eq(((mask >> U32.to_nat(cell) : U32) .&. 1 : U32), 1)

def pattern_row(+mask: U32, +row: U32) -> String:
  +b0 = U32.is_eq(((mask >> U32.to_nat((row * 4 : U32)) : U32) .&. 1 : U32), 1)
  +b1 = U32.is_eq(((mask >> U32.to_nat((row * 4 + 1 : U32)) : U32) .&. 1 : U32), 1)
  +b2 = U32.is_eq(((mask >> U32.to_nat((row * 4 + 2 : U32)) : U32) .&. 1 : U32), 1)
  +b3 = U32.is_eq(((mask >> U32.to_nat((row * 4 + 3 : U32)) : U32) .&. 1 : U32), 1)
  Bool.pick(String, b0, "1", "0") ++ Bool.pick(String, b1, "1", "0") ++ Bool.pick(String, b2, "1", "0") ++ Bool.pick(String, b3, "1", "0")

def pattern_text(+mask: U32, +rows: Nat) -> String:
  match rows:
    case 0n: ""
    case 1n+p:
      +row = U32.from_nat((6n - p : Nat))
      pattern_row(mask, row) ++ Bool.pick(String, Nat.is_eq(p, 0n), "", " / ") ++ pattern_text(mask, p)

def boxes(n: Nat, digit: U32, +mask: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +i = U32.from_nat(p)
      +row = (i / 4 : U32)
      +col = (i % 4 : U32)
      +name = "Row " ++ U32.show((row + 1 : U32)) ++ " col " ++ U32.show((col + 1 : U32))
      +goal = pattern_cell(mask, i)
      boxes(p, digit, mask, base, write_node(i, 1, goal, 0, name, (12 + col * 20 : U32), (12 + row * 14 : U32), 16, 14, base, a))

def odd_nodes(n: Nat, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +i = U32.from_nat(p)
      +number = (mix((seed + i : U32)) % 16 : U32)
      +signed = (number - 5 : U32)
      +odd = U32.is_eq((signed % 2 : U32), 1)
      odd_node = write_node((i * 2 : U32), 0, odd, signed, "Odd " ++ signed_text(signed), 6, (12 + i * 34 : U32), 64, 25, base, a)
      even_node = write_node((i * 2 + 1 : U32), 0, Bool.not(odd), signed, "Even " ++ signed_text(signed), 90, (12 + i * 34 : U32), 64, 25, base, odd_node)
      odd_nodes(p, seed, base, even_node)

def algebra_answer(op: Nat, xfirst: Bool, a: U32, b: U32) -> U32:
  match op xfirst:
    case 0n True{}: (b + a : U32)
    case 0n False{}: (a - b : U32)
    case 1n True{}: (b - a : U32)
    case 1n False{}: (b - a : U32)
    case _ _: 0

def arithmetic_answer(op: Nat, a: U32, b: U32) -> U32:
  match op:
    case 0n: (a - b : U32)
    case 1n: (a + b : U32)
    case _: (a * b : U32)

def math_answer(task: Nat, op: Nat, xfirst: Bool, a: U32, b: U32) -> U32:
  match task:
    case 7n: algebra_answer(op, xfirst, a, b)
    case _: arithmetic_answer(op, a, b)

def operator(op: Nat) -> String:
  match op:
    case 0n: " - "
    case 1n: " + "
    case 2n: " x "
    case _: " ? "

def algebra_equation(xfirst: Bool, symbol: String, a: U32, b: U32) -> String:
  match xfirst:
    case True{}: "x" ++ symbol ++ U32.show(a) ++ " = " ++ U32.show(b)
    case False{}: U32.show(a) ++ symbol ++ "x = " ++ U32.show(b)

def math_problem_text(task: Nat, xfirst: Bool, symbol: String, a: U32, b: U32) -> String:
  match task:
    case 7n: algebra_equation(xfirst, symbol, a, b)
    case _: U32.show(a) ++ symbol ++ U32.show(b) ++ " ="

def math_problem(+task: Nat, +seed: U32) -> U32 & String:
  +a = (mix(seed) % 10 : U32)
  +b = (mix((seed + 1 : U32)) % Bool.pick(U32, Nat.is_eq(task, 7n), 100, 10) : U32)
  +op = (mix((seed + 2 : U32)) % Bool.pick(U32, Nat.is_eq(task, 7n), 2, 3) : U32)
  +xfirst = U32.is_eq((mix((seed + 3 : U32)) .&. 1 : U32), 1)
  +answer = math_answer(task, U32.to_nat(op), xfirst, a, b)
  +symbol = operator(U32.to_nat(op))
  +problem = math_problem_text(task, xfirst, symbol, a, b)
  (answer, problem)

def bound_for_mode(mode: Nat, random: U32) -> U32:
  match mode:
    case 0n: (5 + random % 4 : U32)
    case 1n: (2 + random % 4 : U32)
    case _: 0

def message_for_mode(mode: Nat, bound: U32) -> String:
  match mode:
    case 0n: "Generate a number less than " ++ U32.show(bound) ++ ", then press submit."
    case 1n: "Generate a number greater than " ++ U32.show(bound) ++ ", then press submit."
    case 2n: "Generate an odd number, then press submit."
    case _: "Generate an even number, then press submit."

def generate_number(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +mode = (mix(seed) % 4 : U32)
  +bound = bound_for_mode(U32.to_nat(mode), mix((seed + 1 : U32)))
  +message = message_for_mode(U32.to_nat(mode), bound)
  xs = write_node(0, 0, False{}, 0, "Generate", 25, 75, 72, 30, base, a)
  xs = write_node(1, 0, False{}, 0, "Submit", 105, 75, 72, 30, base, xs)
  xs = write_node(2, 4, False{}, 0, "Generated", 55, 35, 80, 40, base, xs)
  xs = xs[(base + 32 : U32)] <- 0
  xs = xs[(base + 33 : U32)] <- 3
  xs = xs[(base + 37 : U32)] <- bound
  xs = xs[(base + 38 : U32)] <- mode
  xs = xs[(base + 44 : U32)] <- mix((seed + 9 : U32))
  query(message, base, xs)

def guess_number(seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +target = (mix(seed) % 10 : U32)
  xs = write_node(0, 2, False{}, 0, "Guess", 45, 87, 45, 22, base, a)
  xs = write_node(1, 0, False{}, 0, "Submit", 90, 87, 70, 28, base, xs)
  xs = write_node(2, 4, False{}, 0, "Feedback", 20, 45, 150, 24, base, xs)
  xs = xs[(base + 33 : U32)] <- 3
  xs = xs[(base + 36 : U32)] <- target
  query("Guess the number between 0-9 and press Submit. Use the feedback below to find the right number.", base, xs)

def hot_cold(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +target_x = (5 + mix(seed) % 150 : U32)
  +target_y = (5 + mix((seed + 1 : U32)) % 115 : U32)
  xs = write_node(0, 3, False{}, 0, "Touch area", 0, 52, 154, 125, base, a)
  xs = write_node(1, 4, False{}, 0, "Temperature", 0, 180, 160, 25, base, xs)
  xs = xs[(base + 33 : U32)] <- 2
  xs = xs[(base + 36 : U32)] <- target_x
  xs = xs[(base + 37 : U32)] <- target_y
  query("Find and click on the HOT area.", base, xs)

def checkbox_number(seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +digit = (mix(seed) % 10 : U32)
  +mask = pattern_mask(digit)
  a = boxes(28n, digit, mask, base, a)
  a = write_node(28, 0, False{}, 0, "Submit", 28, 128, 70, 28, base, a)
  a = a[(base + 33 : U32)] <- 29
  a = a[(base + 36 : U32)] <- digit
  query("Draw the number \"" ++ U32.show(digit) ++ "\" in the checkboxes using the example on the right and press Submit when finished.", base, a)

def odd_even(seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  a = odd_nodes(3n, seed, base, a)
  a = write_node(6, 0, False{}, 0, "Submit", 45, 126, 70, 28, base, a)
  a = a[(base + 33 : U32)] <- 7
  query("Mark the numbers below as odd or even and press submit when done.", base, a)

def if_math(task: Nat, problem: String) -> String:
  match task:
    case 7n: "Solve for x and type your answer into the textbox. Press Submit when done."
    case _: "Solve the math problem and type your answer into the textbox. Press submit when done."

def math_nodes(pair: U32 & String, task: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  (answer, +problem) = pair
  +q = if_math(task, problem)
  xs = write_node(0, 2, False{}, 0, "Answer", 70, 66, 58, 30, base, a)
  xs = write_node(1, 0, False{}, 0, "Submit", 65, 107, 70, 30, base, xs)
  xs = write_node(2, 4, False{}, 0, "Problem", 8, 32, 150, 30, base, xs)
  xs = node_value(2, problem, base, xs)
  xs = xs[(base + 33 : U32)] <- 3
  xs = xs[(base + 39 : U32)] <- answer
  query(q, base, xs)

def math_number(+task: Nat, seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  pair = math_problem(task, seed)
  math_nodes(pair, task, base, a)

def generate_task(+task: Nat, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match task:
    case 0n:
      xs = write_node(0, 0, False{}, 1, "1", 15, 20, 22, 22, base, a)
      xs = write_node(1, 0, False{}, 2, "2", 48, 18, 22, 22, base, xs)
      xs = write_node(2, 0, False{}, 3, "3", 82, 28, 22, 22, base, xs)
      xs = write_node(3, 0, False{}, 4, "4", 30, 68, 22, 22, base, xs)
      xs = write_node(4, 0, False{}, 5, "5", 99, 88, 22, 22, base, xs)
      query("Click on the numbers in ascending order.", base, xs[(base + 33 : U32)] <- 5)
    case 1n:
      +a0 = (mix(seed) % 26 : U32)
      +offset1 = (1 + mix((seed + 1 : U32)) % 25 : U32)
      +n1 = ((a0 + offset1 : U32) % 26 : U32)
      +offset2 = Bool.pick(U32, U32.is_eq(offset1, 25), 2, (offset1 + 1 : U32))
      +n2 = ((a0 + offset2 : U32) % 26 : U32)
      +max0 = U32.is_gt(a0, n1) && U32.is_gt(a0, n2)
      +max1 = U32.is_gt(n1, a0) && U32.is_gt(n1, n2)
      xs = write_node(0, 5, max0, a0, "Card 1", 8, 20, 44, 64, base, a)
      xs = write_node(1, 5, max1, n1, "Card 2", 60, 20, 44, 64, base, xs)
      xs = write_node(2, 5, Bool.not(max0 || max1), n2, "Card 3", 112, 20, 44, 64, base, xs)
      xs = write_node(3, 0, False{}, 0, "Submit", 48, 102, 70, 30, base, xs)
      query("Find and pick the card with the greatest number, then press submit.", base, xs[(base + 33 : U32)] <- 4)
    case 2n: generate_number(seed, base, a)
    case 3n: guess_number(seed, base, a)
    case 4n: hot_cold(seed, base, a)
    case 5n: checkbox_number(seed, base, a)
    case 6n: odd_even(seed, base, a)
    case 7n: math_number(task, seed, base, a)
    case 8n: math_number(task, seed, base, a)
    case _: a

def deadline_for_task(task: Nat) -> U32:
  match task:
    case 0n: 10000
    case 1n: 10000
    case 2n: 15000
    case 3n: 15000
    case 4n: 15000
    case 5n: 30000
    case 6n: 15000
    case _: 10000

def deadline(task: U32) -> U32:
  deadline_for_task(U32.to_nat(task))

def generate(+task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  # Reset the fixed header; active nodes and both text buffers are rewritten below.
  a = clear(64n, base, a)
  a = a[base] <- 2
  a = a[(base + 1 : U32)] <- task
  a = a[(base + 2 : U32)] <- 0
  a = a[(base + 3 : U32)] <- seed
  a = a[(base + 12 : U32)] <- deadline(task)
  a = a[(base + 44 : U32)] <- mix((seed + task : U32))
  generate_task(U32.to_nat(task), seed, base, a)
