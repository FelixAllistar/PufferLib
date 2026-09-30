import Base
import ./Search.bend as S
import ./SearchGenerate.bend as G
import ../../miniwob/Text.bend as T

def prepend(pair: Array<U32> & U32, xs: +List<U32>) -> Array<U32> & +List<U32>:
  (a, x) = pair
  (a, x <> xs)

def read_units(n: Nat, +at: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, xs) = pair
      read_units(p, at, prepend(a[(at + U32.from_nat(p) : U32)], xs))

def zero(n: Nat, +at: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: zero(p, at, a[(at + U32.from_nat(p) : U32)] <- 0)

def write_units(xs: +List<U32>, +at: U32, a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{x, rest}:
      a[at] <- x
      write_units(rest, (at + 1 : U32), a)

def lower(+ch: U32) -> U32:
  Bool.pick(U32, U32.is_ge(ch, 65) && U32.is_le(ch, 90),
    (ch + 32 : U32), ch)

def same_fold(xs: +List<U32>, ys: +List<U32>) -> Bool:
  match xs ys:
    case Nil{} Nil{}: True{}
    case Con{x, xt} Con{y, yt}:
      U32.is_eq(lower(x), lower(y)) && same_fold(xt, yt)
    case _ _: False{}

def outcome_index(status: Nat, raw: U32) -> S.Outcome:
  match status:
    case 0n: S.Running{}
    case 1n: S.Finished{U32.is_eq(raw, 1065353216)}
    case _: S.TimedOut{}

def outcome(status: U32, raw: U32) -> S.Outcome:
  outcome_index(U32.to_nat(status), raw)

def save_outcome(o: S.Outcome, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case S.Running{}: a
    case S.Finished{+success}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(S.raw(S.Finished{success}))
      a[(base + 11 : U32)] <- F32.bits(S.timed(S.Finished{success}, elapsed, deadline))
    case S.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(s: S.State, elapsed: U32, deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  S.S{page, target, matched, focus, o} = s
  a = a[(base + 32 : U32)] <- page
  a = a[(base + 38 : U32)] <- Bool.to_u32(focus)
  a = a[(base + 39 : U32)] <- Bool.to_u32(matched)
  a = a[(base + 2 : U32)] <- 0
  save_outcome(o, elapsed, deadline, base, a)

def save_field(f: T.Field, +base: U32, a: Array<U32>) -> Array<U32>:
  T.F{+value, start, end_, goal} = f
  a = a[(base + 35 : U32)] <- U32.from_nat(T.length(value))
  a = a[(base + 36 : U32)] <- U32.from_nat(start)
  a = a[(base + 37 : U32)] <- U32.from_nat(end_)
  write_units(value, (base + 2500 : U32),
    zero(128n, (base + 2500 : U32), a))

def edit_index(kind: Nat, payload: +List<U32>) -> T.Edit:
  match kind:
    case 2n: T.Insert{payload}
    case 3n: T.Backspace{}
    case 4n: T.Delete{}
    case 5n: T.Left{}
    case 6n: T.Right{}
    case 7n: T.Home{}
    case 8n: T.End{}
    case _: T.SelectAll{}

def click_result(fake: Bool, +ref: U32) -> S.Action:
  match fake:
    case True{}: S.Result{(ref - 200 : U32), True{}}
    case False{}: S.Result{(ref - 100 : U32), False{}}

def click_page(is_page: Bool, +ref: U32, equal: Bool) -> S.Action:
  match is_page:
    case True{}: S.Page{(ref - 9 : U32), equal}
    case False{}: click_result(U32.is_ge(ref, 200), ref)

def click_search(is_search: Bool, +ref: U32, equal: Bool) -> S.Action:
  match is_search:
    case True{}: S.Search{equal}
    case False{}: click_page(U32.is_lt(ref, 100), ref, equal)

def click_focus(is_focus: Bool, +ref: U32, equal: Bool) -> S.Action:
  match is_focus:
    case True{}: S.Focus{}
    case False{}: click_search(U32.is_eq(ref, 2), ref, equal)

def click_action(kind: Nat, +ref: U32, equal: Bool) -> S.Action:
  match kind:
    case 1n: click_focus(U32.is_eq(ref, 1), ref, equal)
    case _: S.Wait{}

def apply_edit_allowed(allowed: Bool, kind: U32,
    payload: +List<U32>, f: T.Field,
    s: S.State, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match allowed:
    case True{}:
      a = save_field(T.edit(edit_index(U32.to_nat(kind), payload), f), base, a)
      save(S.step(s, S.Wait{}, elapsed, deadline), elapsed, deadline, base, a)
    case False{}:
      save(S.step(s, S.Wait{}, elapsed, deadline), elapsed, deadline, base, a)

def dispatch_edit(editing: Bool, +kind: U32, ref: U32,
    payload: +List<U32>, +f: T.Field,
    s: S.State, allowed: Bool, +elapsed: U32,
    +deadline: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match editing:
    case True{}: apply_edit_allowed(allowed, kind, payload,
      f, s, elapsed, deadline, base, a)
    case False{}:
      T.F{+value, start, end_, +goal} = f
      next = click_action(U32.to_nat(kind), ref, same_fold(value, goal))
      save(S.step(s, next, elapsed, deadline), elapsed, deadline, base, a)

def payload_pair(pair: Array<U32> & +List<U32>, +kind: U32,
    ref: U32, f: T.Field, s: S.State, allowed: Bool,
    elapsed: U32, deadline: U32, +base: U32) -> Array<U32>:
  (a, payload) = pair
  dispatch_edit(U32.is_ge(kind, 2) && U32.is_le(kind, 9),
    kind, ref, payload, f, s, allowed, elapsed, deadline, base, a)

def payload_length(pair: Array<U32> & U32, +kind: U32,
    ref: U32, f: T.Field, s: S.State, allowed: Bool,
    elapsed: U32, deadline: U32, +base: U32) -> Array<U32>:
  (a, length_units) = pair
  payload_pair(read_units(U32.to_nat(length_units),
    (base + 5000 : U32), (a, Nil{})),
    kind, ref, f, s, allowed, elapsed, deadline, base)
# Read only the ABI and task fields required for one search event.
type Args is Data:
  H{status: U32, raw: U32, elapsed: U32, deadline: U32, kind: U32, ref: U32, payload_len: U32, page: U32, target: U32, goal_len: U32, input_len: U32, start: U32, end_: U32, focus: U32, matched: U32}

def offset(i: Nat) -> U32:
  match i:
    case 0n: 9
    case 1n: 10
    case 2n: 8
    case 3n: 12
    case 4n: 4
    case 5n: 5
    case 6n: 6
    case 7n: 32
    case 8n: 33
    case 9n: 34
    case 10n: 35
    case 11n: 36
    case 12n: 37
    case 13n: 38
    case 14n: 39
    case _: 39

def update(i: Nat, h: Args, +value: U32) -> Args:
  match i h:
    case 0n H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}:
      H{value, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}
    case 1n H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}:
      H{status, value, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}
    case 2n H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}:
      H{status, raw, value, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}
    case 3n H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}:
      H{status, raw, elapsed, value, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}
    case 4n H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}:
      H{status, raw, elapsed, deadline, value, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}
    case 5n H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}:
      H{status, raw, elapsed, deadline, kind, value, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}
    case 6n H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}:
      H{status, raw, elapsed, deadline, kind, ref, value, page, target, goal_len, input_len, start, end_, focus, matched}
    case 7n H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}:
      H{status, raw, elapsed, deadline, kind, ref, payload_len, value, target, goal_len, input_len, start, end_, focus, matched}
    case 8n H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}:
      H{status, raw, elapsed, deadline, kind, ref, payload_len, page, value, goal_len, input_len, start, end_, focus, matched}
    case 9n H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}:
      H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, value, input_len, start, end_, focus, matched}
    case 10n H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}:
      H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, value, start, end_, focus, matched}
    case 11n H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}:
      H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, value, end_, focus, matched}
    case 12n H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}:
      H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, value, focus, matched}
    case 13n H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}:
      H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, value, matched}
    case 14n H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}:
      H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, value}
    case _ H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}: H{status, raw, elapsed, deadline, kind, ref, payload_len, page, target, goal_len, input_len, start, end_, focus, matched}

