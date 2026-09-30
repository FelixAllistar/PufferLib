import Base
import ./Editor.bend as E
import ./EditorGenerate.bend as G

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

def write_units(xs: +List<U32>, +at: U32,
    a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{x, rest}:
      write_units(rest, (at + 1 : U32), a[at] <- x)

def outcome_index(status: Nat, raw: U32) -> E.Outcome:
  match status:
    case 0n: E.Running{}
    case 1n: E.Finished{U32.is_eq(raw, 1065353216)}
    case _: E.TimedOut{}

def outcome(status: U32, raw: U32) -> E.Outcome:
  outcome_index(U32.to_nat(status), raw)

def save_outcome(o: E.Outcome, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case E.Running{}: a
    case E.Finished{+success}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(E.raw(E.Finished{success}))
      a[(base + 11 : U32)] <- F32.bits(E.timed(E.Finished{success}, elapsed, deadline))
    case E.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(s: E.State, elapsed: U32, deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  E.S{goal_start, goal_end, goal_style, goal_color,
    length, start, end_, open, styles, o} = s
  a = a[(base + 2 : U32)] <- 0
  a = a[(base + 38 : U32)] <- start
  a = a[(base + 39 : U32)] <- end_
  a = a[(base + 42 : U32)] <- Bool.to_u32(open)
  a = write_units(styles, (base + 2048 : U32), a)
  save_outcome(o, elapsed, deadline, base, a)

def click_color(is_color: Bool, +ref: U32) -> E.Action:
  match is_color:
    case True{}: E.Color{(ref - 19 : U32)}
    case False{}: E.Format{(1 << U32.to_nat((ref - 2 : U32)) : U32)}

def click_submit(is_submit: Bool, +ref: U32) -> E.Action:
  match is_submit:
    case True{}: E.Submit{}
    case False{}:
      Bool.pick(E.Action,U32.is_eq(ref,5),E.OpenColor{},
        click_color(U32.is_ge(ref,20),ref))

def decoded(kind: Nat, +ref: U32, begin: U32, finish: U32) -> E.Action:
  match kind:
    case 1n: click_submit(U32.is_eq(ref, 1), ref)
    case 18n: E.Select{begin, finish}
    case _: E.Wait{}

def after_styles(pair: Array<U32> & +List<U32>,
    goal_start: U32, goal_end: U32, goal_style: U32,
    goal_color: U32, length: U32, start: U32,
    end_: U32, open: U32, kind: U32, ref: U32, arg0: U32,
    arg1: U32, status: U32, raw: U32,
    +elapsed: U32, +deadline: U32,
    +base: U32) -> Array<U32>:
  (a, styles) = pair
  s = {E.S{goal_start, goal_end, goal_style, goal_color,
    length, start, end_, U32.is_eq(open,1), styles,
    outcome(status, raw)} : E.State}
  save(E.step(s, decoded(U32.to_nat(kind), ref, arg0, arg1),
    elapsed, deadline), elapsed, deadline, base, a)

def metadata(pair: Array<U32> & +List<U32>,
    kind: U32, ref: U32, arg0: U32, arg1: U32,
    status: U32, raw: U32, elapsed: U32,
    deadline: U32, +base: U32) -> Array<U32>:
  (a, fields) = pair
  match fields:
    case Con{goal_start, Con{goal_end, Con{goal_style,
         Con{goal_color, Con{single, Con{+length,
         Con{start, Con{end_, Con{count,
         Con{choice, Con{open, Nil{}}}}}}}}}}}}:
      after_styles(read_units(U32.to_nat(length),
        (base + 2048 : U32), (a, Nil{})), goal_start,
        goal_end, goal_style, goal_color, length,
        start, end_, open, kind, ref, arg0, arg1,
        status, raw, elapsed, deadline, base)
    case _: a

def header(pair: Array<U32> & +List<U32>,
    +base: U32) -> Array<U32>:
  (a, fields) = pair
  match fields:
    case Con{kind, Con{ref, Con{arg0, Con{arg1,
         Con{elapsed, Con{status, Con{raw, Con{timed,
         Con{deadline, Nil{}}}}}}}}}}:
      metadata(read_units(11n, (base + 32 : U32),
        (a, Nil{})), kind, ref, arg0, arg1,
        status, raw, elapsed, deadline, base)
    case _: a

def run(+base: U32, a: Array<U32>) -> Array<U32>:
  header(read_units(9n, (base + 4 : U32), (a, Nil{})), base)

def reset_seed(pair: Array<U32> & U32, +base: U32) -> Array<U32>:
  (a, seed) = pair
  G.generate(3, seed, base, a)

def reset(+base: U32, a: Array<U32>) -> Array<U32>:
  reset_seed(a[(base + 3 : U32)], base)
