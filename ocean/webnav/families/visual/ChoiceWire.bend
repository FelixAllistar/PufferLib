import Base
import ./ChoiceModel.bend as M

def prepend(pair: Array<U32> & U32,
    xs: +List<U32>) -> Array<U32> & +List<U32>:
  (a,value)=pair
  (a,value <> xs)

def read_units(n: Nat,+at: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a,xs)=pair
      read_units(p,at,prepend(a[(at + U32.from_nat(p) : U32)],xs))

def read_words(n: Nat,base: U32,offset: U32,
    xs: +List<U32>,a: Array<U32>) -> Array<U32> & +List<U32>:
  read_units(n,(base + offset : U32),(a,xs))

def choice(task: Nat,target: U32) -> U32:
  match task:
    case 6n: (target - 7 : U32)
    case _: (target - 10 : U32)

def action(kind: Nat,task: Nat,target: U32) -> M.Action:
  match kind:
    case 1n: M.Choose{choice(task,target)}
    case _: M.Wait{}

def save_outcome(result: M.Outcome,+elapsed: U32,+deadline: U32,
    +base: U32,a: Array<U32>) -> Array<U32>:
  match result:
    case M.Running{}: a
    case M.Finished{+success}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(M.raw(M.Finished{success}))
      a[(base + 11 : U32)] <- F32.bits(M.timed(M.Finished{success},elapsed,deadline))
    case M.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(state: M.State,elapsed: U32,deadline: U32,
    +base: U32,a: Array<U32>) -> Array<U32>:
  M.S{goal,result}=state
  a[(base + 2 : U32)] <- 0
  save_outcome(result,elapsed,deadline,base,a)

def run_goal(+base: U32,task: Nat,kind: Nat,target: U32,
    +elapsed: U32,+deadline: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a,goal)=pair
  save(M.step(M.S{goal,M.Running{}},action(kind,task,target),
    elapsed,deadline),elapsed,deadline,base,a)

def run_deadline(+base: U32,task: Nat,kind: Nat,target: U32,
    elapsed: U32,pair: Array<U32> & U32) -> Array<U32>:
  (a,deadline)=pair
  run_goal(base,task,kind,target,elapsed,deadline,
    a[(base + 32 : U32)])

def run_header(+base: U32,task: Nat,
    pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a,header)=pair
  match header:
    case Con{kind,Con{target,Con{arg0,Con{arg1,Con{elapsed,Nil{}}}}}}:
      run_deadline(base,task,U32.to_nat(kind),target,elapsed,
        a[(base + 12 : U32)])
    case _: a

def active_status(status: Nat,+base: U32,task: Nat,
    a: Array<U32>) -> Array<U32>:
  match status:
    case 0n: run_header(base,task,read_words(5n,base,4,Nil{},a))
    case _: a[(base + 2 : U32)] <- 0

def load_status(+base: U32,task: Nat,
    pair: Array<U32> & U32) -> Array<U32>:
  (a,status)=pair
  active_status(U32.to_nat(status),base,task,a)

def step(+base: U32,task: Nat,a: Array<U32>) -> Array<U32>:
  load_status(base,task,a[(base + 9 : U32)])
