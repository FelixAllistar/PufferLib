import Base

type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{goal_start: U32, goal_end: U32, goal_style: U32, goal_color: U32,
    length: U32, select_start: U32, select_end: U32,
    color_open: Bool, styles: +List<U32>, outcome: Outcome}

type Action is Data:
  Wait{}
  Select{start: U32, end_: U32}
  OpenColor{}
  Format{bit: U32}
  Color{code: U32}
  Submit{}

def inside(+index: U32, start: U32, end_: U32) -> Bool:
  U32.is_ge(index, start) && U32.is_lt(index, end_)

def all_has(xs: +List<U32>, +index: U32, +start: U32,
    +end_: U32, +bit: U32) -> Bool:
  match xs:
    case Nil{}: True{}
    case Con{style, rest}:
      (Bool.not(inside(index, start, end_)) ||
       U32.is_ne((style .&. bit : U32), 0)) &&
      all_has(rest, (index + 1 : U32), start, end_, bit)

def changed_format(active: Bool, clear: Bool,
    +style: U32, +bit: U32) -> U32:
  match active:
    case False{}: style
    case True{}:
      Bool.pick(U32, clear, (style .^. bit : U32),
        (style .|. bit : U32))

def format_loop(xs: +List<U32>, +index: U32, +start: U32,
    +end_: U32, +bit: U32, +clear: Bool) -> +List<U32>:
  match xs:
    case Nil{}: Nil{}
    case Con{style, rest}:
      changed_format(inside(index, start, end_), clear, style, bit) <>
      format_loop(rest, (index + 1 : U32), start, end_, bit, clear)

def format(+styles: +List<U32>, +start: U32, +end_: U32,
    +bit: U32) -> +List<U32>:
  format_loop(styles, 0, start, end_, bit,
    all_has(styles, 0, start, end_, bit))

def colored(active: Bool, +style: U32, code: U32) -> U32:
  match active:
    case False{}: style
    case True{}: ((style .&. 7 : U32) .|. (code << 3n) : U32)

def color_loop(xs: +List<U32>, +index: U32, +start: U32,
    +end_: U32, +code: U32) -> +List<U32>:
  match xs:
    case Nil{}: Nil{}
    case Con{style, rest}:
      colored(inside(index, start, end_), style, code) <>
      color_loop(rest, (index + 1 : U32), start, end_, code)

def color_when(open: Bool, styles: +List<U32>, start: U32,
    end_: U32, code: U32) -> +List<U32>:
  match open:
    case True{}: color_loop(styles, 0, start, end_, code)
    case False{}: styles

def right_style(+style: U32, +goal_style: U32, goal_color: U32) -> Bool:
  Bool.pick(Bool, U32.is_eq(goal_style, 3),
    U32.is_eq((style .&. 7 : U32), 0) &&
      U32.is_gt((style >> 3n : U32), 0) &&
      U32.is_eq((((style >> 3n : U32) - 1 : U32) / 5 : U32), goal_color),
    U32.is_eq(style, (1 << U32.to_nat(goal_style) : U32)))

def correct(xs: +List<U32>, +index: U32,
    +goal_start: U32, +goal_end: U32,
    +goal_style: U32, +goal_color: U32) -> Bool:
  match xs:
    case Nil{}: True{}
    case Con{+style, rest}:
      Bool.pick(Bool, inside(index, goal_start, goal_end),
        right_style(style, goal_style, goal_color),
        U32.is_eq(style, 0)) &&
      correct(rest, (index + 1 : U32), goal_start,
        goal_end, goal_style, goal_color)

def first_unstyled(xs: +List<U32>) -> Bool:
  match xs:
    case Nil{}: False{}
    case Con{style,rest}: U32.is_eq(style,0)

def submitted(+styles: +List<U32>, +start: U32, +end_: U32,
    style: U32, color: U32, length: U32) -> Outcome:
  Bool.pick(Outcome,
    U32.is_eq(start,0) && U32.is_eq(end_,length) &&
      first_unstyled(styles),
    Running{},Finished{correct(styles,0,start,end_,style,color)})

def act(s: State, action: Action) -> State:
  match s action:
    case S{a,b,c,d,n,start,end_,open,styles,o} Wait{}:
      S{a,b,c,d,n,start,end_,open,styles,o}
    case S{a,b,c,d,n,start,end_,open,styles,o} Select{begin,finish}:
      S{a,b,c,d,n,begin,finish,open,styles,o}
    case S{a,b,c,d,n,start,end_,open,styles,o} OpenColor{}:
      S{a,b,c,d,n,start,end_,Bool.not(open),styles,o}
    case S{a,b,c,d,n,+start,+end_,open,styles,o} Format{bit}:
      S{a,b,c,d,n,start,end_,False{},format(styles,start,end_,bit),o}
    case S{a,b,c,d,n,+start,+end_,open,styles,o} Color{code}:
      S{a,b,c,d,n,start,end_,False{},
        color_when(open,styles,start,end_,code),o}
    case S{+a,+b,+c,+d,+n,start,end_,open,+styles,o} Submit{}:
      S{a,b,c,d,n,start,end_,False{},styles,
        submitted(styles,a,b,c,d,n)}

def guarded(expired: Bool, s: State, action: Action) -> State:
  match expired s:
    case True{} S{a,b,c,d,n,start,end_,open,styles,o}:
      S{a,b,c,d,n,start,end_,open,styles,TimedOut{}}
    case False{} _: act(s,action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{a,b,c,d,n,start,end_,open,styles,Running{}}:
      guarded(U32.is_ge(elapsed, deadline),
        S{a,b,c,d,n,start,end_,open,styles,Running{}}, action)
    case S{a,b,c,d,n,start,end_,open,styles,Finished{success}}:
      S{a,b,c,d,n,start,end_,open,styles,Finished{success}}
    case S{a,b,c,d,n,start,end_,open,styles,TimedOut{}}:
      S{a,b,c,d,n,start,end_,open,styles,TimedOut{}}

def raw(o: Outcome) -> F32:
  match o:
    case Running{}: 0.0
    case Finished{True{}}: 1.0
    case _: (0.0 - 1.0 : F32)

def timed(o: Outcome, elapsed: U32, deadline: U32) -> F32:
  match o:
    case Running{}: 0.0
    case Finished{True{}}:
      (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32)
    case _: (0.0 - 1.0 : F32)
