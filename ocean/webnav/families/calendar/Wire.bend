import Base
import ./Model.bend as M
import ./Generate.bend as G

def prepend(pair: Array<U32> & U32, xs: +List<U32>) -> Array<U32> & +List<U32>:
  (a, value) = pair
  (a, value <> xs)

def read_units(n: Nat, +at: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, xs) = pair
      read_units(p, at, prepend(a[(at + U32.from_nat(p) : U32)], xs))

def read_words(n: Nat, base: U32, offset: U32, xs: +List<U32>, a: Array<U32>) -> Array<U32> & +List<U32>:
  read_units(n, (base + offset : U32), (a, xs))

def outcome_kind(status: Nat, raw: U32) -> M.Outcome:
  match status:
    case 0n: M.Running{}
    case 1n: M.Finished{U32.is_eq(raw, 1065353216)}
    case _: M.TimedOut{}

def outcome(status: U32, raw: U32) -> M.Outcome:
  outcome_kind(U32.to_nat(status), raw)

def fold_ascii(+c: U32) -> U32:
  Bool.pick(U32, U32.is_ge(c, 65) && U32.is_le(c, 90), (c + 32 : U32), c)

def exact(s: String, units: +List<U32>, length: Nat) -> Bool:
  match s units length:
    case SNil{} _ 0n: True{}
    case SCon{ch, rest} Con{unit, tail} 1n+p:
      U32.is_eq(fold_ascii(unit), fold_ascii(Char.to_u32(ch))) && exact(rest, tail, p)
    case _ _ _: False{}

