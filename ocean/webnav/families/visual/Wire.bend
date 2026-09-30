import Base
import ./Model.bend as M
import ./Generate.bend as G
import ./PieGenerate.bend as PG
import ./ShadesGenerate.bend as SG
import ./PieWire.bend as PW
import ./ShadesWire.bend as SW
import ./ShapeGenerate.bend as SHG
import ./ShapeWire.bend as SHW
import ./ChoiceGenerate.bend as CG
import ./ChoiceWire.bend as CW
import ./AdditionGenerate.bend as AG
import ./AdditionWire.bend as AW

def prepend(pair: Array<U32> & U32,
    xs: +List<U32>) -> Array<U32> & +List<U32>:
  (a, value) = pair
  (a, value <> xs)

def read_units(n: Nat, +at: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, xs) = pair
      read_units(p, at,
        prepend(a[(at + U32.from_nat(p) : U32)], xs))

def read_words(n: Nat, base: U32, offset: U32,
    xs: +List<U32>, a: Array<U32>) -> Array<U32> & +List<U32>:
  read_units(n, (base + offset : U32), (a, xs))

def action_index(kind: Nat, +target: U32) -> M.Action:
  match kind:
    case 1n: M.Click{Bool.pick(U32, U32.is_eq(target, 1),
      0, (target - 9 : U32))}
    case _: M.Wait{}

def save_outcome(result: M.Outcome, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match result:
    case M.Running{}: a
    case M.Finished{+success}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(M.raw(M.Finished{success}))
      a[(base + 11 : U32)] <- F32.bits(M.timed(M.Finished{success}, elapsed, deadline))
    case M.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(state: M.State, elapsed: U32, deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  M.S{ca, cb, cc, cd, expected, swatch, result} = state
  a[(base + 2 : U32)] <- 0
  save_outcome(result, elapsed, deadline, base, a)

def run_fields(+base: U32, kind: U32, target: U32,
    +elapsed: U32, +deadline: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, fields) = pair
  match fields:
    case Con{ca, Con{cb, Con{cc, Con{cd,
         Con{expected, Con{swatch, Nil{}}}}}}}:
      state = {M.S{ca, cb, cc, cd, expected,
        U32.is_eq(swatch, 1), M.Running{}} : M.State}
      save(M.step(state, action_index(U32.to_nat(kind), target),
        elapsed, deadline), elapsed, deadline, base, a)
    case _: a

def run_deadline(+base: U32, kind: U32, target: U32,
    elapsed: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, deadline) = pair
  run_fields(base, kind, target, elapsed, deadline,
    read_words(6n, base, 32, Nil{}, a))

def run_header(+base: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, header) = pair
  match header:
    case Con{kind, Con{target, Con{arg0, Con{arg1,
         Con{elapsed, Nil{}}}}}}:
      run_deadline(base, kind, target, elapsed, a[(base + 12 : U32)])
    case _: a

def active_status(status: Nat, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match status:
    case 0n: run_header(base, read_words(5n, base, 4, Nil{}, a))
    case _: a[(base + 2 : U32)] <- 0

def load_status(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, status) = pair
  active_status(U32.to_nat(status), base, a)

def step(+base: U32, a: Array<U32>) -> Array<U32>:
  load_status(base, a[(base + 9 : U32)])

def generated(which: Nat, task: U32, seed: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match which:
    case 0n: G.generate(task, seed, base, a)
    case 1n: PG.generate(task, seed, base, a)
    case 2n: PG.generate(task, seed, base, a)
    case 3n: SG.generate(task, seed, base, a)
    case 4n: SHG.generate(task, seed, base, a)
    case 5n: SHG.generate(task, seed, base, a)
    case 6n: CG.sides_generate(task, seed, base, a)
    case 7n: CG.identify_generate(task, seed, base, a)
    case 8n: AG.generate(task, seed, base, a)
    case _: a

def reset_seed(+base: U32, +task: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, seed) = pair
  generated(U32.to_nat(task), task, seed, base, a)

def task_step(which: Nat, task: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match which:
    case 0n: step(base, a)
    case 1n: PW.step(base, task, a)
    case 2n: PW.step(base, task, a)
    case 3n: SW.step(base, a)
    case 4n: SHW.step(base, a)
    case 5n: SHW.step(base, a)
    case 6n: CW.step(base, which, a)
    case 7n: CW.step(base, which, a)
    case 8n: AW.step(base, a)
    case _: a

def run_task(+base: U32, +task: U32,
    a: Array<U32>) -> Array<U32>:
  task_step(U32.to_nat(task), task, base, a)

def operation_index(op: Nat, +base: U32, task: U32,
    a: Array<U32>) -> Array<U32>:
  match op:
    case 0n: a
    case 1n: reset_seed(base, task, a[(base + 3 : U32)])
    case _: run_task(base, task, a)

def operation(op: U32, +base: U32, task: U32,
    a: Array<U32>) -> Array<U32>:
  operation_index(U32.to_nat(op), base, task, a)

def load_task(+base: U32, op: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a, task) = pair
  operation(op, base, task, a)

def row(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, op) = pair
  load_task(base, op, a[(base + 1 : U32)])

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +base = (U32.from_nat(p) * 2048 : U32)
      batch(p, row(base, a[(base + 2 : U32)]))
