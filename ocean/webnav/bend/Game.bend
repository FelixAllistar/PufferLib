import Base

# The 256-word row is the native ABI. State occupies 0..10, action 13,
# command 14 (0=step, 1=reset, 2=observe), observation 32..159, mask 160..172.
type State is Data:
  S{seed: U32, task: U32, target: U32, order: U32, checked: U32, focus: U32, text: U32, length: U32, tick: U32, done: U32, result: U32}

def pick(c: Bool, a: U32, b: U32) -> U32:
  Bool.pick(U32, c, a, b)

def random(+x: U32) -> U32:
  +a = (x .^. (x >> 16n) : U32)
  +b = (a * 2146121005 : U32)
  +c = (b .^. (b >> 15n) : U32)
  +d = (c * 2221713035 : U32)
  (d .^. (d >> 16n) : U32)

def reset(+seed: U32) -> State:
  +r = random(seed)
  +task = (r % 3 : U32)
  target = pick(U32.is_eq(task, 0), ((r >> 4n) % 4 : U32), pick(U32.is_eq(task, 1), (1 + (r >> 4n) % 14 : U32), ((r >> 4n) % 16 : U32)))
  S{seed, task, target, ((r >> 12n) % 4 : U32), 0, 0, 0, 0, 0, 0, 0}

def terminal(+s: State, success: Bool) -> State:
  S{seed, task, target, order, checked, focus, text, length, tick, done, result} = s
  S{seed, task, target, order, checked, focus, text, length, tick, 1, pick(success, 1, 2)}

def submit(+s: State) -> State:
  S{seed, +task, +target, order, +checked, focus, +text, +length, tick, done, result} = s
  terminal(s, Bool.pick(Bool, U32.is_eq(task, 1), U32.is_eq(checked, target), U32.is_eq(length, 2) && U32.is_eq(text, target)))

def checkbox(+node: U32, label: U32, +s: State) -> State:
  S{seed, task, target, order, +checked, focus, text, length, tick, done, result} = s
  toggled = {S{seed, task, target, order, (checked .^. (1 << U32.to_nat(label)) : U32), focus, text, length, tick, done, result} : State}
  Bool.pick(State, U32.is_eq(node, 4), submit(s), toggled)

def choose_click(task: U32, node: U32, label: U32, +s: State) -> State:
  match task:
    case 0:
      S{seed, task, +target, order, checked, focus, text, length, tick, done, result} = s
      Bool.pick(State, U32.is_lt(node, 4), terminal(s, U32.is_eq(label, target)), s)
    case 1:
      checkbox(node, label, s)
    case _:
      Bool.pick(State, U32.is_eq(node, 4), submit(s), s)

def click(s: State, +node: U32) -> State:
  S{seed, +task, +target, +order, +checked, focus, text, length, tick, done, result} = s
  +label = ((node + order) % 4 : U32)
  +next = {S{seed, task, target, order, checked, (node + 1 : U32), text, length, tick, done, result} : State}
  choose_click(task, node, label, next)

def edit(+s: State, +action: U32) -> State:
  S{seed, +task, target, order, checked, +focus, +text, +length, tick, done, result} = s
  +back = U32.is_eq(action, 10)
  +can = U32.is_eq(focus, 1) && U32.is_eq(task, 2)
  +next_len = pick(back, pick(U32.is_eq(length, 0), 0, (length - 1 : U32)), pick(U32.is_lt(length, 2), (length + 1 : U32), length))
  +next_text = pick(back, pick(U32.is_eq(next_len, 0), 0, (text % 4 : U32)), pick(U32.is_lt(length, 2), (text + (action - 6) * (1 << U32.to_nat((length * 2 : U32))) : U32), text))
  S{seed, task, target, order, checked, focus, pick(can, next_text, text), pick(can, next_len, length), tick, done, result}

def tab(s: State) -> State:
  S{seed, +task, target, order, checked, +focus, text, length, tick, done, result} = s
  next = pick(U32.is_eq(task, 2), pick(U32.is_eq(focus, 1), 5, 1), (focus % pick(U32.is_eq(task, 0), 4, 5) + 1 : U32))
  S{seed, task, target, order, checked, next, text, length, tick, done, result}

def enter(task: U32, +focus: U32, +s: State) -> State:
  match task:
    case 0: Bool.pick(State, U32.is_eq(focus, 0), s, click(s, (focus - 1 : U32)))
    case 1: Bool.pick(State, U32.is_eq(focus, 5), submit(s), s)
    case _: Bool.pick(State, U32.is_eq(focus, 0), s, submit(s))

def enter_state(+s: State) -> State:
  S{seed, +task, target, order, checked, +focus, text, length, tick, done, result} = s
  enter(task, focus, s)

