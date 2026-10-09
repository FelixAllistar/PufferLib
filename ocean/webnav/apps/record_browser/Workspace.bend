import Base
import ./Model.bend as M
import ../../primitives/text/Model.bend as E
import ../../primitives/text/Codec.bend as Text
import ../../primitives/text/Number.bend as Number
import ../../primitives/records/Value.bend as V
import ../../primitives/records/Model.bend as R
import ../../primitives/records/Schema.bend as C
import ../../primitives/transaction/Model.bend as D

# Edit form is a modal over the detail page. Its draft persists until explicit
# save/cancel; normal page controls are disabled while the modal is open.
type State is Data:
  S{listing: M.State, draft: D.Draft, error: D.Error, input: E.Field, focused: Bool}

type Action is Data:
  Browse{action: M.Action}
  Edit{}
  Focus{}
  Editing{command: E.Edit}
  Save{}
  Cancel{}

def schema() -> +List<C.Field>:
  [C.F{C.Text{24n, True{}}, False{}}, C.F{C.Integer{0, 999999}, True{}},
    C.F{C.Integer{0, 2}, False{}}]

def rows(listing: M.State) -> +List<R.Record>:
  M.S{browser, records, input, query, status, descending, page, focused} = listing
  records

def set_rows(listing: M.State, records: +List<R.Record>) -> M.State:
  M.S{browser, old, input, query, status, descending, page, focused} = listing
  M.S{browser, records, input, query, status, descending, page, focused}

def opened(draft: D.Draft) -> Bool:
  match draft:
    case D.Editing{id, version, cells}: True{}
    case _: False{}

def initial_text(value: V.Value) -> +List<U32>:
  match value:
    case V.Integer{bits}: Text.units(U32.show(bits))
    case V.Text{text}: Text.units(text)
    case _: Nil{}

def begin_result(transaction: D.State, listing: M.State) -> State:
  match transaction:
    case D.S{records, D.Editing{id, version, +cells}, error}:
      S{set_rows(listing, records), D.Editing{id, version, cells}, error,
        E.end(E.F{initial_text(R.cell_at(cells, 1n)), 0n, 0n}), True{}}
    case D.S{records, draft, error}: S{set_rows(listing, records), draft, error, E.F{Nil{}, 0n, 0n}, False{}}

def begin_if(valid: Bool, s: State, id: U32) -> State:
  match valid s:
    case False{} _: s
    case True{} S{+listing, draft, error, input, focused}:
      begin_result(D.begin(D.S{rows(listing), draft, error}, id), listing)

def begin(+s: State) -> State:
  S{M.S{+browser, records, input, query, status, descending, page, focused}, +draft, error, editor, edit_focus} = s
  begin_if(M.ready(browser) && U32.is_gt(M.route(browser), 0) && Bool.not(opened(draft)), s, M.route(browser))

def sync(transaction: D.State, listing: M.State, input: E.Field) -> State:
  D.S{records, +draft, error} = transaction
  S{set_rows(listing, records), draft, error, input, opened(draft)}

def edited(+input: E.Field, +listing: M.State, draft: D.Draft, error: D.Error) -> State:
  sync(D.edit(D.S{rows(listing), draft, error}, schema(), 1n, Number.field(E.value(input))), listing, input)

def edit_if(active: Bool, s: State, command: E.Edit) -> State:
  match active s:
    case False{} _: s
    case True{} S{listing, draft, error, input, focused}:
      edited(E.edit(command, input), listing, draft, error)

def act(action: Action, s: State) -> State:
  match action s:
    case Edit{} _: begin(s)
    case Focus{} S{listing, +draft, error, input, focused}:
      S{listing, draft, error, E.end(input), opened(draft)}
    case Editing{command} S{listing, +draft, error, input, +focused}:
      edit_if(focused && opened(draft), S{listing, draft, error, input, focused}, command)
    case Save{} S{+listing, draft, error, input, focused}:
      sync(D.commit(D.S{rows(listing), draft, error}, schema()), listing, input)
    case Cancel{} S{+listing, draft, error, input, focused}:
      sync(D.cancel(D.S{rows(listing), draft, error}), listing, E.F{Nil{}, 0n, 0n})
    case Browse{command} S{listing, D.Editing{id, version, cells}, error, input, focused}: s
    case Browse{command} S{listing, draft, error, input, focused}:
      S{M.act(command, listing), draft, error, input, focused}

def step(s: State, action: Action, elapsed_ms: U32) -> State:
  S{listing, draft, error, input, focused} = s
  act(action, S{M.step(listing, M.Wait{}, elapsed_ms), draft, error, input, focused})
