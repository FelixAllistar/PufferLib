import Base
import ./Model.bend as M
import ../../primitives/browser/Wire.bend as B
import ../../primitives/text/Model.bend as E
import ../../primitives/text/Codec.bend as Text
import ../../primitives/records/Value.bend as V
import ../../primitives/records/Model.bend as R
import ../../primitives/transport/Words.bend as W

# Composition fixture: browser0..63; command64,arg65,delta66,rows67,
# descending68,filter69,page70,focus71,inputlen72,start73,end74,querylen75,
# payloadlen76, public match count77. Input96,applied query128,payload160:32 units each.
# Records192 stride4:key,revision,amount,status; public visible keys256/count260.
def load_rows(n: Nat, +at: Nat, +xs: +List<U32>) -> +List<R.Record>:
  match n:
    case 0n: Nil{}
    case 1n+p:
      R.R{W.at(xs, at), W.at(xs, (at + 1n : Nat)),
        [V.Text{"Row " ++ U32.show(W.at(xs, at))}, V.Integer{W.at(xs, (at + 2n : Nat))},
        V.Integer{W.at(xs, (at + 3n : Nat))}]} <> load_rows(p, (at + 4n : Nat), xs)

def slice(+xs: +List<U32>, at: Nat, length: Nat) -> +List<U32>:
  List.take(&2, U32, List.drop(&2, U32, xs, at), length)

def load(+xs: +List<U32>) -> M.State:
  M.S{B.load(xs), load_rows(U32.to_nat(W.at(xs, 67n)), 192n, xs),
    E.F{slice(xs, 96n, U32.to_nat(W.at(xs, 72n))), U32.to_nat(W.at(xs, 73n)), U32.to_nat(W.at(xs, 74n))},
    Text.text(slice(xs, 128n, U32.to_nat(W.at(xs, 75n)))), W.at(xs, 69n),
    U32.is_eq(W.at(xs, 68n), 1), U32.to_nat(W.at(xs, 70n)), U32.is_eq(W.at(xs, 71n), 1)}

def command(code: Nat, argument: U32, payload: +List<U32>) -> M.Action:
  match code:
    case 1n: M.Back{}
    case 2n: M.Forward{}
    case 3n: M.Retry{}
    case 4n: M.Focus{}
    case 5n: M.Editing{E.Insert{payload}}
    case 6n: M.Editing{E.SelectAll{}}
    case 7n: M.Editing{E.Backspace{}}
    case 8n: M.Search{}
    case 9n: M.Filter{argument}
    case 10n: M.Sort{U32.is_eq(argument, 1)}
    case 11n: M.Previous{}
    case 12n: M.Next{}
    case 13n: M.Open{argument}
    case _: M.Wait{}

def save_input(input: E.Field, a: Array<U32>) -> Array<U32>:
  E.F{+units, start, end} = input
  a = a[72] <- U32.from_nat(List.length(&2, U32, units))
  a = a[73] <- U32.from_nat(start)
  a = a[74] <- U32.from_nat(end)
  W.write(units, 96, W.clear(32n, 96, a))

def save_visible(rows: +List<R.Record>, +at: U32, a: Array<U32>) -> Array<U32>:
  match rows:
    case Nil{}: a
    case Con{row, rest}: save_visible(rest, (at + 1 : U32), a[at] <- R.key(row))

def save(s: M.State, +visible: +List<R.Record>, a: Array<U32>) -> Array<U32>:
  M.S{browser, rows, input, +query, status, descending, page, focused} = s
  a = a[68] <- Bool.to_u32(descending)
  a = a[69] <- status
  a = a[70] <- U32.from_nat(page)
  a = a[71] <- Bool.to_u32(focused)
  a = a[75] <- U32.from_nat(String.length(query))
  a = a[260] <- U32.from_nat(List.length(&2, R.Record, visible))
  a = W.write(Text.units(query), 128, W.clear(32n, 128, a))
  save_visible(visible, 256, W.clear(4n, 256, save_input(input, B.save(browser, 0, a))))

def save_state(+s: M.State, a: Array<U32>) -> Array<U32>:
  save(s, M.visible(s), a[77] <- M.matching_count(s))

def loaded(pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, +xs) = pair
  save_state(M.step(load(xs), command(U32.to_nat(W.at(xs, 64n)), W.at(xs, 65n),
    slice(xs, 160n, U32.to_nat(W.at(xs, 76n)))), W.at(xs, 66n)), a)

def run(a: Array<U32>) -> Array<U32>:
  loaded(W.read(256n, 0, (a, Nil{})))
