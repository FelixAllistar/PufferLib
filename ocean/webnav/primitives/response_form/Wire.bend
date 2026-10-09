import Base
import ./Model.bend as M
import ../finish/Model.bend as F
import ../finish/Wire.bend as FW
import ../text/Model.bend as E
import ../transport/Words.bend as W

# Private 65536-word form transport, with finish-compatible committed header.
# 0version,1phase,2kind,3status,4/5 final UTF8 lengths,8command;
# 16..19 candidate metadata;20kind,21status,22focus,23error;
# 24..26 data scalar length/start/end;27..29 detail scalar length/start/end;
# 30 insertion scalar count,31 argument. Committed UTF8 at64/16448;
# draft scalars18496/34880; candidate UTF8 36928/53312; insert scalars55360.
def focus(code: Nat) -> M.Focus:
  match code:
    case 1n: M.DataField{}
    case 2n: M.DetailField{}
    case _: M.None{}

def error(code: Nat) -> M.Error:
  match code:
    case 1n: M.InvalidData{}
    case 2n: M.InvalidDetail{}
    case _: M.Clean{}

def focus_code(value: M.Focus) -> U32:
  match value:
    case M.DataField{}: 1
    case M.DetailField{}: 2
    case _: 0

def error_code(value: M.Error) -> U32:
  match value:
    case M.InvalidData{}: 1
    case M.InvalidDetail{}: 2
    case _: 0

def command(code: Nat, argument: U32, payload: +List<U32>) -> M.Action:
  match code:
    case 2n: M.ChooseKind{FW.kind(U32.to_nat(argument))}
    case 3n: M.ChooseStatus{FW.status(U32.to_nat(argument))}
    case 4n: M.FocusData{}
    case 5n: M.FocusDetail{}
    case 6n: M.Editing{E.Insert{payload}}
    case 7n: M.Editing{E.Backspace{}}
    case 8n: M.Editing{E.Delete{}}
    case 9n: M.Editing{E.Left{}}
    case 10n: M.Editing{E.Right{}}
    case 11n: M.Editing{E.Home{}}
    case 12n: M.Editing{E.End{}}
    case 13n: M.Editing{E.SelectAll{}}
    case 15n: M.Reject{error(U32.to_nat(argument))}
    case 16n: M.Expire{}
    case 17n: M.Blur{}
    case _: M.Wait{}

def save_field(field: E.Field, +header: U32, at: U32, a: Array<U32>) -> Array<U32>:
  E.F{+units, start, end} = field
  a = a[header] <- U32.from_nat(List.length(&2, U32, units))
  a = a[(header + 1 : U32)] <- U32.from_nat(start)
  a = a[(header + 2 : U32)] <- U32.from_nat(end)
  W.write(units, at, a)

def save(s: M.State, a: Array<U32>) -> Array<U32>:
  M.S{completion, kind, status, data, detail, focused, problem} = s
  a = FW.clear_blocks(128n, 0, a)
  a = a[0] <- 1
  a = a[20] <- FW.kind_code(kind)
  a = a[21] <- FW.status_code(status)
  a = a[22] <- focus_code(focused)
  a = a[23] <- error_code(problem)
  FW.save_clean(completion, save_field(data, 24, 18496, save_field(detail, 27, 34880, a)))

def finish(s: M.State, pair: Array<U32> & F.Reply) -> Array<U32>:
  (a, reply) = pair
  F.R{kind, status, data, detail} = reply
  save(M.step(s, M.Finish{data, detail}), a)

def inserted(s: M.State, +header: +List<U32>, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, payload) = pair
  save(M.step(s, command(U32.to_nat(W.at(header, 8n)), W.at(header, 31n), payload)), a)

def apply(code: Nat, s: M.State, +header: +List<U32>, a: Array<U32>) -> Array<U32>:
  match code:
    case 14n: finish(s, FW.read_reply(header, 16n, 36928, 53312, a))
    case _: inserted(s, header, W.read(U32.to_nat(W.at(header, 30n)), 55360, (a, Nil{})))

def with_detail(completion: F.State, data: +List<U32>, +header: +List<U32>,
    pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, detail) = pair
  state = {M.S{completion, FW.kind(U32.to_nat(W.at(header, 20n))), FW.status(U32.to_nat(W.at(header, 21n))),
    E.F{data, U32.to_nat(W.at(header, 25n)), U32.to_nat(W.at(header, 26n))},
    E.F{detail, U32.to_nat(W.at(header, 28n)), U32.to_nat(W.at(header, 29n))},
    focus(U32.to_nat(W.at(header, 22n))), error(U32.to_nat(W.at(header, 23n)))} : M.State}
  apply(U32.to_nat(W.at(header, 8n)), state, header, a)

def with_data(completion: F.State, +header: +List<U32>, pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, data) = pair
  with_detail(completion, data, header, W.read(U32.to_nat(W.at(header, 27n)), 34880, (a, Nil{})))

def with_completion(completion: F.State, +header: +List<U32>, a: Array<U32>) -> Array<U32>:
  with_data(completion, header, W.read(U32.to_nat(W.at(header, 24n)), 18496, (a, Nil{})))

def with_reply(header: +List<U32>, pair: Array<U32> & F.Reply) -> Array<U32>:
  (a, reply) = pair
  with_completion(F.Submitted{reply}, header, a)

def load(phase: Nat, +header: +List<U32>, a: Array<U32>) -> Array<U32>:
  match phase:
    case 1n: with_reply(header, FW.read_reply(header, 2n, 64, 16448, a))
    case 2n: with_completion(F.Expired{}, header, a)
    case _: with_completion(F.Open{}, header, a)

def dispatch(code: Nat, +header: +List<U32>, a: Array<U32>) -> Array<U32>:
  match code:
    case 1n: save(M.initial(), a)
    case _: load(U32.to_nat(W.at(header, 1n)), header, a)

def loaded(pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, +header) = pair
  dispatch(U32.to_nat(W.at(header, 8n)), header, a)

def run(a: Array<U32>) -> Array<U32>:
  loaded(W.read(32n, 0, (a, Nil{})))
