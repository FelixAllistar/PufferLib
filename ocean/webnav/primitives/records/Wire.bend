import Base
import ./Value.bend as V
import ./Model.bend as M
import ./Query.bend as Q
import ../transport/Words.bend as W

# Numeric fixture transport for core qualification, not the application ABI.
# 0 operation(1=query,2=update),1 rows,2 predicate(0=all,1=equal,2=less),
# 3 comparison,4 descending,5 offset,6 page size,7 key,8 expected revision,
# 9 replacement value,10 write result,11 output count; rows at32 stride3;
# query outputs at128 stride3. Up to16 rows: key,revision,signed integer bits.
def load_rows(n: Nat, +at: Nat, +xs: +List<U32>) -> +List<M.Record>:
  match n:
    case 0n: Nil{}
    case 1n+p:
      M.R{W.at(xs, at), W.at(xs, (at + 1n : Nat)), [V.Integer{W.at(xs, (at + 2n : Nat))}]} <>
        load_rows(p, (at + 3n : Nat), xs)

def integer(value: V.Value) -> U32:
  match value:
    case V.Integer{bits}: bits
    case _: 0

def save_rows(rows: +List<M.Record>, +at: U32, a: Array<U32>) -> Array<U32>:
  match rows:
    case Nil{}: a
    case Con{M.R{id, version, cells}, rest}:
      a = a[at] <- id
      a = a[(at + 1 : U32)] <- version
      a = a[(at + 2 : U32)] <- integer(M.cell_at(cells, 0n))
      save_rows(rest, (at + 3 : U32), a)

def predicate(code: Nat, value: U32) -> Q.Predicate:
  match code:
    case 1n: Q.Equal{0n, V.Integer{value}}
    case 2n: Q.Less{0n, V.Integer{value}}
    case _: Q.All{}

def result_code(result: M.Result) -> U32:
  match result:
    case M.Applied{}: 0
    case M.Missing{}: 1
    case M.Conflict{}: 2
    case M.Invalid{}: 3

def save_write(write: M.Write, a: Array<U32>) -> Array<U32>:
  M.W{rows, result} = write
  a = a[10] <- result_code(result)
  save_rows(rows, 32, a)

def save_query(+rows: +List<M.Record>, a: Array<U32>) -> Array<U32>:
  a = a[11] <- U32.from_nat(List.length(&2, M.Record, rows))
  save_rows(rows, 128, W.clear(48n, 128, a))

def dispatch(operation: Nat, rows: +List<M.Record>, +xs: +List<U32>, a: Array<U32>) -> Array<U32>:
  match operation:
    case 1n:
      save_query(Q.query(rows, predicate(U32.to_nat(W.at(xs, 2n)), W.at(xs, 3n)),
        0n, U32.is_eq(W.at(xs, 4n), 1), U32.to_nat(W.at(xs, 5n)), U32.to_nat(W.at(xs, 6n))), a)
    case 2n:
      save_write(M.update(rows, W.at(xs, 7n), W.at(xs, 8n), [V.Integer{W.at(xs, 9n)}]), a)
    case _: a

def loaded(pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, +xs) = pair
  dispatch(U32.to_nat(W.at(xs, 0n)), load_rows(U32.to_nat(W.at(xs, 1n)), 32n, xs), xs, a)

def run(a: Array<U32>) -> Array<U32>:
  loaded(W.read(80n, 0, (a, Nil{})))
