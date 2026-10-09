import Base
import ./Wire.bend as ListWire
import ./Model.bend as ListModel
import ./Workspace.bend as M
import ./Generate.bend as G
import ../../primitives/text/Model.bend as E
import ../../primitives/text/Number.bend as Number
import ../../primitives/records/Value.bend as V
import ../../primitives/records/Model.bend as R
import ../../primitives/transaction/Model.bend as D
import ../../primitives/transaction/Wire.bend as DraftWire
import ../../primitives/transport/Words.bend as W

# Extends the list fixture: 280 phase,281 key,282 revision,283 error,
# 284 editor length,285/286 selection,287 focus,288 original status;
# editor at320..351. New commands14 edit,15 focus,16 insert,17 selectall,
# 18 backspace,19 save,20 cancel.
def draft(phase: Nat, +id: U32, version: U32, status: U32, input: +List<U32>) -> D.Draft:
  match phase:
    case 1n: D.Editing{id, version, [V.Text{"Row " ++ U32.show(id)}, Number.field(input), V.Integer{status}]}
    case 2n: D.Committed{id, version}
    case 3n: D.Cancelled{}
    case _: D.Closed{}

def load(+xs: +List<U32>) -> M.State:
  +input = ListWire.slice(xs, 320n, U32.to_nat(W.at(xs, 284n)))
  M.S{ListWire.load(xs), draft(U32.to_nat(W.at(xs, 280n)), W.at(xs, 281n), W.at(xs, 282n), W.at(xs, 288n), input),
    DraftWire.error(U32.to_nat(W.at(xs, 283n))), E.F{input, U32.to_nat(W.at(xs, 285n)), U32.to_nat(W.at(xs, 286n))},
    U32.is_eq(W.at(xs, 287n), 1)}

def command(+code: Nat, argument: U32, payload: +List<U32>) -> M.Action:
  match code:
    case 14n: M.Edit{}
    case 15n: M.Focus{}
    case 16n: M.Editing{E.Insert{payload}}
    case 17n: M.Editing{E.SelectAll{}}
    case 18n: M.Editing{E.Backspace{}}
    case 19n: M.Save{}
    case 20n: M.Cancel{}
    case _: M.Browse{ListWire.command(code, argument, payload)}

def numeric(value: V.Value) -> U32:
  match value:
    case V.Integer{bits}: bits
    case _: 0

def save_rows(rows: +List<R.Record>, +at: U32, a: Array<U32>) -> Array<U32>:
  match rows:
    case Nil{}: a
    case Con{R.R{id, version, +cells}, rest}:
      a = a[at] <- id
      a = a[(at + 1 : U32)] <- version
      a = a[(at + 2 : U32)] <- numeric(R.cell_at(cells, 1n))
      a = a[(at + 3 : U32)] <- numeric(R.cell_at(cells, 2n))
      save_rows(rest, (at + 4 : U32), a)

def save_draft(stage: D.Draft, a: Array<U32>) -> Array<U32>:
  match stage:
    case D.Closed{}: a[280] <- 0
    case D.Cancelled{}: a[280] <- 3
    case D.Committed{id, version}:
      a = a[280] <- 2
      a = a[281] <- id
      a[282] <- version
    case D.Editing{id, version, cells}:
      a = a[280] <- 1
      a = a[281] <- id
      a = a[282] <- version
      a[288] <- numeric(R.cell_at(cells, 2n))

def save_input(input: E.Field, a: Array<U32>) -> Array<U32>:
  E.F{+units, start, end} = input
  a = a[284] <- U32.from_nat(List.length(&2, U32, units))
  a = a[285] <- U32.from_nat(start)
  a = a[286] <- U32.from_nat(end)
  W.write(units, 320, W.clear(32n, 320, a))

def save(s: M.State, a: Array<U32>) -> Array<U32>:
  M.S{+listing, stage, error, input, focused} = s
  a = a[67] <- U32.from_nat(List.length(&2, R.Record, M.rows(listing)))
  a = a[283] <- DraftWire.error_code(error)
  a = a[287] <- Bool.to_u32(focused)
  a = W.clear(3n, 280, a)
  a = a[288] <- 0
  save_draft(stage, save_input(input, save_rows(M.rows(listing), 192, ListWire.save_state(listing, a))))

def loaded(pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, +xs) = pair
  save(M.step(load(xs), command(U32.to_nat(W.at(xs, 64n)), W.at(xs, 65n),
    ListWire.slice(xs, 160n, U32.to_nat(W.at(xs, 76n)))), W.at(xs, 66n)), a)

def advance(a: Array<U32>) -> Array<U32>:
  loaded(W.read(352n, 0, (a, Nil{})))

def reset(pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  save(G.initial(seed), W.clear(512n, 0, a))

def dispatch(operation: Nat, a: Array<U32>) -> Array<U32>:
  match operation:
    case 1n: reset(a[13])
    case _: advance(a)

def with_operation(pair: Array<U32> & U32) -> Array<U32>:
  (a, operation) = pair
  dispatch(U32.to_nat(operation), a)

def run(a: Array<U32>) -> Array<U32>:
  with_operation(a[12])
