import Base
import ../Text.bend as T
import ../Widgets.bend as W
import ./Model.bend as M

# Private one-field transport for use-autocomplete-nodelay. This is simulator
# state, not the policy DOM observation ABI.
#
# Header: 0 tag(10), 1 field count(1), 2 focus, 3 outcome, 4 correct,
# 5 elapsed_ms, 6 saved_elapsed_ms, 7 command, 8 command argument,
# 9 raw reward bits, 10 timed reward bits, 11 payload length, 12 match_end,
# 13 prefix length, 14 suffix length, 15 menu kind, 16 value length,
# 17 selection start, 18 selection end, 19 search-term length,
# 20 visible menu count, 21 active item one-based (0 means no active item).
# Value units occupy 32..95, the search term 96..159, prefix 160..191,
# suffix 192..223 and insertion payload 224..255. Input/payload lengths are
# bounded by the C validator; Bend still preserves every word in the row.

def outcome(status: U32, correct: U32) -> W.Outcome:
  match status:
    case 0:
      W.Running{}
    case 1:
      W.binary(U32.is_eq(correct, 1))
    case _:
      W.TimedOut{}

def menu(kind: U32, +term: +List<U32>, active: U32) -> M.Menu:
  match kind:
    case 0:
      M.Closed{}
    case 1:
      M.Open{term}
    case _:
      M.Active{term, U32.to_nat((active - 1 : U32))}

def action(command: U32, index: U32, payload: +List<U32>) -> M.Action:
  match command:
    case 1:
      M.FocusEnd{}
    case 2:
      M.Editing{T.Insert{payload}}
    case 3:
      M.Editing{T.Backspace{}}
    case 4:
      M.Editing{T.Delete{}}
    case 5:
      M.Editing{T.Left{}}
    case 6:
      M.Editing{T.Right{}}
    case 7:
      M.Editing{T.Home{}}
    case 8:
      M.Editing{T.End{}}
    case 9:
      M.Editing{T.SelectAll{}}
    case 10:
      M.Submit{}
    case 11:
      M.Timeout{}
    case 12:
      M.MenuDown{}
    case 13:
      M.MenuUp{}
    case 14:
      M.MenuSelect{U32.to_nat(index)}
    case 15:
      M.MenuAccept{}
    case _:
      M.Wait{}

def prepend(pair: Array<U32> & U32, xs: +List<U32>) -> Array<U32> & +List<U32>:
  (a, x) = pair
  (a, x <> xs)

