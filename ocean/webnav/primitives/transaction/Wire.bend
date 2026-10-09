import Base
import ./Model.bend as M
import ../records/Schema.bend as C
import ../records/Value.bend as V
import ../records/Model.bend as Store
import ../records/Wire.bend as R
import ../transport/Words.bend as W

# Numeric conformance fixture, not an application ABI. Record layout matches
# records/Wire; 0 command(1begin,2edit,3commit,4cancel),7 begin key,9 edit value;
# 12 draft phase,13 key,14 revision,15 draft value,16 public error;
# 18/19 signed min/max,20 field editable,21 edit index.
def draft(code: Nat, id: U32, version: U32, value: U32) -> M.Draft:
  match code:
    case 1n: M.Editing{id, version, [V.Integer{value}]}
    case 2n: M.Committed{id, version}
    case 3n: M.Cancelled{}
    case _: M.Closed{}

def error(code: Nat) -> M.Error:
  match code:
    case 1n: M.Missing{}
    case 2n: M.Conflict{}
    case 3n: M.Invalid{}
    case _: M.Clean{}

def error_code(result: M.Error) -> U32:
  match result:
    case M.Missing{}: 1
    case M.Conflict{}: 2
    case M.Invalid{}: 3
    case M.Clean{}: 0

def save_draft(stage: M.Draft, a: Array<U32>) -> Array<U32>:
  match stage:
    case M.Closed{}: a[12] <- 0
    case M.Cancelled{}: a[12] <- 3
    case M.Committed{id, version}:
      a = a[12] <- 2
      a = a[13] <- id
      a[14] <- version
    case M.Editing{id, version, cells}:
      a = a[12] <- 1
      a = a[13] <- id
      a = a[14] <- version
      a[15] <- R.integer(Store.cell_at(cells, 0n))

def save(s: M.State, a: Array<U32>) -> Array<U32>:
  M.S{rows, stage, result} = s
  a = a[16] <- error_code(result)
  save_draft(stage, W.clear(4n, 12, R.save_rows(rows, 32, a)))

def dispatch(command: Nat, s: M.State, +xs: +List<U32>) -> M.State:
  match command:
    case 1n: M.begin(s, W.at(xs, 7n))
    case 2n: M.edit(s, [C.F{C.Integer{W.at(xs, 18n), W.at(xs, 19n)}, U32.is_eq(W.at(xs, 20n), 1)}],
      U32.to_nat(W.at(xs, 21n)), V.Integer{W.at(xs, 9n)})
    case 3n: M.commit(s, [C.F{C.Integer{W.at(xs, 18n), W.at(xs, 19n)}, U32.is_eq(W.at(xs, 20n), 1)}])
    case 4n: M.cancel(s)
    case _: s

def loaded(pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, +xs) = pair
  s = {M.S{R.load_rows(U32.to_nat(W.at(xs, 1n)), 32n, xs),
    draft(U32.to_nat(W.at(xs, 12n)), W.at(xs, 13n), W.at(xs, 14n), W.at(xs, 15n)),
    error(U32.to_nat(W.at(xs, 16n)))} : M.State}
  save(dispatch(U32.to_nat(W.at(xs, 0n)), s, xs), a)

def run(a: Array<U32>) -> Array<U32>:
  loaded(W.read(80n, 0, (a, Nil{})))
