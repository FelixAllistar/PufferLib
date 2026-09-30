import Base
import ../../miniwob/Text.bend as T

type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{left_count: U32, right_count: U32, field: T.Field,
    focused: Bool, outcome: Outcome}

type Action is Data:
  Wait{}
  Focus{}
  Edit{command: T.Edit}
  Submit{}

def answer_index(index: Nat, +sum: U32) -> +List<U32>:
  match index:
    case 0n: [48]
    case 1n: [49]
    case 2n: [50]
    case 3n: [51]
    case 4n: [52]
    case 5n: [53]
    case 6n: [54]
    case 7n: [55]
    case 8n: [56]
    case 9n: [57]
    case _:
      Bool.pick(+List<U32>, U32.is_lt(sum, 20),
        [49, (sum - 10 + 48 : U32)], [50,48])

def answer(+sum: U32) -> +List<U32>:
  answer_index(U32.to_nat(sum), sum)

def correct(+left_count: U32, right_count: U32, field: T.Field) -> Bool:
  T.equal(T.value(field), answer((left_count + right_count : U32)))

def active(s: State, action: Action) -> State:
  match s action:
    case S{left_count,right_count,field,focused,outcome} Wait{}:
      S{left_count,right_count,field,focused,outcome}
    case S{left_count,right_count,field,focused,outcome} Focus{}:
      S{left_count,right_count,T.edit(T.End{},field),True{},outcome}
    case S{left_count,right_count,field,True{},outcome} Edit{command}:
      S{left_count,right_count,T.edit(command,field),True{},outcome}
    case S{left_count,right_count,field,False{},outcome} Edit{command}:
      S{left_count,right_count,field,False{},outcome}
    case S{+left_count,+right_count,+field,focused,outcome} Submit{}:
      S{left_count,right_count,field,False{},
        Finished{correct(left_count,right_count,field)}}

def expire(s: State) -> State:
  S{left_count,right_count,field,focused,outcome} = s
  S{left_count,right_count,field,focused,TimedOut{}}

def guard(expired: Bool, s: State, action: Action) -> State:
  match expired:
    case True{}: expire(s)
    case False{}: active(s,action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{left_count,right_count,field,focused,Running{}}:
      guard(U32.is_ge(elapsed,deadline),
        S{left_count,right_count,field,focused,Running{}},action)
    case S{left_count,right_count,field,focused,Finished{success}}:
      S{left_count,right_count,field,focused,Finished{success}}
    case S{left_count,right_count,field,focused,TimedOut{}}:
      S{left_count,right_count,field,focused,TimedOut{}}

def raw(result: Outcome) -> F32:
  match result:
    case Running{}: 0.0
    case Finished{True{}}: 1.0
    case _: (0.0 - 1.0 : F32)

def timed(result: Outcome, elapsed: U32, deadline: U32) -> F32:
  match result:
    case Running{}: 0.0
    case Finished{True{}}:
      F32.max(0.0,(1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32))
    case _: (0.0 - 1.0 : F32)