def active(+s: State, +action: U32) -> State:
  match action:
    case 0: s
    case 1: click(s, 0)
    case 2: click(s, 1)
    case 3: click(s, 2)
    case 4: click(s, 3)
    case 5: click(s, 4)
    case 6: edit(s, 6)
    case 7: edit(s, 7)
    case 8: edit(s, 8)
    case 9: edit(s, 9)
    case 10: edit(s, 10)
    case 11: tab(s)
    case 12: enter_state(s)
    case _: s

def advance(s: State, +tick: U32) -> State:
  S{seed, task, target, order, checked, focus, text, length, old_tick, +done, +result} = s
  +timeout = U32.is_ge(tick, 16) && U32.is_eq(done, 0)
  S{seed, task, target, order, checked, focus, text, length, tick, pick(timeout, 1, done), pick(timeout, 2, result)}

def step_if(finished: Bool, +s: State, +task: U32, tick: U32, +action: U32) -> State:
  match finished:
    case True{}: s
    case False{}:
      +valid = Bool.not((U32.is_eq(task, 0) && U32.is_eq(action, 5)) || (U32.is_eq(task, 2) && (U32.is_ge(action, 2) && U32.is_le(action, 4))))
      advance(active(s, pick(valid, action, 0)), (tick + 1 : U32))

def step(+s: State, action: U32) -> State:
  S{seed, +task, target, order, checked, focus, text, length, +tick, +done, result} = s
  step_if(U32.is_eq(done, 1), s, task, tick, action)

def letter(x: U32) -> String:
  SCon{Char.from_u32((97 + x : U32)), SNil{}}

def checks(n: Nat, +target: U32, i: U32) -> String:
  match n:
    case 0n: ""
    case 1n+ +p:
      +j = U32.from_nat(p)
      Bool.pick(String, U32.is_ne((target .&. (1 << U32.to_nat(j)) : U32), 0), checks(p, target, 0) ++ letter(j), checks(p, target, 0))

def instruction(task: U32, +target: U32) -> String:
  match task:
    case 0: "Click " ++ letter(target)
    case 1: "Check " ++ checks(4n, target, 0)
    case _: "Type " ++ letter((target % 4 : U32)) ++ letter((target / 4 : U32))

def put_string(s: String, +offset: U32, a: Array<U32>) -> Array<U32>:
  match s:
    case SNil{}: a
    case SCon{c, rest}:
      a[offset] <- Char.to_u32(c)
      put_string(rest, (offset + 1 : U32), a)