def read_units(n: Nat, +at: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n:
      pair
    case 1n+ +p:
      (a, xs) = pair
      read_units(p, at, prepend(a[(at + U32.from_nat(p) : U32)], xs))

def zero(n: Nat, +at: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n:
      a
    case 1n+ +p:
      a[(at + U32.from_nat(p) : U32)] <- 0
      zero(p, at, a)

def write_units(xs: +List<U32>, +at: U32, a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}:
      a
    case Con{x, rest}:
      a[at] <- x
      write_units(rest, (at + 1 : U32), a)

def save_field(+field: T.Field, +base: U32, a: Array<U32>) -> Array<U32>:
  T.F{+xs, start, end, goal} = field
  a[(base + 16 : U32)] <- U32.from_nat(T.length(xs))
  a[(base + 17 : U32)] <- U32.from_nat(start)
  a[(base + 18 : U32)] <- U32.from_nat(end)
  write_units(xs, (base + 32 : U32), zero(64n, (base + 32 : U32), a))

def save_term(menu: M.Menu, +base: U32, a: Array<U32>) -> Array<U32>:
  match menu:
    case M.Closed{}:
      a[(base + 19 : U32)] <- 0
      zero(64n, (base + 96 : U32), a)
    case M.Open{+term}:
      a[(base + 19 : U32)] <- U32.from_nat(T.length(term))
      write_units(term, (base + 96 : U32), zero(64n, (base + 96 : U32), a))
    case M.Active{+term, index}:
      a[(base + 19 : U32)] <- U32.from_nat(T.length(term))
      write_units(term, (base + 96 : U32), zero(64n, (base + 96 : U32), a))

def save_menu(menu: M.Menu, +field: T.Field, +base: U32, a: Array<U32>) -> Array<U32>:
  match menu:
    case M.Closed{}:
      a[(base + 15 : U32)] <- 0
      a[(base + 20 : U32)] <- 0
      a[(base + 21 : U32)] <- 0
      save_term(menu, base, a)
    case M.Open{+term}:
      a[(base + 15 : U32)] <- 1
      a[(base + 20 : U32)] <- U32.from_nat(M.menu_count_term(term))
      a[(base + 21 : U32)] <- 0
      a[(base + 19 : U32)] <- U32.from_nat(T.length(term))
      write_units(term, (base + 96 : U32), zero(64n, (base + 96 : U32), a))
    case M.Active{+term, index}:
      a[(base + 15 : U32)] <- 2
      a[(base + 20 : U32)] <- U32.from_nat(M.menu_count_term(term))
      a[(base + 21 : U32)] <- U32.from_nat(1n+index)
      a[(base + 19 : U32)] <- U32.from_nat(T.length(term))
      write_units(term, (base + 96 : U32), zero(64n, (base + 96 : U32), a))

def save_outcome(result: W.Outcome, +base: U32, a: Array<U32>) -> Array<U32>:
  match result:
    case W.Running{}:
      a[(base + 3 : U32)] <- 0
      a
    case W.Finished{correct, count, scale}:
      a[(base + 3 : U32)] <- 1
      a[(base + 4 : U32)] <- correct
      a
    case W.TimedOut{}:
      a[(base + 3 : U32)] <- 2
      a[(base + 4 : U32)] <- 0
      a

def save_time(previous: U32, +result: W.Outcome, +elapsed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match previous:
    case 0:
      a[(base + 6 : U32)] <- elapsed
      a[(base + 9 : U32)] <- F32.bits(W.raw_reward(result))
      a[(base + 10 : U32)] <- F32.bits(W.reward_at(result, elapsed))
      a
    case _:
      a

def save(s: M.State, +base: U32, previous: U32, elapsed: U32, a: Array<U32>) -> Array<U32>:
  M.S{task, +field, focus, menu_, +result} = s
  a[(base + 2 : U32)] <- U32.from_nat(focus)
  a1 = save_field(field, base, a)
  a2 = save_menu(menu_, field, base, a1)
  a3 = save_outcome(result, base, a2)
  save_time(previous, result, elapsed, base, a3)

def instantiate(+base: U32, +match_end: U32, +status: U32, correct: U32, focus: U32, +elapsed: U32, command: U32, index: U32, +payload: +List<U32>, +prefix: +List<U32>, +suffix: +List<U32>, +term: +List<U32>, +value: +List<U32>, start: U32, end: U32, kind: U32, active: U32, a: Array<U32>) -> Array<U32>:
  +task = {M.Q{prefix, suffix, U32.is_eq(match_end, 1)} : M.Task}
  +field = {T.F{value, U32.to_nat(start), U32.to_nat(end), Nil{}} : T.Field}
  +state = {M.S{task, field, U32.to_nat(focus), menu(kind, term, active), outcome(status, correct)} : M.State}
  save(M.step_at(state, action(command, index, payload), elapsed), base, status, elapsed, a)

def ready_suffix(+base: U32, match_end: U32, status: U32, correct: U32, focus: U32, elapsed: U32, command: U32, index: U32, payload: +List<U32>, prefix: +List<U32>, term: +List<U32>, value: +List<U32>, start: U32, end: U32, kind: U32, active: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, suffix) = pair
  instantiate(base, match_end, status, correct, focus, elapsed, command, index, payload, prefix, suffix, term, value, start, end, kind, active, a)

def ready_prefix(+base: U32, match_end: U32, status: U32, correct: U32, focus: U32, elapsed: U32, command: U32, index: U32, payload: +List<U32>, term: +List<U32>, value: +List<U32>, start: U32, end: U32, kind: U32, active: U32, prefix_size: U32, +suffix_size: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, prefix) = pair
  ready_suffix(base, match_end, status, correct, focus, elapsed, command, index, payload, prefix, term, value, start, end, kind, active, read_units(U32.to_nat(suffix_size), (base + 192 : U32), (a, Nil{})))

def ready_term(+base: U32, match_end: U32, status: U32, correct: U32, focus: U32, elapsed: U32, command: U32, index: U32, payload: +List<U32>, value: +List<U32>, start: U32, end: U32, kind: U32, active: U32, term_size: U32, +prefix_size: U32, +suffix_size: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, term) = pair
  ready_prefix(base, match_end, status, correct, focus, elapsed, command, index, payload, term, value, start, end, kind, active, prefix_size, suffix_size, read_units(U32.to_nat(prefix_size), (base + 160 : U32), (a, Nil{})))

def ready_value(+base: U32, match_end: U32, status: U32, correct: U32, focus: U32, elapsed: U32, command: U32, index: U32, payload: +List<U32>, start: U32, end: U32, kind: U32, active: U32, value_size: U32, +term_size: U32, prefix_size: U32, suffix_size: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, value) = pair
  ready_term(base, match_end, status, correct, focus, elapsed, command, index, payload, value, start, end, kind, active, term_size, prefix_size, suffix_size, read_units(U32.to_nat(term_size), (base + 96 : U32), (a, Nil{})))

def payload_ready(+base: U32, match_end: U32, status: U32, correct: U32, focus: U32, elapsed: U32, command: U32, index: U32, +value_size: U32, term_size: U32, prefix_size: U32, suffix_size: U32, start: U32, end: U32, kind: U32, active: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, payload) = pair
  ready_value(base, match_end, status, correct, focus, elapsed, command, index, payload, start, end, kind, active, value_size, term_size, prefix_size, suffix_size, read_units(U32.to_nat(value_size), (base + 32 : U32), (a, Nil{})))

def payload_length(+base: U32, match_end: U32, status: U32, correct: U32, focus: U32, elapsed: U32, command: U32, index: U32, value_size: U32, term_size: U32, prefix_size: U32, suffix_size: U32, start: U32, end: U32, kind: U32, active: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, payload_size) = pair
  payload_ready(base, match_end, status, correct, focus, elapsed, command, index, value_size, term_size, prefix_size, suffix_size, start, end, kind, active, read_units(U32.to_nat(payload_size), (base + 224 : U32), (a, Nil{})))

def read15(+base: U32, match_end: U32, status: U32, correct: U32, focus: U32, elapsed: U32, command: U32, index: U32, value_size: U32, term_size: U32, prefix_size: U32, suffix_size: U32, start: U32, end: U32, kind: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, active) = pair
  payload_length(base, match_end, status, correct, focus, elapsed, command, index, value_size, term_size, prefix_size, suffix_size, start, end, kind, active, a[(base + 11 : U32)])

def read14(+base: U32, match_end: U32, status: U32, correct: U32, focus: U32, elapsed: U32, command: U32, index: U32, value_size: U32, term_size: U32, prefix_size: U32, suffix_size: U32, start: U32, end: U32, kind: U32, a: Array<U32>) -> Array<U32>:
  read15(base, match_end, status, correct, focus, elapsed, command, index, value_size, term_size, prefix_size, suffix_size, start, end, kind, a[(base + 21 : U32)])

def read13(+base: U32, match_end: U32, status: U32, correct: U32, focus: U32, elapsed: U32, command: U32, index: U32, value_size: U32, term_size: U32, prefix_size: U32, suffix_size: U32, start: U32, kind: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, end) = pair
  read14(base, match_end, status, correct, focus, elapsed, command, index, value_size, term_size, prefix_size, suffix_size, start, end, kind, a)

def read12(+base: U32, match_end: U32, status: U32, correct: U32, focus: U32, elapsed: U32, command: U32, index: U32, value_size: U32, term_size: U32, prefix_size: U32, suffix_size: U32, kind: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, start) = pair
  read13(base, match_end, status, correct, focus, elapsed, command, index, value_size, term_size, prefix_size, suffix_size, start, kind, a[(base + 18 : U32)])

def read11(+base: U32, match_end: U32, status: U32, correct: U32, focus: U32, elapsed: U32, command: U32, index: U32, value_size: U32, term_size: U32, prefix_size: U32, kind: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, suffix_size_) = pair
  read12(base, match_end, status, correct, focus, elapsed, command, index, value_size, term_size, prefix_size, suffix_size_, kind, a[(base + 17 : U32)])

def read10(+base: U32, match_end: U32, status: U32, correct: U32, focus: U32, elapsed: U32, command: U32, index: U32, value_size: U32, term_size: U32, kind: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, prefix_size_) = pair
  read11(base, match_end, status, correct, focus, elapsed, command, index, value_size, term_size, prefix_size_, kind, a[(base + 14 : U32)])

def read9(+base: U32, match_end: U32, status: U32, correct: U32, focus: U32, elapsed: U32, command: U32, index: U32, value_size: U32, term_size: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, kind) = pair
  read10(base, match_end, status, correct, focus, elapsed, command, index, value_size, term_size, kind, a[(base + 13 : U32)])

def read8(+base: U32, match_end: U32, status: U32, correct: U32, focus: U32, elapsed: U32, command: U32, index: U32, value_size: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, term_size) = pair
  read9(base, match_end, status, correct, focus, elapsed, command, index, value_size, term_size, a[(base + 15 : U32)])

def read7(+base: U32, match_end: U32, status: U32, correct: U32, focus: U32, elapsed: U32, command: U32, index: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, value_size) = pair
  read8(base, match_end, status, correct, focus, elapsed, command, index, value_size, a[(base + 19 : U32)])

def read6(+base: U32, match_end: U32, status: U32, correct: U32, focus: U32, elapsed: U32, command: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, index) = pair
  read7(base, match_end, status, correct, focus, elapsed, command, index, a[(base + 16 : U32)])

def read5(+base: U32, match_end: U32, status: U32, correct: U32, focus: U32, elapsed: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, command) = pair
  read6(base, match_end, status, correct, focus, elapsed, command, a[(base + 8 : U32)])

def read4(+base: U32, match_end: U32, status: U32, correct: U32, focus: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, elapsed) = pair
  read5(base, match_end, status, correct, focus, elapsed, a[(base + 7 : U32)])

def read3(+base: U32, match_end: U32, status: U32, correct: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, focus) = pair
  read4(base, match_end, status, correct, focus, a[(base + 5 : U32)])

def read2(+base: U32, match_end: U32, status: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, correct) = pair
  read3(base, match_end, status, correct, a[(base + 2 : U32)])

def read1(+base: U32, match_end: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  read2(base, match_end, status, a[(base + 4 : U32)])

def read0(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, match_end) = pair
  read1(base, match_end, a[(base + 3 : U32)])

def row(+base: U32, a: Array<U32>) -> Array<U32>:
  read0(base, a[(base + 12 : U32)])

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n:
      a
    case 1n+ +p:
      batch(p, row((U32.from_nat(p) * 256 : U32), a))
