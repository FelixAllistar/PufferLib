import Base
import ./Model.bend as M

# Private 256-word row, not a policy observation. Tags 7=sequence, 8=list.
# Common: status1,success2,command3,args4/5,elapsed6,saved7,raw8,timed9.
# Sequence: clicks10,first11,last_hit12,rects13..16,version17=1.
# List: selected10,target11,version12=1,count13,lengths17..25.
# Options: 16 packed little-endian U32 words (64 ASCII bytes) at32+16*i.
# Commands: wait0,click/select1,submit2(list),timeout3,reset4.
# Reset is a transport operation restoring this instance, outside episode step.

def at(xs: +List<U32>, index: Nat) -> U32:
  match xs index:
    case Nil{} _: 0
    case Con{x, rest} 0n: x
    case Con{x, rest} 1n+p: at(rest, p)

def prepend(xs: +List<U32>, pair: Array<U32> & U32) -> Array<U32> & +List<U32>:
  (a, x) = pair
  (a, x <> xs)

def load(n: Nat, +base: U32, pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, xs) = pair
      load(p, base, prepend(xs, a[(base + U32.from_nat(p) : U32)]))

def unpack(words: +List<U32>) -> +List<U32>:
  match words:
    case Nil{}: Nil{}
    case Con{+w, rest}:
      U32.and(w, 255) <> U32.and(U32.shrn(w, 8n), 255) <> U32.and(U32.shrn(w, 16n), 255) <> U32.and(U32.shrn(w, 24n), 255) <> unpack(rest)

def option_done(size: Nat, options: +List<+List<U32>>, pair: Array<U32> & +List<U32>) -> Array<U32> & +List<+List<U32>>:
  (a, words) = pair
  (a, List.take(&2, U32, unpack(words), size) <> options)

def load_options(n: Nat, +base: U32, +header: +List<U32>, pair: Array<U32> & +List<+List<U32>>) -> Array<U32> & +List<+List<U32>>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, options) = pair
      load_options(p, base, header, option_done(U32.to_nat(at(header, Nat.add(17n, p))), options, load(16n, (base + 32 + 16 * U32.from_nat(p) : U32), (a, Nil{}))))

def outcome(code: U32, correct: U32) -> M.Outcome:
  match code:
    case 0: M.Running{}
    case 1: M.Finished{U32.is_eq(correct, 1)}
    case _: M.TimedOut{}

def sequence_action(command: U32, x: U32, y: U32) -> M.SequenceAction:
  match command:
    case 1: M.Click{x, y}
    case 3: M.Timeout{}
    case _: M.Wait{}

def choose_action(command: U32, index: U32) -> M.ChooseAction:
  match command:
    case 1: M.Select{U32.to_nat(index)}
    case 2: M.Submit{}
    case 3: M.ChooseTimeout{}
    case _: M.ChooseWait{}

def sequence_step(command: U32, s: M.SequenceState, action: M.SequenceAction, elapsed: U32) -> M.SequenceState:
  match command:
    case 4: M.sequence_reset(s)
    case _: M.sequence_step(s, action, elapsed)

def choose_step(command: U32, s: M.ChooseState, action: M.ChooseAction, elapsed: U32) -> M.ChooseState:
  match command:
    case 4: M.choose_reset(s)
    case _: M.choose_step(s, action, elapsed)

def save_reward(previous: U32, +result: M.Outcome, +base: U32, +elapsed: U32, a: Array<U32>) -> Array<U32>:
  match previous:
    case 0:
      a[(base + 7 : U32)] <- elapsed
      a[(base + 8 : U32)] <- F32.bits(M.raw_reward(result))
      a[(base + 9 : U32)] <- F32.bits(M.timed_reward(result, elapsed))
    case _: a

def save(command: U32, +header: +List<U32>, +result: M.Outcome, +base: U32, a: Array<U32>) -> Array<U32>:
  match command:
    case 4:
      a[(base + 1 : U32)] <- 0
      a[(base + 2 : U32)] <- 0
      a[(base + 6 : U32)] <- 0
      save_reward(0, result, base, 0, a)
    case _:
      a[(base + 1 : U32)] <- M.outcome_status(result)
      a[(base + 2 : U32)] <- Bool.to_u32(M.outcome_success(result))
      save_reward(at(header, 1n), result, base, at(header, 6n), a)

def last_hit(command: U32, +header: +List<U32>, one: M.Rect, two: M.Rect) -> U32:
  match command:
    case 4: 0
    case 1:
      Bool.pick(U32, U32.is_eq(at(header, 1n), 0) && U32.is_lt(at(header, 6n), 10000), M.hit_code(M.hit_at(one, two, at(header, 4n), at(header, 5n))), at(header, 12n))
    case _: at(header, 12n)

def sequence_save(s: M.SequenceState, +header: +List<U32>, +base: U32, a: Array<U32>) -> Array<U32>:
  M.S{+one, +two, clicks, first, result} = s
  a[(base + 10 : U32)] <- clicks
  a[(base + 11 : U32)] <- M.hit_code(first)
  a[(base + 12 : U32)] <- last_hit(at(header, 3n), header, one, two)
  save(at(header, 3n), header, result, base, a)

def sequence(+header: +List<U32>, base: U32, a: Array<U32>) -> Array<U32>:
  s = {M.S{M.R{at(header, 13n), at(header, 14n)}, M.R{at(header, 15n), at(header, 16n)}, at(header, 10n), M.code_hit(at(header, 11n)), outcome(at(header, 1n), at(header, 2n))} : M.SequenceState}
  sequence_save(sequence_step(at(header, 3n), s, sequence_action(at(header, 3n), at(header, 4n), at(header, 5n)), at(header, 6n)), header, base, a)

def choose_save(s: M.ChooseState, +header: +List<U32>, +base: U32, a: Array<U32>) -> Array<U32>:
  M.C{options, target, selected, result} = s
  a[(base + 10 : U32)] <- U32.from_nat(selected)
  save(at(header, 3n), header, result, base, a)

def choose_loaded(+header: +List<U32>, base: U32, pair: Array<U32> & +List<+List<U32>>) -> Array<U32>:
  (a, +options) = pair
  s = {M.C{options, M.option_at(options, U32.to_nat(at(header, 11n))), U32.to_nat(at(header, 10n)), outcome(at(header, 1n), at(header, 2n))} : M.ChooseState}
  choose_save(choose_step(at(header, 3n), s, choose_action(at(header, 3n), at(header, 4n)), at(header, 6n)), header, base, a)

def route(tag: U32, +header: +List<U32>, +base: U32, a: Array<U32>) -> Array<U32>:
  match tag:
    case 7: sequence(header, base, a)
    case 8: choose_loaded(header, base, load_options(U32.to_nat(at(header, 13n)), base, header, (a, Nil{})))
    case _: a

def loaded(base: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, +header) = pair
  route(at(header, 0n), header, base, a)

def row(+base: U32, a: Array<U32>) -> Array<U32>:
  loaded(base, load(32n, base, (a, Nil{})))

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: batch(p, row((U32.from_nat(p) * 256 : U32), a))
