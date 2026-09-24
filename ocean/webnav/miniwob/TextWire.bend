import Base
import ./Text.bend as T
import ./Widgets.bend as W

# Private 256-word text transport, task tags 4..6. One ASCII field max64, or two max32.
# 0 task,1 count,2 focus,3 outcome,4 correct,5 elapsed,6 saved elapsed,
# 7 command,8 target,9 raw bits,10 timed bits,11 payload length.
# Field metadata 16+4*i: length,start,end,goal length.
# Values 32+32*i; goals 96+32*i; insertion payload 160..223.
def outcome(status: U32, correct: U32) -> W.Outcome:
  match status:
    case 0: W.Running{}
    case 1: W.binary(U32.is_eq(correct, 1))
    case _: W.TimedOut{}

def action(code: U32, index: U32, payload: +List<U32>) -> T.Action:
  match code:
    case 1: T.FocusEnd{U32.to_nat(index)}
    case 2: T.Editing{T.Insert{payload}}
    case 3: T.Editing{T.Backspace{}}
    case 4: T.Editing{T.Delete{}}
    case 5: T.Editing{T.Left{}}
    case 6: T.Editing{T.Right{}}
    case 7: T.Editing{T.Home{}}
    case 8: T.Editing{T.End{}}
    case 9: T.Editing{T.SelectAll{}}
    case 10: T.Submit{}
    case 11: T.Timeout{}
    case _: T.Wait{}

def prepend(pair: Array<U32> & U32, xs: +List<U32>) -> Array<U32> & +List<U32>:
  (a, x) = pair
  (a, x <> xs)

def read_units(n: Nat, +at: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, xs) = pair
      read_units(p, at, prepend(a[(at + U32.from_nat(p) : U32)], xs))

