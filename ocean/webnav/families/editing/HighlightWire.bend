import Base
import ./Highlight.bend as H
import ./HighlightGenerate.bend as G

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

def outcome_index(status: Nat, raw: U32) -> H.Outcome:
  match status:
    case 0n: H.Running{}
    case 1n: H.Finished{U32.is_eq(raw, 1065353216)}
    case _: H.TimedOut{}

def outcome(status: U32, raw: U32) -> H.Outcome:
  outcome_index(U32.to_nat(status), raw)

def save_outcome(o: H.Outcome, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case H.Running{}: a
    case H.Finished{+success}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(H.raw(H.Finished{success}))
      a[(base + 11 : U32)] <- F32.bits(H.timed(H.Finished{success}, elapsed, deadline))
    case H.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(s: H.State, elapsed: U32, deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  H.S{document, expected, start, end_, o} = s
  a = a[(base + 2 : U32)] <- 0
  a = a[(base + 34 : U32)] <- U32.from_nat(start)
  a = a[(base + 35 : U32)] <- U32.from_nat(end_)
  save_outcome(o, elapsed, deadline, base, a)

def decoded_index(kind: Nat, begin: U32, finish: U32) -> H.Action:
  match kind:
    case 1n: H.Submit{}
    case 18n: H.Select{U32.to_nat(begin), U32.to_nat(finish)}
    case _: H.Wait{}

def after_expected(pair: Array<U32> & +List<U32>,
    document: +List<U32>, start: U32, end_: U32,
    kind: U32, arg0: U32, arg1: U32, status: U32,
    raw: U32, +elapsed: U32, +deadline: U32,
    +base: U32) -> Array<U32>:
  (a, expected) = pair
  s = {H.S{document, expected, U32.to_nat(start),
    U32.to_nat(end_), outcome(status, raw)} : H.State}
  save(H.step(s, decoded_index(U32.to_nat(kind), arg0, arg1),
    elapsed, deadline), elapsed, deadline, base, a)

def after_document(pair: Array<U32> & +List<U32>,
    expected_len: U32, start: U32, end_: U32,
    kind: U32, arg0: U32, arg1: U32, status: U32,
    raw: U32, elapsed: U32, deadline: U32,
    +base: U32) -> Array<U32>:
  (a, document) = pair
  after_expected(read_units(U32.to_nat(expected_len),
    (base + 2048 : U32), (a, Nil{})), document,
    start, end_, kind, arg0, arg1, status, raw,
    elapsed, deadline, base)

def metadata(pair: Array<U32> & +List<U32>,
    kind: U32, arg0: U32, arg1: U32, status: U32,
    raw: U32, elapsed: U32, deadline: U32,
    +base: U32) -> Array<U32>:
  (a, fields) = pair
  match fields:
    case Con{start, Con{end_, Con{document_len,
         Con{expected_len, Nil{}}}}}:
      after_document(read_units(U32.to_nat(document_len),
        (base + 512 : U32), (a, Nil{})), expected_len,
        start, end_, kind, arg0, arg1, status,
        raw, elapsed, deadline, base)
    case _: a

def header(pair: Array<U32> & +List<U32>,
    +base: U32) -> Array<U32>:
  (a, fields) = pair
  match fields:
    case Con{kind, Con{target, Con{arg0, Con{arg1,
         Con{elapsed, Con{status, Con{raw, Con{timed,
         Con{deadline, Nil{}}}}}}}}}}:
      metadata(read_units(4n, (base + 34 : U32),
        (a, Nil{})), kind, arg0, arg1, status,
        raw, elapsed, deadline, base)
    case _: a

def run(+base: U32, a: Array<U32>) -> Array<U32>:
  header(read_units(9n, (base + 4 : U32),
    (a, Nil{})), base)

def reset_seed(pair: Array<U32> & U32, task: U32,
    +base: U32) -> Array<U32>:
  (a, seed) = pair
  G.generate(task, seed, base, a)

def reset_task(pair: Array<U32> & U32, +base: U32) -> Array<U32>:
  (a, task) = pair
  reset_seed(a[(base + 3 : U32)], task, base)

def reset(+base: U32, a: Array<U32>) -> Array<U32>:
  reset_task(a[(base + 1 : U32)], base)
