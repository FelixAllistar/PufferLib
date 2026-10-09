import Base
import ./Value.bend as V

# Schema field indices stay fixed; row position never identifies an entity.
# Callers supply unique nonzero keys and a uniform schema per collection.
type Record is Data:
  R{key: U32, revision: U32, cells: +List<V.Value>}

type Result is Data:
  Applied{}
  Missing{}
  Conflict{}
  Invalid{}

type Write is Data:
  W{rows: +List<Record>, result: Result}

def key(row: Record) -> U32:
  R{id, version, cells} = row
  id

def revision(row: Record) -> U32:
  R{id, version, cells} = row
  version

def cell_at(cells: +List<V.Value>, field: Nat) -> V.Value:
  match cells field:
    case Nil{} _: V.Null{}
    case Con{cell, rest} 0n: cell
    case Con{cell, rest} 1n+p: cell_at(rest, p)

def cell(row: Record, field: Nat) -> V.Value:
  R{id, version, cells} = row
  cell_at(cells, field)

def pick(found: Bool, row: Record, later: Maybe<&2, Record>) -> Maybe<&2, Record>:
  match found:
    case True{}: Some{row}
    case False{}: later

def find(rows: +List<Record>, +id: U32) -> Maybe<&2, Record>:
  match rows:
    case Nil{}: None{}
    case Con{+row, rest}: pick(U32.is_eq(key(row), id), row, find(rest, id))

def replace(rows: +List<Record>, +id: U32, +updated: Record) -> +List<Record>:
  match rows:
    case Nil{}: Nil{}
    case Con{+row, rest}: Bool.pick(Record, U32.is_eq(key(row), id), updated, row) <> replace(rest, id, updated)

def checked_revision(matches: Bool, valid: Bool, rows: +List<Record>, +id: U32,
    version: U32, cells: +List<V.Value>) -> Write:
  match matches valid:
    case False{} _: W{rows, Conflict{}}
    case True{} False{}: W{rows, Invalid{}}
    case True{} True{}: W{replace(rows, id, R{id, (version + 1 : U32), cells}), Applied{}}

def checked_record(found: Maybe<&2, Record>, rows: +List<Record>, expected: U32,
    +cells: +List<V.Value>) -> Write:
  match found:
    case None{}: W{rows, Missing{}}
    case Some{R{id, +version, old}}:
      checked_revision(U32.is_eq(version, expected),
        U32.is_lt(version, 4294967295) && Nat.is_eq(List.length(&2, V.Value, cells), List.length(&2, V.Value, old)),
        rows, id, version, cells)

# Optimistic revision check makes a stale draft fail without partial mutation.
def update(+rows: +List<Record>, id: U32, expected: U32, cells: +List<V.Value>) -> Write:
  checked_record(find(rows, id), rows, expected, cells)

def remove(rows: +List<Record>, +id: U32) -> +List<Record>:
  match rows:
    case Nil{}: Nil{}
    case Con{+row, rest}: List.append(&2, Record,
      Bool.pick(+List<Record>, U32.is_eq(key(row), id), Nil{}, [row]), remove(rest, id))

def insert_if(absent: Bool, rows: +List<Record>, row: Record) -> Write:
  match absent:
    case False{}: W{rows, Conflict{}}
    case True{}: W{List.append(&2, Record, rows, [row]), Applied{}}

def insert_checked(found: Maybe<&2, Record>, rows: +List<Record>, row: Record) -> Write:
  match found:
    case None{}: insert_if(True{}, rows, row)
    case Some{old}: insert_if(False{}, rows, row)

def insert_valid(valid: Bool, +rows: +List<Record>, +row: Record) -> Write:
  match valid:
    case False{}: W{rows, Invalid{}}
    case True{}:
      insert_checked(find(rows, key(row)), rows, row)

def insert(+rows: +List<Record>, +row: Record) -> Write:
  insert_valid(U32.is_gt(key(row), 0) && U32.is_eq(revision(row), 0), rows, row)