def zero(n: Nat, +at: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      a[(at + U32.from_nat(p) : U32)] <- 0
      zero(p, at, a)

def write_units(xs: +List<U32>, +at: U32, a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{x, rest}:
      a[at] <- x
      write_units(rest, (at + 1 : U32), a)

def save_fields(fs: +List<T.Field>, +base: U32, +index: U32, +width: Nat, a: Array<U32>) -> Array<U32>:
  match fs:
    case Nil{}: a
    case Con{T.F{+xs, start, end, goal}, rest}:
      a[(base + 16 + index * 4 : U32)] <- U32.from_nat(T.length(xs))
      a[(base + 17 + index * 4 : U32)] <- U32.from_nat(start)
      a[(base + 18 + index * 4 : U32)] <- U32.from_nat(end)
      +at = (base + 32 + index * 32 : U32)
      save_fields(rest, base, (index + 1 : U32), width, write_units(xs, at, zero(width, at, a)))

def save_outcome(result: W.Outcome, +base: U32, a: Array<U32>) -> Array<U32>:
  match result:
    case W.Running{}: a
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
    case _: a

def save(s: T.State, +base: U32, previous: U32, elapsed: U32, a: Array<U32>) -> Array<U32>:
  T.S{+fs, focus, +result} = s
  a[(base + 2 : U32)] <- U32.from_nat(focus)
  save_fields(fs, base, 0, Bool.pick(Nat, Nat.is_eq(List.length(&2, T.Field, fs), 1n), 64n, 32n), save_outcome(result, base, save_time(previous, result, elapsed, base, a)))

def goal_ready(fs: +List<T.Field>, xs: +List<U32>, start: U32, end: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & +List<T.Field>:
  (a, goal) = pair
  (a, T.F{xs, U32.to_nat(start), U32.to_nat(end), goal} <> fs)

def value_ready(fs: +List<T.Field>, base: U32, index: U32, start: U32, end: U32, size: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & +List<T.Field>:
  (a, xs) = pair
  goal_ready(fs, xs, start, end, read_units(U32.to_nat(size), (base + 96 + index * 32 : U32), (a, Nil{})))

def meta3(fs: +List<T.Field>, +base: U32, +index: U32, size: U32, start: U32, end: U32, pair: Array<U32> & U32) -> Array<U32> & +List<T.Field>:
  (a, goal_size) = pair
  value_ready(fs, base, index, start, end, goal_size, read_units(U32.to_nat(size), (base + 32 + index * 32 : U32), (a, Nil{})))

def meta2(fs: +List<T.Field>, +base: U32, +index: U32, size: U32, start: U32, pair: Array<U32> & U32) -> Array<U32> & +List<T.Field>:
  (a, end) = pair
  meta3(fs, base, index, size, start, end, a[(base + 19 + index * 4 : U32)])

def meta1(fs: +List<T.Field>, +base: U32, +index: U32, size: U32, pair: Array<U32> & U32) -> Array<U32> & +List<T.Field>:
  (a, start) = pair
  meta2(fs, base, index, size, start, a[(base + 18 + index * 4 : U32)])

def meta0(fs: +List<T.Field>, +base: U32, +index: U32, pair: Array<U32> & U32) -> Array<U32> & +List<T.Field>:
  (a, size) = pair
  meta1(fs, base, index, size, a[(base + 17 + index * 4 : U32)])

def load_fields(n: Nat, +base: U32, pair: Array<U32> & +List<T.Field>) -> Array<U32> & +List<T.Field>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, fs) = pair
      +index = U32.from_nat(p)
      load_fields(p, base, meta0(fs, base, index, a[(base + 16 + index * 4 : U32)]))

def ready(+base: U32, focus: U32, +status: U32, correct: U32, +elapsed: U32, code: U32, index: U32, payload: +List<U32>, pair: Array<U32> & +List<T.Field>) -> Array<U32>:
  (a, fs) = pair
  s = {T.S{fs, U32.to_nat(focus), outcome(status, correct)} : T.State}
  save(T.step_at(s, action(code, index, payload), elapsed), base, status, elapsed, a)

def payload_ready(+base: U32, size: U32, focus: U32, status: U32, correct: U32, elapsed: U32, code: U32, index: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, payload) = pair
  ready(base, focus, status, correct, elapsed, code, index, payload, load_fields(U32.to_nat(size), base, (a, Nil{})))

def read11(+base: U32, tag: U32, size: U32, focus: U32, status: U32, correct: U32, elapsed: U32, saved: U32, code: U32, index: U32, raw: U32, timed: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, payload_size) = pair
  payload_ready(base, size, focus, status, correct, elapsed, code, index, read_units(U32.to_nat(payload_size), (base + 160 : U32), (a, Nil{})))

def read10(+base: U32, tag: U32, size: U32, focus: U32, status: U32, correct: U32, elapsed: U32, saved: U32, code: U32, index: U32, raw: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, timed) = pair
  read11(base, tag, size, focus, status, correct, elapsed, saved, code, index, raw, timed, a[(base + 11 : U32)])

def read9(+base: U32, tag: U32, size: U32, focus: U32, status: U32, correct: U32, elapsed: U32, saved: U32, code: U32, index: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, raw) = pair
  read10(base, tag, size, focus, status, correct, elapsed, saved, code, index, raw, a[(base + 10 : U32)])

def read8(+base: U32, tag: U32, size: U32, focus: U32, status: U32, correct: U32, elapsed: U32, saved: U32, code: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, index) = pair
  read9(base, tag, size, focus, status, correct, elapsed, saved, code, index, a[(base + 9 : U32)])

def read7(+base: U32, tag: U32, size: U32, focus: U32, status: U32, correct: U32, elapsed: U32, saved: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, code) = pair
  read8(base, tag, size, focus, status, correct, elapsed, saved, code, a[(base + 8 : U32)])

def read6(+base: U32, tag: U32, size: U32, focus: U32, status: U32, correct: U32, elapsed: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, saved) = pair
  read7(base, tag, size, focus, status, correct, elapsed, saved, a[(base + 7 : U32)])

def read5(+base: U32, tag: U32, size: U32, focus: U32, status: U32, correct: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, elapsed) = pair
  read6(base, tag, size, focus, status, correct, elapsed, a[(base + 6 : U32)])

def read4(+base: U32, tag: U32, size: U32, focus: U32, status: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, correct) = pair
  read5(base, tag, size, focus, status, correct, a[(base + 5 : U32)])

def read3(+base: U32, tag: U32, size: U32, focus: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  read4(base, tag, size, focus, status, a[(base + 4 : U32)])

def read2(+base: U32, tag: U32, size: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, focus) = pair
  read3(base, tag, size, focus, a[(base + 3 : U32)])

def read1(+base: U32, tag: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, size) = pair
  read2(base, tag, size, a[(base + 2 : U32)])

def read0(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, tag) = pair
  read1(base, tag, a[(base + 1 : U32)])

def row(+base: U32, a: Array<U32>) -> Array<U32>:
  read0(base, a[base])
