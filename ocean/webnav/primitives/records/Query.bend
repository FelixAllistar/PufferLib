import Base
import ./Value.bend as V
import ./Model.bend as M

type Predicate is Data:
  All{}
  Equal{field: Nat, value: V.Value}
  Less{field: Nat, value: V.Value}
  Contains{field: Nat, value: V.Value}
  Prefix{field: Nat, value: V.Value}
  Not{term: Predicate}
  And{left: Predicate, right: Predicate}
  Or{left: Predicate, right: Predicate}

def matches(term: Predicate, +row: M.Record) -> Bool:
  match term:
    case All{}: True{}
    case Equal{field, value}: V.equal(M.cell(row, field), value)
    case Less{field, value}: V.less(M.cell(row, field), value)
    case Contains{field, value}: V.contains(M.cell(row, field), value)
    case Prefix{field, value}: V.prefix(M.cell(row, field), value)
    case Not{inner}: Bool.not(matches(inner, row))
    case And{left, right}: matches(left, row) && matches(right, row)
    case Or{left, right}: matches(left, row) || matches(right, row)

def keep(accepted: Bool, row: M.Record, rest: +List<M.Record>) -> +List<M.Record>:
  match accepted:
    case True{}: row <> rest
    case False{}: rest

def filter(rows: +List<M.Record>, +term: Predicate) -> +List<M.Record>:
  match rows:
    case Nil{}: Nil{}
    case Con{+row, rest}: keep(matches(term, row), row, filter(rest, term))

# A total public sort: requested cell order, then ascending persistent key.
# Ties are invariant to DOM/list reordering and page boundaries.
def ordered(comparison: Cmp, descending: Bool, tie: Bool) -> Bool:
  match comparison descending:
    case EQ{} _: tie
    case LT{} False{}: True{}
    case GT{} True{}: True{}
    case _ _: False{}

def before(+a: M.Record, +b: M.Record, +field: Nat, descending: Bool) -> Bool:
  ordered(V.compare(M.cell(a, field), M.cell(b, field)), descending, U32.is_le(M.key(a), M.key(b)))

def split_insert(first: Bool, row: M.Record, head: M.Record, tail: +List<M.Record>,
    later: +List<M.Record>) -> +List<M.Record>:
  match first:
    case True{}: row <> head <> tail
    case False{}: head <> later

def insert_sorted(rows: +List<M.Record>, +row: M.Record, +field: Nat, +descending: Bool) -> +List<M.Record>:
  match rows:
    case Nil{}: [row]
    case Con{+head, +tail}:
      split_insert(before(row, head, field, descending), row, head, tail,
        insert_sorted(tail, row, field, descending))

def sort(rows: +List<M.Record>, +field: Nat, +descending: Bool) -> +List<M.Record>:
  match rows:
    case Nil{}: Nil{}
    case Con{head, tail}: insert_sorted(sort(tail, field, descending), head, field, descending)

def page(rows: +List<M.Record>, offset: Nat, count: Nat) -> +List<M.Record>:
  List.take(&2, M.Record, List.drop(&2, M.Record, rows, offset), count)

def query(rows: +List<M.Record>, term: Predicate, field: Nat, descending: Bool,
    offset: Nat, count: Nat) -> +List<M.Record>:
  page(sort(filter(rows, term), field, descending), offset, count)