def write_units(xs: +List<U32>, +at: U32, a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{x, rest}: write_units(rest, (at + 1 : U32), a[at] <- x)

def write_insert(base: U32, at: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, xs) = pair
  write_units(xs, (base + 64 + at : U32), a)

def copy_insert(n: Nat, +base: U32, at: U32, a: Array<U32>) -> Array<U32>:
  write_insert(base, at, read_words(n, base, 128, Nil{}, a))

def insert_allowed(valid: Bool, +base: U32, +length: U32, +select_all: U32, +count: U32, a: Array<U32>) -> Array<U32>:
  match valid:
    case False{}: a
    case True{}:
      +at = Bool.pick(U32, U32.is_eq(select_all, 1), 0, length)
      a[(base + 42 : U32)] <- (at + count : U32)
      a[(base + 57 : U32)] <- 0
      copy_insert(U32.to_nat(count), base, at, a)

def insert_text(+base: U32, +elapsed: U32, +deadline: U32, +target: U32,
                +modal: U32, +length: U32, +select_all: U32,
                pair: Array<U32> & U32) -> Array<U32>:
  (a, +count) = pair
  +at = Bool.pick(U32, U32.is_eq(select_all, 1), 0, length)
  insert_allowed(U32.is_eq(target, 53) && U32.is_eq(modal, 1) &&
                 U32.is_lt(elapsed, deadline) && U32.is_le((at + count : U32), 32),
                 base, length, select_all, count, a)

def text_action_active(action: Nat, +base: U32, +target: U32, +elapsed: U32,
                +deadline: U32, +modal: U32, +length: U32,
                +select_all: U32, a: Array<U32>) -> Array<U32>:
  match action:
    case 2n: insert_text(base, elapsed, deadline, target, modal, length, select_all,
                          a[(base + 58 : U32)])
    case 3n:
      +valid = U32.is_eq(target, 53) && U32.is_eq(modal, 1) && U32.is_gt(length, 0)
      +remaining = Bool.pick(U32, U32.is_eq(select_all, 1), 0, (length - 1 : U32))
      a[(base + 42 : U32)] <- Bool.pick(U32, valid, remaining, length)
      a[(base + 57 : U32)] <- 0
    case 9n:
      a[(base + 57 : U32)] <- Bool.to_u32(U32.is_eq(target, 53) && U32.is_eq(modal, 1))
    case _: a

def text_action_guarded(active: Bool, action: Nat, +base: U32,
                +target: U32, +elapsed: U32,
                +deadline: U32, +modal: U32, +length: U32,
                +select_all: U32, a: Array<U32>) -> Array<U32>:
  match active:
    case False{}: a
    case True{}: text_action_active(action, base, target, elapsed, deadline,
                                     modal, length, select_all, a)

def text_action(action: Nat, +base: U32, +target: U32, +elapsed: U32,
                +deadline: U32, +modal: U32, +length: U32,
                +select_all: U32, a: Array<U32>) -> Array<U32>:
  text_action_guarded(U32.is_lt(elapsed, deadline), action, base, target,
                      elapsed, deadline, modal, length, select_all, a)

def decoded_target(target_kind: Nat, target: U32, matches: Bool) -> M.Action:
  match target_kind:
    case 55n: M.Create{matches}
    case _: M.Click{target}

def decoded(action: Nat, +target: U32, arg0: U32, matches: Bool) -> M.Action:
  match action:
    case 1n:
      decoded_target(U32.to_nat(target), target, matches)
    case 12n: M.Scroll{arg0}
    case 13n: M.Down{target}
    case 14n: M.Move{target}
    case 15n: M.Up{target}
    case _: M.Wait{}

def save_outcome(o: M.Outcome, +elapsed: U32, +deadline: U32,
                 +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case M.Running{}: a
    case M.Finished{+success}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(M.raw(M.Finished{success}))
      a[(base + 11 : U32)] <- F32.bits(M.timed(M.Finished{success}, elapsed, deadline))
    case M.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(s: M.State, +elapsed: U32, +deadline: U32, +base: U32,
         a: Array<U32>) -> Array<U32>:
  M.S{task, month, selected, popup, target, window, duration, name,
      start, end, modal, scroll, result} = s
  a[(base + 2 : U32)] <- 0
  a[(base + 32 : U32)] <- month
  a[(base + 33 : U32)] <- selected
  a[(base + 34 : U32)] <- Bool.to_u32(popup)
  a[(base + 39 : U32)] <- start
  a[(base + 40 : U32)] <- end
  a[(base + 41 : U32)] <- Bool.to_u32(modal)
  a[(base + 43 : U32)] <- scroll
  save_outcome(result, elapsed, deadline, base, a)

def clear_on_cancel(cancel: Bool, +base: U32, a: Array<U32>) -> Array<U32>:
  match cancel:
    case False{}: a
    case True{}:
      a[(base + 42 : U32)] <- 0
      a[(base + 57 : U32)] <- 0

def run_compared(+base: U32, s: M.State, +action: U32, +target: U32,
                 arg0: U32, +elapsed: U32, +deadline: U32,
                 pair: Array<U32> & Bool) -> Array<U32>:
  (a, matches) = pair
  a = save(M.step(s, decoded(U32.to_nat(action), target, arg0, matches), elapsed,
                  deadline), elapsed, deadline, base, a)
  clear_on_cancel(U32.is_lt(elapsed, deadline) && U32.is_eq(action, 1) &&
                  U32.is_eq(target, 54), base, a)

def name_matches(name: U32, length: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & Bool:
  (a, units) = pair
  (a, exact(G.daily_name(name), units, U32.to_nat(length)))

def state_name(s: M.State) -> U32:
  M.S{task, month, selected, popup, target, window, duration, name,
      start, end, modal, scroll, result} = s
  name

def run_length(+base: U32, +s: M.State, action: U32, target: U32,
               arg0: U32, elapsed: U32, deadline: U32,
               pair: Array<U32> & U32) -> Array<U32>:
  (a, length) = pair
  run_compared(base, s, action, target, arg0, elapsed, deadline,
    name_matches(state_name(s), length, read_words(32n, base, 64, Nil{}, a)))

def run_selection(+base: U32, s: M.State, +action: U32, +target: U32,
                  arg0: U32, +elapsed: U32, +deadline: U32, modal: U32,
                  length: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, select_all) = pair
  a = text_action(U32.to_nat(action), base, target, elapsed, deadline,
                  modal, length, select_all, a)
  run_length(base, s, action, target, arg0, elapsed, deadline, a[(base + 42 : U32)])

def run_fields(+base: U32, task: U32, action: U32, target: U32,
               arg0: U32, elapsed: U32, status: U32, raw: U32,
               deadline: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, fields) = pair
  match fields:
    case Con{month, Con{selected, Con{popup, Con{goal, Con{window,
         Con{duration, Con{name, Con{start, Con{end, Con{+modal,
         Con{length, Con{scroll, Nil{}}}}}}}}}}}}}:
      s = {M.S{task, month, selected, U32.is_eq(popup, 1), goal, window,
                duration, name, start, end, U32.is_eq(modal, 1), scroll,
                outcome(status, raw)} : M.State}
      run_selection(base, s, action, target, arg0, elapsed, deadline, modal,
                     length, a[(base + 57 : U32)])
    case _: a

def run_status(status_kind: Nat, +base: U32, task: U32, action: U32,
               target: U32, arg0: U32, elapsed: U32, status: U32,
               raw: U32, deadline: U32, a: Array<U32>) -> Array<U32>:
  match status_kind:
    case 0n: run_fields(base, task, action, target, arg0, elapsed, status,
                         raw, deadline, read_words(12n, base, 32, Nil{}, a))
    case _: a[(base + 2 : U32)] <- 0

def run_header(+base: U32, task: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, header) = pair
  match header:
    case Con{action, Con{target, Con{arg0, Con{arg1, Con{elapsed,
         Con{+status, Con{raw, Con{timed, Con{deadline, Nil{}}}}}}}}}}:
      run_status(U32.to_nat(status), base, task, action, target, arg0,
                 elapsed, status, raw, deadline, a)
    case _: a

def reset_seed(+base: U32, task: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  G.generate(task, seed, base, a)

def operation_kind(op: Nat, +base: U32, task: U32, a: Array<U32>) -> Array<U32>:
  match op:
    case 0n: a
    case 1n: reset_seed(base, task, a[(base + 3 : U32)])
    case _: run_header(base, task, read_words(9n, base, 4, Nil{}, a))

def operation(op: U32, +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, task) = pair
  operation_kind(U32.to_nat(op), base, task, a)

def row(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, op) = pair
  operation(op, base, a[(base + 1 : U32)])

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +base = (U32.from_nat(p) * 1024 : U32)
      batch(p, row(base, a[(base + 2 : U32)]))
