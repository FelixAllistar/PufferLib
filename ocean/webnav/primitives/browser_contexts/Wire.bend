import Base
import ./Model.bend as M
import ../browser/Wire.bend as B

# Private 2048-word transport. Header: 0 version, 1 tab count, 2 active identity,
# 3 next identity, 4 capacity, 5 route count, 6 seed. Commands: 8 code, 9 argument,
# 10 foreground, 11 elapsed, 12 operation, 13 reset seed, 14 routes, 15 capacity.
# Sixteen 64-word tab slots start at 64. Each embeds the checked browser row in
# words 0..31 and its tab identity at 32. All padding/staging clears on output.

def clear_blocks(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      clear_blocks(p, B.clear(64n, (U32.from_nat(p) * 64 : U32), a))

def save_tabs(tabs: +List<M.Tab>, +at: U32, a: Array<U32>) -> Array<U32>:
  match tabs:
    case Nil{}: a
    case Con{M.T{id, browser}, rest}:
      a = B.save(browser, at, a)
      save_tabs(rest, (at + 64 : U32), a[(at + 32 : U32)] <- id)

def save(s: M.State, a: Array<U32>) -> Array<U32>:
  M.S{+tabs, active, next, capacity, count, seed} = s
  a = clear_blocks(32n, a)
  a = a[0] <- 1
  a = a[1] <- U32.from_nat(List.length(&2, M.Tab, tabs))
  a = a[2] <- active
  a = a[3] <- next
  a = a[4] <- capacity
  a = a[5] <- count
  a = a[6] <- seed
  save_tabs(tabs, 64, a)

def prepend_tab(pair: Array<U32> & +List<U32>, tabs: +List<M.Tab>) -> Array<U32> & +List<M.Tab>:
  (a, +xs) = pair
  (a, M.T{B.word(xs, 32n), B.load(xs)} <> tabs)

def read_tabs(n: Nat, pair: Array<U32> & +List<M.Tab>) -> Array<U32> & +List<M.Tab>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, tabs) = pair
      read_tabs(p, prepend_tab(B.read_words(33n, (64 + U32.from_nat(p) * 64 : U32), (a, Nil{})), tabs))

def command(code: Nat, argument: U32, foreground: U32) -> M.Action:
  match code:
    case 0n: M.Wait{}
    case 6n: M.Open{argument, U32.is_eq(foreground, 1)}
    case 7n: M.Switch{argument}
    case 8n: M.Close{argument}
    case _: M.Browser{B.command(code, argument)}

def loaded_tabs(+xs: +List<U32>, pair: Array<U32> & +List<M.Tab>) -> Array<U32>:
  (a, tabs) = pair
  s = {M.S{tabs, B.word(xs, 2n), B.word(xs, 3n), B.word(xs, 4n), B.word(xs, 5n), B.word(xs, 6n)} : M.State}
  save(M.step(s, command(U32.to_nat(B.word(xs, 8n)), B.word(xs, 9n), B.word(xs, 10n)), B.word(xs, 11n)), a)

def dispatch(operation: Nat, +xs: +List<U32>, a: Array<U32>) -> Array<U32>:
  match operation:
    case 1n: save(M.initial(B.word(xs, 13n), B.word(xs, 14n), B.word(xs, 15n)), a)
    case 2n: loaded_tabs(xs, read_tabs(U32.to_nat(B.word(xs, 1n)), (a, Nil{})))
    case _: a

def loaded_headers(pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, +xs) = pair
  dispatch(U32.to_nat(B.word(xs, 12n)), xs, a)

def run(a: Array<U32>) -> Array<U32>:
  loaded_headers(B.read_words(16n, 0, (a, Nil{})))
