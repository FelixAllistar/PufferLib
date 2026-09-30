import Base

type File is Data:
  F{name: +List<U32>, extension: U32, index: U32}

type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{files: +List<File>, active: U32, expected: U32,
    command: +List<U32>, focused: Bool, output: U32,
    outcome: Outcome}

type Action is Data:
  Wait{}
  Focus{}
  Insert{units: +List<U32>}
  Backspace{}
  Enter{}

type Found is Data:
  Missing{}
  Present{index: U32, extension: U32}

type Decision is Data:
  D{active: U32, output: U32, status: U32}

def equal(xs: +List<U32>, ys: +List<U32>) -> Bool:
  match xs ys:
    case Nil{} Nil{}: True{}
    case Con{x,xt} Con{y,yt}: U32.is_eq(x,y) && equal(xt,yt)
    case _ _: False{}

def prefix(xs: +List<U32>, wanted: +List<U32>) -> Bool:
  match xs wanted:
    case _ Nil{}: True{}
    case Con{x,xt} Con{y,yt}: U32.is_eq(x,y) && prefix(xt,yt)
    case _ _: False{}

def whitespace(+ch: U32) -> Bool:
  U32.is_eq(ch,32) || U32.is_eq(ch,9) ||
  U32.is_eq(ch,10) || U32.is_eq(ch,13)

def kept(keep: Bool, ch: U32, tail: +List<U32>) -> +List<U32>:
  match keep:
    case True{}: ch <> tail
    case False{}: tail

def compact(xs: +List<U32>) -> +List<U32>:
  match xs:
    case Nil{}: Nil{}
    case Con{+ch, tail}:
      kept(Bool.not(whitespace(ch)),ch,compact(tail))

def star(xs: +List<U32>) -> Bool:
  match xs:
    case Nil{}: False{}
    case Con{ch,tail}: U32.is_eq(ch,42) || star(tail)

def token(xs: +List<U32>) -> +List<U32>:
  match xs:
    case Nil{}: Nil{}
    case Con{+ch,tail}:
      Bool.pick(+List<U32>,whitespace(ch),Nil{},ch <> token(tail))

def find(files: +List<File>, +active: U32,
    +wanted: +List<U32>) -> Found:
  match files:
    case Nil{}: Missing{}
    case Con{F{+name,extension,+index},rest}:
      Bool.pick(Found,equal(name,wanted) &&
        U32.is_ne((active .&. (1 << U32.to_nat(index) : U32) : U32),0),
        Present{index,extension},find(rest,active,wanted))

def result(found: Found, +active: U32,
    expected: U32) -> Decision:
  match found:
    case Missing{}: D{active,6,0}
    case Present{+index,extension}:
      D{(active .^. (1 << U32.to_nat(index) : U32) : U32),7,
        Bool.pick(U32,U32.is_eq(extension,expected),1,2)}

def rm_star(has_star: Bool, files: +List<File>,
    +active: U32, expected: U32,
    command: +List<U32>) -> Decision:
  match has_star:
    case True{}: D{active,5,0}
    case False{}:
      result(find(files,active,
        token(List.drop(&2,U32,command,3n))),active,expected)

def rm_empty(empty: Bool, files: +List<File>,
    +active: U32, expected: U32,
    +command: +List<U32>) -> Decision:
  match empty:
    case True{}: D{active,4,0}
    case False{}: rm_star(star(command),files,active,expected,command)

def is_rm(yes: Bool, files: +List<File>,
    +active: U32, expected: U32,
    +command: +List<U32>) -> Decision:
  match yes:
    case True{}:
      rm_empty(equal(compact(command),[114,109]),
        files,active,expected,command)
    case False{}:
      Bool.pick(Decision,equal(command,[101,120,105,116]),
        D{active,0,2},D{active,8,0})

def is_ls(yes: Bool, files: +List<File>,
    +active: U32, expected: U32,
    +command: +List<U32>) -> Decision:
  match yes:
    case True{}:
      Bool.pick(Decision,equal(compact(command),[108,115]),
        D{active,2,0},D{active,3,0})
    case False{}:
      is_rm(prefix(command,[114,109]),files,active,expected,command)

def is_empty(yes: Bool, files: +List<File>,
    +active: U32, expected: U32,
    +command: +List<U32>) -> Decision:
  match yes:
    case True{}: D{active,9,0}
    case False{}:
      is_ls(prefix(command,[108,115]),files,active,expected,command)

def decide(+files: +List<File>, +active: U32, expected: U32,
    +command: +List<U32>) -> Decision:
  Bool.pick(Decision,equal(command,[104,101,108,112]),
    D{active,1,0},
    is_empty(equal(command,Nil{}),files,active,expected,command))

def outcome_index(code: Nat) -> Outcome:
  match code:
    case 1n: Finished{True{}}
    case 2n: Finished{False{}}
    case _: Running{}

def decided(d: Decision, files: +List<File>,
    expected: U32) -> State:
  match d:
    case D{active,output,status}:
      S{files,active,expected,Nil{},True{},output,
        outcome_index(U32.to_nat(status))}

def execute(s: State) -> State:
  S{+files,active,+expected,command,focused,output,outcome} = s
  decided(decide(files,active,expected,command),files,expected)

def pred(n: Nat) -> Nat:
  match n:
    case 0n: 0n
    case 1n+p: p

def drop_last(+xs: +List<U32>) -> +List<U32>:
  List.take(&2,U32,xs,
    pred(List.length(&2,U32,xs)))

def act(s: State, action: Action) -> State:
  match s action:
    case S{files,active,expected,command,focused,output,outcome} Wait{}:
      S{files,active,expected,command,focused,output,outcome}
    case S{files,active,expected,command,focused,output,outcome} Focus{}:
      S{files,active,expected,command,True{},output,outcome}
    case S{files,active,expected,command,focused,output,outcome} Insert{units}:
      S{files,active,expected,List.append(&2,U32,command,units),
        focused,output,outcome}
    case S{files,active,expected,command,focused,output,outcome} Backspace{}:
      S{files,active,expected,drop_last(command),focused,output,outcome}
    case S{files,active,expected,command,focused,output,outcome} Enter{}:
      execute(S{files,active,expected,command,focused,output,outcome})

def guarded(expired: Bool, s: State, action: Action) -> State:
  match expired s:
    case True{} S{files,active,expected,command,focused,output,outcome}:
      S{files,active,expected,command,focused,output,TimedOut{}}
    case False{} _: act(s,action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{files,active,expected,command,focused,output,Running{}}:
      guarded(U32.is_ge(elapsed,deadline),
        S{files,active,expected,command,focused,output,Running{}},action)
    case S{files,active,expected,command,focused,output,Finished{success}}:
      S{files,active,expected,command,focused,output,Finished{success}}
    case S{files,active,expected,command,focused,output,TimedOut{}}:
      S{files,active,expected,command,focused,output,TimedOut{}}

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