def updated(pair: Array<U32> & U32, index: Nat, h: Args) -> Array<U32> & Args:
  (a, value) = pair
  (a, update(index, h, value))

def load(n: Nat, +base: U32, pair: Array<U32> & Args) -> Array<U32> & Args:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, h) = pair
      load(p, base, updated(a[(base + offset(p) : U32)], p, h))

def input_size(h: Args) -> U32:
  H{status, raw, elapsed, deadline, kind, ref, payload_len,
    page, target, goal_len, input_len, start, end_, focus, matched} = h
  input_len

def goal_size(h: Args) -> U32:
  H{status, raw, elapsed, deadline, kind, ref, payload_len,
    page, target, goal_len, input_len, start, end_, focus, matched} = h
  goal_len

def goal_loaded(pair: Array<U32> & +List<U32>,
    input: +List<U32>, h: Args, +base: U32) -> Array<U32>:
  (a, goal) = pair
  H{+status, raw, +elapsed, +deadline, kind, ref, payload_len,
    page, target, goal_len, input_len, start, end_, +focus, matched} = h
  f = {T.F{input, U32.to_nat(start), U32.to_nat(end_), goal} : T.Field}
  s = {S.S{page, target, U32.is_eq(matched, 1),
    U32.is_eq(focus, 1), outcome(status, raw)} : S.State}
  payload_length(a[(base + 6 : U32)], kind, ref, f, s,
    U32.is_eq(status, 0) && U32.is_eq(focus, 1) &&
    U32.is_lt(elapsed, deadline), elapsed, deadline, base)

def input_loaded(pair: Array<U32> & +List<U32>,
    +h: Args, +base: U32) -> Array<U32>:
  (a, input) = pair
  goal_loaded(read_units(U32.to_nat(goal_size(h)),
    (base + 2700 : U32), (a, Nil{})), input, h, base)

def header_loaded(pair: Array<U32> & Args, +base: U32) -> Array<U32>:
  (a, +h) = pair
  input_loaded(read_units(U32.to_nat(input_size(h)),
    (base + 2500 : U32), (a, Nil{})), h, base)

def run(+base: U32, a: Array<U32>) -> Array<U32>:
  header_loaded(load(15n, base,
    (a, H{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0})), base)

def reset_seed(pair: Array<U32> & U32, +base: U32) -> Array<U32>:
  (a, seed) = pair
  G.generate(2, seed, base, a)

def reset(+base: U32, a: Array<U32>) -> Array<U32>:
  reset_seed(a[(base + 3 : U32)], base)
