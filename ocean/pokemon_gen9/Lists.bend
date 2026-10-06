import Base

def pick(yes: Bool, left: U32, right: U32) -> U32:
  match yes:
    case True{}: left
    case False{}: right

def pick_list(yes: Bool, left: +List<U32>, right: +List<U32>) -> +List<U32>:
  match yes:
    case True{}: left
    case False{}: right

def at(xs: +List<U32>, index: Nat) -> U32:
  match xs:
    case Nil{}: 0
    case Con{head, tail}:
      match index:
        case 0n: head
        case 1n+p: at(tail, p)

def length(xs: +List<U32>) -> U32:
  match xs:
    case Nil{}: 0
    case Con{_, tail}: (1 + length(tail) : U32)

def contains(xs: +List<U32>, +value: U32) -> Bool:
  match xs:
    case Nil{}: False{}
    case Con{head, tail}: U32.is_eq(head, value) || contains(tail, value)

def append(xs: +List<U32>, ys: +List<U32>) -> +List<U32>:
  match xs:
    case Nil{}: ys
    case Con{head, tail}: head <> append(tail, ys)

def last(xs: +List<U32>) -> U32:
  match xs:
    case Nil{}: 0
    case Con{head, Nil{}}: head
    case Con{_, tail}: last(tail)

def replace(xs: +List<U32>, index: Nat, +value: U32) -> +List<U32>:
  match xs:
    case Nil{}: Nil{}
    case Con{head, tail}:
      match index:
        case 0n: value <> tail
        case 1n+p: head <> replace(tail, p, value)

def index(xs: +List<U32>, +value: U32, +pos: U32) -> U32:
  match xs:
    case Nil{}: 4294967295
    case Con{head, tail}: pick(U32.is_eq(head, value), pos, index(tail, value, (pos + 1 : U32)))

# TS fastPop replaces the removed member with the old last member.
def pop_at(xs: +List<U32>, +index_: U32, +last_: U32, +pos: U32) -> +List<U32>:
  match xs:
    case Nil{}: Nil{}
    case Con{_, Nil{}}: Nil{}
    case Con{head, tail}:
      pick(U32.is_eq(index_, pos), last_, head) <> pop_at(tail, index_, last_, (pos + 1 : U32))

def pop(+xs: +List<U32>, index_: U32) -> +List<U32>:
  pop_at(xs, index_, last(xs), 0)

def remove(+xs: +List<U32>, value: U32) -> +List<U32>:
  pop(xs, index(xs, value, 0))

def keep_member(yes: Bool, head: U32, rest: +List<U32>) -> +List<U32>:
  match yes:
    case True{}: head <> rest
    case False{}: rest

def filter_in(xs: +List<U32>, +allowed: +List<U32>) -> +List<U32>:
  match xs:
    case Nil{}: Nil{}
    case Con{+head, tail}:
      keep_member(contains(allowed, head), head, filter_in(tail, allowed))

def filter_out(xs: +List<U32>, +excluded: +List<U32>) -> +List<U32>:
  match xs:
    case Nil{}: Nil{}
    case Con{+head, tail}:
      keep_member(Bool.not(contains(excluded, head)), head, filter_out(tail, excluded))

def increment(+xs: +List<U32>, +index_: U32) -> +List<U32>:
  replace(xs, U32.to_nat(index_), (at(xs, U32.to_nat(index_)) + 1 : U32))
