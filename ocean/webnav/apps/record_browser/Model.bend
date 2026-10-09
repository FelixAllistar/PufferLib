import Base
import ../../primitives/browser/Model.bend as B
import ../../primitives/text/Model.bend as E
import ../../primitives/text/Codec.bend as Text
import ../../primitives/records/Value.bend as V
import ../../primitives/records/Model.bend as R
import ../../primitives/records/Query.bend as Q

# First application recipe: name, amount, status. State/queries are composed
# from shared primitives; there is no benchmark task dispatch in this model.
# Route zero is the list; routes 1..16 are persistent record keys.
type State is Data:
  S{browser: B.State, rows: +List<R.Record>, input: E.Field, query: String,
    status_filter: U32, descending: Bool, page: Nat, focused: Bool}

type Action is Data:
  Wait{}
  Back{}
  Forward{}
  Retry{}
  Focus{}
  Editing{command: E.Edit}
  Search{}
  Filter{status: U32}
  Sort{descending: Bool}
  Previous{}
  Next{}
  Open{key: U32}

def route(browser: B.State) -> U32:
  B.S{current, count, latency, failures, back, forward, phase} = browser
  current

def ready(browser: B.State) -> Bool:
  match browser:
    case B.S{current, count, latency, failures, back, forward, B.Ready{}}: True{}
    case _: False{}

def status_term(filtered: Bool, status: U32) -> Q.Predicate:
  match filtered:
    case False{}: Q.All{}
    case True{}: Q.Equal{2n, V.Integer{(status - 1 : U32)}}

def matching(rows: +List<R.Record>, query: String, +status: U32) -> +List<R.Record>:
  Q.filter(rows, Q.And{Q.Contains{0n, V.Text{query}}, status_term(U32.is_gt(status, 0), status)})

def visible(+s: State) -> +List<R.Record>:
  S{browser, rows, input, query, status, descending, page, focused} = s
  Q.page(Q.sort(matching(rows, query, status), 1n, descending), (page * 4n : Nat), 4n)

def matching_count(s: State) -> U32:
  S{browser, rows, input, query, status, descending, page, focused} = s
  U32.from_nat(List.length(&2, R.Record, matching(rows, query, status)))

def exists(found: Maybe<&2, R.Record>) -> Bool:
  match found:
    case None{}: False{}
    case Some{row}: True{}

def open_if(valid: Bool, s: State, key: U32) -> State:
  match valid s:
    case False{} _: s
    case True{} S{browser, rows, input, query, status, descending, page, focused}:
      S{B.navigate(browser, key), rows, input, query, status, descending, page, False{}}

def open(+s: State, +key: U32) -> State:
  open_if(exists(R.find(visible(s), key)), s, key)

def change_page(valid: Bool, s: State, page: Nat) -> State:
  match valid s:
    case False{} _: s
    case True{} S{browser, rows, input, query, status, descending, old, focused}:
      S{browser, rows, input, query, status, descending, page, focused}

def next(+s: State) -> State:
  S{browser, rows, input, query, status, descending, +page, focused} = s
  change_page(Nat.is_lt(((page + 1n : Nat) * 4n : Nat), List.length(&2, R.Record, matching(rows, query, status))), s, (page + 1n : Nat))

def edit_if(focused: Bool, s: State, command: E.Edit) -> State:
  match focused s:
    case False{} _: s
    case True{} S{browser, rows, input, query, status, descending, page, old_focus}:
      S{browser, rows, E.edit(command, input), query, status, descending, page, True{}}

def filter_if(valid: Bool, s: State, status: U32) -> State:
  match valid s:
    case False{} _: s
    case True{} S{browser, rows, input, query, old, descending, page, focused}:
      S{browser, rows, input, query, status, descending, 0n, focused}

def on_list(action: Action, s: State) -> State:
  match action s:
    case Focus{} S{browser, rows, input, query, status, descending, page, focused}:
      S{browser, rows, E.end(input), query, status, descending, page, True{}}
    case Editing{command} S{browser, rows, input, query, status, descending, page, +focused}:
      edit_if(focused, S{browser, rows, input, query, status, descending, page, focused}, command)
    case Search{} S{browser, rows, +input, query, status, descending, page, focused}:
      S{browser, rows, input, Text.text(E.value(input)), status, descending, 0n, False{}}
    case Filter{+tag} _: filter_if(U32.is_le(tag, 3), s, tag)
    case Sort{order} S{browser, rows, input, query, status, descending, page, focused}:
      S{browser, rows, input, query, status, order, 0n, focused}
    case Previous{} S{browser, rows, input, query, status, descending, page, focused}:
      S{browser, rows, input, query, status, descending, E.pred(page), focused}
    case Next{} _: next(s)
    case Open{key} _: open(s, key)
    case _ _: s

def list_if(active: Bool, action: Action, s: State) -> State:
  match active:
    case False{}: s
    case True{}: on_list(action, s)

def act(action: Action, s: State) -> State:
  match action s:
    case Back{} S{browser, rows, input, query, status, descending, page, focused}:
      S{B.go_back(browser), rows, input, query, status, descending, page, False{}}
    case Forward{} S{browser, rows, input, query, status, descending, page, focused}:
      S{B.go_forward(browser), rows, input, query, status, descending, page, False{}}
    case Retry{} S{browser, rows, input, query, status, descending, page, focused}:
      S{B.retry(browser), rows, input, query, status, descending, page, focused}
    case _ S{+browser, rows, input, query, status, descending, page, focused}:
      list_if(ready(browser) && U32.is_eq(route(browser), 0), action,
        S{browser, rows, input, query, status, descending, page, focused})

def step(s: State, action: Action, elapsed_ms: U32) -> State:
  S{browser, rows, input, query, status, descending, page, focused} = s
  act(action, S{B.advance(browser, elapsed_ms), rows, input, query, status, descending, page, focused})
