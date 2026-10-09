import Base
import ../records/Value.bend as V
import ../records/Model.bend as R
import ../records/Schema.bend as C

type Draft is Data:
  Closed{}
  Editing{key: U32, revision: U32, cells: +List<V.Value>}
  Committed{key: U32, revision: U32}
  Cancelled{}

type Error is Data:
  Clean{}
  Missing{}
  Conflict{}
  Invalid{}

type State is Data:
  S{rows: +List<R.Record>, draft: Draft, error: Error}

def open_record(result: Maybe<&2, R.Record>, rows: +List<R.Record>) -> State:
  match result:
    case None{}: S{rows, Closed{}, Missing{}}
    case Some{R.R{id, version, cells}}: S{rows, Editing{id, version, cells}, Clean{}}

def begin(s: State, id: U32) -> State:
  match s:
    case S{rows, Editing{key, revision, cells}, error}: s
    case S{+rows, draft, error}: open_record(R.find(rows, id), rows)

def replace_cell(cells: +List<V.Value>, index: Nat, value: V.Value) -> +List<V.Value>:
  match cells index:
    case Nil{} _: Nil{}
    case Con{old, rest} 0n: value <> rest
    case Con{old, rest} 1n+p: old <> replace_cell(rest, p, value)

def edit_allowed(allowed: Bool, s: State, index: Nat, value: V.Value) -> State:
  match allowed s:
    case True{} S{rows, Editing{id, version, cells}, error}:
      S{rows, Editing{id, version, replace_cell(cells, index, value)}, Clean{}}
    case False{} S{rows, draft, error}: S{rows, draft, Invalid{}}
    case True{} _: s

# Invalid values may remain visible in a draft for correction. Only commit
# validates the full schema. Read-only and absent fields reject edits.
def edit(s: State, fields: +List<C.Field>, +index: Nat, value: V.Value) -> State:
  edit_allowed(C.editable(fields, index), s, index, value)

def committed(result: R.Write, id: U32, +version: U32, cells: +List<V.Value>) -> State:
  match result:
    case R.W{rows, R.Applied{}}: S{rows, Committed{id, (version + 1 : U32)}, Clean{}}
    case R.W{rows, R.Missing{}}: S{rows, Editing{id, version, cells}, Missing{}}
    case R.W{rows, R.Conflict{}}: S{rows, Editing{id, version, cells}, Conflict{}}
    case R.W{rows, R.Invalid{}}: S{rows, Editing{id, version, cells}, Invalid{}}

def commit_valid(valid: Bool, s: State) -> State:
  match valid s:
    case False{} S{rows, draft, error}: S{rows, draft, Invalid{}}
    case True{} S{rows, Editing{+id, +version, +cells}, error}:
      committed(R.update(rows, id, version, cells), id, version, cells)
    case True{} _: s

def commit(s: State, fields: +List<C.Field>) -> State:
  match s:
    case S{rows, Editing{id, version, +cells}, error}:
      commit_valid(C.valid(fields, cells), S{rows, Editing{id, version, cells}, error})
    case _: s

def cancel(s: State) -> State:
  match s:
    case S{rows, Editing{id, version, cells}, error}: S{rows, Cancelled{}, Clean{}}
    case _: s

def persisted(s: State) -> +List<R.Record>:
  S{rows, draft, error} = s
  rows