def nodes(n: Nat, +base: U32, +s: State, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      S{seed, +task, target, +order, +checked, +focus, +text, +length, tick, done, result} = s
      +i = U32.from_nat(p)
      +present = pick(U32.is_eq(task, 0), Bool.to_u32(U32.is_lt(i, 4)), pick(U32.is_eq(task, 1), 1, Bool.to_u32(U32.is_eq(i, 0) || U32.is_eq(i, 4))))
      +role = pick(U32.is_eq(i, 4) || U32.is_eq(task, 0), 1, pick(U32.is_eq(task, 1), 2, 3))
      +label = ((i + order) % 4 : U32)
      +at = (base + 64 + i * 16 : U32)
      a[at] <- present
      a[(at + 1 : U32)] <- (present * role : U32)
      a[(at + 2 : U32)] <- 0
      a[(at + 3 : U32)] <- (present * 16 : U32)
      a[(at + 4 : U32)] <- (present * (48 + i * 32) : U32)
      a[(at + 5 : U32)] <- (present * 128 : U32)
      a[(at + 6 : U32)] <- (present * 24 : U32)
      a[(at + 7 : U32)] <- Bool.to_u32(U32.is_eq(task, 1) && U32.is_lt(i, 4) && U32.is_ne((checked .&. (1 << U32.to_nat(label)) : U32), 0))
      a[(at + 8 : U32)] <- (present * Bool.to_u32(U32.is_eq(focus, (i + 1 : U32))) : U32)
      a[(at + 9 : U32)] <- present
      a[(at + 10 : U32)] <- (present * pick(U32.is_eq(i, 4), 115, pick(U32.is_eq(role, 3), 116, (97 + label : U32))) : U32)
      a[(at + 14 : U32)] <- pick(U32.is_eq(present, 1) && U32.is_eq(role, 3) && U32.is_ge(length, 1), (97 + text % 4 : U32), 0)
      a[(at + 15 : U32)] <- pick(U32.is_eq(present, 1) && U32.is_eq(role, 3) && U32.is_ge(length, 2), (97 + text / 4 : U32), 0)
      nodes(p, base, s, a)

def clear(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      a[(base + U32.from_nat(p) : U32)] <- 0
      clear(p, base, a)

def masks(n: Nat, +base: U32, +task: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +i = U32.from_nat(p)
      valid = Bool.not((U32.is_eq(task, 0) && U32.is_eq(i, 5)) || (U32.is_eq(task, 2) && U32.is_ge(i, 2) && U32.is_le(i, 4)))
      a[(base + 160 + i : U32)] <- Bool.to_u32(valid)
      masks(p, base, task, a)

def store(+base: U32, +s: State, a: Array<U32>) -> Array<U32>:
  S{seed, +task, +target, order, checked, focus, text, length, tick, done, result} = s
  a[base] <- seed
  a[(base + 1 : U32)] <- task
  a[(base + 2 : U32)] <- target
  a[(base + 3 : U32)] <- order
  a[(base + 4 : U32)] <- checked
  a[(base + 5 : U32)] <- focus
  a[(base + 6 : U32)] <- text
  a[(base + 7 : U32)] <- length
  a[(base + 8 : U32)] <- tick
  a[(base + 9 : U32)] <- done
  a[(base + 10 : U32)] <- result
  a = clear(128n, (base + 32 : U32), a)
  a = put_string(instruction(task, target), (base + 32 : U32), a)
  masks(13n, base, task, nodes(5n, base, s, a))

def dispatch(command: U32, s: State, seed: U32, action: U32) -> State:
  match command:
    case 1: reset(seed)
    case 2: s
    case _: step(s, action)

def read12(+base: U32, +seed: U32, +task: U32, +target: U32, +order: U32, +checked: U32, +focus: U32, +text: U32, +length: U32, +tick: U32, +done: U32, +result: U32, +action: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, +command) = pair
  s = {S{seed, task, target, order, checked, focus, text, length, tick, done, result} : State}
  store(base, dispatch(command, s, seed, action), a)

def read11(+base: U32, +seed: U32, +task: U32, +target: U32, +order: U32, +checked: U32, +focus: U32, +text: U32, +length: U32, +tick: U32, +done: U32, +result: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, +action) = pair
  read12(base, seed, task, target, order, checked, focus, text, length, tick, done, result, action, a[(base + 14 : U32)])

def read10(+base: U32, +seed: U32, +task: U32, +target: U32, +order: U32, +checked: U32, +focus: U32, +text: U32, +length: U32, +tick: U32, +done: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, +result) = pair
  read11(base, seed, task, target, order, checked, focus, text, length, tick, done, result, a[(base + 13 : U32)])

def read9(+base: U32, +seed: U32, +task: U32, +target: U32, +order: U32, +checked: U32, +focus: U32, +text: U32, +length: U32, +tick: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, +done) = pair
  read10(base, seed, task, target, order, checked, focus, text, length, tick, done, a[(base + 10 : U32)])

def read8(+base: U32, +seed: U32, +task: U32, +target: U32, +order: U32, +checked: U32, +focus: U32, +text: U32, +length: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, +tick) = pair
  read9(base, seed, task, target, order, checked, focus, text, length, tick, a[(base + 9 : U32)])

def read7(+base: U32, +seed: U32, +task: U32, +target: U32, +order: U32, +checked: U32, +focus: U32, +text: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, +length) = pair
  read8(base, seed, task, target, order, checked, focus, text, length, a[(base + 8 : U32)])

def read6(+base: U32, +seed: U32, +task: U32, +target: U32, +order: U32, +checked: U32, +focus: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, +text) = pair
  read7(base, seed, task, target, order, checked, focus, text, a[(base + 7 : U32)])

def read5(+base: U32, +seed: U32, +task: U32, +target: U32, +order: U32, +checked: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, +focus) = pair
  read6(base, seed, task, target, order, checked, focus, a[(base + 6 : U32)])

def read4(+base: U32, +seed: U32, +task: U32, +target: U32, +order: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, +checked) = pair
  read5(base, seed, task, target, order, checked, a[(base + 5 : U32)])

def read3(+base: U32, +seed: U32, +task: U32, +target: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, +order) = pair
  read4(base, seed, task, target, order, a[(base + 4 : U32)])

def read2(+base: U32, +seed: U32, +task: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, +target) = pair
  read3(base, seed, task, target, a[(base + 3 : U32)])

def read1(+base: U32, +seed: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, +task) = pair
  read2(base, seed, task, a[(base + 2 : U32)])

def read0(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, +seed) = pair
  read1(base, seed, a[(base + 1 : U32)])

def row(+base: U32, a: Array<U32>) -> Array<U32>:
  read0(base, a[base])

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      batch(p, row((U32.from_nat(p) * 256 : U32), a))
