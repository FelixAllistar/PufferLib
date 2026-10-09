import Base
import ./Model.bend as L
import ./Workspace.bend as M
import ../../primitives/browser/Model.bend as B
import ../../primitives/text/Model.bend as E
import ../../primitives/records/Value.bend as V
import ../../primitives/records/Model.bend as R
import ../../primitives/transaction/Model.bend as D

# Independent synthetic worlds, without benchmark instances or private answers.
# The odd stride permutes sixteen persistent keys independently of row order.
def mix(+x: U32) -> U32:
  +a = (x .^. (x >> 16n) : U32)
  +b = (a * 2146121005 : U32)
  +c = (b .^. (b >> 15n) : U32)
  +d = (c * 2221713035 : U32)
  (d .^. (d >> 16n) : U32)

def records(n: Nat, +index: U32, +seed: U32) -> +List<R.Record>:
  match n:
    case 0n: Nil{}
    case 1n+p:
      +stride = (1 + 2 * (mix((seed .^. 2654435769 : U32)) % 8 : U32) : U32)
      +id = (1 + (mix(seed) + index * stride : U32) % 16 : U32)
      +amount = (mix((seed + index * 37 + 2246822507 : U32)) % 1000000 : U32)
      status = (mix((seed + index * 71 + 3266489909 : U32)) % 3 : U32)
      R.R{id, 0, [V.Text{"Row " ++ U32.show(id)}, V.Integer{amount}, V.Integer{status}]} <>
        records(p, (index + 1 : U32), seed)

def initial(+seed: U32) -> M.State:
  count = (8 + mix((seed .^. 668265263 : U32)) % 9 : U32)
  latency = (25 + mix((seed .^. 374761393 : U32)) % 7 * 25 : U32)
  failures = (mix((seed .^. 1274126177 : U32)) % 3 : U32)
  M.S{L.S{B.S{0, 17, latency, failures, Nil{}, Nil{}, B.Ready{}},
    records(U32.to_nat(count), 0, seed), E.F{Nil{}, 0n, 0n}, "", 0, False{}, 0n, False{}},
    D.Closed{}, D.Clean{}, E.F{Nil{}, 0n, 0n}, False{}}
