import Base
import ./Body.bend as B
import ./Generate.bend as G
import ../../miniwob/autocomplete/Model.bend as M
import ../../miniwob/training/TextGenerate.bend as T

# ABI-v2 header, 256-word legacy body at 32, public query at 320,
# at most 64 packed menu labels of 64 bytes at 512; 2048-word rows.
def write_menu(limit: Nat, items: +List<String>, +index: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match limit items:
    case 0n _: a
    case _ Nil{}: a
    case 1n+p Con{item, rest}:
      a = T.write_packed(16n, item, (base + 512 + index * 16 : U32), a)
      write_menu(p, rest, (index + 1 : U32), base, a)

def project_term(open: Bool, base: U32, pair: Array<U32> & +List<U32>) -> Array<U32>:
  match open:
    case False{}:
      (a, term) = pair
      a
    case True{}:
      (a, term) = pair
      write_menu(64n, M.menu_items_term(term), 0, base, a)

def term_size(menu: U32, +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, size) = pair
  project_term(Bool.not(U32.is_eq(menu, 0)), base,
    B.read_units(U32.to_nat(size), (base + 128 : U32), (a, Nil{})))

def menu_kind(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, menu) = pair
  term_size(menu, base, a[(base + 51 : U32)])

def publish_reward(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, reward) = pair
  a = a[(base + 11 : U32)] <- reward
  a = T.clear(1024n, (base + 512 : U32), a)
  menu_kind(base, a[(base + 47 : U32)])

def publish_raw(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, raw) = pair
  a = a[(base + 10 : U32)] <- raw
  publish_reward(base, a[(base + 42 : U32)])

def publish_elapsed(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, elapsed) = pair
  a = a[(base + 8 : U32)] <- elapsed
  publish_raw(base, a[(base + 41 : U32)])

def publish_status(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  a = a[(base + 9 : U32)] <- status
  a = a[(base + 2 : U32)] <- 0
  a = a[(base + 39 : U32)] <- 0
  a = a[(base + 40 : U32)] <- 0
  a = a[(base + 43 : U32)] <- 0
  publish_elapsed(base, a[(base + 38 : U32)])

def publish(+base: U32, a: Array<U32>) -> Array<U32>:
  publish_status(base, a[(base + 35 : U32)])

def dispatch(op: Nat, task: U32, seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match op:
    case 1n: G.generate(task, seed, base, a)
    case 2n: publish(base, B.row((base + 32 : U32), a))
    case _: a

def task_ready(op: U32, seed: U32, base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, task) = pair
  dispatch(U32.to_nat(op), task, seed, base, a)

def seed_ready(op: U32, +base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  task_ready(op, seed, base, a[(base + 1 : U32)])

def op_ready(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, op) = pair
  seed_ready(op, base, a[(base + 3 : U32)])

def row(+base: U32, a: Array<U32>) -> Array<U32>:
  op_ready(base, a[(base + 2 : U32)])

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: batch(p, row((U32.from_nat(p) * 2048 : U32), a))
