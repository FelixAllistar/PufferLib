import Base
import ./Widgets.bend as W

# Version 1 independent seeded form generator. Matches upstream field ranges,
# alphabet and goal rules, not seedrandom's exact stream or browser layout.
type Entry is Data:
  E{label: String, target: Bool}

type Form is Data:
  F{radio: Bool, entries: +List<Entry>}

def mix(+x: U32) -> U32:
  +a = (x .^. (x >> 16n) : U32)
  +b = (a * 2146121005 : U32)
  +c = (b .^. (b >> 15n) : U32)
  +d = (c * 2221713035 : U32)
  (d .^. (d >> 16n) : U32)

def next(x: U32) -> U32:
  mix((x + 2654435769 : U32))

def letter(+x: U32) -> Char:
  Char.from_u32(Bool.pick(U32, U32.is_lt(x, 26), (65 + x : U32), Bool.pick(U32, U32.is_lt(x, 52), (97 + x - 26 : U32), (48 + x - 52 : U32))))

def text(n: Nat, +seed: U32) -> String:
  match n:
    case 0n: SNil{}
    case 1n+p: SCon{letter((seed % 62 : U32)), text(p, next(seed))}

def entries(n: Nat, +radio: Bool, +goal: U32, +index: U32, seed: U32) -> +List<Entry>:
  match n:
    case 0n: Nil{}
    case 1n+p:
      +r = next(seed)
      +r2 = next(r)
      E{text(U32.to_nat((2 + r % 6 : U32)), mix((r2 .^. 277803737 : U32))), Bool.pick(Bool, radio, U32.is_eq(index, goal), U32.is_eq((next(r2) % 2 : U32), 1))} <> entries(p, radio, goal, (index + 1 : U32), next(next(r2)))

def generate(+radio: Bool, seed: U32) -> Form:
  +r = next(seed)
  +count = (2 + r % 5 : U32)
  F{radio, entries(U32.to_nat(count), radio, (next(r) % count : U32), 0, next(next(r)))}

def nodes(es: +List<Entry>, +radio: Bool) -> +List<W.Node>:
  match es:
    case Nil{}: W.N{W.Button{}, False{}, False{}} <> Nil{}
    case Con{E{label, target}, rest}:
      W.N{Bool.pick(W.Role, radio, W.Radio{}, W.Checkbox{}), False{}, target} <> nodes(rest, radio)

def state(f: Form) -> W.State:
  F{+radio, es} = f
  W.S{Bool.pick(W.Task, radio, W.Options{}, W.Checkboxes{}), nodes(es, radio), 0, W.Running{}}

def requested(es: +List<Entry>, prefix: String) -> String:
  match es:
    case Nil{}: SNil{}
    case Con{E{label, False{}}, rest}: requested(rest, prefix)
    case Con{E{label, True{}}, rest}: prefix ++ label ++ requested(rest, ", ")

def nonempty(names: String) -> String:
  match names:
    case SNil{}: "nothing"
    case SCon{h, t}: SCon{h, t}

def instruction(f: Form) -> String:
  F{radio, es} = f
  "Select " ++ nonempty(requested(es, "")) ++ " and click Submit."

def count(f: Form) -> U32:
  F{radio, es} = f
  (U32.from_nat(List.length(&2, Entry, es)) + 1 : U32)

def all_clear(ns: +List<W.Node>) -> Bool:
  match ns:
    case Nil{}: True{}
    case Con{W.N{role, selected, target}, rest}: Bool.not(selected) && all_clear(rest)

def is_initial(s: W.State) -> Bool:
  match s:
    case W.S{task, nodes_, 0, W.Running{}}: all_clear(nodes_)
    case _: False{}
