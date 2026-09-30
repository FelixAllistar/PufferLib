import Base
import ./ShapeModel.bend as M

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

def read_words(n: Nat,+base: U32,+offset: U32,
    xs: +List<U32>,a: Array<U32>) -> Array<U32> & +List<U32>:
  read_units(n,(base + offset : U32),(a,xs))

def shape_words(pair: Array<U32> & +List<U32>) ->
    Array<U32> & M.Shape:
  (a,fields)=pair
  match fields:
    case Con{x,Con{y,Con{color,Con{size,
         Con{kind,Con{glyph,Nil{}}}}}}}:
      (a,M.Item{x,y,color,size,kind,glyph})
    case _: (a,M.Item{0,0,0,0,1,65})

def prepend_shape(pair: Array<U32> & M.Shape,
    xs: +List<M.Shape>) -> Array<U32> & +List<M.Shape>:
  (a,item)=pair
  (a,item <> xs)

def read_shapes(n: Nat,+base: U32,
    pair: Array<U32> & +List<M.Shape>) ->
    Array<U32> & +List<M.Shape>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a,xs)=pair
      +offset=(128 + U32.from_nat(p) * 6 : U32)
      read_shapes(p,base,
        prepend_shape(shape_words(read_words(6n,base,offset,Nil{},a)),xs))

def click_target(mode: Bool,target: U32) -> M.Action:
  match mode:
    case True{}: M.ChooseCount{U32.to_nat((target - 40 : U32))}
    case False{}: M.ClickShape{U32.to_nat((target - 10 : U32))}

def click_blank(blank: Bool,mode: Bool,target: U32) -> M.Action:
  match blank:
    case True{}: M.ClickBlank{}
    case False{}: click_target(mode,target)

def action(kind: Nat,mode: Bool,+target: U32) -> M.Action:
  match kind:
    case 1n: click_blank(U32.is_eq(target,1),mode,target)
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
  M.S{mode,items,desc,buttons,result}=state
  a[(base + 2 : U32)] <- 0
  save_outcome(result,elapsed,deadline,base,a)

def run_buttons(+base: U32,kind: Nat,target: U32,
    +elapsed: U32,+deadline: U32,+mode: Bool,
    desc: M.Desc,items: +List<M.Shape>,
    pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a,buttons)=pair
  save(M.step(M.S{mode,items,desc,buttons,M.Running{}},
    action(kind,mode,target),elapsed,deadline),elapsed,deadline,base,a)

def run_shapes(+base: U32,kind: Nat,target: U32,
    elapsed: U32,deadline: U32,mode: Bool,
    desc: M.Desc,pair: Array<U32> & +List<M.Shape>) -> Array<U32>:
  (a,items)=pair
  run_buttons(base,kind,target,elapsed,deadline,mode,desc,items,
    read_words(5n,base,64,Nil{},a))

def run_fields(+base: U32,kind: Nat,target: U32,
    elapsed: U32,deadline: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a,fields)=pair
  match fields:
    case Con{count,Con{size,Con{color,Con{token,
         Con{answer,Con{mode,Nil{}}}}}}}:
      run_shapes(base,kind,target,elapsed,deadline,U32.is_eq(mode,1),
        M.D{size,color,token},
        read_shapes(U32.to_nat(count),base,(a,Nil{})))
    case _: a

def run_deadline(+base: U32,kind: Nat,target: U32,
    elapsed: U32,pair: Array<U32> & U32) -> Array<U32>:
  (a,deadline)=pair
  run_fields(base,kind,target,elapsed,deadline,
    read_words(6n,base,32,Nil{},a))

def run_header(+base: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a,header)=pair
  match header:
    case Con{kind,Con{target,Con{arg0,Con{arg1,Con{elapsed,Nil{}}}}}}:
      run_deadline(base,U32.to_nat(kind),target,elapsed,
        a[(base + 12 : U32)])
    case _: a

def active_status(status: Nat,+base: U32,
    a: Array<U32>) -> Array<U32>:
  match status:
    case 0n: run_header(base,read_words(5n,base,4,Nil{},a))
    case _: a[(base + 2 : U32)] <- 0

def load_status(+base: U32,
    pair: Array<U32> & U32) -> Array<U32>:
  (a,status)=pair
  active_status(U32.to_nat(status),base,a)

def step(+base: U32,a: Array<U32>) -> Array<U32>:
  load_status(base,a[(base + 9 : U32)])
